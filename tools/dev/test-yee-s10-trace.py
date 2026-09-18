import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from agent_scenario_fixture import dataset, Fixture
import yee_s10_trace as trace

spec=importlib.util.spec_from_file_location('oracle',Path(__file__).with_name('verify-agent-scenario.py'))
oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)


def records(data):
    rows=[]; origin='http://127.0.0.1:54442'
    tabs=[{'tab':f'cap-{i}', 'url':origin+d['source'], 'permission':'granted',
           'metadata_truncated':False, 'active':i==0} for i,d in enumerate(data['documents'])]
    def pair(command, response, **args):
        rid=str(len(rows))
        rows.extend([{'sequence':len(rows)+1,'kind':'request','request':{'id':rid,'command':command,**args}},
                     {'sequence':len(rows)+2,'kind':'response','response_source':'mailbox',
                      'response':{'id':rid,'ok':True,'execution_settled':True,**response}}])
    pair('tabs',{'tabs':copy.deepcopy(tabs)})
    for i in (1,2,0):
        d=data['documents'][i]
        snapshot=f"{d['id']} {d['source']} Processing: {d['processing_days']} days Free shipping minimum: {d['free_shipping_minimum_krw']} KRW Returns: {d['return_days']} days"
        pair('select-tab',{'tab':f'cap-{i}','snapshot':snapshot,'truncated':False},tab=f'cap-{i}')
    pair('tabs',{'tabs':copy.deepcopy(tabs)})
    return rows,origin


class TraceTests(unittest.TestCase):
    def test_three_seeds_require_complete_facts_and_original_return(self):
        for seed in (11,29,47):
            data=dataset('S10',seed); rows,origin=records(data)
            result=trace.verify(data,rows,origin)
            self.assertEqual(result['tab_audit_status'],'verified')
            self.assertTrue(result['original_grants_preserved'])
            self.assertFalse(result['model_autonomy_verified'])

    def test_missing_or_truncated_policy_facts_fail(self):
        data=dataset('S10',11)
        for key,value in [('snapshot','only a title'),('truncated',True),('tab','outside')]:
            rows,origin=records(data);rows[3]['response'][key]=value
            with self.subTest(key=key), self.assertRaises(ValueError):trace.verify(data,rows,origin)

    def test_closed_replaced_expired_or_wrong_final_tab_fails(self):
        data=dataset('S10',11)
        for key,value in [('tab','replacement'),('permission','expired'),
                          ('url','http://127.0.0.1:54442/?document=UNRELATED-11'),('active',False)]:
            rows,origin=records(data);rows[-1]['response']['tabs'][0][key]=value
            with self.subTest(key=key), self.assertRaises(ValueError):trace.verify(data,rows,origin)

    def test_mutation_reconsent_navigation_and_replay_fail(self):
        data=dataset('S10',11)
        for command in ('attach','navigate','click','fill','detach'):
            rows,origin=records(data);rows[2]['request']['command']=command
            with self.subTest(command=command),self.assertRaises(ValueError):trace.verify(data,rows,origin)
        rows,origin=records(data);rows[3]['response']['execution_settled']=False
        with self.assertRaises(ValueError):trace.verify(data,rows,origin)
        rows,origin=records(data);rows[2]['request']['id']=rows[3]['response']['id']='0'
        with self.assertRaises(ValueError):trace.verify(data,rows,origin)

    def test_oracle_requires_both_content_and_tab_evidence(self):
        with tempfile.TemporaryDirectory() as root:
            f=Fixture('S10',11,Path(root)/'run')
            try:
                for d in f.data['documents']:f.view({'document':d['id']})
                keys=('id','processing_days','free_shipping_minimum_krw','return_days','source')
                answer={'documents':[{k:d[k] for k in keys} for d in f.data['documents']]}
                rows,origin=records(f.data)
                self.assertFalse(oracle.verify(f.directory,answer)['success'])
                self.assertTrue(oracle.verify(f.directory,answer,rows,tab_origin=origin)['success'])
                answer['documents'][0]['processing_days']+=1
                self.assertFalse(oracle.verify(f.directory,answer,rows,tab_origin=origin)['success'])
            finally:f.close()

    def test_cleanup_after_valid_terminal_inventory_is_still_failure(self):
        data=dataset('S10',11)
        for command in ('detach','cancel'):
            rows,origin=records(data)
            self.assertTrue(trace.verify(data,rows,origin)['original_grants_preserved'])
            n=len(rows)
            rows.extend([
                {'sequence':n+1,'kind':'request','request':{'id':'cleanup','command':command}},
                {'sequence':n+2,'kind':'response','response_source':'mailbox',
                 'response':{'id':'cleanup','ok':True,'execution_settled':True,'status':'detached'}}])
            with self.subTest(command=command),self.assertRaisesRegex(ValueError,'begin and end'):
                trace.verify(data,rows,origin)


if __name__=='__main__':unittest.main()
