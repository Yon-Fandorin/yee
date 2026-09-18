import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from yee_s05_wait_trace import inspect


class WaitTraceTests(unittest.TestCase):
    origin = 'http://127.0.0.1:12345'

    def records(self):
        rows=[]
        for number, command in enumerate(('observe','wait-change'),1):
            req={'id':str(number),'command':command}
            if number==2:req.update(document='doc',baseline_document='doc',baseline_revision=1,wait_ms=100)
            res={'id':str(number),'ok':True,'execution_settled':True,'document':'doc',
                 'revision':number,'tab':'tab','truncated':False,
                 'snapshot':f'page @doc rev={number} title="Live tickets" origin="{self.origin}"\n+@doc_1 text "Ticket"\n'}
            if number==2:res['wait']={'reason':'limit','mechanism':'accessibility_events','probes':2}
            rows.extend([{'sequence':number*2-1,'time_ns':number*2,'kind':'request','request':req},
                         {'sequence':number*2,'time_ns':number*2+1,'kind':'response','response':res,
                          'response_source':'mailbox'}])
        return rows

    def test_classifies_wait_without_claiming_task_or_event_completeness(self):
        result=inspect(self.records(),self.origin,1)
        self.assertEqual(result['strategy'],'recorded_accessibility_wait')
        self.assertEqual(result['waits'],[{'ok':True,'reason':'limit','probes':2,
            'requested_ms':100,'native_elapsed_ms':None,'early_reprobe_change':None}])
        self.assertFalse(result['model_strategy_verified'])
        self.assertFalse(result['event_delivery_completeness_verified'])
        self.assertNotIn('success',result)

    def test_changed_at_initial_probe_or_limit_is_not_early_event_evidence(self):
        for reason,probes,elapsed,expected in [('changed',2,50,True),('changed',1,50,False),
                ('changed',2,100,False),('changed',3,101,False),('limit',2,50,False)]:
            rows=self.records();res=rows[3]['response']
            res['wait'].update(reason=reason,probes=probes)
            res['timing']={'native_elapsed_ms':elapsed}
            result=inspect(rows,self.origin,1)
            self.assertEqual(result['waits'][0]['early_reprobe_change'],expected)
            self.assertFalse(result['event_delivery_completeness_verified'])
        for elapsed in (None,True,-1,float('nan'),float('inf')):
            rows=self.records();rows[3]['response']['timing']={'native_elapsed_ms':elapsed}
            result=inspect(rows,self.origin,1)
            self.assertIsNone(result['waits'][0]['early_reprobe_change'])
            self.assertIsNone(result['waits'][0]['native_elapsed_ms'])

    def test_oracle_keeps_limit_separate_from_ticket_completion(self):
        from agent_scenario_fixture import Fixture
        spec=importlib.util.spec_from_file_location('oracle',Path(__file__).with_name('verify-agent-scenario.py'))
        oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
        with tempfile.TemporaryDirectory() as directory:
            clock=[0.0]
            fixture=Fixture('S05',11,str(Path(directory)/'fixture'),clock=lambda:clock[0])
            try:
                fixture.view();fixture.action({'action':'start_monitor'})
                answer={'tickets':[{k:t[k] for k in ('id','service','symptom')}
                    for t in fixture.data['tickets'] if t['after_ms']>0 and t['urgent']]}
                created=json.loads((fixture.directory/'manifest.json').read_text())['created_ns']
                rows=self.records()
                for event in rows:event['time_ns']+=created
                result=oracle.verify(fixture.directory,answer,rows,tab_origin=self.origin)
                self.assertFalse(result['success'])
                self.assertEqual(result['waiting_strategy']['strategy'],'recorded_accessibility_wait')
                clock[0]=10;fixture.view()
                result=oracle.verify(fixture.directory,answer,rows,tab_origin=self.origin)
                self.assertFalse(result['success'])
                rows[3]['response']['snapshot']+='\n+@doc_9 text "All scheduled arrivals delivered"\n'
                result=oracle.verify(fixture.directory,answer,rows,tab_origin=self.origin)
                self.assertTrue(result['success'])
                self.assertEqual(result['waiting_strategy']['waits'][0]['reason'],'limit')
                self.assertFalse(result['waiting_strategy']['model_strategy_verified'])
            finally:fixture.close()

    def test_client_error_between_requests_is_retained(self):
        rows=self.records()
        rows.insert(2,{'kind':'client_error','error':'missing document','time_ns':3})
        for i,row in enumerate(rows,1):row['sequence']=i
        result=inspect(rows,self.origin,1)
        self.assertEqual(result['client_errors'],[{'sequence':3,'error':'missing document'}])
        self.assertEqual(result['successful_waits'],1)
        rows[2],rows[3]=rows[3],rows[2]
        for i,row in enumerate(rows,1):row.update(sequence=i,time_ns=i)
        with self.assertRaises(ValueError):inspect(rows,self.origin,1)

    def test_completion_requires_observed_text_node(self):
        for line, expected in [
                ('+@doc_9 text "All scheduled arrivals delivered"',True),
                ('-@doc_9 text "All scheduled arrivals delivered"',False),
                ('+@doc_9 text "Not All scheduled arrivals delivered"',False),
                ('+@doc_9 button "All scheduled arrivals delivered"',False)]:
            rows=self.records();rows[3]['response']['snapshot']+='\n'+line+'\n'
            self.assertEqual(inspect(rows,self.origin,1)['completion_message_observed'],expected)

    def test_no_wait_and_failed_wait_are_distinct(self):
        rows=self.records()
        self.assertEqual(inspect(rows[:2],self.origin,1)['strategy'],'no_recorded_wait')
        rows[3]['response']={'id':'2','ok':False,'execution_settled':True,'error':'request_expired'}
        result=inspect(rows,self.origin,1)
        self.assertEqual(result['strategy'],'failed_wait_attempts')
        self.assertEqual(result['failed_waits'],1)
        self.assertIsNone(result['waits'][0]['probes'])

    def test_failed_wait_cost_is_retained_without_inventing_missing_time(self):
        rows=self.records()
        rows[3]['response']['timing']={'native_elapsed_ms':101}
        rows.extend([
            {'sequence':5,'time_ns':6,'kind':'request','request':{
                'id':'3','command':'wait-change','document':'doc',
                'baseline_document':'doc','baseline_revision':2,'wait_ms':30000}},
            {'sequence':6,'time_ns':7,'kind':'response','response_source':'mailbox',
             'response':{'id':'3','ok':False,'execution_settled':True,
                         'error':'wait_cancelled','timing':{'native_elapsed_ms':140.804}}}])
        result=inspect(rows,self.origin,1)
        self.assertEqual(result['failed_waits'],1)
        self.assertAlmostEqual(result['total_native_wait_ms'],241.804)
        self.assertEqual(result['unmeasured_wait_attempts'],0)
        self.assertEqual(result['waits'][-1]['error'],'wait_cancelled')
        self.assertEqual(result['waits'][-1]['requested_ms'],30000)
        self.assertIsNone(result['waits'][-1]['probes'])
        for unknown in (None,True,-1,float('nan'),float('inf')):
            rows[-1]['response']['timing']={'native_elapsed_ms':unknown}
            result=inspect(rows,self.origin,1)
            self.assertIsNone(result['total_native_wait_ms'])
            self.assertEqual(result['measured_native_wait_ms'],101)
            self.assertEqual(result['unmeasured_wait_attempts'],1)
        no_wait=inspect(rows[:2],self.origin,1)
        self.assertEqual(no_wait['total_native_wait_ms'],0)
        self.assertEqual(no_wait['wait_attempts'],0)

    def test_content_mode_requires_matching_native_report(self):
        rows=self.records();rows[2]['request']['wait_mode']='content'
        with self.assertRaises(ValueError):inspect(rows,self.origin,1)
        rows[3]['response']['wait']['comparison']='content'
        self.assertEqual(inspect(rows,self.origin,1)['waits'][0]['comparison'],'content')
        del rows[2]['request']['wait_mode']
        with self.assertRaises(ValueError):inspect(rows,self.origin,1)

    def test_arrival_requires_new_fixture_id_in_complete_snapshots(self):
        tickets=[{'id':'T-11-1','after_ms':0},{'id':'T-11-3','after_ms':600}]
        for initial,final,expected in [('', 'T-11-3', ['T-11-3']),
                ('T-11-3','T-11-3',[]),('', 'T-11-1',[]),('', 'T-11-30',[])]:
            rows=self.records()
            rows[1]['response']['snapshot']+=initial
            rows[3]['response']['snapshot']+=final
            rows[3]['response']['wait']['reason']='changed'
            rows[3]['response']['timing']={'native_elapsed_ms':50}
            wait=inspect(rows,self.origin,1,tickets=tickets)['waits'][0]
            self.assertEqual(wait['new_fixture_ticket_ids'],expected)
            self.assertEqual(wait['early_arrival_observed'],bool(expected))
        rows=self.records()
        rows[1]['response']['snapshot']=rows[1]['response']['snapshot'].replace('\n',' delta\n',1)
        rows[3]['response']['snapshot']+='T-11-3'
        rows[3]['response']['timing']={'native_elapsed_ms':50}
        wait=inspect(rows,self.origin,1,tickets=tickets)['waits'][0]
        self.assertIsNone(wait['new_fixture_ticket_ids'])
        self.assertIsNone(wait['early_arrival_observed'])

    def test_bad_identity_settlement_baseline_and_scope_fail(self):
        for kind,key,value in [('response','id','wrong'),('response','execution_settled',False),
                               ('response','tab','other'),('request','document','old'),
                               ('request','baseline_revision',True),('request','wait_ms',30001)]:
            rows=self.records(); rows[3 if kind=='response' else 2][kind][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):inspect(rows,self.origin,1)
        rows=self.records();rows[3]['response_source']='native_archive'
        with self.assertRaises(ValueError):inspect(rows,self.origin,1)
        rows=self.records();rows[1]['response']['snapshot']=rows[1]['response']['snapshot'].replace(self.origin,'http://127.0.0.1:54321')
        with self.assertRaises(ValueError):inspect(rows,self.origin,1)

    def test_partial_replayed_or_truncated_records_do_not_prove_wait(self):
        rows=self.records()
        for invalid in (rows[:-1],rows+rows,rows[::-1]):
            with self.assertRaises(ValueError):inspect(invalid,self.origin,1)
        for report in ({'reason':'changed','mechanism':'sleep','probes':2},
                       {'reason':'limit','mechanism':'accessibility_events','probes':True},
                       {'reason':'scope_limit','mechanism':'accessibility_events','probes':2}):
            altered=copy.deepcopy(rows);altered[3]['response']['wait']=report
            with self.assertRaises(ValueError):inspect(altered,self.origin,1)
        rows[3]['response']['truncated']=True
        rows[3]['response']['wait']['reason']='scope_limit'
        self.assertEqual(inspect(rows,self.origin,1)['waits'][0]['reason'],'scope_limit')


if __name__=='__main__':unittest.main()
