#!/usr/bin/env python3
import importlib.util
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('codex_runner', Path(__file__).with_name('run-codex-recorded.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class UsageTests(unittest.TestCase):
    def event(self):
        return {'type': 'turn.completed', 'usage': {'input_tokens': 100, 'output_tokens': 20,
            'cached_input_tokens': 80, 'cache_write_input_tokens': 0, 'reasoning_output_tokens': 5}}

    def test_cached_and_reasoning_are_not_added_twice(self):
        self.assertEqual(runner.normalize_usage([self.event()])['total_tokens'], 120)

    def test_missing_or_duplicate_completion_is_not_usage(self):
        self.assertIsNone(runner.normalize_usage([]))
        self.assertIsNone(runner.normalize_usage([self.event(), self.event()]))

    def test_partial_malformed_or_impossible_counters_rejected(self):
        for key, value in [('input_tokens', None), ('output_tokens', -1),
                           ('input_tokens', True), ('cached_input_tokens', 101),
                           ('reasoning_output_tokens', 21)]:
            event = self.event()
            event['usage'][key] = value
            self.assertIsNone(runner.normalize_usage([event]))

    def test_effective_prompt_delivers_policy_and_records_its_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            probe = root/'probe'; probe.mkdir(mode=0o700)
            bridge = root/'bridge'; bridge.mkdir(mode=0o700)
            binary = root/'codex'; binary.write_bytes(b'not executed')
            prompt = root/'prompt'; prompt.write_text('Read the authorized page.')
            (probe/'result.json').write_text('{}')
            (probe/'manifest.json').write_text(json.dumps({
                'codex_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'command': ['codex', '-c', 'agents.enabled=false']}))
            args = argparse.Namespace(policy_probe=probe, bridge=bridge,
                record=root/'record', timeout=10, codex=binary,
                python=Path('/usr/bin/python3'), prompt=prompt, prepare_only=True)
            checked = {'plumbing_probe_pass': True, 'nested_tools': [
                'apply_patch', 'list_mcp_resource_templates', 'list_mcp_resources',
                'mcp__yee__yee_browser', 'read_mcp_resource']}
            with patch.object(runner.probe, 'evaluate', return_value=checked):
                command, manifest = runner.prepare(args)
            policy = Path(runner.__file__).with_name('browser-agent-policy.md')
            self.assertEqual(command[-1], prompt.read_text()+'\n\n'+policy.read_text())
            self.assertEqual(manifest['completion_instruction_sha256'], runner.supervisor.sha(policy))
            self.assertEqual(manifest['effective_prompt_sha256'],
                             hashlib.sha256(command[-1].encode()).hexdigest())
            self.assertFalse(args.record.exists())


if __name__ == '__main__':
    unittest.main()
