import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('timing', Path(__file__).with_name('inspect-native-timing.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def pair(timing=None):
    return [{'sequence': 1, 'kind': 'request', 'request': {'id': 'a', 'command': 'batch'}},
            {'sequence': 2, 'kind': 'response', 'response': {'id': 'a', 'timing': timing}}]


class AuditTests(unittest.TestCase):
    def test_wait_is_retained_and_whole_task_unknown(self):
        result = module.analyze(pair({'native_elapsed_ms': 1000, 'user_wait_ms': 980}))
        self.assertTrue(result['timing_audit_pass'])
        self.assertEqual(result['totals_ms']['native_nonwait_ms'], 20)
        self.assertEqual(result['totals_ms']['native_elapsed_ms'], 1000)
        self.assertIsNone(result['whole_task_elapsed_seconds'])

    def test_invalid_or_missing_timing_fails(self):
        for timing in (None, {}, {'native_elapsed_ms': True, 'user_wait_ms': 0},
                       {'native_elapsed_ms': float('nan'), 'user_wait_ms': 0},
                       {'native_elapsed_ms': 1, 'user_wait_ms': 2},
                       {'native_elapsed_ms': 1, 'user_wait_ms': -1}):
            with self.subTest(timing=timing):
                self.assertFalse(module.analyze(pair(timing))['timing_audit_pass'])

    def test_unpaired_duplicate_and_sequence_fail(self):
        good = pair({'native_elapsed_ms': 1, 'user_wait_ms': 0})
        for events in (good[:1], good[1:], good + good, []):
            self.assertFalse(module.analyze(events)['timing_audit_pass'])

    def test_client_error_prevents_pass(self):
        events = pair({'native_elapsed_ms': 1, 'user_wait_ms': 0})
        events.append({'sequence': 3, 'kind': 'client_error'})
        self.assertFalse(module.analyze(events)['timing_audit_pass'])


if __name__ == '__main__':
    unittest.main()
