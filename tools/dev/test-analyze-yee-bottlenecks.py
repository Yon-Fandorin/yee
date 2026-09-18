import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('analyzer', Path(__file__).with_name('analyze-yee-bottlenecks.py'))
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)

class Tests(unittest.TestCase):
    def records(self):
        return ({'runner_elapsed_seconds': 10}, [
            {'sequence': 1, 'kind': 'request', 'request': {'id': 'x', 'command': 'fill'}},
            {'sequence': 2, 'kind': 'response', 'response': {'id': 'x', 'timing': {'native_elapsed_ms': 6000, 'user_wait_ms': 5000}}}], [
            {'kind': 'mcp_request', 'invocation': 'x', 'time_ns': 1000000000},
            {'kind': 'mcp_response', 'invocation': 'x', 'time_ns': 8000000000, 'content': ['{"ok":true}']}], {})

    def test_disjoint_time_attribution(self):
        result = a.analyze(*self.records())
        self.assertEqual(result['seconds'], {'native_approval_wait':5, 'native_processing':1,
                         'inside_mcp_other':1, 'outside_mcp_unattributed':3})
        self.assertEqual(sum(result['percent'].values()), 100)

    def test_missing_or_unmatched_responses_rejected(self):
        records = self.records()
        records[2].pop()
        with self.assertRaises(ValueError): a.analyze(*records)
        records = self.records()
        records[2][-1]['invocation'] = 'wrong'
        with self.assertRaises(ValueError): a.analyze(*records)

    def test_overlapping_or_inconsistent_clocks_rejected(self):
        records = self.records()
        records[2].insert(1, records[2][0].copy())
        with self.assertRaises(ValueError): a.analyze(*records)
        records = self.records()
        records[0]['runner_elapsed_seconds'] = 2
        with self.assertRaises(ValueError): a.analyze(*records)

    def test_missing_native_timing_does_not_become_zero(self):
        records = self.records()
        del records[1][1]['response']['timing']
        with self.assertRaises(ValueError): a.analyze(*records)

if __name__ == '__main__': unittest.main()
