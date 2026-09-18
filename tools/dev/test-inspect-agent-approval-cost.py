import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('approval', Path(__file__).with_name('inspect-agent-approval-cost.py'))
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


def pair(command, identity, stamp, wait):
    return [
        {'sequence': 1, 'kind': 'request', 'time_ns': stamp,
         'request': {'id': identity, 'command': command}},
        {'sequence': 2, 'kind': 'response', 'time_ns': stamp+1, 'response_source': 'mailbox',
         'response': {'id': identity, 'ok': True, 'tab': 'tab', 'execution_settled': True,
                      'timing': {'native_elapsed_ms': wait+1, 'user_wait_ms': wait}}}]


class Tests(unittest.TestCase):
    def test_initial_wait_is_included_and_default_policy_is_ask(self):
        result = a.inspect(pair('attach', 'a', 10, 5000), pair('click', 'b', 20, 0))
        self.assertEqual(result['combined_recorded_wait_seconds'], 5)
        self.assertEqual(result['execution_wait_seconds'], 0)
        self.assertEqual(result['declared_task_rules'], dict(fill='ask', click='ask', navigate='ask'))
        self.assertIsNone(result['whole_task_seconds'])
        self.assertFalse(result['runtime_permission_enforcement_verified'])

    def test_missing_timing_wrong_tab_or_unsettled_is_not_zero_cost(self):
        for field, value in (('timing', {}), ('tab', 'other'), ('execution_settled', False)):
            action = pair('click', 'b', 20, 0)
            action[-1]['response'][field] = value
            with self.assertRaises(ValueError): a.inspect(pair('attach', 'a', 10, 5000), action)

    def test_stale_or_overlapping_consent_and_authority_changes_rejected(self):
        for command, identity, stamp in (('click', 'b', 5), ('click', 'a', 20), ('attach', 'b', 20),
                                          ('select-tab', 'b', 20), ('detach', 'b', 20)):
            with self.assertRaises(ValueError):
                a.inspect(pair('attach', 'a', 10, 5000), pair(command, identity, stamp, 0))

    def test_failed_consent_and_unknown_rules_rejected(self):
        consent = pair('attach', 'a', 10, 5000)
        consent[-1]['response']['ok'] = False
        with self.assertRaises(ValueError): a.inspect(consent, pair('click', 'b', 20, 0))
        consent = pair('attach', 'a', 10, 5000)
        consent[0]['request']['permissions'] = {'shell': 'allow'}
        with self.assertRaises(ValueError): a.inspect(consent, pair('click', 'b', 20, 0))


if __name__ == '__main__': unittest.main()
