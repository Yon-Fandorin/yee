#!/usr/bin/env python3
"""임시 루트·출력과 가짜 도구로 개발 환경 선택 가드를 검사한다."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

NO_BUILD_ENTRIES = (
    ('test-agent-browser.sh', ['--no-build']),
    ('test-agent-prompt.sh', ['--no-build']),
    ('test-omnibox-accessibility.sh', ['--no-build']),
    ('test-site-controls.sh', ['--no-build']),
    ('test-header.sh', ['--no-build']),
    ('test-internal-pages.sh', ['--no-build']),
    ('test-youtube-live.sh', ['off', '--no-build']),
    ('test-sidebar.sh', ['--no-build']),
    ('test-browser-surface-layout.sh', ['--no-build']),
)

RUN_ENTRIES = (
    ('run.sh', []), ('smoke-test.sh', []), ('run-agent-prototype.sh', []),
    ('package-macos.sh', []), ('package-macos.sh', ['--skip-build']),
)


@unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
class BuildSelectionTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-build-selection-')
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name).resolve() / 'repo with spaces'
        self.dev = self.repo / 'tools/dev'
        self.dev.mkdir(parents=True)
        shutil.copytree(ROOT / 'tools/dev/lib', self.dev / 'lib')
        for name in ('common.zsh', 'checkout.sh', 'configure.sh', 'check-compile.sh',
                     'build.sh', 'build-ui.sh', 'test-content-blocking-native.sh',
                     'test-sidebar.sh', 'test-browser-surface-layout.sh', 'test-header.sh',
                     'test-agent-browser.sh', 'test-internal-pages.sh',
                     'test-omnibox-accessibility.sh', 'test-site-controls.sh', 'test-youtube-live.sh',
                     'test-agent-prompt.sh', 'run.sh', 'smoke-test.sh', 'package-macos.sh',
                     'run-agent-prototype.sh', 'doctor.sh', 'setup-metal.sh', 'ui-test-window-args.sh',
                     'gracefully-quit-yee.swift', 'yee_build_paths.py', 'ui_test_display.py',
                     'test-procedural-content-blocking.py', 'test-baseline-filter-updates.py',
                     'test-content-blocking-browser-fixture.py'):
            shutil.copy(ROOT / 'tools/dev' / name, self.dev / name)
        overlay = self.repo / 'tools/overlay'
        overlay.mkdir()
        (overlay / 'brand_config.py').write_text('print("Yee")\n')
        (self.dev / 'chromium-version.py').write_text(
            'import sys\n'
            'print("f" * 40 if sys.argv[-1] == "revision" else "fixture")\n')
        (self.repo / 'build').mkdir()
        (self.repo / 'build/args.gn').write_text('is_debug = false\n')
        self.log = self.repo / 'events.log'
        self.bin = self.repo / 'fake tools'
        self.bin.mkdir()
        # 상위 개발 셸의 루트·출력·도구 선택 값을 테스트 하위 프로세스에 상속하지 않는다.
        self.env = {key: value for key, value in os.environ.items() if not key.startswith('YEE_')}
        self.env.update(PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        PYTHONDONTWRITEBYTECODE='1', CONTRACT_LOG=str(self.log), YEE_BUILD_JOBS='3')
        self.set_free_gib(500)
        self.write_shell(self.bin / 'git', 'print git >> "$CONTRACT_LOG"\nexit 71\n')
        self.write_shell(self.bin / 'gclient', 'print gclient >> "$CONTRACT_LOG"\nexit 72\n')
        self.write_shell(self.bin / 'caffeinate', 'shift\nexec "$@"\n')
        self.write_shell(self.bin / 'nice', 'shift 2\nexec "$@"\n')
        self.write_shell(self.bin / 'autoninja', 'print -r -- "autoninja:$*" >> "$CONTRACT_LOG"\n')
        self.write_shell(self.bin / 'metal', 'exit 0\n')
        self.write_shell(self.bin / 'xcrun', 'print xcrun >> "$CONTRACT_LOG"\nexit 73\n')
        self.env['YEE_METAL_BIN'] = str(self.bin / 'metal')
        self.write_python(self.bin / 'gn', '''import os, sys
from pathlib import Path
with open(os.environ['CONTRACT_LOG'], 'a') as stream:
    stream.write('gn:' + ' '.join(sys.argv[1:]) + '\\n')
if sys.argv[1] == 'gen':
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)
    (output / 'build.ninja').write_text('가짜 graph\\n')
else:
    print('is_debug = false')
''')
        for name, event in [('apply.sh', 'apply'), ('install-branding.sh', 'branding'),
                            ('install-brand-assets.sh', 'assets'), ('install-yee-ui-sources.sh', 'sources')]:
            self.write_shell(overlay / name, f'print {event} >> "$CONTRACT_LOG"\n')
        self.write_shell(self.dev / 'usage.sh', 'print usage >> "$CONTRACT_LOG"\n')
        self.write_shell(self.dev / 'test-run-preflight.sh', 'exit 0\n')

    def write_shell(self, path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/bin/zsh\nset -euo pipefail\n' + body)
        path.chmod(0o755)

    def write_python(self, path, body):
        path.write_text(f'#!{sys.executable}\n' + body)
        path.chmod(0o755)

    def set_free_gib(self, free_gib):
        self.write_shell(self.bin / 'df',
                         'print df >> "$CONTRACT_LOG"\n'
                         f'printf "header\\nfixture 0 0 {free_gib * 1024 * 1024} 0 /\\n"\n')

    def make_root(self, name='.local-build/stable157', output=None):
        local = self.repo / name
        (local / 'depot_tools/.git').mkdir(parents=True)
        self.write_shell(local / 'depot_tools/gclient', 'print gclient >> "$CONTRACT_LOG"\nexit 72\n')
        src = local / 'chromium/src'
        (src / 'chrome/browser/ui/views/yee').mkdir(parents=True)
        (src / 'BUILD.gn').touch()
        (src / 'chrome/browser/ui/views/yee/BUILD.gn').touch()
        if output:
            graph = src / 'out' / output / 'build.ninja'
            graph.parent.mkdir(parents=True)
            graph.touch()
        return local

    def run_zsh(self, body, **env):
        return subprocess.run(['zsh', '-c', 'source "$1"\n' + body,
                               'selection-contract', str(self.dev / 'common.zsh')],
                              env={**self.env, **env}, cwd=self.repo, capture_output=True, text=True)

    def run_entry(self, name, *args, **env):
        return subprocess.run(['zsh', str(self.dev / name), *args],
                              env={**self.env, **env}, cwd=self.repo, capture_output=True, text=True)

    def events(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_explicit_flags_are_captured_before_defaults(self):
        """명시 여부와 실제 선택 값을 기록하고 빈 값은 기존 기본값과 같이 처리한다."""
        body = 'print -rl -- "$YEE_LOCAL_BUILD_ROOT_EXPLICIT:$YEE_OUT_NAME_EXPLICIT" "$LOCAL_BUILD_ROOT" "$YEE_OUT_NAME"'
        default = self.run_zsh(body)
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertEqual(default.stdout.splitlines(), ['false:false', str(self.repo / '.local-build'), 'YeePilot'])
        explicit = self.run_zsh(body, YEE_LOCAL_BUILD_ROOT=str(self.repo / 'chosen'), YEE_OUT_NAME='Chosen')
        self.assertEqual(explicit.stdout.splitlines(), ['true:true', str(self.repo / 'chosen'), 'Chosen'])
        empty = self.run_zsh(body, YEE_LOCAL_BUILD_ROOT='', YEE_OUT_NAME='')
        self.assertEqual(empty.stdout, default.stdout)

    def test_root_candidates_stop_missing_inputs_and_checkout_before_mutations(self):
        """소스 또는 depot_tools만 있는 후보도 알리며 새 checkout 안내·도구 실행을 차단한다."""
        source_root = self.repo / '.local-build/source root'
        (source_root / 'chromium/src').mkdir(parents=True)
        (source_root / 'chromium/src/BUILD.gn').touch()
        depot_root = self.repo / '.local-build/depot root'
        (depot_root / 'depot_tools').mkdir(parents=True)
        (depot_root / 'depot_tools/gclient').touch()
        cache = self.repo / '.local-build/cache-only'
        cache.mkdir()
        for check in ('require_depot_tools', 'require_chromium_src', 'checkout.sh'):
            with self.subTest(check=check):
                result = self.run_entry(check) if check.endswith('.sh') else self.run_zsh(check)
                self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
                self.assertIn('환경 미설정', result.stderr)
                for candidate in (source_root, depot_root):
                    self.assertIn(str(candidate), result.stderr)
                self.assertNotIn(str(cache), result.stderr)
                self.assertIn('YEE_LOCAL_BUILD_ROOT', result.stderr)
                self.assertIn('YEE_OUT_NAME', result.stderr)
                self.assertIn('기본 루트를 쓰려면 YEE_LOCAL_BUILD_ROOT="$YEE_ROOT/.local-build"', result.stderr)
                default_command = result.stderr.splitlines()[-1].strip()
                resolved = self.run_zsh('unset YEE_ROOT\n' + default_command +
                                        '\nprint -r -- "$YEE_LOCAL_BUILD_ROOT"')
                self.assertEqual(resolved.returncode, 0, resolved.stderr)
                self.assertEqual(resolved.stdout.strip(), str(self.repo / '.local-build'))
                self.assertNotIn('checkout.sh', result.stdout + result.stderr)
        self.assertEqual(self.events(), [])
        self.assertFalse((self.repo / '.local-build/chromium').exists())
        self.assertFalse((self.repo / '.local-build/depot_tools').exists())

    def test_explicit_root_is_used_without_automatic_reselection(self):
        """유효한 명시 루트는 통과하고 없는 명시 루트도 다른 후보로 자동 전환하지 않는다."""
        local = self.make_root()
        result = self.run_zsh('require_depot_tools; require_chromium_src; print -r -- "$CHROMIUM_SRC"',
                               YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(local / 'chromium/src'), result.stdout)
        missing = self.run_zsh('require_depot_tools', YEE_LOCAL_BUILD_ROOT=str(self.repo / 'missing'))
        self.assertEqual(missing.returncode, 5)
        self.assertNotIn('환경 미설정', missing.stderr)
        self.assertEqual(self.events(), [])

    def test_linked_root_candidate_also_stops_checkout_guidance(self):
        """디렉터리 링크로 연결한 기존 루트도 후보로 찾아 새 checkout을 막는다."""
        target = self.make_root('external build')
        linked = self.repo / '.local-build/linked root'
        linked.parent.mkdir()
        linked.symlink_to(target, target_is_directory=True)
        for check in ('require_depot_tools', 'require_chromium_src', 'checkout.sh'):
            result = self.run_entry(check) if check.endswith('.sh') else self.run_zsh(check)
            self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
            self.assertIn(str(linked), result.stderr)
            self.assertNotIn('checkout.sh', result.stdout + result.stderr)
        self.assertEqual(self.events(), [])

    def test_no_candidate_keeps_original_missing_input_and_checkout_behavior(self):
        """새 장비의 누락 안내와 checkout 진입은 유지하며 다운로드는 가짜 git에서 멈춘다."""
        for check, code in [('require_depot_tools', 5), ('require_chromium_src', 6)]:
            result = self.run_zsh(check)
            self.assertEqual(result.returncode, code)
            self.assertIn('checkout.sh', result.stderr)
            self.assertNotIn('환경 미설정', result.stderr)
        checkout = self.run_entry('checkout.sh')
        self.assertEqual(checkout.returncode, 71, checkout.stdout + checkout.stderr)
        self.assertIn('Cloning depot_tools', checkout.stdout)
        self.assertEqual(self.events(), ['df', 'git'])

    def test_existing_default_root_is_kept_when_other_roots_exist(self):
        """기본 루트의 입력이 이미 있으면 후보가 있어도 기존 기본 선택을 유지한다."""
        local = self.make_root('.local-build')
        self.make_root('.local-build/another')
        result = self.run_zsh('require_depot_tools; require_chromium_src; print -r -- "$CHROMIUM_SRC"')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(local / 'chromium/src'), result.stdout)

    def test_implicit_output_stops_configuration_and_all_build_paths(self):
        """입력 준비·GN·직접 빌드·공통 회귀와 보호된 테스트 진입점에서 잘못된 출력을 막는다."""
        local = self.make_root(output='YeeStable157')
        env = {'YEE_LOCAL_BUILD_ROOT': str(local)}
        checks = [('function', 'prepare_yee_build_inputs'),
                  ('function', 'build_regression_targets "selection" fixture'),
                  ('configure.sh', ''), ('build.sh', ''), ('build-ui.sh', '')]
        for entry, argument in checks:
            with self.subTest(entry=entry):
                self.log.unlink(missing_ok=True)
                result = (self.run_zsh(argument, **env) if entry == 'function' else
                          self.run_entry(entry, *([argument] if argument else []), **env))
                self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
                self.assertIn('후보 출력:', result.stderr)
                self.assertIn(str(local / 'chromium/src/out/YeeStable157'), result.stderr)
                self.assertIn('export YEE_OUT_NAME=YeeStable157', result.stderr)
                self.assertFalse(any(event.startswith(('gn:', 'autoninja:')) for event in self.events()))
                self.assertFalse((local / 'chromium/src/out/YeePilot').exists())
                self.assertFalse((local / 'cache').exists())

    def test_compile_listing_reports_missing_output_selection_before_missing_graph(self):
        """목록 조회는 기본 graph 누락보다 출력 선택 안내를 먼저 보고한다."""
        local = self.make_root(output='YeeStable157')
        result = self.run_entry('check-compile.sh', '--list', YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
        self.assertIn('export YEE_OUT_NAME=YeeStable157', result.stderr)
        self.assertNotIn('먼저 configure.sh', result.stderr)
        self.assertEqual(self.events(), [])
        self.assertFalse((local / 'chromium/src/out/YeePilot').exists())
        self.assertFalse((local / 'cache').exists())
        selected = self.run_entry('check-compile.sh', '--list',
                                  YEE_LOCAL_BUILD_ROOT=str(local), YEE_OUT_NAME='Missing')
        self.assertEqual(selected.returncode, 6, selected.stdout + selected.stderr)
        self.assertIn('먼저 configure.sh', selected.stderr)
        self.assertEqual(self.events(), [])

    def test_build_reports_missing_output_selection_before_low_disk_space(self):
        """24 GiB 환경에서는 출력 미선택을 먼저 알리고 명시한 출력의 공간 검사는 유지한다."""
        local = self.make_root(output='YeeStable157')
        self.set_free_gib(24)
        result = self.run_entry('build.sh', YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
        self.assertIn('export YEE_OUT_NAME=YeeStable157', result.stderr)
        self.assertEqual(self.events(), [])
        self.assertFalse((local / 'chromium/src/out/YeePilot').exists())
        self.assertFalse((local / 'cache').exists())
        selected = self.run_entry('build.sh', YEE_LOCAL_BUILD_ROOT=str(local), YEE_OUT_NAME='YeeStable157')
        self.assertEqual(selected.returncode, 4, selected.stdout + selected.stderr)
        self.assertIn('24 GiB is available', selected.stderr)
        self.assertEqual(self.events(), ['df'])

    def test_other_build_entries_report_missing_output_selection_before_low_disk_space(self):
        """UI·회귀 진입점도 공간 부족 전에 출력 선택을 확인하고 명시한 출력의 공간 가드를 유지한다."""
        local = self.make_root(output='YeeStable157')
        self.set_free_gib(14)
        checks = [('build-ui.sh', []), ('test-content-blocking-native.sh', []),
                  ('test-header.sh', ['unit']), ('test-agent-browser.sh', []),
                  ('test-internal-pages.sh', []), ('test-omnibox-accessibility.sh', []),
                  ('test-site-controls.sh', []), ('test-youtube-live.sh', ['off']),
                  ('test-sidebar.sh', ['unit']), ('test-browser-surface-layout.sh', ['fast'])]
        for entry, arguments in checks:
            with self.subTest(entry=entry):
                self.log.unlink(missing_ok=True)
                result = self.run_entry(entry, *arguments, YEE_LOCAL_BUILD_ROOT=str(local))
                self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
                self.assertIn('export YEE_OUT_NAME=YeeStable157', result.stderr)
                self.assertEqual(self.events(), [])
                self.assertFalse((local / 'chromium/src/out/YeePilot').exists())
                self.assertFalse((local / 'cache').exists())
                selected = self.run_entry(entry, *arguments, YEE_LOCAL_BUILD_ROOT=str(local),
                                          YEE_OUT_NAME='YeeStable157')
                self.assertEqual(selected.returncode, 4, selected.stdout + selected.stderr)
                self.assertIn('14 GiB is available', selected.stderr)
                self.assertEqual(self.events(), ['df'])

    def test_no_build_entries_stop_before_test_helpers_and_binaries(self):
        """빌드 생략 때도 루트·출력 선택을 먼저 확인해 보조 도구·프로필·바이너리 실행을 막는다."""
        local = self.make_root(output='YeeStable157')
        self.write_shell(self.dev / 'test-run-preflight.sh', 'print preflight >> "$CONTRACT_LOG"\n')
        before = set(self.repo.rglob('*'))
        for entry, arguments in NO_BUILD_ENTRIES:
            for env, code in [({}, 17), ({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18)]:
                with self.subTest(entry=entry, code=code):
                    result = self.run_entry(entry, *arguments, **env)
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    self.assertIn('환경 미설정', result.stderr)
                    self.assertEqual(self.events(), [])
                    self.assertEqual(set(self.repo.rglob('*')), before)

    def test_agent_prompt_build_path_checks_output_before_sync(self):
        """프롬프트 검사의 기본 빌드 경로도 소스 동기화 전에 출력 미선택을 알린다."""
        local = self.make_root(output='YeeStable157')
        result = self.run_entry('test-agent-prompt.sh', YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(result.returncode, 18, result.stdout + result.stderr)
        self.assertEqual(self.events(), [])
        self.assertFalse((local / 'cache').exists())

    def test_launch_and_packaging_entries_stop_before_effects(self):
        """앱 실행·프로토타입·패키징은 빌드 생략을 포함해 두 가드를 가장 먼저 적용한다."""
        local = self.make_root(output='YeeStable157')
        before = set(self.repo.rglob('*'))
        for entry, arguments in RUN_ENTRIES:
            for env, code in [({}, 17), ({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18)]:
                with self.subTest(entry=entry, arguments=arguments, code=code):
                    result = self.run_entry(entry, *arguments, **env)
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    self.assertIn('환경 미설정', result.stderr)
                    self.assertEqual(self.events(), [])
                    self.assertEqual(set(self.repo.rglob('*')), before)

    def test_metal_and_display_helpers_check_selection_before_cache_or_tools(self):
        """Metal cache와 표시 helper의 직접·공통 호출도 선택되지 않은 루트에 쓰지 않는다."""
        local = self.make_root(output='YeeStable157')
        self.write_shell(self.bin / 'uname', 'print Darwin\n')
        checks = [('setup-metal.sh', None), ('ui-test-window-args.sh', None),
                  (None, 'prepare_ui_test_display')]
        before = set(self.repo.rglob('*'))
        for entry, body in checks:
            cases = [({}, 17)]
            if entry != 'setup-metal.sh':
                cases.append(({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18))
            for env, code in cases:
                with self.subTest(entry=entry, code=code):
                    result = self.run_entry(entry, **env) if entry else self.run_zsh(body, **env)
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    self.assertEqual(self.events(), [])
                    self.assertEqual(set(self.repo.rglob('*')), before)

    def test_grok_cohort_reuses_selection_guards_for_path_and_freshness(self):
        """cohort의 실제 경로 함수와 최신성 호출은 부가 의존성·앱 실행 없이 두 가드를 전파한다."""
        shutil.copy(ROOT / 'tools/dev/run-grok-scenario-cohort.py', self.dev)
        local = self.make_root(output='YeeStable157')
        binary = local / 'chromium/src/out/Chosen/Yee.app/Contents/MacOS/Yee'
        self.write_shell(binary, 'print unexpected-app-launch >> "$CONTRACT_LOG"\nexit 99\n')
        script = '''import ast
from pathlib import Path
import subprocess
import sys
from yee_build_paths import selected_yee_browser_bin
tree = ast.parse(Path('run-grok-scenario-cohort.py').read_text())
if sys.argv[1] == 'path':
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'yee_executable')
    namespace = {'selected_yee_browser_bin': selected_yee_browser_bin}
    exec(compile(ast.Module(body=[function], type_ignores=[]), '<cohort>', 'exec'), namespace)
    print(namespace['yee_executable']())
else:
    call = next(node for node in ast.walk(tree) if isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute) and node.func.attr == 'run'
                and any(isinstance(child, ast.Constant) and isinstance(child.value, str)
                        and 'require_integrated_yee_app_current' in child.value for child in ast.walk(node)))
    try:
        eval(compile(ast.Expression(body=call), '<cohort>', 'eval'),
             {'subprocess': subprocess, 'REPO': Path.cwd().parents[1]})
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
'''
        for mode in ('path', 'freshness'):
            for env, code in [({}, 17), ({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18),
                              ({'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'Chosen'}, 0)]:
                with self.subTest(mode=mode, code=code):
                    result = subprocess.run([sys.executable, '-c', script, mode], cwd=self.dev,
                                            env={**self.env, **env}, capture_output=True, text=True)
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    if mode == 'path' and code == 0:
                        self.assertEqual(result.stdout.strip(), str(binary))
        self.assertEqual(self.events(), [])

    def fake_shutdown_compiler(self):
        self.write_python(self.bin / 'xcrun', '''import os, sys
from pathlib import Path
assert sys.argv[1] == 'swiftc'
with open(os.environ['CONTRACT_LOG'], 'a') as stream:
    stream.write('swiftc\\n')
binary = Path(sys.argv[sys.argv.index('-o') + 1])
binary.write_text('#!/bin/zsh\\nprint -rl -- "$@" >> "$CONTRACT_LOG"\\n')
binary.chmod(0o755)
''')

    def test_shutdown_helper_builds_once_and_propagates_compiler_failure(self):
        """선택한 루트에서만 helper를 만들고 최신 파일은 재사용하며 swiftc 실패를 전달한다."""
        local = self.make_root(output='YeeStable157')
        env = {'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'YeeStable157'}
        self.fake_shutdown_compiler()
        for _ in range(2):
            result = self.run_zsh('prepare_yee_shutdown_helper', **env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.events(), ['swiftc'])
        helper = local / 'yee-shutdown-helper/gracefully-quit-yee'
        self.assertTrue(os.access(helper, os.X_OK))
        helper.chmod(0o644)
        result = self.run_zsh('prepare_yee_shutdown_helper', **env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.events(), ['swiftc', 'swiftc'])
        helper.unlink()
        self.write_shell(self.bin / 'xcrun', 'exit 73\n')
        result = self.run_zsh('prepare_yee_shutdown_helper', **env)
        self.assertEqual(result.returncode, 73, result.stdout + result.stderr)
        self.assertFalse(helper.exists())
        self.assertFalse((self.repo / '.local-build/yee-shutdown-helper').exists())

    def test_doctor_keeps_diagnosing_after_selection_hint(self):
        """진단 명령은 루트·출력 힌트를 출력한 뒤 도구 확인을 계속한다."""
        local = self.make_root(output='YeeStable157')
        for name, output in [('uname', 'Darwin'), ('sw_vers', 'fixture'),
                             ('xcode-select', '/fixture/Xcode'), ('xcodebuild', 'fixture'),
                             ('xcrun', '/fixture/SDK')]:
            self.write_shell(self.bin / name, f'print {output}\n')
        for env, hint in [({}, '후보 빌드 루트:'),
                          ({'YEE_LOCAL_BUILD_ROOT': str(local)}, '후보 출력:')]:
            result = self.run_entry('doctor.sh', **env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(hint, result.stderr)
            self.assertIn('진단은 계속한다', result.stdout)
            self.assertIn('macOS SDK:      /fixture/SDK', result.stdout)
        self.assertFalse((local / 'cache').exists())

    def test_python_path_helper_preserves_guards_and_selected_product_name(self):
        """Python 경로 헬퍼가 두 가드를 전파하고 공백이 든 제품 이름까지 공통 경로를 사용한다."""
        local = self.make_root(output='YeeStable157')
        (self.repo / 'tools/overlay/brand_config.py').write_text('print("Orbit Preview")\n')
        command = [sys.executable, '-c',
                   'from yee_build_paths import selected_yee_browser_bin; print(selected_yee_browser_bin())']
        for env, code in [({}, 17), ({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18),
                          ({'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'Chosen'}, 0)]:
            with self.subTest(code=code):
                result = subprocess.run(command, env={**self.env, **env}, cwd=self.dev,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                if not code:
                    self.assertEqual(result.stdout.strip(), str(local /
                        'chromium/src/out/Chosen/Orbit Preview.app/Contents/MacOS/Orbit Preview'))
                else:
                    self.assertIn('환경 미설정', result.stderr)
        self.assertEqual(self.events(), [])

    def test_python_yee_fixtures_check_selection_before_display_and_server(self):
        """실제 fixture 진입점은 경로 미선택이면 표시 helper·서버·프로필 생성 전에 중단한다."""
        local = self.make_root(output='YeeStable157')
        entries = [('test-procedural-content-blocking.py', []), ('test-baseline-filter-updates.py', []),
                   ('test-content-blocking-browser-fixture.py', ['--yee'])]
        before = set(self.repo.rglob('*'))
        for entry, arguments in entries:
            for env, code in [({}, 17), ({'YEE_LOCAL_BUILD_ROOT': str(local)}, 18)]:
                with self.subTest(entry=entry, code=code):
                    result = subprocess.run([sys.executable, str(self.dev / entry), *arguments],
                                            env={**self.env, **env}, cwd=self.repo,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                    self.assertIn('환경 미설정', result.stderr)
                    self.assertEqual(self.events(), [])
                    self.assertEqual(set(self.repo.rglob('*')), before)

    def test_explicit_output_allows_fake_generation_and_regression_build(self):
        """명시한 출력은 다른 graph가 있어도 허용하며 가짜 도구에 해당 이름만 전달한다."""
        local = self.make_root(output='YeeStable157')
        env = {'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'Chosen'}
        configured = self.run_entry('configure.sh', **env)
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        self.assertTrue((local / 'chromium/src/out/Chosen/build.ninja').is_file())
        built = self.run_zsh('build_regression_targets "selection" fixture', **env)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertIn('autoninja:-C out/Chosen -j 3 fixture', self.events())
        self.assertTrue((local / 'chromium/src/out/YeeStable157/build.ninja').is_file())

    def test_fresh_or_existing_default_output_keeps_legacy_behavior(self):
        """다른 graph가 없는 첫 설정과 이미 있는 기본 graph는 기존 선택을 유지한다."""
        local = self.make_root()
        configured = self.run_entry('configure.sh', YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
        other = local / 'chromium/src/out/Other/build.ninja'
        other.parent.mkdir()
        other.touch()
        built = self.run_zsh('build_regression_targets "selection" fixture', YEE_LOCAL_BUILD_ROOT=str(local))
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertIn('autoninja:-C out/YeePilot -j 3 fixture', self.events())

    @unittest.skipUnless(shutil.which('node') and sys.platform == 'darwin', 'macOS와 Node가 필요하다')
    def test_youtube_observer_uses_selected_app_and_fails_before_helpers(self):
        """실제 관찰기를 앱 실행 전에 실패시켜 선택한 앱 경로와 미설정 가드를 검증한다."""
        for name in ('observe-youtube-live.mjs', 'content_blocking_test_runtime.mjs',
                     'performance_trace.mjs', 'performance_cpu.mjs'):
            shutil.copy(ROOT / 'tools/dev' / name, self.dev / name)
        for name in ('renderer/content_blocking/youtube.js', 'renderer/content_blocking/generic_cosmetic.js',
                     'renderer/content_blocking/selector_validation.js', 'components/content_blocking/scriptlet_runtime.js'):
            source = self.repo / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('// 임시 관찰 입력\n')
        local = self.make_root(output='YeeStable157')
        (self.repo / 'tools/overlay/brand_config.py').write_text('print("Orbit Preview")\n')
        command = ['node', str(self.dev / 'observe-youtube-live.mjs'), 'perf', '10']
        missing_env = subprocess.run(command, env=self.env, cwd=self.repo, capture_output=True, text=True)
        self.assertNotEqual(missing_env.returncode, 0)
        self.assertIn('환경 미설정', missing_env.stderr)
        selected = subprocess.run(command, env={**self.env, 'YEE_LOCAL_BUILD_ROOT': str(local),
                                                'YEE_OUT_NAME': 'Chosen'},
                                  cwd=self.repo, capture_output=True, text=True)
        self.assertNotEqual(selected.returncode, 0)
        self.assertIn('Yee 앱 경로가 없다', selected.stderr)
        self.assertIn(str(local / 'chromium/src/out/Chosen/Orbit Preview.app'), selected.stderr)
        self.assertFalse((self.repo / '.local-build/youtube-review').exists())
        self.assertEqual(self.events(), [])
        app = local / 'chromium/src/out/Chosen/Orbit Preview.app'
        app.mkdir(parents=True)
        failed_helper = subprocess.run(command,
            env={**self.env, 'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'Chosen'},
            cwd=self.repo, capture_output=True, text=True)
        self.assertNotEqual(failed_helper.returncode, 0)
        self.assertIn('yee-live-shutdown-helper', failed_helper.stderr)
        self.assertFalse((self.repo / '.local-build/youtube-review').exists())
        self.assertEqual(self.events(), ['xcrun'])

    @unittest.skipUnless(shutil.which('node'), 'Node가 필요하다')
    def test_youtube_observer_fallback_uses_selected_helper_and_browser(self):
        """관찰기의 실제 경로 선택·종료 callback만 평가해 CDP 실패 시 선택한 helper 인수를 검사한다."""
        for name in ('observe-youtube-live.mjs', 'content_blocking_test_runtime.mjs'):
            shutil.copy(ROOT / 'tools/dev' / name, self.dev / name)
        local = self.make_root(output='YeeStable157')
        (self.repo / 'tools/overlay/brand_config.py').write_text('print("Orbit Preview")\n')
        helper = local / 'yee-shutdown-helper/gracefully-quit-yee'
        (local / 'chromium/src/out/Chosen/Orbit Preview.app').mkdir(parents=True)
        self.fake_shutdown_compiler()
        # 앱·HTTP 서버를 시작하는 최상위 코드는 실행하지 않고 실제 두 연결부를 가져온다.
        script = r'''
import assert from 'node:assert/strict';
import {execFile} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {promisify} from 'node:util';
import vm from 'node:vm';
const root = process.argv[1];
const {closeOwnedBrowser} = await import(pathToFileURL(path.join(root, 'tools/dev/content_blocking_test_runtime.mjs')));
const source = await fs.readFile(path.join(root, 'tools/dev/observe-youtube-live.mjs'), 'utf8');
const selectionStart = source.indexOf('const {stdout: yeePaths}');
const selectionEnd = source.indexOf('const chromeApp', selectionStart);
const callbackStart = source.indexOf('  const closeBrowser=');
const callbackEnd = source.indexOf('\n  try {', callbackStart);
assert.ok(selectionStart >= 0 && selectionEnd > selectionStart && callbackStart >= 0 && callbackEnd > callbackStart);
let selections = 0, fallbacks = 0;
const selected = await vm.runInNewContext(`(async () => {
  ${source.slice(selectionStart, selectionEnd)}
  const browser = {pid: 123, exitCode: null, signalCode: null}, app = yeeApp, pid = 123;
  const browserCDP = {async call() {throw Error('가짜 CDP 연결 실패');}, close() {}};
  ${source.slice(callbackStart, callbackEnd)}
  await closeBrowser();
  return [yeeApp, yeeBrowserBin, shutdown];
})()`, {root, path, execFile, fs,
  promisify: fn => async (...args) => {
    if (args[0] === 'zsh') ++selections;
    return promisify(fn)(...args);
  },
  closeOwnedBrowser: (browser, cdp, quit) => closeOwnedBrowser(browser, cdp, async () => {
    ++fallbacks; await quit(); browser.exitCode = 0;
  }, async () => {})
});
assert.equal(selections, 2, '공통 경로를 선택하고 실행 전에 종료 helper를 준비한다');
assert.equal(fallbacks, 1, 'CDP 실패 뒤 실제 관찰기 callback을 호출한다');
console.log(JSON.stringify(selected));
'''
        result = subprocess.run(['node', '--input-type=module', '-e', script, str(self.repo)],
                                env={**self.env, 'YEE_LOCAL_BUILD_ROOT': str(local), 'YEE_OUT_NAME': 'Chosen'},
                                cwd=self.repo, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        app = local / 'chromium/src/out/Chosen/Orbit Preview.app'
        binary = app / 'Contents/MacOS/Orbit Preview'
        self.assertEqual(json.loads(result.stdout), [str(app), str(binary), str(helper)])
        self.assertEqual(self.events(), ['swiftc', str(binary), '--pid=123'])
        self.assertFalse((self.repo / '.local-build/yee-shutdown-helper').exists())


if __name__ == '__main__':
    unittest.main()
