"""Protocol/process tests use a local fake peer; no actual model is invoked."""
import io
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

from grok_acp_session import AcpSession
from grok_acp_usage import normalize


FAKE = '''import json,sys,time
mode=sys.argv[1]
def send(v):print(json.dumps(v),flush=True)
for line in sys.stdin:
 q=json.loads(line); method=q.get('method')
 if not method:continue
 if method=='session/cancel':continue
 if method=='initialize':result={'protocolVersion':1}
 elif method=='session/new':result={'sessionId':'owned-session'}
 else:
  if mode=='eof':break
  if mode=='slow':time.sleep(.15)
  if mode=='foreign':send({'method':'session/update','params':{'sessionId':'other','update':{}}})
  if mode=='replay':send({'id':q['id']-1,'result':{}})
  if mode in ('permission','filesystem'):
   call={'id':'client-request','method':'session/request_permission' if mode=='permission' else 'fs/read_text_file','params':{'options':[]}}
   send(call); reply=json.loads(next(sys.stdin))
   send({'method':'test/client_reply','params':reply})
  counts={'inputTokens':5,'outputTokens':3,'totalTokens':8,'cachedReadTokens':2,'reasoningTokens':1,'modelCalls':1,'apiDurationMs':2}
  usage={**counts,'modelUsage':{'fake':counts}}
  result={'stopReason':'end_turn','_meta':{'sessionId':'owned-session','requestId':'prompt-'+str(q['id']),'promptId':'prompt-'+str(q['id']),'modelId':'fake','totalTokens':1,'usage':usage}}
 send({'jsonrpc':'2.0','id':q['id'],'result':result})
'''


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.peer=self.root/'fake.py';self.peer.write_text(FAKE)
        self.events=io.StringIO()
        self.err=(self.root/'stderr').open('w');self.addCleanup(self.err.close)

    def session(self, mode='normal'):
        s=AcpSession([sys.executable,str(self.peer),mode],self.root,self.events,self.err)
        self.addCleanup(s.close)
        setup=s.initialize(self.root,[])
        return s,setup

    def frames(self):
        return [json.loads(l) for l in self.events.getvalue().splitlines()]

    def test_same_live_process_session_and_setup_timing_for_two_prompts(self):
        s,setup=self.session();pid=s.process.pid
        prompts=[s.prompt('one'),s.prompt('two')]
        self.assertIsNone(s.process.poll());self.assertEqual(s.process.pid,pid)
        self.assertEqual(s.session_id,'owned-session')
        self.assertEqual(normalize(prompts,s.session_id)['totals']['totalTokens'],16)
        self.assertGreater(setup['agent_and_mcp_setup_seconds'],0)
        self.assertLessEqual(setup['new']['received_monotonic_ns'],prompts[0]['started_monotonic_ns'])
        requests=[r['value'] for r in self.frames() if r['direction']=='request']
        self.assertEqual([r['method'] for r in requests],['initialize','session/new','session/prompt','session/prompt'])
        self.assertEqual(requests[0]['params']['clientCapabilities']['terminal'],False)
        self.assertEqual(requests[1]['params']['_meta'],{'yoloMode':False,'autoMode':False})
        s.close();s.close();self.assertIsNotNone(s.process.poll())
        self.assertTrue(all(r['monotonic_ns']>0 for r in self.frames()))

    def test_permission_is_cancelled_without_general_approval(self):
        s,_=self.session('permission');s.prompt('one')
        self.assertEqual(s.permission_requests,1)
        replies=[r['value'] for r in self.frames() if r['direction']=='request' and r['value'].get('id')=='client-request']
        self.assertEqual(replies[0]['result'],{'outcome':{'outcome':'cancelled'}})

    def test_client_filesystem_request_is_denied(self):
        s,_=self.session('filesystem');s.prompt('one')
        replies=[r['value'] for r in self.frames() if r['direction']=='request' and r['value'].get('id')=='client-request']
        self.assertEqual(replies[0]['error']['code'],-32601)

    def test_foreign_or_replayed_response_breaks_session(self):
        for mode in ('foreign','replay'):
            s,_=self.session(mode)
            with self.assertRaises(ValueError):s.prompt('one')
            with self.assertRaises(RuntimeError):s.prompt('never replay')
            self.assertTrue(s.broken);s.close()

    def test_timeout_cancels_and_forbids_replay_even_after_late_response(self):
        s,_=self.session('slow')
        with self.assertRaises(TimeoutError):s.prompt('one',timeout=.01)
        with self.assertRaises(RuntimeError):s.prompt('never replay')
        methods=[r['value'].get('method') for r in self.frames() if r['direction']=='request']
        self.assertEqual(methods.count('session/prompt'),1)
        self.assertEqual(methods.count('session/cancel'),1)
        failures=[r['value'] for r in self.frames() if r['direction']=='request_failed']
        self.assertEqual(len(failures),1)
        failure=failures[0]
        self.assertFalse(failure['success'])
        self.assertTrue(failure['session_unavailable'])
        self.assertTrue(failure['cancel_sent'])
        self.assertEqual(failure['error_type'],'TimeoutError')
        late=failure['original_late_response']
        self.assertEqual(late['response']['id'],failure['rpc_id'])
        self.assertGreater(late['received_monotonic_ns'],failure['failed_monotonic_ns'])
        # Even a late end_turn is retained only as failed-request evidence.
        self.assertEqual(late['response']['result']['stopReason'],'end_turn')
        self.assertEqual(normalize([late],s.session_id)['totals']['totalTokens'],8)

    def test_eof_and_invalid_prompt_never_start_replacement_process(self):
        s,_=self.session('eof');pid=s.process.pid
        with self.assertRaises(ValueError):s.prompt('')
        with self.assertRaises(ValueError):s.prompt('one',timeout=0)
        with self.assertRaises(RuntimeError):s.prompt('one')
        with self.assertRaises(RuntimeError):s.prompt('never replay')
        self.assertEqual(s.process.pid,pid)
        failure=next(r['value'] for r in self.frames() if r['direction']=='request_failed')
        self.assertFalse(failure['success'])
        self.assertIsNone(failure['original_late_response'])


class ScopedPermissionTests(unittest.TestCase):
    def setUp(self):
        self.session=object.__new__(AcpSession)
        self.session.session_id='owned-session';self.session.closed=False;self.session.broken=False
        self.session.permission_requests=0;self.session.permission_approvals=0
        self.session.authorized_mcp_tools=frozenset({'yee__yee_browser'})
        self.session.permission_tool_calls=set();self.sent=[];self.records=[]
        self.session._send=self.sent.append
        self.session._record=lambda direction,value:self.records.append((direction,value))
        self.request={'id':0,'method':'session/request_permission','params':{
            'sessionId':'owned-session','toolCall':{'toolCallId':'owned-call','title':'yee__yee_browser',
                'rawInput':{'variant':'UseTool','tool_name':'yee__yee_browser','tool_input':{'action':'observe','full':True}},
                '_meta':{'x.ai/tool':{'version':1,'name':'use_tool','kind':'use_tool'}}},
            'options':[{'optionId':'once','kind':'allow_once'},{'optionId':'always','kind':'allow_always'}]}}

    def test_declared_tool_selects_only_once_and_replay_is_denied(self):
        self.session._client_request(self.request)
        self.assertEqual(self.sent[-1]['result'],{'outcome':{'outcome':'selected','optionId':'once'}})
        self.session._client_request(self.request)
        self.assertEqual(self.sent[-1]['result'],{'outcome':{'outcome':'cancelled'}})
        self.assertEqual(self.session.permission_approvals,1)

    def test_foreign_disguised_malformed_and_ambiguous_requests_are_denied(self):
        variants=[]
        def variant():
            q=copy.deepcopy(self.request);q['params']['toolCall']['toolCallId']='call-'+str(len(variants));variants.append(q);return q
        variant()['params']['sessionId']='foreign'
        variant()['params']['toolCall']['rawInput']['tool_name']='Bash'
        variant()['params']['toolCall']['rawInput']['variant']='Bash'
        variant()['params']['toolCall']['rawInput']['extra']='not a discovered field'
        variant()['params']['toolCall']['rawInput']['tool_input']='not an object'
        variant()['params']['toolCall']['_meta']['x.ai/tool']['version']=True
        variant()['params']['options'].append({'optionId':'once','kind':'allow_always'})
        variant()['params']['options']=[{'optionId':'always','kind':'allow_always'}]
        variant()['params']['toolCall']=None
        for q in variants:
            self.session._client_request(q)
            self.assertEqual(self.sent[-1]['result'],{'outcome':{'outcome':'cancelled'}})
        self.assertEqual(self.session.permission_approvals,0)

    def test_cancelled_or_closed_session_cannot_approve_late_permission(self):
        for field in ['broken','closed']:
            self.setUp();setattr(self.session,field,True)
            self.session._client_request(self.request)
            self.assertEqual(self.sent[-1]['result'],{'outcome':{'outcome':'cancelled'}})


if __name__=='__main__':unittest.main()
