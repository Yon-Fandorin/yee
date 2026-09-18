import importlib.util
import json
import tempfile
import subprocess
import sys
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('record', Path(__file__).with_name('run-grok-recorded.py'))
record = importlib.util.module_from_spec(spec)
spec.loader.exec_module(record)


class UsageTests(unittest.TestCase):
    def test_host_handoff_requires_explicit_aside_scope(self):
        allowed = record.mcp_arguments('aside__repl', 12, True)
        self.assertIn('MCPTool(trial_host__request_user)', allowed)
        self.assertNotIn('MCPTool(trial_host__request_user)', record.mcp_arguments('aside__repl', 12))
        with self.assertRaises(ValueError): record.mcp_arguments('yee__yee_browser', 12, True)
        def stream(tool):
            return json.dumps({'message': {'content': [{'type': 'tool_use', 'id': '1',
                'name': 'use_tool', 'input': {'tool_name': tool}}]}}).encode()
        self.assertTrue(record.audit_authority(stream('trial_host__request_user'), [],
                        mcp_tool='aside__repl', handoff=True)['passed'])
        self.assertFalse(record.audit_authority(stream('trial_host__request_user'), [],
                         mcp_tool='aside__repl')['passed'])
        self.assertFalse(record.audit_authority(stream('trial_host__other'), [],
                         mcp_tool='aside__repl', handoff=True)['passed'])
        self.assertFalse(record.audit_authority(stream('trial_host__request_user'), [],
                         mcp_tool='yee__yee_browser', handoff=True)['passed'])

    def test_resume_and_kimi_review_fail_closed_before_provider_call(self):
        with tempfile.TemporaryDirectory() as directory:
            command = [sys.executable, str(Path(__file__).with_name('run-grok-recorded.py')),
                       '--binary','/missing','--cwd',directory,'--prompt','/missing',
                       '--record',directory+'/new','--model','test']
            result = subprocess.run(command+['--mcp-tool','yee__yee_browser','--resume-record','/missing'],
                                    capture_output=True,text=True)
            self.assertEqual(result.returncode,2)
            self.assertIn('resume execution held',result.stderr)
            self.assertFalse((Path(directory)/'new').exists())
            profile = Path(directory)/'review.md'
            profile.write_text('tools: [Bash]')
            result = subprocess.run(command+['--provider','kimi','--kimi-review-profile',str(profile)],
                                    capture_output=True,text=True)
            self.assertEqual(result.returncode,2)
            self.assertIn('exact no-tools profile',result.stderr)
            self.assertFalse((Path(directory)/'new').exists())

    def test_resume_requires_explicit_successful_unchanged_private_record(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            identity = {'cwd':'/private/tmp/scoped','tool':'yee__yee_browser','model':'grok-4.6'}
            summary = {'session_identity':identity,'returncode':0,'timed_out':False,
                       'usage':{'total_tokens':10},'authority_audit':{'passed':True},
                       'session_id':'807b65e7-6236-4807-af28-48b45cf5eb82'}
            path = parent / 'summary.json'
            record.private_write(path, summary)
            self.assertEqual(record.resume_session(parent,identity),summary['session_id'])
            for changes in ({'session_id':'latest'}, {'returncode':1}, {'usage':None},
                            {'authority_audit':{'passed':False}}, {'session_identity':{}}, {'timed_out':True}):
                path.write_text(json.dumps({**summary,**changes}))
                with self.assertRaises(ValueError):
                    record.resume_session(parent,identity)
            path.write_text(json.dumps(summary))
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                record.resume_session(parent,identity)

    def test_disabled_shell_path_exits_before_creating_record_or_invoking_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'record'
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('run-grok-recorded.py')),
                '--binary', '/does-not-exist', '--cwd', directory, '--prompt', '/does-not-exist',
                '--record', str(target), '--model', 'test', '--direct-command', 'safe'],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('file/shell model trials disabled', result.stderr)
            self.assertFalse(target.exists())

    def test_project_trust_requires_a_scoped_mcp_trial_before_creating_record(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'record'
            result = subprocess.run([sys.executable, str(Path(__file__).with_name('run-grok-recorded.py')),
                '--binary', '/does-not-exist', '--cwd', directory, '--prompt', '/does-not-exist',
                '--record', str(target), '--model', 'test', '--trust-project'],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('scoped MCP trial', result.stderr)
            self.assertFalse(target.exists())

    def test_authority_audit_rejects_extra_shell_and_file_calls(self):
        calls = [('run_terminal_command', {'command': 'safe'}),
                 ('read_file', {'target_file': '/private/tmp/plan.py'}),
                 ('run_terminal_command', {'command': 'ls /other'}),
                 ('read_file', {'target_file': '/other/source.py'})]
        data = json.dumps({'message': {'content': [
            {'type': 'tool_use', 'id': str(i), 'name': name, 'input': args}
            for i, (name, args) in enumerate(calls)]}}).encode()
        audit = record.audit_authority(data, ['safe'], '/private/tmp/plan.py')
        self.assertFalse(audit['passed'])
        self.assertEqual([v['tool_call_id'] for v in audit['violations']], ['2', '3'])

    def test_mcp_authority_audit_allows_only_discovery_and_exact_target(self):
        def stream(name, args):
            return json.dumps({'message': {'content': [
                {'type': 'tool_use', 'id': '1', 'name': name, 'input': args}]}}).encode()
        for name, args, expected in (
            ('search_tool', {}, True),
            ('use_tool', {'tool_name': 'yee__yee_browser'}, True),
            ('use_tool', {'tool_name': 'aside__repl'}, False),
            ('read_file', {'target_file': '/tmp/file'}, False),
        ):
            self.assertEqual(record.audit_authority(stream(name, args), [],
                             mcp_tool='yee__yee_browser')['passed'], expected)

    def test_python_plan_grant_is_explicit_and_separate_from_jsonl(self):
        with tempfile.TemporaryDirectory(prefix='yee-agent.', dir='/private/tmp') as directory:
            path = Path(directory) / 'plan.py'
            path.write_text('print(page_info())\n')
            path.chmod(0o600)
            with self.assertRaises(ValueError):
                record.plan_file_arguments(path)
            self.assertEqual(record.plan_file_arguments(path, True),
                             ['--allow', f'Edit({path})', '--allow', f'Read({path})'])

    def test_form_preflight_requires_empty_correct_size_and_origin(self):
        ready = {'ok': True, 'truncated': False, 'document': 'test-doc',
                 'viewport': {'width': 1440, 'height': 900},
                 'snapshot': 'page origin="http://127.0.0.1:8766"\n+@4 field "Name" value=""\n+@8 text "Waiting for input"\n'}
        self.assertTrue(record.form_ready(ready))
        for changed in (None, {}, {**ready, 'snapshot': None},
                        {**ready, 'viewport': {'width': 698, 'height': 900}},
                        {**ready, 'truncated': True},
                        {**ready, 'snapshot': ready['snapshot'].replace('value=""', 'value="Cedar"')},
                        {**ready, 'snapshot': ready['snapshot'].replace('127.0.0.1:8766', 'example.com')}):
            self.assertFalse(record.form_ready(changed))

    def test_mcp_grant_is_one_exact_tool_without_shell_or_file_access(self):
        args = record.mcp_arguments('yee__yee_browser', 6)
        self.assertEqual(args, ['--tools', 'search_tool,use_tool', '--permission-mode',
                                'dontAsk', '--allow', 'MCPTool(yee__yee_browser)'])
        self.assertIn('MCPTool(aside__repl)', record.mcp_arguments('aside__repl', 6))
        for name in ('*', 'yee__*', 'other__tool', 'yee__yee_browser) Bash(*)'):
            with self.assertRaises(ValueError):
                record.mcp_arguments(name, 6)

    def test_plan_edit_grant_is_one_existing_private_test_file(self):
        with tempfile.TemporaryDirectory(prefix='yee-agent.', dir='/private/tmp') as directory:
            path = Path(directory) / 'plan.jsonl'
            path.write_text('REPLACE_WITH_JSONL_PLAN\n')
            path.chmod(0o600)
            grants = record.plan_file_arguments(path)
            self.assertEqual(grants, ['--allow', f'Edit({path})', '--allow', f'Read({path})'])
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                record.plan_file_arguments(path)

    def test_plan_grant_rejects_symlinks_and_product_paths(self):
        with self.assertRaises(ValueError):
            record.plan_file_arguments(Path('/Users/yongjunkim/erast/yee/source.jsonl'))
        with tempfile.TemporaryDirectory(prefix='yee-agent.', dir='/private/tmp') as directory:
            path = Path(directory) / 'actual.jsonl'
            path.write_text('test')
            path.chmod(0o600)
            link = Path(directory) / 'plan.jsonl'
            link.symlink_to(path)
            with self.assertRaises(ValueError):
                record.plan_file_arguments(link)

    def test_shell_stdin_redirection_is_not_granted(self):
        command = 'python3 /tmp/yee.py session < /private/tmp/yee-agent.Ab12/plan.jsonl'
        with self.assertRaises(ValueError):
            record.direct_arguments([command], 4)
        for suffix in (' < /etc/passwd', ' << EOF', ' > /private/tmp/out',
                       ' < /private/tmp/yee-agent.Ab12/../secret.jsonl',
                       ' < /private/tmp/yee-agent.Ab12/plan.jsonl; echo bad'):
            with self.assertRaises(ValueError):
                record.direct_arguments(['python3 /tmp/yee.py session' + suffix], 4)

    def test_direct_grants_are_exact_and_do_not_bypass_permissions(self):
        args = record.direct_arguments(['python3 /tmp/yee.py observe'], 4)
        self.assertIn('Bash(python3 /tmp/yee.py observe)', args)
        self.assertIn('dontAsk', args)
        for command in ['python3 *', 'echo ok; rm x', '$(whoami)', 'echo\nsecret', 'x(y)']:
            with self.assertRaises(ValueError):
                record.direct_arguments([command], 4)

    def test_stream_requires_one_terminal_result(self):
        self.assertEqual(record.parse_grok_output(b'{"type":"assistant"}\n{"type":"result","usage":{}}', True),
                         {'type': 'result', 'usage': {}})
        self.assertEqual(record.parse_grok_output(b'{"type":"assistant"}', True), {})
        self.assertEqual(record.parse_grok_output(b'{"type":"result"}\n{"type":"result"}', True), {})

    def test_stream_total_derived_only_after_per_turn_reconciliation(self):
        usage = {'input_tokens': 10, 'output_tokens': 3, 'cache_read_input_tokens': 7,
                 'cache_creation_input_tokens': 0}
        events = [{'type': 'assistant', 'message': {'usage': usage}},
                  {'type': 'result', 'usage': dict(usage)}]
        encode = lambda: '\n'.join(json.dumps(e) for e in events).encode()
        self.assertEqual(record.normalize_usage(record.parse_grok_output(encode(), True))['total_tokens'], 20)
        events[-1]['usage']['input_tokens'] = 9
        self.assertIsNone(record.normalize_usage(record.parse_grok_output(encode(), True)))

    def test_cli_cache_is_added_but_reasoning_is_not_added_twice(self):
        result = record.normalize_usage({'usage': {'input_tokens': 10, 'cache_read_input_tokens': 5,
            'cache_creation_input_tokens': 2, 'output_tokens': 4, 'reasoning_tokens': 3, 'total_tokens': 21}})
        self.assertEqual(result['input_tokens'], 17)
        self.assertEqual(result['total_tokens'], 21)

    def test_unknown_or_inconsistent_usage_does_not_become_zero(self):
        for usage in ({}, {'input_tokens': 10}, {'input_tokens': True,
                     'cache_read_input_tokens': 0, 'cache_creation_input_tokens': 0,
                     'output_tokens': 0, 'total_tokens': 1}):
            self.assertIsNone(record.normalize_usage({'usage': usage}))
        complete = {'input_tokens': 10, 'cache_read_input_tokens': 0,
                    'cache_creation_input_tokens': 0, 'output_tokens': 4, 'total_tokens': 99}
        self.assertIsNone(record.normalize_usage({'usage': complete}))


if __name__ == '__main__':
    unittest.main()
