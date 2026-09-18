"""MCP consumer ownership exercised through the real locked CLI mailbox."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import anyio
from yee_browser_results import unpack_results

spec=importlib.util.spec_from_file_location('baseline_adapter',Path(__file__).with_name('yee-browser-mcp.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def page(doc,rev,*,delta=False,base=None,truncated=False):
    return {'ok':True,'execution_settled':True,'receipt_persisted':True,
            'document':doc,'revision':rev,'tab':'11111111-1111-4111-8111-111111111111',
            'scope':'main_document_visible_dom','url':'https://example.test/',
            'viewport':{'width':1440,'height':900,'deviceScaleFactor':1},
            'scroll':{'y':0,'can_scroll_down':False},'truncated':truncated,
            'snapshot':f'page @{doc} rev={rev} title="Example" origin="https://example.test"'+
                       (f' delta\nbase_rev={base}\n+@{doc}_2 text "Changed {rev}"\n' if delta else
                        f'\n+@{doc}_1 heading "Required unchanged heading"\n+@{doc}_2 text "Changed {rev}"\n')}

class MailboxTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.bridge=Path(self.tmp.name).resolve()
        self.config=argparse.Namespace(bridge=str(self.bridge),timeout=2,request_timeout=2,
                                      compact=True,_transcript=None)
        self.worker=m.Adapter(self.config);self.doc='first-doc';self.rev=0
        self.requests=[];self.stop=threading.Event();self.peer_errors=[];self.truncate_next=False
        self.thread=threading.Thread(target=self.peer);self.thread.start()

    def tearDown(self):
        self.stop.set();self.thread.join(3);self.assertFalse(self.thread.is_alive())
        self.tmp.cleanup();self.assertEqual(self.peer_errors,[])

    def peer(self):
        seen=set()
        try:
            while not self.stop.is_set():
                req=m.cli.read_json(str(self.bridge/'request.json'))
                if not isinstance(req,dict) or req['id'] in seen:
                    time.sleep(.002);continue
                seen.add(req['id']);self.requests.append(req)
                self.assertEqual(req['command'],'observe')
                self.rev+=1
                response=page(self.doc,self.rev,delta=not req.get('full',False),
                              base=req.get('baseline_revision'),truncated=self.truncate_next)
                self.truncate_next=False;response['id']=req['id']
                m.cli.atomic_write(str(self.bridge/'response.json'),json.dumps(response).encode())
        except BaseException as exc:
            self.peer_errors.append(repr(exc))

    def observe(self,full=False):
        result=anyio.run(self.worker.call,{'action':'observe',**({'full':True} if full else {})})
        self.assertFalse(result.is_error,result)
        return unpack_results(json.loads(result.content[0].text))

    def external_observation(self,doc,rev):
        self.doc,self.rev=doc,rev
        # Same shared state that a separate CLI client acknowledges under its lock.
        m.cli.update_state(str(self.bridge),page(doc,rev))

    def test_unseen_document_gets_full_snapshot_in_one_native_request(self):
        self.observe();self.external_observation('second-doc',40)
        count=len(self.requests);shown=self.observe()
        self.assertEqual(len(self.requests)-count,1)
        self.assertTrue(self.requests[-1]['full'])
        self.assertNotIn('baseline_document',self.requests[-1])
        self.assertNotIn('_expected_observation_baseline',self.requests[-1])
        self.assertIn('Required unchanged heading',shown['snapshot'])

    def test_external_revision_on_same_document_gets_full_snapshot(self):
        self.observe();self.external_observation(self.doc,40)
        shown=self.observe()
        self.assertTrue(self.requests[-1]['full'])
        self.assertNotIn('baseline_revision',self.requests[-1])
        self.assertIn('Required unchanged heading',shown['snapshot'])

    def test_known_changed_deltas_advance_the_owned_baseline_without_extra_reads(self):
        self.observe();a=self.observe();b=self.observe()
        self.assertEqual([r['full'] for r in self.requests],[True,False,False])
        self.assertEqual([r.get('baseline_revision') for r in self.requests],[None,1,2])
        self.assertIn('base_rev=1',a['snapshot']);self.assertIn('base_rev=2',b['snapshot'])
        self.assertEqual(len(self.requests),3)

    def test_baseline_is_checked_after_bridge_lock_not_at_adapter_call_start(self):
        self.observe();entered=threading.Event();original=m.cli.acquire_lock
        held=original(str(self.bridge/'request.lock'),1)
        def acquire(path,timeout):
            entered.set();return original(path,timeout)
        try:
            with patch.object(m.cli,'acquire_lock',acquire),ThreadPoolExecutor(max_workers=1) as pool:
                task=pool.submit(self.observe)
                self.assertTrue(entered.wait(1))
                self.external_observation('swapped-under-lock',80)
                held.close();held=None
                shown=task.result(timeout=4)
            self.assertTrue(self.requests[-1]['full'])
            self.assertIn('Required unchanged heading',shown['snapshot'])
        finally:
            if held is not None:held.close()

    def test_truncated_observation_cannot_be_a_delta_baseline(self):
        self.truncate_next=True;shown=self.observe();self.assertTrue(shown['truncated'])
        shown=self.observe()
        self.assertEqual([r['full'] for r in self.requests],[True,True])
        self.assertIn('Required unchanged heading',shown['snapshot'])

    def test_explicit_full_and_non_mcp_cli_semantics_remain(self):
        self.observe();self.observe(full=True)
        args=m.cli.session_command(['observe'],argparse.Namespace(bridge=str(self.bridge),timeout=2,request_timeout=2,compact=True,_transcript=None))
        code,response=m.cli.run(args,emit_output=False)
        self.assertEqual(code,0)
        self.assertEqual([r['full'] for r in self.requests],[True,True,False])
        self.assertEqual(self.requests[-1]['baseline_revision'],2)

class ReceiptTests(unittest.TestCase):
    def config(self):return argparse.Namespace(bridge='/unused',timeout=1,request_timeout=1,compact=True,_transcript=None)

    def test_bad_or_unsettled_snapshots_never_authorize_a_delta_baseline(self):
        for change in ({'truncated':True},{'execution_settled':False},{'receipt_persisted':False},
                       {'partial_effect_possible':True},{'revision':True},
                       {'document':'wrong-header-doc'},{'snapshot':'page @first-doc rev=1 delta\nbase_rev=99\n'}):
            with self.subTest(change=change):
                flags=[]
                def execute(c):
                    flags.append(c.full);return 0,{**page('first-doc',1),**change}
                worker=m.Adapter(self.config(),execute)
                anyio.run(worker.call,{'action':'observe'});anyio.run(worker.call,{'action':'observe'})
                self.assertEqual(flags,[True,True])

    def test_connection_abort_does_not_commit_a_partly_returned_compound_baseline(self):
        flags=[]
        def execute(c):
            flags.append(c.full);return 0,page('first-doc',1)
        worker=m.Adapter(self.config(),execute)
        async def run():
            token=worker.explicit_cancel.set(threading.Event())
            try:
                with anyio.CancelScope() as scope:
                    original=worker.remember_observation
                    def remember(*args,**kwargs):
                        result=original(*args,**kwargs);scope.cancel();return result
                    with patch.object(worker,'remember_observation',remember):
                        await worker.call({'commands':[['observe'],['observe']]})
                await worker.call({'action':'observe'})
            finally:worker.explicit_cancel.reset(token)
        anyio.run(run)
        self.assertEqual(flags,[True,True])

    def test_failed_audit_response_does_not_commit_model_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            flags=[]
            def execute(c):flags.append(c.full);return 0,page('first-doc',1)
            config=self.config()
            with m.cli.Transcript(str(Path(directory)/'native')) as native:
                config._transcript=native;worker=m.Adapter(config,execute)
                class Audit:
                    def write(self,row):
                        if row['kind']=='mcp_response':raise m.cli.RecordingError('receipt lost')
                calls=m.AuditedCalls(worker,Audit())
                with self.assertRaises(m.cli.RecordingError):anyio.run(calls.call,'yee_browser',{'action':'observe'})
                anyio.run(worker.call,{'action':'observe'})
            self.assertEqual(flags,[True,True])

if __name__=='__main__':unittest.main()
