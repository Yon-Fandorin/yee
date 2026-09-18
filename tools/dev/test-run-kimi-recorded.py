import argparse
import hashlib
import importlib.util
import json
import os
import signal
import subprocess
import sys
import time
import contextlib
import io
from pathlib import Path
import tempfile
import tomllib
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('runner', Path(__file__).with_name('run-kimi-recorded.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        for name in ('bridge', 'credentials'):
            (self.root/name).mkdir(mode=0o700)
        runner.write(self.root/'credentials/token.json', 'SECRET_TOKEN_CANARY')
        runner.write(self.root/'prompt', 'Read the approved synthetic task.')
        config = '''[thinking]
enabled = true
[models."kimi-code/kimi-for-coding"]
model = "kimi-for-coding"
provider = "managed:kimi-code"
max_context_size = 262144
capabilities = ["thinking", "always_thinking", "tool_use"]
[providers."managed:kimi-code"]
type = "kimi"
base_url = "https://api.kimi.com/coding/v1"
[providers."managed:kimi-code".oauth]
storage = "file"
key = "token"
[hooks]
unwanted = "HOOK_CANARY"
'''
        runner.write(self.root/'config.toml', config)
        runner.write(self.root/'kimi', 'fake binary, never executed by preparation')
        self.args = argparse.Namespace(record=self.root/'run', bridge=self.root/'bridge',
            credentials=self.root/'credentials', config=self.root/'config.toml',
            prompt=self.root/'prompt', kimi=self.root/'kimi', python=Path('/usr/bin/python3'),
            prepare_only=True, timeout=10)

    def tearDown(self):
        self.tmp.cleanup()

    def test_preparation_isolated_config_and_no_token_copy(self):
        command, env = runner.prepare(self.args)
        home = self.args.record/'kimi-home'
        config = tomllib.loads((home/'config.toml').read_text())
        self.assertNotIn('hooks', config)
        self.assertTrue(config['thinking']['enabled'])
        self.assertEqual(config['image']['max_edge_px'],4096)
        self.assertNotIn('KIMI_IMAGE_MAX_EDGE_PX',env)
        self.assertEqual(list(config['providers']), ['managed:kimi-code'])
        self.assertEqual((home/'credentials').resolve(), self.args.credentials)
        self.assertNotIn('SECRET_TOKEN_CANARY', (home/'config.toml').read_text())
        self.assertEqual(env['KIMI_CODE_HOME'], str(home))
        mcp=json.loads((home/'mcp.json').read_text())
        self.assertEqual(mcp['mcpServers']['yee']['toolTimeoutMs'],self.args.timeout*1000)
        self.assertNotIn('--yolo', command)
        self.assertNotIn('--auto', command)
        self.assertIn('subagents: []', (self.args.record/'browser-only.md').read_text())
        self.assertIn('one raw JSON object', (self.args.record/'browser-only.md').read_text())
        self.assertIn('do not emit progress reports', (self.args.record/'browser-only.md').read_text())
        policy=Path(runner.__file__).with_name('browser-agent-policy.md')
        self.assertTrue((self.args.record/'browser-only.md').read_text().endswith(policy.read_text()))
        manifest=json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(manifest['completion_instruction_sha256'],runner.sha(policy))
        self.assertEqual((self.args.record/'manifest.json').stat().st_mode & 0o777, 0o600)
        self.assertEqual(list((self.args.record/'workspace').iterdir()), [self.args.record/'workspace/.git'])

    def test_existing_record_is_not_overwritten(self):
        runner.prepare(self.args)
        original = (self.args.record/'manifest.json').read_bytes()
        with self.assertRaises(FileExistsError):
            runner.prepare(self.args)
        self.assertEqual((self.args.record/'manifest.json').read_bytes(), original)

    def test_draft_grounding_contract_is_recorded_with_effective_prompt(self):
        self.args.draft_grounding = True
        runner.prepare(self.args)
        source = (self.args.record/'source-prompt.txt').read_text()
        effective = (self.args.record/'prompt.txt').read_text()
        manifest = json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(source, self.args.prompt.read_text())
        self.assertEqual(effective,
                         source + '\n\n' + runner.DRAFT_GROUNDING_CONTRACT)
        self.assertEqual(manifest['draft_grounding_contract'],
                         'source-grounded-prose-v2')
        self.assertEqual(manifest['draft_grounding_contract_sha256'],
                         hashlib.sha256(runner.DRAFT_GROUNDING_CONTRACT.encode()).hexdigest())
        self.assertEqual(manifest['source_prompt_sha256'],
                         hashlib.sha256(source.encode()).hexdigest())

    def test_strict_final_json_contract_is_recorded_with_effective_prompt(self):
        self.args.strict_final_json = True
        runner.prepare(self.args)
        source = (self.args.record/'source-prompt.txt').read_text()
        effective = (self.args.record/'prompt.txt').read_text()
        manifest = json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(effective,
                         source + '\n\n' + runner.STRICT_FINAL_JSON_CONTRACT)
        self.assertEqual(manifest['strict_final_json_contract'],
                         'terminal-json-object-v2')
        self.assertEqual(manifest['strict_final_json_contract_sha256'],
                         hashlib.sha256(runner.STRICT_FINAL_JSON_CONTRACT.encode()).hexdigest())

    def test_already_present_required_contract_is_kept_once(self):
        self.args.strict_final_json = True
        source = self.args.prompt.read_text() + '\n\n' + runner.STRICT_FINAL_JSON_CONTRACT
        self.args.prompt.write_text(source)
        runner.prepare(self.args)
        effective = (self.args.record/'prompt.txt').read_text()
        manifest = json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(effective, source)
        self.assertEqual(effective.count(runner.STRICT_FINAL_JSON_CONTRACT), 1)
        self.assertEqual(manifest['strict_final_json_contract'], 'terminal-json-object-v2')
        self.assertEqual(manifest['source_prompt_sha256'], hashlib.sha256(source.encode()).hexdigest())

    def test_explicit_source_quotation_is_recorded_and_frozen(self):
        self.args.source_quotation = True
        runner.prepare(self.args)
        effective = (self.args.record/'prompt.txt').read_text()
        manifest = json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(effective.count(runner.SOURCE_QUOTATION_CONTRACT), 1)
        self.assertEqual(manifest['source_quotation_contract'], 'verbatim-source-v1')
        composer = Path(runner.__file__).with_name('browser_agent_contracts.py')
        self.assertEqual(manifest['output_contract_composer_sha256'], runner.sha(composer))
        self.assertEqual(manifest['mcp_source_sha256'][composer.name], runner.sha(composer))

    def test_mixed_draft_json_does_not_duplicate_terminal_contract(self):
        self.args.draft_grounding = True
        self.args.strict_final_json = True
        source = 'Save customer prose and give a JSON report.\n' + runner.STRICT_FINAL_JSON_CONTRACT
        self.args.prompt.write_text(source)
        runner.prepare(self.args)
        effective = (self.args.record/'prompt.txt').read_text()
        self.assertTrue(effective.startswith(source))
        self.assertEqual(effective.count(runner.STRICT_FINAL_JSON_CONTRACT), 1)
        self.assertEqual(effective.count(runner.DRAFT_GROUNDING_CONTRACT), 1)
        self.assertIn('saved artifact remains prose', effective)
        self.assertNotIn(runner.SOURCE_QUOTATION_CONTRACT, effective)

    def test_explicit_host_channel_is_separate_and_recorded(self):
        channel=self.root/'channel';channel.mkdir(mode=0o700)
        runner.write(channel/'config.json',{'schema':'yee.host-channel.v1','scenario':'S08','run_id':'test',
            'target_id':'A'*32,'origin':'http://127.0.0.1:8787'})
        self.args.handoff_channel=channel
        runner.prepare(self.args)
        manifest=json.loads((self.args.record/'manifest.json').read_text())
        servers=json.loads((self.args.record/'kimi-home/mcp.json').read_text())['mcpServers']
        self.assertEqual(set(servers),{'yee','trial_host'})
        self.assertEqual(manifest['model_tools'],['mcp__yee__yee_browser','mcp__trial_host__request_user'])
        self.assertEqual(manifest['host_handoff_config_sha256'],runner.sha(channel/'config.json'))
        self.assertNotIn('browser-trial-handoff',str(servers['yee']))

    def test_retired_browser_harness_rejected_before_record_creation(self):
        self.args.browser = 'browser_harness'
        with self.assertRaisesRegex(ValueError, 'unsupported browser'):
            runner.prepare(self.args)
        self.assertFalse(self.args.record.exists())

    def test_shared_profile_does_not_prescribe_one_browsers_scan_arguments(self):
        policy = Path(runner.__file__).with_name('browser-agent-policy.md')
        for browser in ('yee', 'aside'):
            with self.subTest(browser=browser):
                self.args.browser = browser
                self.args.record = self.root / ('run-' + browser)
                self.args.bridge = self.root / 'bridge' if browser == 'yee' else None
                if browser == 'aside':
                    self.args.aside = self.root / 'aside'
                    runner.write(self.args.aside, 'synthetic executable; never run')
                runner.prepare(self.args)
                profile = (self.args.record / 'browser-only.md').read_text()
                self.assertTrue(profile.endswith(policy.read_text()))
                self.assertNotIn('Start with the document selector', profile)
                self.assertNotIn('continue with only\nthe returned cursor', profile)
                self.assertIn('traverse the whole document', profile)
                config = tomllib.loads((self.args.record / 'kimi-home/config.toml').read_text())
                self.assertEqual(config['permission']['rules'], [
                    {'decision': 'allow', 'pattern': 'mcp__' + browser + '__' +
                     ('yee_browser' if browser == 'yee' else 'repl')}
                ])

    def test_aside_preparation_uses_real_identity_and_raw_relay_only(self):
        self.args.browser = 'aside'; self.args.bridge = None
        self.args.aside = self.root/'aside'
        runner.write(self.args.aside, 'synthetic executable; never run')
        self.assertEqual(runner.run(self.args), 0)
        home = self.args.record/'kimi-home'
        config = tomllib.loads((home/'config.toml').read_text())
        self.assertEqual(config['permission']['rules'], [{'decision':'allow','pattern':'mcp__aside__repl'}])
        mcp = json.loads((home/'mcp.json').read_text())
        self.assertEqual(list(mcp['mcpServers']), ['aside'])
        arguments = mcp['mcpServers']['aside']['args']
        self.assertTrue(arguments[0].endswith('record-mcp-stdio.py'))
        self.assertEqual(arguments[-3:], ['--', str(self.args.aside), 'mcp'])
        self.assertNotIn('--bridge', arguments)
        manifest = json.loads((self.args.record/'manifest.json').read_text())
        self.assertEqual(manifest['upstream_sha256'], runner.sha(self.args.aside))
        self.assertEqual(manifest['model_tool'], 'mcp__aside__repl')
        self.assertFalse(manifest['comparator_execution_ready'])
        self.assertIsNone(manifest['bridge'])

    def test_aside_live_execution_fails_before_setup_or_timeline(self):
        self.args.browser = 'aside'; self.args.prepare_only = False
        self.args.timeline = self.root/'must-not-exist.jsonl'
        for entry in (runner.run, runner.run_body):
            with self.assertRaisesRegex(ValueError, 'isolation'): entry(self.args)
        self.assertFalse(self.args.record.exists())
        self.assertFalse(self.args.timeline.exists())

    def test_aside_rejects_yee_only_presentation_option(self):
        self.args.browser = 'aside'; self.args.short_documents = True
        with self.assertRaisesRegex(ValueError, 'only to Yee'): runner.prepare(self.args)
        self.assertFalse(self.args.record.exists())

    def test_owned_aside_scope_requires_exact_target_and_prompt(self):
        self.args.browser = 'aside'; self.args.prepare_only = False
        for target in (None, 'bad', 'A'*31, 'a'*32):
            self.args.aside_owned_tab = target
            with self.assertRaises(ValueError): runner.owned_aside_scope(self.args)
        self.args.aside_owned_tab = 'A'*32
        with self.assertRaises(ValueError): runner.owned_aside_scope(self.args)
        self.args.prompt.write_text(
            'Use only the prepared Aside tab ' + 'A'*32 + ' at http://127.0.0.1:8787/ '
            'Verify its exact URL before reading content or editing. '
            'Do not list, attach, read or change any other tab; do not use files, shell or other sites.')
        scope = runner.owned_aside_scope(self.args)
        self.assertFalse(scope['runtime_isolation_verified'])
        self.assertTrue(scope['retrospective_tool_audit_required'])

    def test_document_variant_records_exact_mcp_configuration(self):
        for enabled in (False, True):
            self.args.record = self.root / ('short' if enabled else 'default')
            self.args.short_documents = enabled
            runner.prepare(self.args)
            manifest = json.loads((self.args.record/'manifest.json').read_text())
            config_path = self.args.record/'kimi-home/mcp.json'
            config = json.loads(config_path.read_text())
            self.assertEqual('--short-documents' in config['mcpServers']['yee']['args'], enabled)
            self.assertIs(manifest['short_documents'], enabled)
            self.assertEqual(manifest['mcp_config_sha256'], runner.sha(config_path))
            self.assertEqual(manifest['mcp_source_sha256']['yee_document_handles.py'],
                             runner.sha(Path(runner.__file__).with_name('yee_document_handles.py')))

    def test_metadata_permission_requires_explicit_flag_and_matching_prompt(self):
        self.args.browser='aside';self.args.prepare_only=False;self.args.aside_owned_tab='A'*32
        strict=('Use only the prepared Aside tab '+'A'*32+' at http://127.0.0.1:8787/ '
            'Verify its exact URL before reading content or editing. '
            'Do not list, attach, read or change any other tab; do not use files, shell or other sites.')
        allowed=strict.replace('Do not list, attach, read or change any other tab;',
            'Tab inventory metadata may be listed for this authorized measurement. Do not attach to, read content from or change any other tab;')
        self.args.prompt.write_text(strict)
        self.assertFalse(runner.owned_aside_scope(self.args)['tab_inventory_metadata_authorized'])
        self.args.allow_aside_tab_metadata=True
        with self.assertRaisesRegex(ValueError,'boundary'):runner.owned_aside_scope(self.args)
        self.args.prompt.write_text(allowed)
        self.assertTrue(runner.owned_aside_scope(self.args)['tab_inventory_metadata_authorized'])
        self.args.allow_aside_tab_metadata=False
        with self.assertRaisesRegex(ValueError,'boundary'):runner.owned_aside_scope(self.args)

    def test_three_owned_tabs_require_exact_private_scope_and_prompt(self):
        self.args.browser = 'aside'; self.args.prepare_only = False
        self.args.aside_owned_tabs = self.root/'tabs.json'
        tabs=[{'target_id':str(i)*32,'url':f'http://127.0.0.1:8787/?document=D-11-{i}'} for i in range(1,4)]
        scope={'schema':'yee.aside-owned-tabs.v1','tabs':tabs,'original_target_id':tabs[0]['target_id']}
        runner.write(self.args.aside_owned_tabs,scope)
        with self.assertRaisesRegex(ValueError,'boundary'):runner.owned_aside_scope(self.args)
        boundary=('Use only these prepared Aside tabs: '+json.dumps(tabs,separators=(',',':'))+
                  '. Original tab: '+scope['original_target_id']+
                  '. Verify each exact URL before reading. Do not list, attach, read or change any other tab; '
                  'do not use files, shell or other sites. Do not navigate or close the prepared tabs.')
        self.args.prompt.write_text(boundary)
        self.assertFalse(runner.owned_aside_scope(self.args)['runtime_isolation_verified'])
        self.args.aside_owned_tabs.chmod(0o644)
        with self.assertRaisesRegex(ValueError,'private'):runner.owned_aside_scope(self.args)
        self.args.aside_owned_tabs.chmod(0o600)
        for change in ('foreign','duplicate','missing'):
            bad=json.loads(json.dumps(scope))
            if change=='foreign':bad['tabs'][1]['url']='https://example.com/'
            if change=='duplicate':bad['tabs'][1]['target_id']=bad['tabs'][0]['target_id']
            if change=='missing':bad['original_target_id']='A'*32
            self.args.aside_owned_tabs.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):runner.owned_aside_scope(self.args)

    def test_prepare_only_never_executes_binary(self):
        self.assertEqual(runner.run(self.args), 0)
        self.assertFalse(json.loads((self.args.record/'prepared.json').read_text())['model_launched'])
        self.assertFalse((self.args.record/'stdout.jsonl').exists())

    def test_logical_oauth_prefix_maps_to_native_filename(self):
        text = self.args.config.read_text().replace('key = "token"', 'key = "oauth/token"')
        self.args.config.write_text(text)
        runner.prepare(self.args)
        self.assertIn('key = "oauth/token"', (self.args.record/'kimi-home/config.toml').read_text())

    def test_insecure_or_symlink_credentials_fail_before_record_creation(self):
        token = self.args.credentials/'token.json'
        token.chmod(0o644)
        with self.assertRaises(ValueError): runner.prepare(self.args)
        self.assertFalse(self.args.record.exists())
        token.unlink(); token.symlink_to(self.root/'prompt')
        with self.assertRaises(ValueError): runner.prepare(self.args)
        self.assertFalse(self.args.record.exists())

    def test_external_environment_overrides_are_not_inherited(self):
        old = os.environ.get('KIMI_API_BASE')
        try:
            os.environ['KIMI_API_BASE'] = 'https://unwanted.test'
            _, env = runner.prepare(self.args)
            self.assertNotIn('KIMI_API_BASE', env)
        finally:
            if old is None: os.environ.pop('KIMI_API_BASE', None)
            else: os.environ['KIMI_API_BASE'] = old

    def execute_fixture(self, source, timeout=2):
        with (self.root/'out').open('wb') as out, (self.root/'err').open('wb') as err:
            return runner.execute([sys.executable, '-c', source], env=dict(os.environ),
                                  cwd=self.root, timeout=timeout, stdout=out, stderr=err, grace=.1)

    def assert_child_stopped(self, pid):
        # A killed orphan can briefly remain a zombie until the OS reaps it.
        for _ in range(40):
            result = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)],
                                    capture_output=True, text=True)
            if result.returncode or result.stdout.strip().startswith('Z'):
                return
            time.sleep(.025)
        os.kill(pid, signal.SIGKILL)  # clean the test-owned child even on failure
        self.fail('owned fixture child remained running after supervisor returned')

    def child_source(self, parent_wait):
        return '''import os,signal,time,pathlib
pid=os.fork()
if pid==0:
 signal.signal(signal.SIGTERM,signal.SIG_IGN)
 pathlib.Path('child.pid').write_text(str(os.getpid()))
 while True:time.sleep(.1)
else:
 while not pathlib.Path('child.pid').exists():time.sleep(.005)
 print('PARENT_OUTPUT_RETAINED',flush=True)
 ''' + ('time.sleep(60)' if parent_wait else 'os._exit(0)')

    def test_timeout_kills_term_ignoring_child_after_parent_exits(self):
        result = self.execute_fixture(self.child_source(True), timeout=.3)
        self.assertTrue(result['timed_out'])
        self.assertTrue(result['group_cleanup_requested'])
        self.assert_child_stopped(int((self.root/'child.pid').read_text()))
        self.assertIn('PARENT_OUTPUT_RETAINED', (self.root/'out').read_text())

    def test_successful_parent_cannot_leave_live_helper(self):
        result = self.execute_fixture(self.child_source(False))
        self.assertEqual(result['returncode'], 0)
        self.assertFalse(result['timed_out'])
        self.assertTrue(result['descendants_after_normal_exit'])
        self.assert_child_stopped(int((self.root/'child.pid').read_text()))

    def test_regular_exit_preserves_output_without_cleanup(self):
        result = self.execute_fixture('print("NORMAL_EXIT")')
        self.assertEqual(result['returncode'], 0)
        self.assertFalse(result['group_cleanup_requested'])
        self.assertEqual((self.root/'out').read_text(), 'NORMAL_EXIT\n')

    def test_timeout_preserves_summary_and_does_not_invent_usage(self):
        self.args.kimi.write_text('#!'+sys.executable+'\n'
            'import sys,time\n'
            'if "--version" in sys.argv: print("0.41.0")\n'
            'elif "doctor" in sys.argv: print("fixture config accepted")\n'
            'else:\n print("PARTIAL_FIXTURE_OUTPUT",flush=True)\n time.sleep(60)\n')
        self.args.kimi.chmod(0o700)
        self.args.prepare_only = False; self.args.timeout = .2
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.run(self.args), 2)
        summary = json.loads((self.args.record/'summary.json').read_text())
        self.assertTrue(summary['timed_out'])
        self.assertIsNone(summary['usage'])
        self.assertIsNone(summary['whole_task_elapsed_seconds'])
        self.assertGreaterEqual(summary['runner_elapsed_seconds'], .2)
        self.assertFalse(summary['native_settlement_verified'])
        self.assertIn('PARTIAL_FIXTURE_OUTPUT', (self.args.record/'stdout.jsonl').read_text())

    def test_sigterm_runner_preserves_interruption_summary(self):
        self.args.kimi.write_text('#!'+sys.executable+'\n'
            'import sys,time,os\n'
            'if "--version" in sys.argv: print("0.41.0")\n'
            'elif "doctor" in sys.argv: print("fixture config accepted")\n'
            'else:\n print(os.getpid(),flush=True)\n time.sleep(60)\n')
        self.args.kimi.chmod(0o700)
        command = [sys.executable, str(Path(runner.__file__).resolve())]
        for key in ('record', 'bridge', 'config', 'credentials', 'prompt', 'kimi', 'python'):
            command += ['--'+key, str(getattr(self.args, key))]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        try:
            output = self.args.record/'stdout.jsonl'
            for _ in range(200):
                if output.exists() and output.stat().st_size: break
                if process.poll() is not None: self.fail('runner exited before fixture launch')
                time.sleep(.01)
            else: self.fail('fixture launch deadline expired')
            child = int(output.read_text())
            process.send_signal(signal.SIGTERM)
            process.communicate(timeout=10)
            self.assertEqual(process.returncode, 130)
            summary = json.loads((self.args.record/'summary.json').read_text())
            self.assertTrue(summary['interrupted'])
            self.assertFalse(summary['timed_out'])
            self.assertFalse(summary['native_settlement_verified'])
            self.assertIsNone(summary['usage'])
            self.assert_child_stopped(child)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def test_failed_preflight_retains_cost_without_launching_model(self):
        self.args.kimi.write_text('#!'+sys.executable+'\n'
            'import sys,pathlib\n'
            'if "--version" in sys.argv: print("0.41.0")\n'
            'elif "doctor" in sys.argv: print("FIXTURE_DOCTOR_FAILURE"); sys.exit(7)\n'
            'else: pathlib.Path("MODEL_SHOULD_NOT_RUN").touch()\n')
        self.args.kimi.chmod(0o700)
        self.args.prepare_only = False
        self.assertEqual(runner.run(self.args), 2)
        summary = json.loads((self.args.record/'summary.json').read_text())
        self.assertEqual(summary['failed_stage'], 'doctor')
        self.assertFalse(summary['model_launched'])
        self.assertEqual(summary['returncode'], 7)
        self.assertGreater(summary['runner_elapsed_seconds'], 0)
        self.assertIsNone(summary['usage'])
        self.assertFalse((self.args.record/'workspace/MODEL_SHOULD_NOT_RUN').exists())
        self.assertIn('FIXTURE_DOCTOR_FAILURE', (self.args.record/'doctor.json').read_text())

    def test_failed_execution_still_closes_timeline_execution_phase(self):
        self.args.timeline=self.root/'timeline.jsonl'
        runner.yee_trial_timeline.append(self.args.timeline,'setup_started',record=self.args.record)
        self.args.prepare_only=False
        # Invalid executable: setup succeeds, process launch fails. The outer
        # timeline still records the failed attempt rather than losing its time.
        self.args.kimi.chmod(0o700)
        with self.assertRaises(OSError):runner.run(self.args)
        rows=[json.loads(l) for l in self.args.timeline.read_text().splitlines()]
        self.assertEqual([r['phase'] for r in rows],list(runner.yee_trial_timeline.PHASES[:3]))
        self.assertEqual(rows[-1]['outcome'], {'status':'raised','exception_type':'OSError'})
        result=runner.yee_trial_timeline.validate(rows)
        self.assertFalse(result['complete'])
        self.assertGreater(result['elapsed_seconds_so_far'],0)
        self.assertIsNone(result['whole_task_elapsed_seconds'])


if __name__ == '__main__':
    unittest.main()
