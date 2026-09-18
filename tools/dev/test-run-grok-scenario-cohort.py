import ast
import importlib.util
import json
from pathlib import Path
import tempfile
import types
import unittest
import shutil
import uuid
from unittest import mock

s=importlib.util.spec_from_file_location('suite',Path(__file__).with_name('run-grok-scenario-cohort.py'))
suite=importlib.util.module_from_spec(s);s.loader.exec_module(suite)
from agent_scenario_fixture import dataset

class Tests(unittest.TestCase):
    def test_reclaim_removes_only_stopped_owned_test_profile_and_keeps_journals(self):
        bridge=Path('/private/tmp/yee-agent.'+uuid.uuid4().hex);bridge.mkdir(mode=0o700)
        try:
            profile=bridge/'profile';profile.mkdir(mode=0o700);(profile/'Preferences').write_text('test only')
            journal=bridge/'native-journal.jsonl';journal.write_text('receipt')
            with mock.patch.object(suite.subprocess,'check_output',return_value=''):
                proof=suite.reclaim_test_profile(bridge)
            self.assertTrue(proof['removed']);self.assertFalse(profile.exists());self.assertEqual(journal.read_text(),'receipt')
            self.assertFalse(suite.parse_args(['--record','/tmp/default']).keep_yee_test_profiles)
        finally:shutil.rmtree(bridge)

    def test_reclaim_preserves_active_profile_with_canonical_tmp_alias(self):
        bridge=Path('/private/tmp/yee-agent.'+uuid.uuid4().hex);bridge.mkdir(mode=0o700)
        try:
            profile=bridge/'profile';profile.mkdir(mode=0o700)
            command='Yee --user-data-dir='+str(profile).replace('/private/tmp/','/tmp/')
            with mock.patch.object(suite.subprocess,'check_output',return_value=command):
                with self.assertRaises(ValueError):suite.reclaim_test_profile(bridge)
            self.assertTrue(profile.exists())
        finally:shutil.rmtree(bridge)

    def test_reclaim_rejects_symlinked_profile_without_touching_its_target(self):
        bridge=Path('/private/tmp/yee-agent.'+uuid.uuid4().hex);bridge.mkdir(mode=0o700)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                target=Path(tmp);(target/'keep').write_text('preserved')
                (bridge/'profile').symlink_to(target,target_is_directory=True)
                with self.assertRaises(ValueError):suite.reclaim_test_profile(bridge)
                self.assertEqual((target/'keep').read_text(),'preserved')
        finally:shutil.rmtree(bridge)

    def app_fixture(self, root):
        import plistlib
        app = Path(root)/'Relocated Yee.app'
        (app/'Contents/MacOS').mkdir(parents=True)
        (app/'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleExecutable':'Yee', 'CFBundleIdentifier':'org.chromium.Chromium'}))
        executable = app/'Contents/MacOS/Yee'
        executable.write_text('built executable');executable.chmod(0o700)
        framework = app/'Contents/Frameworks/Yee Framework.framework/Yee Framework'
        framework.parent.mkdir(parents=True);framework.write_text('built framework')
        return app, executable, framework

    def test_explicit_app_is_selected_without_live_build_and_framework_is_frozen(self):
        with tempfile.TemporaryDirectory() as tmp:
            app, executable, framework = self.app_fixture(tmp)
            args = suite.parse_args(['--record','/tmp/record','--yee-app',str(app)])
            with mock.patch.object(suite.subprocess, 'check_output', side_effect=AssertionError('live build consulted')):
                self.assertEqual(suite.yee_executable(args.yee_app),executable.resolve())
            before = suite.frozen_sources(app)
            self.assertIn(str(executable.resolve()), before)
            self.assertIn(str(framework.resolve()), before)
            framework.write_text('concurrent new framework')
            after = suite.frozen_sources(app)
            self.assertNotEqual(before,after)
            self.assertEqual(before[str(executable.resolve())],after[str(executable.resolve())])

    def test_explicit_app_rejects_other_identity_incomplete_and_escaping_bundles(self):
        import plistlib
        with tempfile.TemporaryDirectory() as tmp:
            app, executable, framework = self.app_fixture(tmp)
            framework.unlink()
            with self.assertRaises(ValueError):suite.yee_executable(app)
            framework.write_text('built framework')
            outside = Path(tmp)/'external-framework';outside.write_text('uncontained')
            framework.unlink();framework.symlink_to(outside)
            with self.assertRaises(ValueError):suite.yee_executable(app)
            framework.unlink();framework.write_text('built framework')
            info = app/'Contents/Info.plist'
            info.write_bytes(plistlib.dumps({'CFBundleExecutable':'Yee','CFBundleIdentifier':'com.brave.Browser'}))
            with self.assertRaises(ValueError):suite.yee_executable(app)
            info.write_bytes(plistlib.dumps({'CFBundleExecutable':'../Yee','CFBundleIdentifier':'org.chromium.Chromium'}))
            with self.assertRaises(ValueError):suite.yee_executable(app)

    def test_seed_defaults_and_can_be_overridden(self):
        default=suite.parse_args(['--record','/tmp/default-seed'])
        chosen=suite.parse_args(['--record','/tmp/chosen-seed','--seed','29'])
        self.assertEqual(default.seed,11)
        self.assertEqual(chosen.seed,29)

    def test_all_twelve_common_inputs_are_browser_neutral_and_explicit_json(self):
        for i in range(1,13):
            scenario=f'S{i:02}';prompt=suite.common_prompt(scenario,dataset(scenario,11))
            self.assertIn(suite.task_for(scenario,dataset(scenario,11)),prompt)
            self.assertIn(suite.contract('STRICT_FINAL_JSON_CONTRACT'),prompt)
            self.assertNotIn('Yee',prompt);self.assertNotIn('Aside',prompt)
            self.assertEqual(suite.contract('SOURCE_QUOTATION_CONTRACT') in prompt,
                             scenario == 'S11')
            if scenario=='S09':
                self.assertIn(suite.contract('DRAFT_GROUNDING_CONTRACT'),prompt)
                self.assertIn('list every requested point',prompt)
                self.assertIn('Save the reviewed draft once',prompt)
    def test_host_configuration_is_explicit_and_keeps_oracle_out_of_workspace(self):
        import tomllib
        with tempfile.TemporaryDirectory() as tmp:
            case=Path(tmp);args=types.SimpleNamespace(aside=Path('/explicit/aside'))
            workspace=suite.configuration(case,args,'aside',host=True)
            config=tomllib.loads((workspace/'.grok/config.toml').read_text())
            self.assertEqual(set(config['mcp_servers']),{'aside','trial_host'})
            self.assertIn(str(case/'host-wire.jsonl'),config['mcp_servers']['trial_host']['args'])
            self.assertEqual([str(p.relative_to(workspace)) for p in workspace.rglob('*') if p.is_file()],['.grok/config.toml'])
    def test_yee_launch_receipt_is_separate_and_configuration_pins_direct_child(self):
        import tomllib
        with tempfile.TemporaryDirectory() as tmp:
            case=Path(tmp);bridge=case/'private-bridge';bridge.mkdir(mode=0o700)
            suite.save(case/'bridge.json',{'bridge':str(bridge)})
            suite.save(case/'launch.json',{'bridge':str(bridge),'peer_pid':4321,'pid_source':'direct_child_launch'})
            workspace=suite.configuration(case,types.SimpleNamespace(),'yee',bridge)
            argv=tomllib.loads((workspace/'.grok/config.toml').read_text())['mcp_servers']['yee']['args']
            self.assertEqual(argv[argv.index('--peer-pid')+1],'4321')
            self.assertEqual(json.loads((case/'bridge.json').read_text()),{'bridge':str(bridge)})
            tree=ast.parse(Path(suite.__file__).read_text())
            trial=next(n for n in tree.body if isinstance(n,ast.AsyncFunctionDef) and n.name=='yee_trial')
            publications=[n for n in ast.walk(trial) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='save']
            self.assertEqual(sum(any(isinstance(x,ast.Constant) and x.value=='bridge.json' for x in ast.walk(n.args[0])) for n in publications),1)
            self.assertEqual(sum(any(isinstance(x,ast.Constant) and x.value=='launch.json' for x in ast.walk(n.args[0])) for n in publications),1)
    def test_plain_browser_does_not_receive_host_tool(self):
        import tomllib
        with tempfile.TemporaryDirectory() as tmp:
            case=Path(tmp);args=types.SimpleNamespace(aside=Path('/explicit/aside'))
            workspace=suite.configuration(case,args,'aside')
            self.assertEqual(set(tomllib.loads((workspace/'.grok/config.toml').read_text())['mcp_servers']),{'aside'})
if __name__=='__main__':unittest.main()
