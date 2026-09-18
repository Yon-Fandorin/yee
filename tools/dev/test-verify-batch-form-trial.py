import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('gate', Path(__file__).with_name('verify-batch-form-trial.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class BatchGateTests(unittest.TestCase):
    def fixture(self):
        initial = {'id':'a','ok':True,'truncated':False,'document':'doc','revision':1,
                   'viewport':{'width':1440,'height':900},
                   'snapshot':'page @doc rev=1\n+@doc_1 field "Name" value=""\n+@doc_2 button "Save locally"\n+@doc_3 text "Waiting for input"\n'}
        final = {**initial,'id':'b','revision':2,'completed':2,
                 'snapshot':'page @doc rev=2 delta\n~@doc_1 field "Name" value="Cedar"\n~@doc_3 text "Saved: Cedar"\n'}
        requests = [{'id':'a','command':'observe','full':True},
                    {'id':'b','command':'batch','baseline_document':'doc','baseline_revision':1,
                     'actions':gate.resolve_batch(initial,gate.PLAN)[1]}]
        rows = []
        for request, response in zip(requests,(initial,final)):
            rows.extend([{'kind':'request','request':request},{'kind':'response','response':response}])
        summary = {'usage':{'total_tokens':100},'elapsed_seconds':5,'reported_cost_usd':0.1,
                   'returncode':0,'timed_out':False,'allowed_mcp_tool':'yee__yee_browser'}
        evidence = {'provider_usage':summary['usage'],'total_native_user_wait_seconds':2,
                    'terminal_error':False,'terminal_subtype':'success','answer':'Saved: Cedar',
                    'calls':[{'id':'t1','tool':'search_tool'}, {'id':'t2','tool':'use_tool','input':{
                        'tool_name':'yee__yee_browser','tool_input':{'commands':[['batch-named',json.dumps(gate.PLAN)]]}}}],
                    'results':[{'id':'t1'},{'id':'t2','server':'yee','tool':'yee_browser','output':json.dumps(gate.compact_response(final))}]}
        return summary,evidence,rows

    def test_valid(self):
        self.assertTrue(gate.verify_evidence(*self.fixture())['success'])

    def test_tampering_is_not_success_and_usage_retained(self):
        for mutate in (lambda s,e,r:r[2]['request']['actions'][0].update(value='Birch'),
                       lambda s,e,r:r[2]['request'].update(baseline_revision=2),
                       lambda s,e,r:r[3]['response'].update(completed=1),
                       lambda s,e,r:r[3]['response'].update(viewport={'width':1,'height':1}),
                       lambda s,e,r:e['calls'].append(e['calls'][0]),
                       lambda s,e,r:e['results'][1].update(output='{}'),
                       lambda s,e,r:e.update(total_native_user_wait_seconds=None),
                       lambda s,e,r:s.update(elapsed_seconds=float('nan'))):
            args = self.fixture()
            mutate(*args)
            result = gate.verify_evidence(*args)
            self.assertFalse(result['success'])
            self.assertEqual(result['tokens'],100)


if __name__ == '__main__':
    unittest.main()
