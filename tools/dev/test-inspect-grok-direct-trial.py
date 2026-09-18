import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('inspector', Path(__file__).with_name('inspect-grok-direct-trial.py'))
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


class TimingTests(unittest.TestCase):
    def test_actual_turn_costs_include_cache_but_never_private_content(self):
        events = [{'type':'assistant','message':{'usage':{
            'input_tokens':10,'cache_read_input_tokens':20,'cache_creation_input_tokens':3,'output_tokens':4},
            'content':[{'type':'thinking','thinking':'PRIVATE_THOUGHT','signature':'PRIVATE_SIGNATURE'},
                       {'type':'tool_use','name':'use_tool','input':{'secret':'PRIVATE_ARGUMENT'}}]}}]
        result = inspector.model_turn_costs(events)
        self.assertEqual(result,[{'index':1,'input_tokens_including_cache':33,'output_tokens':4,
                                  'total_tokens':37,'cached_input_tokens':20,'tools':['use_tool']}])
        self.assertNotIn('PRIVATE',json.dumps(result))
        events[0]['message']['usage']['input_tokens'] = True
        self.assertIsNone(inspector.model_turn_costs(events)[0]['total_tokens'])

    def test_mcp_result_keeps_tool_identity_and_native_text(self):
        value = {'type': 'MCP', 'server_name': 'yee', 'tool_name': 'yee_browser',
                 'output': {'OkayOutput': '{"ok":true}'}}
        result = inspector.tool_result({'tool_use_id': 'm1', 'content': json.dumps(value)})
        self.assertEqual(result['server'], 'yee')
        self.assertEqual(result['tool'], 'yee_browser')
        self.assertEqual(result['output'], '{"ok":true}')
        self.assertIsNone(result['exit_code'])
        value['output'] = {'NewUnknownShape': 'failure'}
        self.assertTrue(inspector.tool_result({'tool_use_id': 'm1',
                                              'content': json.dumps(value)})['unrecognized_result'])

    def test_file_tools_preserve_evidence_without_fabricated_exit_codes(self):
        for content, expected in [
            ({'type': 'ReadFile', 'FileContent': {'absolute_path': '/test', 'raw_output': 'plan'}}, 'plan'),
            ({'type': 'SearchReplace', 'EditsApplied': {'absolute_path': '/test',
              'tool_output_for_prompt': 'updated'}}, 'updated')]:
            result = inspector.tool_result({'tool_use_id': 'call-1', 'content': json.dumps(content)})
            self.assertEqual(result['output'], expected)
            self.assertEqual(result['path'], '/test')
            self.assertIsNone(result['exit_code'])
            self.assertNotIn('unrecognized_result', result)

    def test_unknown_and_cancelled_tool_results_are_not_silently_successful(self):
        unknown = inspector.tool_result({'tool_use_id': 'x', 'content': '{"type":"NewShape"}'})
        self.assertTrue(unknown['unrecognized_result'])
        cancelled = inspector.tool_result({'tool_use_id': 'x', 'content': '["cancelled"]'})
        self.assertTrue(cancelled['tool_error'])
        self.assertIsNone(cancelled['exit_code'])

    def test_native_timings_are_separate_from_cli_roundtrip(self):
        self.assertEqual(inspector.native_timing({'timing': {'native_elapsed_ms': 1250, 'user_wait_ms': 1000}}),
                         {'native_elapsed_seconds': 1.25, 'user_wait_seconds': 1.0, 'native_non_wait_seconds': 0.25})

    def test_missing_invalid_and_impossible_timings_stay_unknown(self):
        for timing in [None, {}, {'native_elapsed_ms': 1, 'user_wait_ms': 2},
                       {'native_elapsed_ms': True, 'user_wait_ms': 0},
                       {'native_elapsed_ms': float('nan'), 'user_wait_ms': 0},
                       {'native_elapsed_ms': 1, 'user_wait_ms': -1}]:
            self.assertIsNone(inspector.native_timing({'timing': timing}))


if __name__ == '__main__':
    unittest.main()
