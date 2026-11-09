"""Windows native gate의 계획·발견·출처·XML·중단 계약을 검사한다."""

from collections import Counter
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/dev/lib'))
import windows_native_gate as gate
import background_test
import browser_bundle_executables
import native_gate_record
import native_test_discovery
import test_tiers
import windows_build_provenance
from test_tiers import case_names
from test_gate_coverage import (
    chromium_checkout, covered_by_gate, destination_path, gn_targets,
    tier_inventory, tier_violations, windows_gate_runs,
)


class WindowsNativeGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='yee-windows-gate-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def copied_configuration(self):
        repo = self.directory / 'repository'
        dev = repo / 'tools/dev'
        (dev / 'lib').mkdir(parents=True)
        for source in (ROOT / 'tools/dev').glob('test-*.sh'):
            shutil.copyfile(source, dev / source.name)
        shutil.copyfile(ROOT / 'tools/dev/lib/windows_gate_modes.json',
                        dev / 'lib/windows_gate_modes.json')
        manifest = repo / 'tests/tooling'
        manifest.mkdir(parents=True)
        shutil.copyfile(ROOT / 'tests/tooling/test_tiers.json', manifest / 'test_tiers.json')
        return repo

    def sample_run(self):
        return gate.GateRun('sample', 'default', 'sample_tests', 'area:sample_tests',
                            'Fixture.Case', (), 'A')

    def test_required_plan_keeps_new_binaries_and_exact_installer_scope(self):
        """이전한 실행 파일과 SW·설치 fixture를 선택하고 opt-in을 분리한다."""
        runs = gate.plan(ROOT, 'required', None, 'all')
        names = {run.binary for run in runs}
        self.assertIn('yee_interactive_ui_tests', names)
        self.assertIn('yee_aura_unittests', names)
        self.assertIn('setup_unittests', names)
        self.assertNotIn('interactive_ui_tests', names)
        self.assertNotIn('content_unittests', names)
        self.assertNotIn('youtube-live', {run.gate for run in runs})
        installer, = (run for run in runs if run.gate == 'setup')
        self.assertEqual('TaskbarPinningTest.*:YeeInstallShortcutTest.*', installer.pattern)
        service_worker, = (run for run in runs if run.gate == 'content-blocking-native'
                           and run.binary == 'yee_service_worker_unittests')
        self.assertEqual('YeeServiceWorkerContainerHostTest.WrapSubresourceLoaderFactory_*',
                         service_worker.pattern)
        self.assertTrue(any('--enable-features=TabStripUnification' in run.arguments for run in runs))
        self.assertTrue(any('--force-ui-direction=rtl' in run.arguments
                            and '--force-device-scale-factor=1.25' in run.arguments for run in runs))

    def test_plan_checks_every_script_and_rejects_changed_source(self):
        """스크립트·모드 변경이 Windows 계획의 누락으로 남지 않게 한다."""
        repo = self.copied_configuration()
        data = gate.read_configuration(repo)
        self.assertEqual('macOS', data['non_native']['test-macos-install.sh'])
        self.assertEqual('tooling', data['non_native']['test-run-preflight.sh'])
        path = repo / 'tools/dev/test-header.sh'
        expected = gate.plan(repo, 'header', 'unit', 'background')
        lf = path.read_bytes().replace(b'\r\n', b'\n')
        path.write_bytes(lf.replace(b'\n', b'\r\n'))
        self.assertEqual(expected, gate.plan(repo, 'header', 'unit', 'background'))
        path.write_bytes(path.read_bytes() + b'\n# changed\n')
        with self.assertRaisesRegex(ValueError, 'Windows gate'):
            gate.plan(repo, 'header', 'unit', 'background')

    def test_acceptance_runs_only_the_four_desktop_checks(self):
        windows, live = gate.plan(ROOT, 'browser-surface-layout', 'acceptance', 'foreground')
        self.assertEqual({
            'YeeWindowsTabContextMenuCreatesGroup',
            'YeeWindowsTabContextMenuCreatesSplit',
            'YeeWindowsTaskbarRestoresMinimizedWindow',
        }, {pattern.split('.')[-1] for pattern in windows.expected_patterns})
        self.assertEqual(('YeeWindowsLivePlaybackUiTest.DISABLED_PublicLivePlaybackShowsWholeWindow',),
                         live.expected_patterns)
        self.assertTrue(all(gate.uses_desktop(run) for run in (windows, live)))
        self.assertEqual([], gate.plan(ROOT, 'browser-surface-layout', 'acceptance', 'background'))
        self.assertNotIn(live.name, {run.name for run in gate.plan(ROOT, 'required', None, 'all')})

    def test_unknown_mode_filter_and_implicit_live_experiment_fail(self):
        for name, mode, custom in (('missing', None, None), ('header', 'fast', None),
                                   ('header', 'unit', 'Fixture.*'), ('youtube-live', None, None)):
            with self.subTest(name=name, mode=mode):
                with self.assertRaises(ValueError):
                    gate.plan(ROOT, name, mode, 'all', custom)

    def test_custom_filter_does_not_treat_brackets_as_wildcards(self):
        case = 'InternalURLsBrowserTest.[E]xternalLaunchUsesCanonicalPolicy'
        for tier in ('background', 'foreground', 'all'):
            with self.subTest(tier=tier):
                with self.assertRaisesRegex(ValueError, '양수 패턴'):
                    gate.plan(ROOT, 'internal-pages', 'ui-on', tier, case)

    def test_print_filters_never_opens_a_test_or_build_process(self):
        with mock.patch.object(gate.subprocess, 'Popen', side_effect=AssertionError('native 실행 금지')), \
             mock.patch.object(sys, 'stdout', new_callable=io.StringIO) as output:
            self.assertEqual(0, gate.main(['--repo', str(ROOT), '--gate', 'header',
                                           '--mode', 'unit', '--print-filters']))
        self.assertTrue(json.loads(output.getvalue()))

    @unittest.skipUnless(os.name == 'nt', 'Windows 전면 실행 경로가 필요하다')
    def test_desktop_start_waits_once_before_the_first_display_run(self):
        background = self.sample_run()
        foreground = gate.GateRun('sample', 'display', 'yee_interactive_ui_tests',
                                  'area:yee_interactive_ui_tests', 'Fixture.Case',
                                  gate.DISPLAY_FLAGS, 'C')
        output = self.directory / 'out' / 'Test'
        output.mkdir(parents=True)
        args = mock.Mock(repo=ROOT, chromium=self.directory, out_name='Test',
                         local_build=self.directory / 'evidence',
                         start_on_desktop=True, list_only=False)
        order = []

        def execute_run(run, directory, *args, **kwargs):
            directory.mkdir()
            order.append(run.name)

        with mock.patch.object(gate, 'require_idle'), \
             mock.patch.object(gate, 'build_inputs', return_value={}), \
             mock.patch.object(gate, 'create_build_evidence', return_value={}), \
             mock.patch.object(gate, 'artifact_scope', return_value=([], {})), \
             mock.patch.object(gate, 'artifacts', return_value={}), \
             mock.patch.object(gate, 'wait_for_desktop_start',
                               side_effect=lambda: order.append('start')), \
             mock.patch.object(gate, 'execute_run', side_effect=execute_run), \
             mock.patch.object(sys, 'stdout', new_callable=io.StringIO):
            self.assertEqual(0, gate.execute(args, [background, foreground, foreground]))
        self.assertEqual(['default', 'start', 'display', 'display'], order)
        with mock.patch.object(gate.Path, 'resolve', return_value=self.directory / 'redirected'), \
             mock.patch.object(gate, 'require_idle') as idle:
            with self.assertRaisesRegex(ValueError, '다른 경로'):
                gate.execute(args, [background])
            idle.assert_not_called()

    def test_background_and_foreground_keep_disjoint_manifest_cases(self):
        """tier 합집합은 all과 같고 background에는 실제 전면 사례가 없다."""
        manifest = gate.load_manifest(ROOT / 'tests/tooling/test_tiers.json')

        def selected(runs):
            result = set()
            for run in runs:
                positive, _, negative = run.pattern.partition('-')
                for key, entry in manifest.items():
                    if 'Windows' not in entry['os'] or run.binary not in entry['binaries']:
                        continue
                    for name in case_names(key, entry, 'Windows'):
                        if (any(gate.matches_pattern(name, part) for part in positive.split(':'))
                                and not any(gate.matches_pattern(name, part)
                                            for part in negative.split(':') if part)):
                            result.add((run.gate, run.name, run.binary, name))
            return result

        background_runs = gate.plan(ROOT, 'required', None, 'background')
        foreground_runs = gate.plan(ROOT, 'required', None, 'foreground')
        background = selected(background_runs)
        foreground = selected(foreground_runs)
        self.assertFalse(background & foreground)
        self.assertEqual(selected(gate.plan(ROOT, 'required', None, 'all')), background | foreground)
        self.assertTrue(all('--yee-test-tier=background' in run.arguments for run in background_runs))
        self.assertTrue(all('--headless' in run.arguments for run in background_runs
                            if run.binary in gate.HEADLESS_BINARIES))
        self.assertTrue(any('--enable-pixel-output-in-tests' in run.arguments
                            for run in background_runs))
        self.assertFalse(any(gate.uses_desktop(run) for run in background_runs))
        self.assertTrue(all('--headless' not in run.arguments for run in foreground_runs))

    def test_discovery_preserves_parameters_disabled_filters_and_pre_order(self):
        listing = ('Fixture.\n  Case\n  PRE_Case\n  PRE_PRE_Case\n  DISABLED_Optional\n'
                   'Prefix/Parameterized.\n  Case/0\n  Case/1\n')
        report = gate.discover(listing, 'Fixture.*:Prefix/Parameterized.Case/*-'
                               'Prefix/Parameterized.Case/1', ())
        self.assertTrue(report['ok'])
        self.assertEqual(['Fixture.PRE_PRE_Case', 'Fixture.PRE_Case', 'Fixture.Case',
                          'Prefix/Parameterized.Case/0'], report['cases'])
        self.assertFalse(gate.discover(listing, 'Missing.*', ())['ok'])
        self.assertEqual(['Fixture.DISABLED_Optional'],
                         gate.discover(listing, 'Fixture.DISABLED_Optional',
                                       ('--gtest_also_run_disabled_tests',))['cases'])
        report = gate.discover(listing, 'Prefix/Parameterized.Case/*', (),
                               ('Prefix/Parameterized.Case/0', 'Prefix/Parameterized.Case/2'))
        self.assertFalse(report['ok'])
        self.assertEqual(['Prefix/Parameterized.Case/2'], report['missing_manifest_patterns'])

    def test_windows_list_excludes_mac_immersive_instance(self):
        fixture = 'All/YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest.TopContainerBackground'
        runs = gate.plan(ROOT, 'browser-surface-layout', 'interactive', 'all')
        expected = {name for run in runs for name in run.expected_patterns if name.startswith(fixture)}
        self.assertEqual({fixture + '/0', fixture + '/1'}, expected)
        manifest = gate.load_manifest(ROOT / 'tests/tooling/test_tiers.json')
        key, = (key for key in manifest if fixture.split('/', 1)[1] in key)
        self.assertIn(fixture + '/2', case_names(key, manifest[key], 'Darwin'))

    def xml(self, case='', attrs=''):
        path = self.directory / 'results.xml'
        path.write_text(f'<testsuites tests="1" {attrs}>'
                        f'<testsuite name="Fixture" tests="1">{case}</testsuite></testsuites>',
                        encoding='utf-8')
        return path

    def test_xml_pass_failure_skip_and_wrong_name_are_separate(self):
        case = '<testcase classname="Fixture" name="Case" status="run" result="completed"/>'
        path = self.xml(case)
        self.assertEqual('passed', gate.xml_result(path, 'Fixture.Case', 0))
        self.assertEqual('failed', gate.xml_result(path, 'Fixture.Case', 1))
        with self.assertRaisesRegex(ValueError, '이름'):
            gate.xml_result(path, 'Fixture.Other', 0)
        path = self.xml(case.replace('/>', '><failure/></testcase>'), 'failures="1"')
        self.assertEqual('failed', gate.xml_result(path, 'Fixture.Case', 0))
        path = self.xml(case.replace('/>', '><skipped/></testcase>'), 'skipped="1"')
        self.assertEqual('skipped', gate.xml_result(path, 'Fixture.Case', 0))
        path = self.xml(case.replace('completed', 'notrun'))
        self.assertEqual('failed', gate.xml_result(path, 'Fixture.Case', 0))

    def test_xml_declared_totals_cannot_mask_missing_or_extra_cases(self):
        with self.assertRaisesRegex(ValueError, '한 개'):
            gate.xml_result(self.xml(), 'Fixture.Case', 0)
        case = '<testcase classname="Fixture" name="Case" status="run"/>'
        with self.assertRaisesRegex(ValueError, '집계'):
            gate.xml_result(self.xml(case, 'failures="1"'), 'Fixture.Case', 0)
        with self.assertRaisesRegex(ValueError, '한 개'):
            gate.xml_result(self.xml(case + case), 'Fixture.Case', 0)
        path = self.xml(case)
        valid = path.read_text(encoding='utf-8')
        path.write_text(valid.replace('testsuites', 'other'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'GTest'):
            gate.xml_result(path, 'Fixture.Case', 0)
        path.write_text(valid.replace('name="Fixture" tests="1"',
                                      'name="Fixture" tests="9" failures="2"'),
                        encoding='utf-8')
        with self.assertRaisesRegex(ValueError, '집계'):
            gate.xml_result(path, 'Fixture.Case', 0)

    def test_profile_is_reused_only_with_matching_ownership(self):
        path = self.directory / 'profile'
        gate.prepare_profile(path)
        owner = path / '.yee-native-gate-profile.json'
        before = owner.read_bytes()
        gate.prepare_profile(path)
        self.assertEqual(before, owner.read_bytes())
        owner.write_text('{}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, '소유'):
            gate.prepare_profile(path)

    def test_profile_without_ownership_is_preserved(self):
        path = self.directory / 'foreign'
        path.mkdir()
        keep = path / 'Preferences'
        keep.write_bytes(b'original')
        with self.assertRaisesRegex(ValueError, '소유'):
            gate.prepare_profile(path)
        self.assertEqual(b'original', keep.read_bytes())

    def test_timeout_keeps_process_evidence_and_does_not_terminate_another_process(self):
        child = mock.Mock(pid=123)
        child.wait.side_effect = subprocess.TimeoutExpired('sample', 300)
        directory = self.directory / 'case'
        with mock.patch.dict(os.environ, {'GTEST_REPEAT': '7', 'TEST_SHARD_INDEX': '3',
                                          'gtest_filter': 'foreign', 'YEE_GATE_FIXTURE': 'keep'}), \
             mock.patch.object(gate, 'require_idle'), \
             mock.patch.object(gate, 'require_artifacts'), \
             mock.patch.object(gate.subprocess, 'Popen', return_value=child) as launch:
            with self.assertRaises(subprocess.TimeoutExpired):
                gate.invoke(self.sample_run(), 'Fixture.Case', directory,
                            self.directory / 'profile', self.directory / 'tmp',
                            self.directory, {})
        record = json.loads((directory / 'process.json').read_text(encoding='utf-8'))
        self.assertEqual(123, record['pid'])
        self.assertIsNone(record['exit_code'])
        child.terminate.assert_not_called()
        child.kill.assert_not_called()
        environment = launch.call_args.kwargs['env']
        self.assertFalse(any(key.upper().startswith(('GTEST_', 'TEST_SHARD_'))
                             for key in environment))
        self.assertEqual('keep', environment['YEE_GATE_FIXTURE'])

    def test_foreground_case_passes_permission_only_to_its_child(self):
        """전면 사례 실행에만 자기 자식 PID와 파이프 시작 신호를 사용한다."""
        run = gate.GateRun('sample', 'foreground', 'yee_interactive_ui_tests',
                           'area:yee_interactive_ui_tests', 'Fixture.Case', gate.DISPLAY_FLAGS, 'C')
        for listing in (False, True):
            with self.subTest(listing=listing):
                directory = self.directory / str(listing)
                directory.mkdir()
                child = mock.Mock(pid=123, returncode=0)
                child.wait.return_value = 0
                child.stdin.closed = False
                user32 = mock.Mock()
                user32.AllowSetForegroundWindow.return_value = True
                with mock.patch.object(gate, 'require_idle'), \
                     mock.patch.object(gate, 'require_artifacts'), \
                     mock.patch.object(gate.ctypes, 'WinDLL', return_value=user32, create=True), \
                     mock.patch.object(gate.subprocess, 'Popen', return_value=child) as launch:
                    code, _ = gate.invoke(run, 'Fixture.Case', directory / 'case',
                                          directory / 'profile', directory / 'tmp',
                                          self.directory, {}, listing=listing)
                self.assertEqual(0, code)
                record = json.loads((directory / 'case/process.json').read_text(encoding='utf-8'))
                self.assertEqual(not listing, gate.FOREGROUND_HANDOFF in record['command'])
                if listing:
                    user32.AllowSetForegroundWindow.assert_not_called()
                    child.communicate.assert_not_called()
                    self.assertIsNone(launch.call_args.kwargs['stdin'])
                else:
                    user32.AllowSetForegroundWindow.assert_called_once_with(123)
                    child.communicate.assert_called_once_with(input=b'Y', timeout=300)
                    child.stdin.close.assert_called_once_with()
                    self.assertEqual(subprocess.PIPE, launch.call_args.kwargs['stdin'])
                    self.assertTrue(record['foreground_granted'])

    def test_failed_pre_family_records_the_blocked_following_case(self):
        """PRE 생략·실패 뒤 후속 사례를 실행하거나 통과로 옮기지 않는다."""
        listing = self.directory / 'listing.txt'
        listing.write_text('Fixture.\n  PRE_Case\n  Case\n', encoding='utf-8')

        def invoke(run, name, directory, *args, **kwargs):
            directory.mkdir(parents=True)
            return 0, listing

        directory = self.directory / 'gate'
        run = gate.GateRun('sample', 'default', 'sample_tests', 'area:sample_tests',
                           'Fixture.*', (), 'A')
        counts = Counter()
        with mock.patch.object(gate, 'invoke', side_effect=invoke) as launch, \
             mock.patch.object(gate, 'xml_result', return_value='skipped'):
            gate.execute_run(run, directory, self.directory / 'runtime',
                             self.directory, {}, counts, list_only=False)
        self.assertEqual({'skipped': 1, 'blocked': 1}, dict(counts))
        self.assertEqual(2, launch.call_count)
        blocked = json.loads((directory / gate.small_key('Fixture.Case') /
                              'result.json').read_text(encoding='utf-8'))
        self.assertEqual('blocked', blocked['status'])

    def test_powershell_print_filters_uses_no_native_environment(self):
        """5.1·7 진입점의 대소문자·공백 경로와 목록 전용 부작용 계약을 검사한다."""
        powershells = set(filter(None, (shutil.which('pwsh'), shutil.which('powershell'))))
        if not powershells:
            self.skipTest('PowerShell이 필요하다')
        repo = self.copied_configuration()
        dev = repo / 'tools/dev'
        entry = dev / 'test-native.ps1'
        shutil.copyfile(ROOT / 'tools/dev/test-native.ps1', entry)
        for module in (gate, background_test, browser_bundle_executables,
                       native_gate_record, native_test_discovery,
                       test_tiers, windows_build_provenance):
            source = Path(module.__file__)
            shutil.copyfile(source, dev / 'lib' / source.name)

        def quoted(value):
            return "'" + str(value).replace("'", "''") + "'"

        common = (
            "$ErrorActionPreference='Stop'\n"
            "$script:YeeRoot=" + quoted(repo) + "\n"
            "function Get-DepotCommand { param([string] $Name) " + quoted(sys.executable) + " }\n"
            "function Assert-YeeOutputSelection { throw 'native-environment-is-forbidden' }\n")
        (dev / 'common.ps1').write_text(common, encoding='utf-8-sig')
        for shell in powershells:
            with self.subTest(shell=shell):
                result = subprocess.run(
                    [shell, '-NoProfile', '-NonInteractive', '-File', str(entry),
                     '-Gate', 'Header', '-Mode', 'unit', '-Tier', 'ALL', '-PrintFilters'],
                    capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertTrue(json.loads(result.stdout.lstrip('\ufeff')))
                command = [shell, '-NoProfile', '-NonInteractive', '-File', str(entry)]
                invalid = subprocess.run([*command, '-Gate', 'Header', '-Mode', 'missing'],
                                         capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertNotEqual(0, invalid.returncode)
                self.assertNotIn('native-environment-is-forbidden', invalid.stderr)
                empty = subprocess.run([*command, '-Gate', 'setup', '-Tier', 'foreground'],
                                       capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertEqual(0, empty.returncode, empty.stderr)
                wrong_tier = subprocess.run(
                    [*command, '-Gate', 'setup', '-StartOnDesktop'],
                    capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertNotEqual(0, wrong_tier.returncode)
                self.assertNotIn('native-environment-is-forbidden', wrong_tier.stderr)
                self.assertFalse((repo / '.local-build').exists())

    @unittest.skipUnless(os.name == 'nt', 'Windows PowerShell 경로 선택이 필요하다')
    def test_powershell_provenance_path_follows_set_location(self):
        powershells = set(filter(None, (shutil.which('pwsh'), shutil.which('powershell'))))
        if not powershells:
            self.skipTest('PowerShell이 필요하다')
        repo = self.directory / 'working directory'
        dev = repo / 'tools/dev'
        (dev / 'lib').mkdir(parents=True)
        entry = dev / 'test-native.ps1'
        source = (ROOT / 'tools/dev/test-native.ps1').read_text(encoding='utf-8-sig')
        entry.write_text(
            source.replace(r'Local\YeeWindowsNativeGate',
                           r'Local\YeeWindowsNativeGate-test-' + self.directory.name),
            encoding='utf-8-sig')
        (dev / 'lib/windows_native_gate.py').write_text(
            'import json, sys\n'
            'print("[{}]" if "--print-filters" in sys.argv else '
            'json.dumps(sys.argv[sys.argv.index("--provenance") + 1]))\n',
            encoding='utf-8')
        proof = repo / 'build proof.json'
        proof.write_text('{}', encoding='utf-8')
        output = repo / 'chromium/out/Test'
        output.mkdir(parents=True)
        (output / 'build.ninja').touch()
        (dev / 'common.ps1').write_text('''$ErrorActionPreference='Stop'
$script:YeeRoot=$env:YEE_TEST_REPO
$script:ChromiumSrc=Join-Path $script:YeeRoot 'chromium'
$script:YeeOutDir=Join-Path $script:ChromiumSrc 'out/Test'
$script:YeeOutName='Test'
$script:DepotToolsDir=$script:YeeRoot
$script:LocalBuildRoot=$script:YeeRoot
function Get-DepotCommand { param($Name) $env:YEE_TEST_PYTHON }
function Assert-YeeOutputSelection {}
function Add-DepotToolsToPath {}
function Assert-ChromiumSrc {}
function Get-YeeBuildJobs { 1 }
function Get-CimInstance {}
function Get-Command { [pscustomobject]@{ Source=$env:YEE_TEST_PYTHON } }
''', encoding='utf-8')
        overlay = repo / 'tools/overlay/lib'
        overlay.mkdir(parents=True)
        (overlay / 'overlay-tools.ps1').write_text('''
function Get-YeeOverlayPatchPlan {
    param($ChromiumSrc, $RepoRoot)
    [pscustomobject]@{ Pending = $env:YEE_TEST_PENDING_OVERLAY -eq 'yes' }
}
''', encoding='utf-8')
        driver = self.directory / 'driver.ps1'
        driver.write_text('''Set-Location -LiteralPath $env:YEE_TEST_REPO
& (Join-Path $env:YEE_TEST_REPO 'tools/dev/test-native.ps1') -Gate header -NoBuild -ProvenanceFile 'build proof.json'
exit $LASTEXITCODE
''', encoding='utf-8')
        env = {**os.environ, 'YEE_TEST_REPO': str(repo), 'YEE_TEST_PYTHON': sys.executable}
        for shell in powershells:
            with self.subTest(shell=shell):
                result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-File', str(driver)],
                                        cwd=self.directory, env=env, capture_output=True,
                                        text=True, encoding='utf-8', errors='replace')
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual(proof, Path(json.loads(result.stdout)))
                stale = subprocess.run(
                    [shell, '-NoProfile', '-NonInteractive', '-File', str(driver)],
                    cwd=self.directory, env={**env, 'YEE_TEST_PENDING_OVERLAY': 'yes'},
                    capture_output=True, text=True, encoding='utf-8', errors='replace')
                self.assertNotEqual(0, stale.returncode)
                self.assertIn('configure.ps1', stale.stderr)

    def test_reused_build_requires_same_sources_targets_and_every_artifact(self):
        chromium = self.directory / 'src'
        output = chromium / 'out' / 'Test'
        output.mkdir(parents=True)
        (output / 'sample_tests.exe').write_bytes(b'test')
        (output / 'sample.dll').write_bytes(b'dll')
        (output / 'locales').mkdir()
        resources = output / 'locales/en-US.pak'
        resources.write_bytes(b'resource')
        (output / 'data').mkdir()
        generated = output / 'data/test-fixture.html'
        generated.write_bytes(b'fixture')
        with mock.patch.object(gate.os, 'scandir', side_effect=PermissionError('fixture')):
            with self.assertRaises(PermissionError):
                gate.runtime_leaves(generated.parent)
        (chromium / 'tools').mkdir()
        installed_tool = chromium / 'tools/native-test-tool.exe'
        installed_tool.write_bytes(b'package')
        runtime = output / 'sample_tests.runtime_deps'
        runtime.write_text(
            'sample_tests.exe\nsample.dll\nlocales/en-US.pak\ndata/\n'
            '../../tools/native-test-tool.exe\n', encoding='utf-8')
        provenance = self.directory / 'build.json'
        files, membership = gate.artifact_scope(output, {'sample_tests'})
        record = {'schema': gate.BUILD_SCHEMA, 'inputs': {'source': 1},
                  'exit_code': 0, 'targets': ['area:sample_tests'],
                  'runtime_membership': membership,
                  'artifacts': gate.artifacts(output, files)}
        provenance.write_text(json.dumps(record), encoding='utf-8')
        args = mock.Mock(provenance=provenance)
        destination = self.directory / 'evidence'
        destination.mkdir()
        package_stat = installed_tool.stat()
        locale_stat = resources.stat()

        def change_earlier_artifact(path):
            if path == installed_tool:
                resources.write_bytes(b'new-resource')
            return record_artifact(path)

        record_artifact = gate.artifact_record
        with mock.patch.object(gate.subprocess, 'run', side_effect=AssertionError('native 실행 금지')):
            self.assertEqual(record['artifacts'],
                             gate.create_build_evidence(args, {'source': 1},
                                                        [self.sample_run()], destination, output))
            if os.name == 'nt':
                (output / 'other_tests.exe').write_bytes(b'other')
                (output / 'other_tests.runtime_deps').write_text(
                    'locales/EN-US.PAK\n', encoding='utf-8')
                larger_files, larger_membership = gate.artifact_scope(
                    output, ['other_tests', 'sample_tests'])
                larger_record = {**record, 'targets': ['area:sample_tests', 'area:other_tests'],
                                 'runtime_membership': larger_membership,
                                 'artifacts': gate.artifacts(output, larger_files)}
                provenance.write_text(json.dumps(larger_record), encoding='utf-8')
                self.assertEqual(record['artifacts'],
                                 gate.create_build_evidence(args, {'source': 1},
                                                            [self.sample_run()], destination, output))
                provenance.write_text(json.dumps(record), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, '기록'):
                gate.create_build_evidence(args, {'source': 2},
                                            [self.sample_run()], destination, output)
            with mock.patch.object(gate, 'artifact_record', side_effect=change_earlier_artifact):
                with self.assertRaisesRegex(RuntimeError, '런타임 파일'):
                    gate.create_build_evidence(args, {'source': 1},
                                                [self.sample_run()], destination, output)
            resources.write_bytes(b'resource')
            os.utime(resources, ns=(locale_stat.st_atime_ns, locale_stat.st_mtime_ns))
            installed_tool.write_bytes(b'changed')
            os.utime(installed_tool, ns=(package_stat.st_atime_ns, package_stat.st_mtime_ns))
            with self.assertRaisesRegex(ValueError, '런타임'):
                gate.create_build_evidence(args, {'source': 1},
                                            [self.sample_run()], destination, output)
            installed_tool.write_bytes(b'package')
            os.utime(installed_tool, ns=(package_stat.st_atime_ns, package_stat.st_mtime_ns))
            (output / 'sample.dll').unlink()
            with self.assertRaises(FileNotFoundError):
                gate.create_build_evidence(args, {'source': 1},
                                            [self.sample_run()], destination, output)
            (output / 'sample.dll').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, '런타임'):
                gate.create_build_evidence(args, {'source': 1},
                                            [self.sample_run()], destination, output)
            generated.unlink()
            with self.assertRaisesRegex(ValueError, '집합'):
                gate.create_build_evidence(args, {'source': 1},
                                            [self.sample_run()], destination, output)
        runtime.write_text('../../\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, '다른 출력 또는 checkout'):
            gate.artifact_scope(output, {'sample_tests'})
        with mock.patch.object(gate.Path, 'iterdir', side_effect=PermissionError('fixture')):
            with self.assertRaises(PermissionError):
                gate.component_dlls(output)


class WindowsGateCoverageTest(unittest.TestCase):
    def test_windows_declarations_have_their_build_filter_and_tier(self):
        """zsh 없이도 모든 Windows 선언의 GN·필터·tier 연결을 검사한다."""
        patch = (ROOT / 'patches/0001-integrate-yee-shell.patch').read_text(encoding='utf-8')
        chromium = chromium_checkout(ROOT, os.environ)
        targets, catalog = gn_targets(ROOT, patch, chromium)
        manifest = gate.load_manifest(ROOT / 'tests/tooling/test_tiers.json')
        exceptions = json.loads(
            (ROOT / 'tests/tooling/gate_coverage_allowlist.json').read_text(encoding='utf-8'))
        allowlist = {key: value for key, value in exceptions.items() if value['kind'] == 'opt-in'}
        runs = windows_gate_runs(ROOT)
        inventory = tier_inventory(ROOT, chromium, patch, targets, catalog, runs)
        inventory = {key: parameters for key, parameters in inventory.items()
                     if key not in manifest or 'Windows' in manifest[key]['os']}
        windows = {}
        for key, entry in manifest.items():
            if ('Windows' not in entry['os'] or
                    (chromium is None and not (ROOT / key.split('::')[0]).is_file())):
                continue
            windows[key] = {**entry, 'os': ['Windows'],
                            'needs_display': {platform: reason
                                              for platform, reason in entry.get('needs_display', {}).items()
                                              if platform == 'Windows'}}
        self.assertEqual([], tier_violations(inventory, windows, targets, catalog, runs, allowlist))
        missing = []
        for key, parameters in inventory.items():
            if key in allowlist:
                continue
            path, name = key.split('::')
            if not covered_by_gate(destination_path(path, catalog), name, parameters, targets, runs):
                missing.append(key)
        self.assertEqual([], missing, '\n'.join(missing))


if __name__ == '__main__':
    unittest.main()
