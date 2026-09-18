import copy
import importlib.util
import json
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
s=importlib.util.spec_from_file_location('audit',Path(__file__).with_name('inspect-grok-scenario-cohort.py'));audit=importlib.util.module_from_spec(s);s.loader.exec_module(audit)

class Tests(unittest.TestCase):
    def test_utf8_byte_prefix_preserves_the_exact_bound_archive(self):
        original='가'*10000
        session={'session_identity':{'cwd':'/trial/workspace'},'session_id':'session'}
        archive='/Users/yongjunkim/.grok/sessions/%2Ftrial%2Fworkspace/session/mcp/call-utf8.txt'
        prefix=original.encode()[:20000].decode(errors='ignore')
        suffix='\n\n[MCP output truncated: showing first 19.5 KB of 29.3 KB. Full output written to: '+archive+'.]'
        with patch.object(Path,'is_symlink',return_value=False),patch.object(Path,'read_text',return_value=original),patch.object(audit,'sha',return_value='hash'):
            result=audit.received_text(prefix+suffix,original,session)
            self.assertEqual(result['received_utf8_bytes'],19998)
            self.assertFalse(result['received_full_output'])
            with self.assertRaises(ValueError):audit.received_text(prefix[:-1]+suffix,original,session)

    def test_grok_text_truncation_requires_exact_session_archive_content(self):
        from urllib.parse import quote
        original='x'*30000
        session={'session_identity':{'cwd':'/private/tmp/owned-trial/workspace'},'session_id':'owned-session'}
        directory=Path('/Users/yongjunkim/.grok/sessions')/quote(session['session_identity']['cwd'],safe='')/session['session_id']/'mcp'
        archive=directory/'call-owned.txt'
        header=f'\n\n[MCP output truncated: showing first 19.5 KB of {len(original.encode())/1024:.1f} KB. Full output written to: {archive}.'
        for explanation in ('',' The full output has a very long line, so grep/read_file are ineffective on it — use `bash` to slice/search the saved file (e.g. `python3`, `sed`, or `cut`).'):
            received=original[:20000]+header+explanation+']'
            with patch.object(Path,'is_symlink',return_value=False),patch.object(Path,'read_text',return_value=original),patch.object(audit,'sha',return_value='original-hash'):
                result=audit.received_text(received,original,session)
                self.assertFalse(result['received_full_output']);self.assertEqual(result['archive_sha256'],'original-hash')
            with patch.object(Path,'is_symlink',return_value=False),patch.object(Path,'read_text',return_value='changed'):
                with self.assertRaises(ValueError):audit.received_text(received,original,session)
        with self.assertRaises(ValueError):audit.received_text(original[:20000]+header+' unexpected explanation]',original,session)

    def test_failed_terminal_keeps_cost_and_native_wait_accounting(self):
        for browser in ('yee','aside'):
            with self.subTest(browser=browser), tempfile.TemporaryDirectory() as d:
                root=Path(d);case=root/browser/'S02';model=case/'model';model.mkdir(parents=True)
                fixture=case/'fixture';fixture.mkdir();reviews=root/'reviews';reviews.mkdir()
                def write(p,v):p.write_text(json.dumps(v))
                write(model/'summary.json',dict(usage={'total_tokens':1234},elapsed_seconds=12,
                    returncode=1,timed_out=False,authority_audit={'passed':True}))
                write(model/'stdout.json',{'type':'result','subtype':'error'})
                write(fixture/'dataset.json',{});(fixture/'events.jsonl').write_text('')
                write(root/'frozen.json',{});write(case/'source-integrity-at-finish.json',{})
                (model/'mcp-native.jsonl').write_text('');(model/'mcp-calls.jsonl').write_text('')
                write(reviews/'aside-S02-scope.json',dict(passed=True,
                    model_stdout_sha256=audit.sha(model/'stdout.json')))
                with patch.object(audit,'correlate',return_value={'mcp_calls':12}), \
                     patch.object(audit.native_audit,'inspect',return_value={
                         'native_user_wait_seconds':3,'native_settlement_verified':True}), \
                     patch.object(audit.oracle,'verify',return_value={'success':False}):
                    result=audit.inspect(case,reviews)
                self.assertFalse(result['accepted']);self.assertEqual(result['usage']['total_tokens'],1234)
                self.assertEqual(result['zero_wait_seconds'],9 if browser=='yee' else 12)
                self.assertIn('model_did_not_complete',result['failures'])
                self.assertTrue(any(f.startswith('terminal:') for f in result['failures']))

    def evidence(self):
        events=[];ops=[]
        for i in range(2):
            args={'action':'observe'};payload='result'+str(i)
            events.append({'message':{'content':[{'type':'tool_use','id':str(i),'name':'use_tool','input':{'tool_name':'yee__yee_browser','tool_input':args}}]}})
            events.append({'message':{'content':[{'type':'tool_result','tool_use_id':str(i),'content':json.dumps({'type':'MCP','server_name':'yee','tool_name':'yee_browser','output':{'OkayOutput':payload}})}]}})
            ops += [{'operation':'tools/call','request_received_monotonic_ns':i,'params':{'name':'yee_browser','arguments':args}}, {'result':{'isError':False,'content':[{'type':'text','text':payload}]}}]
        return events,ops
    def check(self,events,ops):
        with patch.object(audit,'lines',return_value=[]),patch.object(audit,'derive',return_value=(ops,{})):
            return audit.correlate(events,Path('/explicit/wire'))
    def test_sequential_repeated_inputs_match_ordered_original_responses(self):
        e,o=self.evidence();self.assertEqual(self.check(e,o)['mcp_calls'],2)
        bad=copy.deepcopy(e);bad[1],bad[3]=bad[3],bad[1]
        # IDs retain correlation despite delivery order; swaps of actual payloads fail.
        a=json.loads(bad[1]['message']['content'][0]['content']);a['output']['OkayOutput']='wrong'
        bad[1]['message']['content'][0]['content']=json.dumps(a)
        with self.assertRaises(ValueError):self.check(bad,o)
    def test_tampered_scope_inputs_payloads_and_missing_results_fail(self):
        for change in ('input','server','missing','image'):
            e,o=self.evidence()
            if change=='input':e[0]['message']['content'][0]['input']['tool_input']={'action':'click'}
            elif change=='server':
                p=json.loads(e[1]['message']['content'][0]['content']);p['server_name']='other';e[1]['message']['content'][0]['content']=json.dumps(p)
            elif change=='missing':e.pop()
            else:o[1]['result']['content']=[{'type':'image','mimeType':'image/png','data':'x'}]
            with self.assertRaises((ValueError,KeyError)):self.check(e,o)
    def test_identical_parallel_requests_are_not_guessed(self):
        e,o=self.evidence();e[0]['message']['content']+=e[2]['message']['content'];del e[2]
        with self.assertRaisesRegex(ValueError,'parallel'):self.check(e,o)
    def test_error_result_requires_matching_flags_and_preserves_error(self):
        e,o=self.evidence()
        b=e[1]['message']['content'][0];w=json.loads(b['content'])
        w['output']={'Error':'result0'};w['is_error']=True;b['is_error']=True
        b['content']=json.dumps(w);o[1]['result']['isError']=True
        self.assertEqual(self.check(e,o)['mcp_errors'],1)
        b['is_error']=False
        with self.assertRaisesRegex(ValueError,'flags'):self.check(e,o)
    def test_only_aside_errors_allow_newline_at_original_block_boundary(self):
        e,o=self.evidence()
        e[0]['message']['content'][0]['input']['tool_name']='aside__repl'
        o[0]['params']['name']='repl';o[1]['result']['isError']=True
        o[1]['result']['content'].append({'type':'text','text':'console log'})
        b=e[1]['message']['content'][0];b['is_error']=True
        w={'type':'MCP','server_name':'aside','tool_name':'repl','is_error':True,'output':{'Error':'result0\nconsole log'}}
        b['content']=json.dumps(w)
        self.assertEqual(self.check(e,o)['aside_error_text_join_count'],1)
        w['output']['Error']='result0\n\nconsole log';b['content']=json.dumps(w)
        with self.assertRaises(ValueError):self.check(e,o)
    def test_aside_images_require_exact_host_placeholder_without_text_changes(self):
        e,o=self.evidence();e[0]['message']['content'][0]['input']['tool_name']='aside__repl';o[0]['params']['name']='repl'
        o[1]['result']['content']=[{'type':'text','text':'before'},{'type':'image','mimeType':'image/png','data':'YWJj'},
                                  {'type':'text','text':'after'}]
        b=e[1]['message']['content'][0]
        w=json.loads(b['content']);w['server_name']='aside';w['tool_name']='repl'
        w['output']['OkayOutput']='before\n[image content will be provided separately]\nafter';b['content']=json.dumps(w)
        result=self.check(e,o)
        self.assertFalse(result['received_all_mcp_media'])
        self.assertEqual(result['media_transforms'][0]['image_count'],1)
        w['output']['OkayOutput']='before\n[image content will be provided separately]\nchanged';b['content']=json.dumps(w)
        with self.assertRaisesRegex(ValueError,'placeholder changed'):self.check(e,o)
    def test_truncation_requires_bound_archive_and_exact_prefix(self):
        original='x'*21000
        session={'session_identity':{'cwd':'/trial/workspace'},'session_id':'session'}
        archive='/Users/yongjunkim/.grok/sessions/%2Ftrial%2Fworkspace/session/mcp/call-abc-2.json'
        suffix='\n\n[MCP output truncated: showing first 19.5 KB of 20.5 KB. Full output written to: '+archive+'. The full output is valid JSON with a very long line, so grep/read_file are ineffective on it — use `bash` to query the saved file (e.g. `jq` or `python3`).]'
        with patch.object(Path,'is_symlink',return_value=False),patch.object(Path,'read_text',return_value=original),patch.object(audit,'sha',return_value='hash'):
            self.assertFalse(audit.received_text(original[:20000]+suffix,original,session)['received_full_output'])
            alternate=suffix.replace('with a very long line, so grep/read_file are ineffective on it — use `bash` to query the saved file',
                                      'saved to the file above; use `bash` to query it')
            self.assertFalse(audit.received_text(original[:20000]+alternate,original,session)['received_full_output'])
            for bad in [('z'+original[1:20000]+suffix),original[:20000]+suffix.replace('/session/','/other/'),original[:20000]+suffix.replace('20.5','99.5')]:
                with self.assertRaises(ValueError):audit.received_text(bad,original,session)
        with patch.object(Path,'is_symlink',return_value=False),patch.object(Path,'read_text',return_value=original+'tampered'):
            with self.assertRaises(ValueError):audit.received_text(original[:20000]+suffix,original,session)
if __name__=='__main__':unittest.main()
