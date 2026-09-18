import importlib.util
import copy
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('usage', Path(__file__).with_name('import-kimi-usage.py'))
usage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(usage)


class UsageTests(unittest.TestCase):
    def events(self):
        return [{'type': 'llm.request', 'modelAlias': 'kimi-code/kimi-for-coding', 'model': 'kimi-for-coding'},
                {'type': 'usage.record', 'model': 'kimi-code/kimi-for-coding', 'usageScope': 'turn',
                 'usage': {'inputOther': 2, 'inputCacheRead': 5, 'inputCacheCreation': 1, 'output': 3}}]

    def test_cache_is_inclusive_and_mirrors_are_not_double_counted(self):
        events = self.events() + [{'type': 'context.append_loop_event', 'event': {'type': 'step.end', 'usage': {}}}]
        result = usage.normalize(events)
        self.assertEqual(result['usage']['input_tokens'], 8)
        self.assertEqual(result['usage']['total_tokens'], 11)
        self.assertIsNone(result['reported_cost_usd'])

    def test_duplicate_missing_or_wrong_model_fails(self):
        events = self.events()
        for invalid in (events[:1], events + events[1:]):
            with self.assertRaises(ValueError):
                usage.normalize(invalid)
        events[0]['model'] = 'different'
        with self.assertRaises(ValueError):
            usage.normalize(events)

    def multi_events(self):
        events = []
        for kind, scope, scale in [('loop', 'turn', 1), ('compaction', 'session', 2),
                                   ('loop', 'turn', 3)]:
            request, record = self.events()
            request['kind'] = kind
            record['usageScope'] = scope
            record['usage'] = {key: value * scale for key, value in record['usage'].items()}
            events.extend([request, {'type': 'step.end', 'usage': record['usage']}, record])
        return events

    def test_multiple_calls_include_compaction_and_cache_once(self):
        result = usage.normalize(self.multi_events(), multi_call=True)
        self.assertEqual(result['model_calls'], 3)
        self.assertEqual(result['loop_calls'], 2)
        self.assertEqual(result['compaction_calls'], 1)
        self.assertEqual(result['usage']['input_tokens'], 48)
        self.assertEqual(result['usage']['cached_input_tokens'], 30)
        self.assertEqual(result['usage']['output_tokens'], 18)
        self.assertEqual(result['usage']['total_tokens'], 66)
        self.assertEqual([c['request_event_index'] for c in result['calls']], [0, 3, 6])
        self.assertFalse(result['trial_boundary_verified'])
        self.assertFalse(result['billing_reconciled'])

    def test_multi_call_is_opt_in(self):
        with self.assertRaises(ValueError):
            usage.normalize(self.multi_events())

    def test_retry_missing_duplicate_and_replayed_usage_fail(self):
        events = self.multi_events()
        for invalid in (events[:-1], events[2:], events + events[-1:],
                        events[:1] + events, events[:2] + events[3:], []):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                usage.normalize(invalid, multi_call=True)

    def test_scope_model_and_version_must_be_known(self):
        for index, key, value in [(0, 'kind', None), (0, 'kind', 'unknown'),
                                   (3, 'model', 'other'), (5, 'model', 'other'),
                                   (5, 'usageScope', 'turn'), (2, 'usageScope', 'session'),
                                   (0, 'agentId', 'main'), (2, 'agentId', 'main')]:
            events = self.multi_events()
            events[index][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True)

    def test_partial_boolean_negative_and_noninteger_counters_fail(self):
        for value in (None, True, -1, 1.5, '3'):
            events = self.multi_events()
            events[5]['usage']['inputCacheRead'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True)
        for raw in ({}, None, []):
            events = copy.deepcopy(self.multi_events())
            events[5]['usage'] = raw
            with self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True)

    def test_nonobject_event_fails(self):
        with self.assertRaises(ValueError):
            usage.normalize([None], multi_call=True)

    def v2_events(self):
        events = self.multi_events()
        for event in events:
            event['agentId'] = 'main'
        events[0]['turnStep'] = '0.1'
        events[6]['turnStep'] = '0.2'
        return events

    def test_v2_main_counts_each_call_and_compaction_once(self):
        result = usage.normalize(self.v2_events(), multi_call=True, v2_main=True)
        self.assertEqual(result['usage']['total_tokens'], 66)
        self.assertEqual(result['compaction_calls'], 1)
        self.assertEqual(result['wire_format'], 'agent-core-v2')
        self.assertFalse(result['model_identity_verified'])
        self.assertFalse(result['provider_retry_accounting_verified'])
        self.assertFalse(result['trial_boundary_verified'])

    def test_v2_cannot_silently_enter_v1_single_or_multi_mode(self):
        for kwargs in ({}, {'multi_call': True}, {'v2_main': True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                usage.normalize(self.v2_events()[:3], **kwargs)

    def test_v2_mixed_identity_and_background_agent_fail(self):
        for index in (0, 1, 2, 5):
            events = self.v2_events()
            events[index]['agentId'] = 'child'
            with self.subTest(index=index), self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True, v2_main=True)
        for index in (0, 2):
            events = self.v2_events()
            del events[index]['agentId']
            with self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True, v2_main=True)

    def test_v2_replayed_complete_pair_and_invalid_step_fail(self):
        events = self.v2_events()
        with self.assertRaises(ValueError):
            usage.normalize(events + events[:3], multi_call=True, v2_main=True)
        for step in (None, True, '0.1', '00.2', '0.0', 'bad'):
            events = self.v2_events()
            events[6]['turnStep'] = step
            with self.subTest(step=step), self.assertRaises(ValueError):
                usage.normalize(events, multi_call=True, v2_main=True)

    def test_v2_fake_provider_model_is_not_actual_kimi_usage(self):
        events = self.v2_events()
        events[0].update(modelAlias='local-probe', model='claude-sonnet-4-20250514')
        events[2]['model'] = 'local-probe'
        with self.assertRaisesRegex(ValueError, 'unexpected model'):
            usage.normalize(events, multi_call=True, v2_main=True)


if __name__ == '__main__':
    unittest.main()
