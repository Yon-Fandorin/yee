import copy
import json
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('inspect-yee-mcp-calls.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditTests(unittest.TestCase):
    def test_schema_rejected_local_call_has_no_invented_mcp_interval(self):
        name='mcp__yee__yee_browser'
        schema={'type':'object','properties':{'action':{'const':'observe'}},
                'required':['action'],'additionalProperties':False}
        events=[{'role':'assistant','tool_calls':[{'id':'local','function':{'name':name,
                  'arguments':'{"action":"observe","extra":true}'}}]},
                {'role':'tool','tool_call_id':'local','content':f'Invalid args for tool "{name}": unexpected extra property'}]
        rejected=[]
        self.assertTrue(audit.correlate_model(events,[],input_schemas={name:schema},local_rejections=rejected))
        self.assertEqual([r['call_id'] for r in rejected],['local'])
        # A log gap cannot become a local rejection merely by labeling its
        # result as one: valid args, missing replies and wrong identities fail.
        mutations=[]
        valid=copy.deepcopy(events);valid[0]['tool_calls'][0]['function']['arguments']='{"action":"observe"}';mutations.append(valid)
        wrong=copy.deepcopy(events);wrong[1]['content']='Invalid args for tool "other": error';mutations.append(wrong)
        missing=copy.deepcopy(events[:1]);mutations.append(missing)
        for rows in mutations:
            with self.assertRaises(ValueError):audit.correlate_model(rows,[],input_schemas={name:schema})
        with self.assertRaises(ValueError):audit.correlate_model(events,[])
        with self.assertRaises(ValueError):audit.correlate_model(events,[],input_schemas={name:{'$ref':'https://example.test/schema'}})

    def records(self):
        calls = [
            {'sequence': 1, 'kind': 'mcp_request', 'invocation': 'a', 'native_sequence_start': 0},
            {'sequence': 2, 'kind': 'mcp_response', 'invocation': 'a', 'native_sequence_end': 4,
             'is_error': False, 'content': ['{}']},
            {'sequence': 3, 'kind': 'mcp_request', 'invocation': 'b', 'native_sequence_start': 4},
            {'sequence': 4, 'kind': 'mcp_response', 'invocation': 'b', 'native_sequence_end': 4,
             'is_error': True, 'content': ['{"error":"syntax"}']}]
        native = []
        for i in range(2):
            native.extend([
                {'sequence': i*2+1, 'kind': 'request', 'request': {'id': str(i), 'command': 'observe'}},
                {'sequence': i*2+2, 'kind': 'response', 'response_source': 'mailbox', 'response': {
                    'id': str(i), 'ok': True, 'execution_settled': True,
                    'timing': {'native_elapsed_ms': 10, 'user_wait_ms': 5}}}])
        return calls, native

    def test_expanded_commands_and_zero_dispatch_errors(self):
        result = audit.inspect(*self.records())
        self.assertEqual(result['mcp_calls'], 2)
        self.assertEqual(result['native_requests'], 2)
        self.assertEqual(result['native_user_wait_seconds'], .01)
        self.assertEqual(result['intervals'][1]['native_commands'], [])
        self.assertFalse(result['model_call_correlation_verified'])
        self.assertIsNone(result['scenario_success'])

    def recovered_records(self):
        calls, native = self.records()
        calls[2].update(native_sequence_start=4)
        calls[3].update(native_sequence_end=6, is_error=False, content=['{}'])
        native[1]['time_ns'] = 100
        native.extend([
            {'sequence': 5, 'kind': 'recovery', 'request_id': '0', 'time_ns': 200},
            {'sequence': 6, 'kind': 'response', 'time_ns': 300,
             'response_source': 'native_archive',
             'response': copy.deepcopy(native[1]['response'])}])
        return calls, native

    def test_recovered_same_receipt_keeps_dispatch_and_wait_once(self):
        result = audit.inspect(*self.recovered_records())
        self.assertEqual(result['native_requests'], 2)
        self.assertEqual(result['receipt_recoveries'], 1)
        self.assertEqual(result['native_user_wait_seconds'], .01)
        self.assertEqual(result['intervals'][1]['native_commands'], [])

    def test_recovery_cannot_hide_changed_missing_or_reordered_receipt(self):
        changes = [
            (4, 'request_id', 'unknown'), (4, 'request_id', []),
            (4, 'time_ns', 99), (4, 'time_ns', True),
            (5, 'kind', 'request'), (5, 'response_source', 'invented'),
            (5, 'time_ns', 199),
            (5, 'response', {'id': '0', 'ok': True}),
        ]
        for index, key, value in changes:
            calls, native = self.recovered_records()
            native[index][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                audit.inspect(calls, native)
        calls, native = self.recovered_records()
        native[5]['response']['timing']['user_wait_ms'] = True
        with self.assertRaises(ValueError): audit.inspect(calls, native)

    def test_recovery_does_not_make_unknown_or_unsettled_original_known(self):
        for key, value in [('timing', None), ('execution_settled', False)]:
            calls, native = self.recovered_records()
            native[1]['response'][key] = value
            native[5]['response'] = copy.deepcopy(native[1]['response'])
            result = audit.inspect(calls, native)
            if key == 'timing': self.assertIsNone(result['native_user_wait_seconds'])
            else: self.assertFalse(result['native_settlement_verified'])

    def test_missing_reordered_overlapping_and_outside_intervals_fail(self):
        base_calls, native = self.records()
        for index, key, value in [(1, 'native_sequence_end', 3),
                                  (2, 'native_sequence_start', 2),
                                  (3, 'native_sequence_end', 3),
                                  (3, 'invocation', 'wrong'), (1, 'sequence', 1)]:
            calls = copy.deepcopy(base_calls); calls[index][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): audit.inspect(calls, native)
        with self.assertRaises(ValueError): audit.inspect(base_calls[:-1], native)
        with self.assertRaises(ValueError): audit.inspect(base_calls[2:], native)

    def client_error_records(self, after_success=True):
        calls, native = self.records()
        calls = calls[:2]
        native = native[:2] if after_success else []
        native.append({'sequence': len(native) + 1, 'kind': 'client_error',
                       'time_ns': 100, 'error': 'missing document'})
        calls[1].update(native_sequence_end=len(native), is_error=True,
                        content=[json.dumps([{'ok': True},
                            {'ok': False, 'error': 'missing document'}])])
        return calls, native

    def test_terminal_client_error_preserves_native_cost_and_failure(self):
        for after_success in (False, True):
            result = audit.inspect(*self.client_error_records(after_success))
            self.assertEqual(result['native_requests'], int(after_success))
            self.assertEqual(result['client_errors'], 1)
            self.assertEqual(result['native_user_wait_seconds'], .005 if after_success else 0)
            self.assertTrue(result['intervals'][0]['tool_error'])
            self.assertIsNone(result['scenario_success'])

    def test_client_error_cannot_hide_incomplete_or_later_dispatch(self):
        for order in ([0, 2], [2, 0, 1], [0, 1, 2, 2]):
            calls, native = self.client_error_records()
            native = [copy.deepcopy(native[i]) for i in order]
            for sequence, event in enumerate(native, 1): event['sequence'] = sequence
            calls[1]['native_sequence_end'] = len(native)
            with self.subTest(order=order), self.assertRaises(ValueError):
                audit.inspect(calls, native)

    def test_client_error_requires_matching_failed_envelope(self):
        for key, value in [('is_error', False), ('content', []),
                           ('content', ['not json']), ('content', ['{"ok":true,"error":"missing document"}']),
                           ('content', ['{"ok":false,"error":"other"}'])]:
            calls, native = self.client_error_records(); calls[1][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                audit.inspect(calls, native)
        for key, value in [('kind', 'unknown'), ('time_ns', True), ('error', '')]:
            calls, native = self.client_error_records(); native[-1][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): audit.inspect(calls, native)

    def test_duplicate_and_mismatched_native_ids_fail(self):
        calls, native = self.records()
        native[3]['response']['id'] = 'other'
        with self.assertRaises(ValueError): audit.inspect(calls, native)
        native[2]['request']['id'] = native[3]['response']['id'] = '0'
        with self.assertRaises(ValueError): audit.inspect(calls, native)

    def test_unknown_replayed_or_unsettled_timing_is_not_zero(self):
        for field, value in [('timing', None), ('timing', {'native_elapsed_ms': 1, 'user_wait_ms': 2}),
                             ('timing', {'native_elapsed_ms': 1, 'user_wait_ms': True}),
                             ('execution_settled', False)]:
            calls, native = self.records(); native[1]['response'][field] = value
            self.assertIsNone(audit.inspect(calls, native)['native_user_wait_seconds'])
        calls, native = self.records(); native[1]['response_source'] = 'native_archive'
        self.assertIsNone(audit.inspect(calls, native)['native_user_wait_seconds'])

    def test_exact_model_call_and_result_required(self):
        calls, _ = self.records(); calls = calls[:2]
        calls[0].update(name='yee_browser', arguments={'commands': [['status']]})
        events = [{'role': 'assistant', 'tool_calls': [{'id': 'tool1', 'function': {
            'name': 'mcp__yee__yee_browser', 'arguments': json.dumps(calls[0]['arguments'])}}]},
            {'role': 'tool', 'tool_call_id': 'tool1', 'content': '{}'}]
        self.assertTrue(audit.correlate_model(events, calls))
        for invalid in (events[:1], events+events, events[1:]):
            with self.assertRaises(ValueError): audit.correlate_model(invalid, calls)
        changed = copy.deepcopy(events); changed[1]['content'] = '{"altered":true}'
        with self.assertRaises(ValueError): audit.correlate_model(changed, calls)

    def test_historical_multiple_blocks_match_observed_kimi_concatenation(self):
        calls, _ = self.records(); calls = calls[:2]
        calls[0].update(name='yee_browser', arguments={'commands': [['status'], ['status']]})
        calls[1]['content'] = ['{"n":1}', '{"n":2}']
        blocks = [{'type': 'text', 'text': t} for t in calls[1]['content']]
        events = [{'role': 'assistant', 'tool_calls': [{'id': 'tool1', 'function': {
            'name': 'mcp__yee__yee_browser', 'arguments': json.dumps(calls[0]['arguments'])}}]},
            {'role': 'tool', 'tool_call_id': 'tool1', 'content': ''.join(calls[1]['content'])}]
        self.assertTrue(audit.correlate_model(events, calls))
        for invalid in (blocks[::-1], blocks[:1], blocks+blocks,
                        '\n'.join(calls[1]['content'])):
            changed = copy.deepcopy(events); changed[1]['content'] = invalid
            with self.assertRaises(ValueError): audit.correlate_model(changed, calls)


if __name__ == '__main__': unittest.main()
