#!/usr/bin/env python3
"""임시 원본·도구로 Windows 경로 가드와 사전 컴파일 실행 계약을 확인한다."""

import importlib.util
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
POWERSHELL = shutil.which('pwsh') or shutil.which('powershell')


class WindowsToolsFixture(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-windows-selection-')
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name).resolve() / "repo with spaces and 'quote"
        self.dev = self.repo / 'tools/dev'
        self.dev.mkdir(parents=True)
        for source in (ROOT / 'tools/dev').glob('*.ps1'):
            shutil.copy(source, self.dev)
        shutil.copytree(ROOT / 'tools/dev/lib', self.dev / 'lib')
        overlay = self.repo / 'tools/overlay'
        overlay.mkdir()
        shutil.copytree(ROOT / 'tools/overlay/lib', overlay / 'lib')
        (overlay / 'brand-config.ps1').write_text(
            'function Get-YeeBrandConfig { param([switch]$SkipAssetValidation) '
            "[pscustomobject]@{Name='Yee'} }\n", encoding='utf-8')
        for name, event in (('apply.ps1', 'apply'), ('install-branding.ps1', 'branding'),
                            ('install-brand-assets.ps1', 'assets'),
                            ('install-yee-ui-sources.ps1', 'sources')):
            (overlay / name).write_text(
                'param([string]$ChromiumSrc, [switch]$CheckOnly)\n'
                f"Add-Content -LiteralPath $env:CONTRACT_LOG -Value '{event}'\n", encoding='utf-8')
        (self.repo / 'build').mkdir()
        (self.repo / 'build/args.gn').write_text('is_debug = false\n')
        self.log = self.repo / 'events.log'
        self.env = {key: value for key, value in os.environ.items()
                    if not key.upper().startswith('YEE_')}
        self.env.update(CONTRACT_LOG=str(self.log), PYTHONDONTWRITEBYTECODE='1',
                        PYTHONUTF8='1', YEE_BUILD_JOBS='2',
                        PATH=str(Path(sys.executable).parent) + os.pathsep + os.environ['PATH'])

    def make_root(self, name='.local-build/stable157', output=None):
        local = self.repo / name
        depot = local / 'depot_tools'
        depot.mkdir(parents=True)
        (depot / 'gclient.bat').write_text('@echo gclient>>"%CONTRACT_LOG%"\n@exit /b 0\n')
        (depot / 'python3.bat').write_text(
            f'@"{sys.executable}" "%~dp0fixture-python.py" %*\n@exit /b %errorlevel%\n')
        (depot / 'fixture-python.py').write_text('''import os, sys
from pathlib import Path
with open(os.environ['CONTRACT_LOG'], 'a', encoding='utf-8') as stream:
    stream.write('python:' + ' '.join(sys.argv[1:]) + '\\n')
sys.exit(int(os.environ.get('PIN_EXIT', '0')))
''', encoding='utf-8')
        (depot / 'gn.bat').write_text('@echo gn>>"%CONTRACT_LOG%"\n@exit /b 0\n')
        (depot / 'autoninja.bat').write_text(
            '@echo autoninja:%*>>"%CONTRACT_LOG%"\n'
            '@if defined BUILD_EXIT echo FAILED: obj/yee/main.obj\n'
            '@if defined BUILD_EXIT exit /b %BUILD_EXIT%\n@echo compile complete\n@exit /b 0\n')
        (depot / 'autoninja.py').write_text('''import json, os, sys
from pathlib import Path
with open(os.environ['CONTRACT_LOG'], 'a', encoding='utf-8') as stream:
    stream.write('autoninja:' + ' '.join(sys.argv[1:]) + '\\n')
Path(os.environ['CONTRACT_LOG'] + '.args.json').write_text(json.dumps(sys.argv[1:]))
if os.environ.get('BUILD_STDERR'):
    print('diagnostic from compiler', file=sys.stderr)
if os.environ.get('BUILD_EXIT'):
    print('FAILED: obj/yee/main.obj')
    sys.exit(int(os.environ['BUILD_EXIT']))
print('compile complete')
''', encoding='utf-8')
        src = local / 'chromium/src'
        (src / 'chrome/browser/ui/views/yee').mkdir(parents=True)
        (src / 'BUILD.gn').touch()
        (src / 'chrome/browser/ui/views/yee/BUILD.gn').touch()
        if output:
            graph = src / 'out' / output / 'build.ninja'
            graph.parent.mkdir(parents=True)
            graph.touch()
        return local

    def run_ps(self, body, **env):
        common = str(self.dev / 'common.ps1').replace("'", "''")
        return subprocess.run([POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive',
                               '-ExecutionPolicy', 'Bypass', '-Command',
                               f". '{common}'\n" + body], cwd=self.repo,
                              env={**self.env, **env}, capture_output=True, text=True,
                              encoding='utf-8', errors='replace')

    def run_entry(self, name, *args, **env):
        return subprocess.run([POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive',
                               '-ExecutionPolicy', 'Bypass', '-File', str(self.dev / name), *args],
                              cwd=self.repo, env={**self.env, **env}, capture_output=True,
                              text=True, encoding='utf-8', errors='replace')

    def events(self):
        return self.log.read_text(encoding='utf-8-sig').splitlines() if self.log.exists() else []


@unittest.skipUnless(os.name == 'nt' and POWERSHELL, 'Windows PowerShell이 필요하다')
class WindowsBuildSelectionTest(WindowsToolsFixture):
    def test_explicit_flags_and_empty_defaults(self):
        """명시 여부를 기본값 전에 보존하며 빈 값은 기존 기본값으로 처리한다."""
        body = ('Write-Output "$($script:YeeLocalBuildRootExplicit):'
                '$($script:YeeChromiumRootExplicit):$($script:YeeOutNameExplicit)"')
        for changes, expected in (({}, 'False:False:False'),
                                  ({'YEE_LOCAL_BUILD_ROOT': str(self.repo),
                                    'YEE_CHROMIUM_ROOT': str(self.repo), 'YEE_OUT_NAME': 'Chosen'},
                                   'True:True:True'),
                                  ({'YEE_LOCAL_BUILD_ROOT': '', 'YEE_CHROMIUM_ROOT': '',
                                    'YEE_OUT_NAME': ''}, 'False:False:False')):
            result = self.run_ps(body, **changes)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), expected)

    def test_root_candidates_stop_before_checkout_or_missing_tool_advice(self):
        """부분 후보를 알리되 캐시만 있는 폴더는 제외하고 checkout 부작용 전에 멈춘다."""
        local = self.make_root()
        shutil.rmtree(local / 'depot_tools')
        depot_only = self.repo / '.local-build/depot only/depot_tools'
        depot_only.mkdir(parents=True)
        (depot_only / 'gclient.bat').touch()
        (self.repo / '.local-build/cache only').mkdir()
        for command in ('Assert-YeeBuildRootSelection', 'Add-DepotToolsToPath', 'Assert-ChromiumSrc'):
            result = self.run_ps(command)
            self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
            self.assertIn(str(local), result.stderr)
            self.assertIn(str(depot_only.parent), result.stderr)
            self.assertNotIn('cache only', result.stderr)
            self.assertIn('YEE_CHROMIUM_ROOT', result.stderr)
            # 안내의 작은따옴표 이스케이프를 그대로 복사해 실행할 수 있어야 한다.
            example = result.stderr.splitlines()[-1].split(': ', 1)[1]
            selected = self.run_ps(example + '\nWrite-Output $env:YEE_LOCAL_BUILD_ROOT')
            self.assertEqual(selected.stdout.strip(), str(self.repo / '.local-build'))
        result = self.run_entry('checkout.ps1')
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertEqual(self.events(), [])
        self.assertFalse((self.repo / '.local-build/chromium').exists())

    def test_explicit_roots_and_existing_default_take_precedence(self):
        """명시 루트와 유효한 기본 루트는 후보가 있어도 재선택하지 않는다."""
        self.make_root()
        for variable in ('YEE_LOCAL_BUILD_ROOT', 'YEE_CHROMIUM_ROOT'):
            result = self.run_ps('Assert-YeeBuildRootSelection; Write-Output ok',
                                 **{variable: str(self.repo / 'missing chosen')})
            self.assertEqual(result.returncode, 0, result.stderr)
        self.make_root('.local-build')
        result = self.run_ps('Assert-YeeBuildRootSelection; Write-Output ok')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'ok')

    def test_new_machine_has_no_selection_error(self):
        """후보 없는 새 장비는 기존 준비 절차를 유지한다."""
        result = self.run_ps('Assert-YeeOutputSelection; Write-Output ok')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'ok')

    def test_output_guard_precedes_all_entry_side_effects(self):
        """다른 graph가 있으면 저용량 검사·동기화·프로필 생성·실행 전에 종료 코드 18로 멈춘다."""
        local = self.make_root(output='YeeStable157')
        for entry, args in (('configure.ps1', []), ('configure.ps1', ['-CheckOverlayOnly']),
                            ('build.ps1', []), ('build-ui.ps1', []), ('run.ps1', []),
                            ('smoke-test.ps1', []), ('package-windows.ps1', []),
                            ('check-compile.ps1', ['-List'])):
            with self.subTest(entry=entry, args=args):
                result = self.run_entry(entry, *args, YEE_LOCAL_BUILD_ROOT=str(local))
                self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
                self.assertNotIn('Disk guard:', result.stdout)
                self.assertIn('YeeStable157', result.stderr)
                self.assertIn('YEE_CHROMIUM_ROOT', result.stderr)
        for command in ('Initialize-YeeBuildInputs', "Invoke-YeeBuildTarget -Target 'fixture'"):
            result = self.run_ps(command, YEE_LOCAL_BUILD_ROOT=str(local))
            self.assertEqual(result.returncode, 18, result.stderr)
        self.assertEqual(self.events(), [])
        for path in ('runtime-profile', 'cache', 'releases', 'chromium/src/out/YeePilot'):
            self.assertFalse((local / path).exists(), path)

    def test_default_graph_and_explicit_output_are_preserved(self):
        """기존 기본 graph와 명시한 새 출력은 후보 자동 선택 없이 통과한다."""
        local = self.make_root(output='YeeStable157')
        for output in ('YeeStable157', 'NewChosen'):
            result = self.run_ps('Assert-YeeOutputSelection; Write-Output $script:YeeOutName',
                                 YEE_LOCAL_BUILD_ROOT=str(local), YEE_OUT_NAME=output)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), output)
        graph = local / 'chromium/src/out/YeePilot/build.ninja'
        graph.parent.mkdir()
        graph.touch()
        result = self.run_ps('Assert-YeeOutputSelection; Write-Output $script:YeeOutName',
                             YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'YeePilot')

    def test_multi_object_build_exceeds_batch_limit_without_losing_arguments(self):
        """8191자보다 긴 목록을 단일 Python 실행으로 보내 object 인수를 보존한다."""
        local = self.make_root(output='YeeStable157')
        targets = [f'obj/chrome/path_with_a_long_component_name/file_{i}.obj' for i in range(220)]
        self.assertGreater(len(' '.join(targets)), 8191)
        body = ('Add-DepotToolsToPath\nInvoke-YeeBuildTarget -KeepGoing -Target @(' +
                ','.join("'" + target + "'" for target in targets) + ')')
        result = self.run_ps(body, YEE_LOCAL_BUILD_ROOT=str(local), YEE_OUT_NAME='YeeStable157')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        recorded = json.loads(Path(str(self.log) + '.args.json').read_text())
        self.assertEqual(recorded, ['-C', 'out\\YeeStable157', '-j', '2', '-k', '0', *targets])


@unittest.skipUnless(os.name == 'nt' and POWERSHELL, 'Windows PowerShell이 필요하다')
class WindowsCompilePreflightTest(WindowsToolsFixture):
    def setUp(self):
        super().setUp()
        self.local = self.make_root(output='YeeStable157')
        self.src = self.local / 'chromium/src'
        self.out = self.src / 'out/YeeStable157'
        self.env.update(YEE_LOCAL_BUILD_ROOT=str(self.local), YEE_OUT_NAME='YeeStable157')
        # 실행기 경계를 검사한다. 계획 조회만 대체하고 완료·실패 보고는 실제 공통 코드를 실행한다.
        (self.local / 'depot_tools/fixture-python.py').write_text('''import json, os, runpy, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ['CONTRACT_LOG'], 'a', encoding='utf-8') as stream:
    stream.write('python:' + ' '.join(args) + '\\n')
if Path(args[0]).name == 'chromium-version.py':
    sys.exit(int(os.environ.get('PIN_EXIT', '0')))
command = args[1]
if command == 'check-graph':
    sys.exit(int(os.environ.get('GRAPH_EXIT', '0')))
if command == 'plan':
    if '--save-to' in args:
        directory = Path(args[args.index('--save-to') + 1])
        plan = {'chromium_src': args[2], 'out_dir': args[3],
                'compiled': {'chrome/main.cc': ['obj/yee/main.obj']},
                'include_unbuilt': '--include-unbuilt' in args,
                'unbuilt_objects': {}, 'uncompiled_sources': ['chrome/platform_mac.mm']}
        (directory / 'plan.json').write_text(json.dumps(plan), encoding='utf-8')
        (directory / 'targets.txt').write_text('obj/yee/main.obj\\nobj/yee/extra.obj\\n')
    print('목록 준비')
    sys.exit(int(os.environ.get('PLAN_EXIT', '0')))
sys.argv = args
runpy.run_path(args[0], run_name='__main__')
''', encoding='utf-8')

    def test_listing_is_read_only_and_includes_requested_scope(self):
        """목록 조회는 동기화·기록·빌드를 실행하지 않으며 미빌드 옵션을 전달한다."""
        result = self.run_entry('check-compile.ps1', '-List', '-IncludeUnbuilt')
        self.assertEqual(result.returncode, 0, result.stderr)
        events = self.events()
        self.assertEqual(len(events), 2, events)
        self.assertIn('--include-unbuilt --list', events[-1])
        self.assertFalse((self.local / 'logs').exists())
        self.assertFalse((self.local / 'cache').exists())

    def test_stale_graph_and_pin_fail_before_build_logs(self):
        """핀 오류는 소스 복사 전에, 오래된 graph는 실행 기록 폴더 생성 전에 중단한다."""
        pin = self.run_entry('check-compile.ps1', PIN_EXIT='8')
        self.assertNotEqual(pin.returncode, 0)
        self.assertNotIn('sources', self.events())
        self.log.unlink()
        stale = self.run_entry('check-compile.ps1', GRAPH_EXIT='19')
        self.assertEqual(stale.returncode, 19, stale.stderr)
        self.assertIn('sources', self.events())
        self.assertFalse((self.local / 'logs').exists())

    def test_build_logs_targets_and_scope_summary(self):
        """복수 object를 공통 빌드에 전달하고 UTF-8 기록과 미컴파일 범위를 남긴다."""
        result = self.run_entry('check-compile.ps1', '-IncludeUnbuilt')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        build = next(event for event in self.events() if event.startswith('autoninja:'))
        self.assertIn('-j 2 -k 0 obj/yee/main.obj obj/yee/extra.obj', build)
        self.assertIn('sources', self.events())
        self.assertNotIn('branding', self.events())
        self.assertIn('완전한 통과 아님', result.stdout)
        logs = list((self.local / 'logs').glob('compile-preflight.*'))
        self.assertEqual(len(logs), 1)
        self.assertIn('compile complete', (logs[0] / 'build.log').read_text(encoding='utf-8'))

    def test_build_failure_preserves_exit_and_reports_source(self):
        """Ninja 실패 코드를 보존하며 실패 object와 원본 소스를 기록한다."""
        result = self.run_entry('check-compile.ps1', BUILD_EXIT='9')
        self.assertEqual(result.returncode, 9, result.stdout + result.stderr)
        directory = next((self.local / 'logs').glob('compile-preflight.*'))
        self.assertEqual((directory / 'failed-sources.txt').read_text(encoding='utf-8'),
                         'chrome/main.cc\n')
        self.assertEqual((directory / 'failed-objects.txt').read_text(encoding='utf-8'),
                         'obj/yee/main.obj\n')

    def test_native_stderr_keeps_real_exit_in_both_powershell_versions(self):
        """PowerShell 5.1·7 모두 native 경고만으로 실패하지 않고 실제 실패 코드를 보존한다."""
        engines = list(dict.fromkeys(filter(None, (POWERSHELL, shutil.which('powershell')))))
        for engine in engines:
            for exit_code in ('0', '9'):
                with self.subTest(engine=engine, exit_code=exit_code):
                    with mock.patch(__name__ + '.POWERSHELL', engine):
                        result = self.run_entry('check-compile.ps1', BUILD_STDERR='1', BUILD_EXIT=exit_code)
                    self.assertEqual(result.returncode, int(exit_code), result.stdout + result.stderr)
        for directory in (self.local / 'logs').glob('compile-preflight.*'):
            self.assertIn('diagnostic from compiler',
                          (directory / 'build.log').read_text(encoding='utf-8'))


class WindowsCompilePlannerTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('compile_preflight', ROOT / 'tools/dev/lib/compile_preflight.py')
        self.planner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.planner)

    def test_ninja_executable_follows_host_platform(self):
        """Windows에서는 확장자 있는 Ninja를 선택하며 Mac 경로는 보존한다."""
        for platform, filename in (('win32', 'ninja.exe'), ('darwin', 'ninja')):
            with mock.patch.object(sys, 'platform', platform):
                self.assertEqual(self.planner.ninja_path(Path('src')),
                                 Path('src/third_party/ninja') / filename)

    def test_single_output_plan_uses_object_targets_on_windows(self):
        """실제 단일 출력 계획도 Windows에서 cmd가 소비하는 caret 대상 대신 object를 쓴다."""
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            (out / 'build.ninja').touch()
            source = '../../chrome/main.cc'
            object_path = 'obj/chrome/main.obj'
            catalog = {'patches': [{'role': 'shell', 'file': 'unused'}]}
            with (mock.patch.object(self.planner, 'load_catalog', return_value=catalog),
                  mock.patch.object(self.planner, 'patch_paths', return_value=['chrome/main.cc']),
                  mock.patch.object(self.planner, 'owned_source_files', return_value=[]),
                  mock.patch.object(self.planner, 'graph_sources', return_value={source}),
                  mock.patch.object(self.planner, 'query_graph',
                                    return_value={source: {'outputs': [object_path]}})):
                for platform, target in (('win32', object_path), ('darwin', source + '^')):
                    with mock.patch.object(sys, 'platform', platform):
                        plan = self.planner.compile_plan(Path('src'), out, include_unbuilt=True)
                    self.assertEqual(plan['targets'], [target])

    def test_depfile_drive_letters_and_escaped_spaces(self):
        """Windows 드라이브의 콜론과 이스케이프한 공백을 GN depfile 입력으로 읽는다."""
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            source = out / 'sdk path/header.h'
            source.parent.mkdir()
            source.touch()
            token = str(source).replace('\\', '/').replace(' ', '\\ ')
            (out / 'build.ninja.d').write_text(f'build.ninja.stamp: {token}\n')
            (out / 'build.ninja.stamp').touch()
            self.assertEqual(self.planner.stale_graph_inputs(out), [])
            source.unlink()
            self.assertIn('(누락)', self.planner.stale_graph_inputs(out)[0])


if __name__ == '__main__':
    unittest.main()
