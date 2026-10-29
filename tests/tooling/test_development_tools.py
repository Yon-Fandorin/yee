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
                     'test-header.sh', 'test-sidebar.sh', 'test-browser-surface-layout.sh',
                     'browser_bundle_executables.py', 'ui-test-display.swift'):
            shutil.copy(ROOT / 'tools/dev' / name, self.dev / name)
        self.overlay = self.repo / 'tools/overlay'
        self.overlay.mkdir(parents=True)
        shutil.copy(ROOT / 'tools/overlay/brand_config.py', self.overlay)
        (self.repo / 'logo.png').write_bytes(b'asset fixture; never rendered')
        (self.repo / 'branding').mkdir()
        (self.repo / 'branding/brand.json').write_text(json.dumps({
            'name': 'Orbit Preview', 'internal_url_scheme': 'orbit',
            'logo_source': 'logo.png', 'logo_crop_size': 820}))
        shutil.copy(ROOT / 'tools/dev/chromium-version.py', self.dev)
        shutil.copy(ROOT / 'tools/overlay/product_version.py', self.overlay)
        shutil.copy(ROOT / 'branding/VERSION', self.repo / 'branding/VERSION')
        self.local = self.repo / 'local build'
        self.src = self.local / 'chromium/src'
        self.out = self.src / 'out/YeePilot'
        self.out.mkdir(parents=True)
        (self.src / 'BUILD.gn').touch()
        (self.src / 'chrome').mkdir(exist_ok=True)
        manifest = json.loads((ROOT / 'build/chromium-version.json').read_text())
        (self.src / 'chrome/VERSION').write_text(''.join(f'{key}={part}\n' for key,part in
            zip(('MAJOR','MINOR','BUILD','PATCH'),manifest['version'].split('.'))))
        for arguments in (('init',), ('add', 'BUILD.gn', 'chrome/VERSION'),
                          ('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture')):
            subprocess.run(['git', '-C', str(self.src), *arguments], check=True, capture_output=True)
        revision = subprocess.check_output(['git', '-C', str(self.src), 'rev-parse', 'HEAD'], text=True).strip()
        manifest['revision'] = revision
        (self.repo / 'build').mkdir()
        (self.repo / 'build/chromium-version.json').write_text(json.dumps(manifest))
        (self.src / 'chrome/browser/ui/views/yee').mkdir(parents=True)
        (self.src / 'chrome/browser/ui/views/yee/BUILD.gn').touch()
        self.log = self.repo / 'events.log'
        self.bin = self.repo / 'fake tools'
        self.bin.mkdir()
        self.env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1',
                    'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
                    'YEE_LOCAL_BUILD_ROOT': str(self.local), 'YEE_BUILD_JOBS': '3',
                    'YEE_OUT_NAME': 'YeePilot',
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

    def test_candidate_output_is_one_directory_and_preserves_the_old_output(self):
        """후보 출력은 기존 빌드와 분리하며 경로 이탈 이름은 변경 전에 거부한다."""
        script = 'source "$1"; print -r -- "$YEE_OUT_DIR"'
        result = subprocess.run(['zsh', '-c', script, 'test', str(self.dev/'common.zsh')],
                                env={**self.env,'YEE_OUT_NAME':'YeeStable157'}, capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.strip(),str(self.src/'out/YeeStable157'))
        self.assertTrue(self.out.is_dir())
        for name in ('..','../elsewhere','folder/name'):
            result = subprocess.run(['zsh','-c',script,'test',str(self.dev/'common.zsh')],
                                    env={**self.env,'YEE_OUT_NAME':name},capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.events(),[])

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
        self.write_tool(self.bin / 'uname', 'print Darwin\n')
        self.write_tool(self.bin / 'xcrun',
                        'print "display-probe" >> "$YEE_TEST_LOG"\n'
                        'print -r -- --window-position=1776,-1395\n')
        for binary in ('yee_layout_unittests', 'yee_header_unittests', 'interactive_ui_tests'):
            self.write_tool(self.out / binary,
                            'listing=false\nfilter=""\n'
                            'for argument in "$@"; do\n'
                            '  [[ "$argument" == --gtest_list_tests ]] && listing=true\n'
                            '  if [[ "$argument" == --gtest_filter=* ]]; then\n'
                            '    filter="${argument#--gtest_filter=}"\n'
                            '  fi\n'
                            'done\n'
                            'if [[ "$listing" == true ]]; then\n'
                            '  for pattern in "${(@s.:.)filter}"; do\n'
                            '    pattern="${pattern//\\*/Fixture}"\n'
                            '    pattern="${pattern//\\?/X}"\n'
                            '    [[ "$pattern" == *.* ]] || pattern+=".Fixture"\n'
                            '    print -rl -- "${pattern%%.*}." "  ${pattern#*.}"\n'
                            '  done\n'
                            '  exit 0\n'
                            'fi\n'
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
                    visible_runs = expected.count('interactive_ui_tests')
                    self.assertEqual(self.events().count('display-probe'), visible_runs)
                    self.assertEqual(self.events().count('arg:--window-position=1776,-1395'),
                                     visible_runs)
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

    def test_layout_gates_require_only_cases_compiled_for_the_host(self):
        """다른 OS에서 빠지는 사례는 만들어 주지 않아 필수 필터의 잘못된 혼합을 잡는다."""
        self.write_tool(self.dev / 'test-run-preflight.sh', 'exit 0\n')
        self.write_tool(self.bin / 'uname', 'print -r -- "$YEE_TEST_PLATFORM"\n')
        self.write_tool(self.bin / 'xcrun', 'exit 0\n')
        self.write_tool(self.bin / 'pgrep', 'exit 1\n')
        for binary in ('yee_layout_unittests', 'multi_contents_geometry_unittests',
                       'interactive_ui_tests', 'browser_tests'):
            self.write_tool(self.out / binary, '''listing=false
filter=""
for argument in "$@"; do
  [[ "$argument" == --gtest_list_tests ]] && listing=true
  [[ "$argument" == --gtest_filter=* ]] && filter="${argument#--gtest_filter=}"
done
if [[ "$listing" == true ]]; then
  for pattern in "${(@s.:.)filter}"; do
    # native 소스의 IS_WIN 및 is_mac 컴파일 경계와 같은 목록의 부재를 흉내 낸다.
    if [[ "$YEE_TEST_PLATFORM" == Darwin &&
          ( "$pattern" == *YeeWindows* ||
            "$pattern" == CustomizeChromeInteractiveTest.YeeTabLayoutSelectPopupFollowsItsHost ||
            "$pattern" == TabDragging/DetachToBrowserTabDragControllerTest.YeeGroupMembershipDragSurvivesLayoutSwitch/0 ||
            "$pattern" == VerticalTabStripRegionViewInteractiveUiTest.YeeFooterPointerExitKeepsPopupAndEscapeRestoresFocus ) ]]; then
      continue
    fi
    if [[ "$YEE_TEST_PLATFORM" != Darwin &&
          ( "$pattern" == ImmersiveModeControllerMac* ||
            "$pattern" == BrowserNativeWidgetMacGlassTest.* ||
            "$pattern" == OmniboxPopupViewWebUITest.MultiWindowActivationRestartsAutocompleteWithoutStaleState ||
            "$pattern" == BrowserViewTabbedLayoutImplRtlFractionalDsfUiTest.YeeFindBarUsesPixelStableBoundsAtMinimumWidth ) ]]; then
      continue
    fi
    pattern="${pattern//\\*/Fixture}"
    pattern="${pattern//\\?/X}"
    [[ "$pattern" == *.* ]] || pattern+=".Fixture"
    print -rl -- "${pattern%%.*}." "  ${pattern#*.}"
  done
  exit 0
fi
printf 'run:%s\\n' "${0:t}" >> "$YEE_TEST_LOG"
printf 'arg:%s\\n' "$@" >> "$YEE_TEST_LOG"
''')
        for platform in ('Darwin', 'MSYS_NT-10.0'):
            for name, mode, expected_runs in (
                    ('test-browser-surface-layout.sh', 'horizontal', 3),
                    ('test-browser-surface-layout.sh', 'interactive', 3),
                    ('test-browser-surface-layout.sh', 'all', 6),
                    ('test-sidebar.sh', 'interactive', 1),
                    ('test-sidebar.sh', 'all', 2)):
                with self.subTest(platform=platform, script=name, mode=mode):
                    self.log.unlink(missing_ok=True)
                    result = self.run_script(name, mode, '--no-build',
                                             YEE_TEST_PLATFORM=platform)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    filters = [event.removeprefix('arg:--gtest_filter=')
                               for event in self.events()
                               if event.startswith('arg:--gtest_filter=')]
                    self.assertEqual(len(filters), expected_runs)
                    combined = ':'.join(filters)
                    if platform == 'Darwin':
                        self.assertNotIn('YeeWindows', combined)
                        self.assertNotIn('YeeTabLayoutSelectPopupFollowsItsHost', combined)
                        self.assertNotIn('YeeGroupMembershipDragSurvivesLayoutSwitch', combined)
                        self.assertNotIn('YeeFooterPointerExitKeepsPopupAndEscapeRestoresFocus',
                                         combined)
                        if name == 'test-browser-surface-layout.sh' and mode != 'horizontal':
                            self.assertIn('BrowserNativeWidgetMacGlassTest.', combined)
                            self.assertIn('MultiWindowActivationRestartsAutocompleteWithoutStaleState',
                                          combined)
                            self.assertIn('YeeFindBarUsesPixelStableBoundsAtMinimumWidth', combined)
                    else:
                        self.assertNotIn('BrowserNativeWidgetMacGlassTest.', combined)
                        self.assertNotIn('ImmersiveModeControllerMac', combined)
                        self.assertNotIn('MultiWindowActivationRestartsAutocompleteWithoutStaleState',
                                         combined)
                        self.assertNotIn('YeeFindBarUsesPixelStableBoundsAtMinimumWidth', combined)
                        if name == 'test-sidebar.sh':
                            self.assertIn('YeeFooterPointerExitKeepsPopupAndEscapeRestoresFocus',
                                          combined)
                            continue
                        self.assertIn('YeeWindowsHorizontalTabsConnectPageSurfaceToHeader',
                                      combined)
                        self.assertIn('YeeGroupMembershipDragSurvivesLayoutSwitch', combined)
                        if mode != 'horizontal':
                            self.assertIn('YeeWindowsNativeClipSurvivesRendererAttachment',
                                          combined)
                            self.assertIn('YeeTabLayoutSelectPopupFollowsItsHost', combined)


if __name__ == '__main__':
    unittest.main()
