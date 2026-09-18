#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('codex_probe', Path(__file__).with_name('probe-codex-tool-policy.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def record(self):
        return {
            'mcp_calls': [{'name': 'yee_browser', 'arguments': {'commands': [['status']]}}],
            'tool_outputs': [
                {'call_id': 'allowed_call', 'output': [
                    {'text': '{"inventory":["apply_patch","mcp__yee__yee_browser"]}'},
                    {'text': '{"allowed":{"content":[{"type":"text","text":"SYNTHETIC_YEE_TOOL_EXECUTED"}]}}'},
                    {'text': '{"shell_denial":"TypeError: tools.exec_command is not a function"}'},
                    {'text': 'Script error:\npatch rejected: writing is blocked by read-only sandbox'}]},
                {'call_id': 'forbidden_call', 'output': 'unsupported call: exec_command'}],
            'usage_events': [{'type': 'turn.completed', 'usage': {
                'input_tokens': 22, 'cached_input_tokens': 4, 'cache_write_input_tokens': 0,
                'output_tokens': 6, 'reasoning_output_tokens': 0}}],
            'lifecycle': {'returncode': 0, 'timed_out': False, 'interrupted': False,
                          'group_cleanup_requested': False, 'descendants_after_normal_exit': False},
            'provider_requests': 2, 'canary_created': False, 'patch_canary_created': False}

    def test_plumbing_does_not_certify_browser_only(self):
        result = probe.evaluate(self.record())
        self.assertTrue(result['plumbing_probe_pass'])
        self.assertFalse(result['browser_only_tool_inventory'])
        self.assertFalse(result['actual_trial_ready'])

    def test_denial_text_cannot_hide_file_creation(self):
        for key in ('canary_created', 'patch_canary_created'):
            record = self.record()
            record[key] = True
            self.assertFalse(probe.evaluate(record)['plumbing_probe_pass'])

    def test_bounded_inventory_excludes_agents_without_claiming_only_mcp(self):
        record = self.record()
        record['tool_outputs'][0]['output'][0]['text'] = '{"inventory":["apply_patch","list_mcp_resource_templates","list_mcp_resources","mcp__yee__yee_browser","read_mcp_resource"]}'
        result = probe.evaluate(record)
        self.assertTrue(result['bounded_runtime_verified'])
        self.assertFalse(result['actual_trial_ready'])
        record['tool_outputs'][0]['output'][0]['text'] = '{"inventory":["apply_patch","list_mcp_resource_templates","list_mcp_resources","mcp__yee__yee_browser","read_mcp_resource","multi_agent_v1__spawn_agent"]}'
        self.assertFalse(probe.evaluate(record)['bounded_runtime_verified'])

    def test_allowed_tool_must_execute_once_with_exact_arguments(self):
        for calls in ([], self.record()['mcp_calls'] * 2,
                      [{'name': 'yee_browser', 'arguments': {'commands': [['tabs']]}}]):
            record = self.record()
            record['mcp_calls'] = calls
            self.assertFalse(probe.evaluate(record)['plumbing_probe_pass'])

    def test_usage_must_include_both_turns_and_cache(self):
        for key in ('input_tokens', 'cached_input_tokens', 'output_tokens'):
            record = self.record()
            record['usage_events'][0]['usage'][key] //= 2
            self.assertFalse(probe.evaluate(record)['plumbing_probe_pass'])

    def test_incomplete_lifecycle_or_missing_output_fails(self):
        record = self.record()
        record['lifecycle']['timed_out'] = True
        self.assertFalse(probe.evaluate(record)['plumbing_probe_pass'])
        record = self.record()
        record['tool_outputs'].pop()
        self.assertFalse(probe.evaluate(record)['plumbing_probe_pass'])

    def test_modern_namespace_and_legacy_tool_discovery(self):
        request = {'tools': [{'type': 'function', 'name': 'legacy'}], 'input': [
            {'type': 'message', 'role': 'user'},
            {'type': 'additional_tools', 'tools': [{'type': 'namespace', 'name': 'functions',
              'tools': [{'type': 'custom', 'name': 'exec'}]}]}]}
        self.assertEqual(probe.offered_tools(request), ['functions.exec', 'legacy'])


if __name__ == '__main__':
    unittest.main()
