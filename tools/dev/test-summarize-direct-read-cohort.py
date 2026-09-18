import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('summary', Path(__file__).with_name('summarize-direct-read-cohort.py'))
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class IncompleteTests(unittest.TestCase):
    def test_missing_trials_remain_failed_and_unknown(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'cohort.json').write_text(json.dumps({'trials': [{'trial': '1-yee', 'runner_exit': 1}]}))
            result = summary.summarize(root)
            self.assertEqual(len(result['rows']), 1)
            self.assertFalse(result['rows'][0]['success'])
            self.assertIsNone(result['rows'][0]['tokens'])
            self.assertEqual(result['aggregates']['yee']['unknown_usage_trials'], 1)
            self.assertIsNone(result['aggregates']['yee']['mean_tokens'])
            self.assertIsNone(result['aggregates']['yee']['mean_wall_seconds'])

    def test_partial_stream_is_not_dropped_or_counted_as_zero_tokens(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'cohort.json').write_text(json.dumps({'trials': [{'trial': '1-aside', 'runner_exit': 1}]}))
            call = root / '1-aside'
            call.mkdir()
            (call / 'summary.json').write_text(json.dumps({'elapsed_seconds': 120, 'reported_cost_usd': None}))
            (call / 'stdout.json').write_text('{"type":')
            result = summary.summarize(root)
            self.assertFalse(result['rows'][0]['success'])
            self.assertIsNone(result['rows'][0]['tokens'])
            self.assertEqual(result['aggregates']['aside']['mean_wall_seconds'], 120)

    def test_historical_warm_label_is_not_session_reuse_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'cohort.json').write_text(json.dumps({'boundary':'warm read-only',
                'trials':[{'trial':'1-yee','runner_exit':1}]}))
            result=summary.summarize(root)
            self.assertFalse(result['persistent_session_reuse_verified'])
            self.assertFalse(result['whole_task_timing_verified'])
            self.assertIn('persistent-session reuse unverified',result['scope'])

    def test_duplicate_unknown_harness_and_path_escape_entries_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for labels in (['1-yee','1-yee'],['1-other'],['../1-yee']):
                (root/'cohort.json').write_text(json.dumps({'trials':[
                    {'trial':label,'runner_exit':0} for label in labels]}))
                with self.assertRaises(ValueError):summary.summarize(root)

    def test_invalid_time_is_unmeasured_not_zero_or_nan(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);call=root/'1-yee';call.mkdir()
            (root/'cohort.json').write_text(json.dumps({'trials':[{'trial':'1-yee','runner_exit':1}]}))
            for wall in (True,-1,float('nan'),float('inf'),'120'):
                (call/'summary.json').write_text(json.dumps({'elapsed_seconds':wall}))
                result=summary.summarize(root)
                self.assertIsNone(result['aggregates']['yee']['mean_wall_seconds'])
                self.assertIsNone(result['rows'][0]['wall_seconds'])


if __name__ == '__main__':
    unittest.main()
