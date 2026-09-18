import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    'latency', Path(__file__).with_name('analyze-browser-trial-latency.py'))
latency = importlib.util.module_from_spec(spec)
spec.loader.exec_module(latency)


class Tests(unittest.TestCase):
    def summary(self):
        return {'model_process_timing': {'clock': 'monotonic_ns',
                'started_monotonic_ns': 1_000_000_000, 'finished_monotonic_ns': 11_000_000_000,
                'elapsed_seconds': 10.0}}

    def yee_rows(self):
        return [
            {'kind': 'mcp_request', 'invocation': 'a', 'name': 'yee_browser', 'monotonic_ns': 2_000_000_000},
            {'kind': 'mcp_response', 'invocation': 'a', 'monotonic_ns': 3_000_000_000},
            {'kind': 'mcp_request', 'invocation': 'b', 'name': 'yee_browser', 'monotonic_ns': 6_000_000_000},
            {'kind': 'mcp_response', 'invocation': 'b', 'monotonic_ns': 7_000_000_000},
        ]

    def test_disjoint_process_accounting(self):
        result = latency.analyze(self.summary(), 'yee', self.yee_rows())
        self.assertEqual(result['seconds'], {
            'before_first_mcp_seconds': 1.0,
            'inside_mcp_seconds': 2.0,
            'between_mcp_seconds': 3.0,
            'after_last_mcp_seconds': 4.0,
        })
        self.assertEqual(result['browser_tool_mcp_seconds'], 2.0)

    def test_missing_or_overlapping_timing_is_rejected(self):
        rows = self.yee_rows()
        del rows[0]['monotonic_ns']
        with self.assertRaises(ValueError):
            latency.analyze(self.summary(), 'yee', rows)

    def test_aside_journal_path_prefers_explicit_or_recorded_absolute_location(self):
        record = Path('/private/tmp/record/model')
        explicit = Path('/private/tmp/record/explicit-wire.jsonl')
        self.assertEqual(latency.aside_wire_path(record, self.summary(), explicit), explicit)
        recorded = '/private/tmp/record/recorded-wire.jsonl'
        self.assertEqual(latency.aside_wire_path(record,
                         {**self.summary(), 'mcp_wire_record': recorded}), Path(recorded))
        with self.assertRaises(ValueError):
            latency.aside_wire_path(record, {**self.summary(), 'mcp_wire_record': 'relative.jsonl'})
        rows = self.yee_rows()
        rows[2]['monotonic_ns'] = 2_500_000_000
        with self.assertRaises(ValueError):
            latency.analyze(self.summary(), 'yee', rows)


if __name__ == '__main__':
    unittest.main()
