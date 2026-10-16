#!/usr/bin/env python3
"""Verify script contracts using fake tools. No compiler or browser is invoked."""

import fnmatch
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('zsh'), 'requires zsh')
class DevelopmentToolsTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='yee-development-structure-')
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name).resolve() / 'product with spaces'
        self.dev = self.repo / 'tools/dev'
        self.dev.mkdir(parents=True)
        shutil.copytree(ROOT / 'tools/dev/lib', self.dev / 'lib')
        for name in ('common.zsh', 'build.sh', 'build-ui.sh',
                     'test-header.sh', 'test-sidebar.sh',
                     'browser_bundle_executables.py'):
            shutil.copy(ROOT / 'tools/dev' / name, self.dev / name)
        self.overlay = self.repo / 'tools/overlay'
        self.overlay.mkdir(parents=True)
        shutil.copy(ROOT / 'tools/overlay/brand_config.py', self.overlay)
        (self.repo / 'logo.png').write_bytes(b'asset fixture; never rendered')
        (self.repo / 'branding').mkdir()
        (self.repo / 'branding/brand.json').write_text(json.dumps({
            'name': 'Orbit Preview', 'internal_url_scheme': 'orbit',
            'logo_source': 'logo.png', 'logo_crop_size': 820}))
        self.local = self.repo / 'local build'
        self.src = self.local / 'chromium/src'
        self.out = self.src / 'out/YeePilot'
        self.out.mkdir(parents=True)
        (self.src / 'BUILD.gn').touch()
        (self.src / 'chrome/browser/ui/views/yee').mkdir(parents=True)
        (self.src / 'chrome/browser/ui/views/yee/BUILD.gn').touch()
        self.log = self.repo / 'events.log'
        self.bin = self.repo / 'fake tools'
        self.bin.mkdir()
        self.env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1',
                    'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
                    'YEE_LOCAL_BUILD_ROOT': str(self.local), 'YEE_BUILD_JOBS': '3',
                    'YEE_TEST_LOG': str(self.log), 'YEE_TEST_OUT': str(self.out)}
        self.write_tool(self.local / 'depot_tools/gclient', 'exit 0\n')
        self.write_tool(self.bin / 'df', 'printf "Filesystem 1024-blocks Used Available Capacity Mounted\\nfixture 200000000 1000000 100000000 1%% /\\n"\n')
        self.write_tool(self.bin / 'caffeinate', 'shift\nexec "$@"\n')
        self.write_tool(self.bin / 'nice', 'shift 2\nexec "$@"\n')
        self.write_tool(self.bin / 'autoninja', 'printf "target:%s\\ncwd:%s\\n" "$*" "$PWD" >> "$YEE_TEST_LOG"\n')
        self.write_tool(self.bin / 'metal', 'exit 0\n')
        self.env['YEE_METAL_BIN'] = str(self.bin / 'metal')
        for name, label in [('install-branding.sh', 'branding'),
                            ('install-brand-assets.sh', 'assets'),
                            ('install-yee-ui-sources.sh', 'sources'), ('apply.sh', 'apply')]:
            self.write_tool(self.overlay / name, f'printf "%s\\n" "{label}" >> "$YEE_TEST_LOG"\n')
        self.write_tool(self.dev / 'configure.sh', 'printf "configure\\n" >> "$YEE_TEST_LOG"\ntouch "$YEE_TEST_OUT/build.ninja"\n')
        self.write_tool(self.dev / 'usage.sh', 'printf "usage\\n" >> "$YEE_TEST_LOG"\n')

    def write_tool(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/bin/zsh\nset -euo pipefail\n' + text)
        path.chmod(0o755)

    def run_script(self, name, *arguments, **changes):
        return subprocess.run(['zsh', str(self.dev / name), *arguments], env={**self.env, **changes},
                              cwd=self.repo.parent, capture_output=True, text=True)

    def events(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_ui_build_prepares_once_and_uses_shared_cache(self):
        result = self.run_script('build-ui.sh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.events(), ['branding', 'assets', 'sources', 'configure',
                         'target:-C out/YeePilot -j 3 chrome/browser/ui/views/yee:yee_ui',
                         'cwd:' + str(self.src)])
        for path in ('clang/ModuleCache', 'go-build', 'go-mod', 'cargo', 'npm', 'pip'):
            self.assertTrue((self.local / 'cache' / path).is_dir())

    def test_integrated_build_reuses_existing_configuration(self):
        (self.out / 'build.ninja').touch()
        result = self.run_script('build.sh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.events(), ['branding', 'assets', 'sources',
                         'target:-C out/YeePilot -j 3 chrome', 'cwd:' + str(self.src), 'usage'])

    def test_invalid_jobs_fail_before_input_or_build_mutations(self):
        for jobs in ('0', '-1', 'invalid'):
            for name in ('build.sh', 'build-ui.sh'):
                with self.subTest(jobs=jobs, name=name):
                    result = self.run_script(name, YEE_BUILD_JOBS=jobs)
                    self.assertEqual(result.returncode, 14, result.stderr)
                    self.assertEqual(self.events(), [])
                    self.assertFalse((self.local / 'cache').exists())

    def test_fresh_overlay_uses_apply_entry_point(self):
        (self.src / 'chrome/browser/ui/views/yee/BUILD.gn').unlink()
        result = self.run_script('build-ui.sh')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.events()[:4], ['branding', 'assets', 'apply', 'configure'])

    def test_runtime_paths_and_regression_cache_alias_survive_library_move(self):
        script = ('source "$1"; configure_regression_build_cache; '
                  'print -rl -- "$COMMON_DIR" "$YEE_ROOT" "$YEE_BROWSER_BIN" "$GOCACHE"')
        result = subprocess.run(['zsh', '-c', script, 'test', str(self.dev / 'common.zsh')],
                                env=self.env, capture_output=True, text=True, cwd=self.repo.parent)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [str(self.dev), str(self.repo),
                         str(self.out / 'Orbit Preview.app/Contents/MacOS/Orbit Preview'),
                         str(self.local / 'cache/go-build')])
        self.assertEqual(self.events(), [])

    def test_native_gates_require_explicit_interaction_and_do_not_repeat_header_cases(self):
        """기본 실행은 단위 검사만, 실제 입력과 Header 중복은 명시한 범위만 실행한다."""
        self.write_tool(self.dev / 'test-run-preflight.sh', 'exit 0\n')
        for binary in ('yee_layout_unittests', 'yee_header_unittests', 'interactive_ui_tests'):
            self.write_tool(self.out / binary,
                            'printf "run:%s\\n" "${0:t}" >> "$YEE_TEST_LOG"\n'
                            'printf "arg:%s\\n" "$@" >> "$YEE_TEST_LOG"\n')
        filters = {}
        for name, unit_binary, interactive_runs in (
                ('test-header.sh', 'yee_header_unittests', 2),
                ('test-sidebar.sh', 'yee_layout_unittests', 1)):
            for arguments, expected in (
                    ((), [unit_binary]),
                    (('--no-build',), [unit_binary]),
                    (('unit', '--no-build'), [unit_binary]),
                    (('interactive', '--no-build'), ['interactive_ui_tests'] * interactive_runs),
                    (('all', '--no-build'), [unit_binary] + ['interactive_ui_tests'] * interactive_runs)):
                with self.subTest(name=name, arguments=arguments):
                    self.log.unlink(missing_ok=True)
                    result = self.run_script(name, *arguments)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual([event.removeprefix('run:') for event in self.events()
                                      if event.startswith('run:')], expected)
                    if arguments == ('interactive', '--no-build'):
                        filters[name] = next(event.removeprefix('arg:--gtest_filter=')
                                             for event in self.events()
                                             if event.startswith('arg:--gtest_filter='))

        def selected(name, case):
            return any(fnmatch.fnmatchcase(case, pattern)
                       for pattern in filters[name].split(':'))

        for layout in (0, 1):
            for contract in ('YeeSingleAndSplitHeadersShareLocationBarGeometry',
                             'YeeRenderedBoundaryTracksDelayedCssWithoutMetadata',
                             'YeePresentationFollowsSingleAndSplitSources',
                             'YeeInactivePaneHeaderActivatesAddressEditing',
                             'YeeOmniboxPopupFollowsSingleAndSplitHeader'):
                case = f'SplitTabLayout/MultiContentsViewUiTest.{contract}/{layout}'
                self.assertTrue(selected('test-header.sh', case), case)
                self.assertFalse(selected('test-sidebar.sh', case), case)
            for contract in ('YeeSidebarControlsPersistAcrossSplitAndCollapse',
                             'YeeSplitCommandsUpdateChromiumModel'):
                case = f'SplitTabLayout/MultiContentsViewUiTest.{contract}/{layout}'
                self.assertTrue(selected('test-sidebar.sh', case), case)


if __name__ == '__main__':
    unittest.main()
