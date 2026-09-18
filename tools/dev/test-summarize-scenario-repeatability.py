import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('repeatability',Path(__file__).with_name('summarize-scenario-repeatability.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Tests(unittest.TestCase):
    def rows(self):
        return [dict(case=f'/sample/{i}/{browser}/S11',browser=browser,scenario='S11',accepted=i==0,
            failures=[] if i==0 else ['model_did_not_complete'],usage={'total_tokens':100+i*50},
            zero_wait_seconds=10+i*20,fixture_seed=11 if i==0 else 29,
            collection_status='model_sample_collected',runtime_fingerprint='same',
            correlation={'mcp_calls':5+i}) for i in range(2) for browser in ('yee','aside')]
    def test_failures_count_in_totals_median_and_tail(self):
        v=m.summarize(self.rows());g=v['groups']['S11']['yee']
        self.assertEqual(v['totals']['yee']['tokens_including_failures'],250)
        self.assertEqual(g['seconds'],dict(n=2,median=20,minimum=10,maximum=30,range=20))
        self.assertEqual(g['accepted'],1);self.assertFalse(v['groups']['S11']['all_samples_accepted_in_both'])
    def test_duplicates_source_mix_and_unknown_cost_refused(self):
        for key,value in [('runtime_fingerprint','other'),('usage',None),('zero_wait_seconds',float('nan'))]:
            rows=self.rows();rows[1][key]=value
            with self.assertRaises(ValueError):m.summarize(rows)
        rows=self.rows()
        with self.assertRaisesRegex(ValueError,'duplicate'):m.summarize([*rows,copy.deepcopy(rows[0])])
    def test_setup_failure_never_becomes_a_zero_cost_model_sample(self):
        rows=self.rows();rows.append(dict(case='/setup/yee/S01',collection_status='no_model_sample',accepted=False))
        v=m.summarize(rows);self.assertEqual(len(v['setup_failures']),1)
        self.assertEqual(v['totals']['yee']['samples'],2)
    def test_no_samples_is_unknown_not_a_performance_win(self):
        d=m.distribution([]);self.assertEqual(d['n'],0);self.assertIsNone(d['median'])
        self.assertIsNone(m.summarize([])['runtime_fingerprint'])

if __name__=='__main__':unittest.main()
