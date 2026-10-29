#!/usr/bin/env python3
"""가짜 GTest 목록과 실행 파일로 발견 검사와 셸의 실패 전달을 확인한다."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / 'tools/dev/lib/native_test_discovery.py'
SPEC = importlib.util.spec_from_file_location('native_test_discovery', CLI)
DISCOVERY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DISCOVERY)


class NativeTestDiscoveryTest(unittest.TestCase):
    def test_plain_typed_and_parameterized_listings_ignore_comments(self):
        """타입·값의 설명은 이름에 포함하지 않고 매개변수 접두·접미사는 유지한다."""
        listing = '''Running main() from test_main.cc
PlainTest.
  First
TypedTest/0.  # TypeParam = std::tuple<int, std::string>
  Works
All/ValueTest.
  Works/Vertical  # GetParam() = "vertical # value"
  Works/Horizontal  # GetParam() = "horizontal"
'''
        self.assertEqual(DISCOVERY.parse_test_listing(listing), {
            'PlainTest.First', 'TypedTest/0.Works',
            'All/ValueTest.Works/Vertical', 'All/ValueTest.Works/Horizontal'})
        report = DISCOVERY.check_test_listing(listing, 'PlainTest.*:TypedTest/*.Works:All/ValueTest.*')
        self.assertTrue(report['ok'])
        self.assertEqual(report['pattern_counts'], {
            'PlainTest.*': 1, 'TypedTest/*.Works': 1, 'All/ValueTest.*': 2})
        self.assertEqual(report['selected_tests'], 4)

    def test_logs_and_orphan_cases_are_not_tests(self):
        """스위트 없는 들여쓰기와 목록 사이의 일반 로그는 사례로 세지 않는다."""
        listing = '  Orphan\nNot a suite.\n  Orphan\nSuite.\n  First\n[INFO] log\n  Orphan\n'
        self.assertEqual(DISCOVERY.parse_test_listing(listing), {'Suite.First'})
        self.assertEqual(DISCOVERY.parse_test_listing('Suite.\n  First\nSuite.\n  First\n'),
                         {'Suite.First'})

    def test_empty_or_non_listing_output_is_rejected(self):
        """성공처럼 보이는 로그나 스위트 이름만 있어도 실제 사례가 없으면 실패한다."""
        for listing in ('', ' \n\n', 'Running tests: 0 passed\n', 'Suite.\n'):
            with self.subTest(listing=listing):
                report = DISCOVERY.check_test_listing(listing, '*')
                self.assertFalse(report['ok'])
                self.assertEqual(report['listed_tests'], 0)
                self.assertEqual(report['missing_patterns'], ['*'])

    def test_every_positive_pattern_must_match_an_active_case(self):
        """다른 패턴의 사례가 있어도 누락된 패턴 하나를 전체 성공으로 덮지 않는다."""
        report = DISCOVERY.check_test_listing('PresentTest.\n  Case\n', 'PresentTest.*:MissingTest.*')
        self.assertFalse(report['ok'])
        self.assertEqual(report['selected_tests'], 1)
        self.assertEqual(report['pattern_counts'], {'PresentTest.*': 1, 'MissingTest.*': 0})
        self.assertEqual(report['missing_patterns'], ['MissingTest.*'])

    def test_old_parameterized_header_filter_fails_for_plain_test_f(self):
        """실제 TEST_F에 잘못 붙인 All/ 접두사와 /* 접미사를 발견한다."""
        listing = ('FavoritesTest.\n  Capacity\nTabGroupHeaderViewTest.\n'
                   '  YeeVerticalHoverUpdatesSurfaceAndForegroundTogether\n')
        old = 'All/TabGroupHeaderViewTest.YeeVerticalHoverUpdatesSurfaceAndForegroundTogether/*'
        report = DISCOVERY.check_test_listing(listing, f'Favorites*:{old}')
        self.assertFalse(report['ok'])
        self.assertEqual(report['missing_patterns'], [old])
        fixed = DISCOVERY.check_test_listing(listing, 'Favorites*:TabGroupHeaderViewTest.Yee*')
        self.assertTrue(fixed['ok'])

    def test_negative_patterns_apply_before_each_positive_count(self):
        """음수 필터가 한 양수 패턴을 전부 없애면 그 패턴을 누락으로 보고한다."""
        listing = 'OneTest.\n  First\n  Second\nTwoTest.\n  First\n  Skip\n'
        report = DISCOVERY.check_test_listing(
            listing, 'OneTest.*:TwoTest.*-OneTest.*:TwoTest.Skip')
        self.assertFalse(report['ok'])
        self.assertEqual(report['pattern_counts'], {'OneTest.*': 0, 'TwoTest.*': 1})
        self.assertEqual(report['missing_patterns'], ['OneTest.*'])
        self.assertEqual(report['selected_tests'], 1)

    def test_negative_only_filter_uses_implicit_universal_positive(self):
        """양수 부분을 생략한 음수 필터는 GTest와 같이 *에서 제외한다."""
        listing = 'Suite.\n  First\n  Skip\n  Skip-Again\n'
        report = DISCOVERY.check_test_listing(listing, '-Suite.Skip:Suite.Skip-Again')
        self.assertTrue(report['ok'])
        self.assertEqual(report['pattern_counts'], {'*': 1})

    def test_empty_positive_patterns_do_not_become_wildcards(self):
        """빈 전체 필터와 콜론 사이의 빈 양수 패턴은 조용히 허용하지 않는다."""
        for gtest_filter in ('', ':Suite.*', 'Suite.*:', 'Suite.*::Suite.First'):
            with self.subTest(gtest_filter=gtest_filter):
                report = DISCOVERY.check_test_listing('Suite.\n  First\n', gtest_filter)
                self.assertFalse(report['ok'])
                self.assertIn('', report['missing_patterns'])

    def test_matching_is_case_sensitive_and_uses_only_gtest_wildcards(self):
        """Windows에서도 대소문자를 구분하고 대괄호는 GTest의 일반 문자로 취급한다."""
        listing = 'Suite.\n  CaseA\n  CaseB\nAll/Param.\n  Case/Vertical\n'
        report = DISCOVERY.check_test_listing(listing, 'Suite.Case?:All/*.*Vertical')
        self.assertTrue(report['ok'])
        self.assertEqual(report['pattern_counts'], {'Suite.Case?': 2, 'All/*.*Vertical': 1})
        self.assertFalse(DISCOVERY.check_test_listing(listing, 'suite.*')['ok'])
        self.assertFalse(DISCOVERY.check_test_listing(listing, 'Suite.Case[AB]')['ok'])
        self.assertTrue(DISCOVERY.matches_pattern('Suite.Case[AB]', 'Suite.Case[AB]'))

    def test_disabled_suites_and_cases_do_not_satisfy_patterns(self):
        """일반·타입·값 매개변수의 DISABLED_ 이름을 GTest 규칙대로 제외한다."""
        listing = '''DISABLED_Suite.
  Case
Prefix/DISABLED_Typed/0.  # TypeParam = int
  Case
Suite.
  DISABLED_Case
  disabled_lowercase
  NotDISABLED_Case
All/Param.
  DISABLED_Case/Vertical  # GetParam() = true
  Prefix/DISABLED_Case/0
  Active/Horizontal  # GetParam() = false
'''
        report = DISCOVERY.check_test_listing(listing, '*')
        self.assertTrue(report['ok'])
        self.assertEqual(report['listed_tests'], 8)
        self.assertEqual(report['active_tests'], 3)
        self.assertEqual(report['selected_tests'], 3)
        report = DISCOVERY.check_test_listing(listing, 'Suite.*:*/DISABLED_*')
        self.assertFalse(report['ok'])
        self.assertEqual(report['missing_patterns'], ['*/DISABLED_*'])

    def test_a_disabled_only_listing_is_rejected(self):
        """비활성 이름을 명시적으로 선택해도 활성 사례가 없으면 실패한다."""
        listing = 'Suite.\n  DISABLED_Case\nAll/DISABLED_Suite.\n  Case/0\n'
        for gtest_filter in ('*', 'Suite.DISABLED_Case', 'All/DISABLED_Suite.*'):
            with self.subTest(gtest_filter=gtest_filter):
                report = DISCOVERY.check_test_listing(listing, gtest_filter)
                self.assertFalse(report['ok'])
                self.assertEqual(report['active_tests'], 0)


class NativeTestDiscoveryCliTest(unittest.TestCase):
    def invoke(self, *arguments, listing=None):
        return subprocess.run([sys.executable, str(CLI), *arguments], input=listing,
                              capture_output=True, text=True, encoding='utf-8')

    def test_stdin_and_utf8_file_emit_the_same_json(self):
        """순수 CLI는 셸·네이티브 실행 없이 표준 입력과 Windows용 UTF-8 파일을 읽는다."""
        listing = 'Suite.\n  Case  # GetParam() = "한글 설명"\n'
        stdin = self.invoke('--filter=Suite.*', listing='\ufeff' + listing)
        self.assertEqual(stdin.returncode, 0, stdin.stderr)
        with tempfile.TemporaryDirectory(prefix='yee-discovery-cli-') as temporary:
            filename = Path(temporary) / '목록 with spaces.txt'
            filename.write_text(listing, encoding='utf-8-sig')
            saved = self.invoke(str(filename), '--filter=Suite.*')
        self.assertEqual(saved.returncode, 0, saved.stderr)
        self.assertEqual(json.loads(stdin.stdout), json.loads(saved.stdout))
        self.assertEqual(json.loads(saved.stdout)['pattern_counts'], {'Suite.*': 1})

    def test_cli_missing_pattern_returns_json_and_nonzero_status(self):
        """누락된 패턴은 JSON의 이름·개수와 실패 종료 코드로 전달한다."""
        result = self.invoke('-', '--filter=Suite.*:Absent.*', listing='Suite.\n  Case\n')
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['missing_patterns'], ['Absent.*'])
        self.assertEqual(report['pattern_counts']['Absent.*'], 0)

    def test_cli_file_read_failure_does_not_pass(self):
        """읽을 수 없는 목록 파일은 설명을 포함한 JSON과 별도 오류 코드로 보고한다."""
        with tempfile.TemporaryDirectory(prefix='yee-discovery-cli-') as temporary:
            result = self.invoke(str(Path(temporary) / 'missing.txt'), '--filter=*')
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads(result.stdout)
        self.assertFalse(report['ok'])
        self.assertIn('error', report)


@unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
class NativeTestGateShellTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-native-gate-')
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.log = self.directory / 'calls.txt'
        self.listing = self.directory / 'listing.txt'
        self.listing.write_text('Suite.\n  Case\n', encoding='utf-8')
        self.binary = self.directory / 'fake native test'
        self.binary.write_text('''#!/bin/zsh
set -euo pipefail
printf '%s\\n' call "$@" >> "$YEE_NATIVE_LOG"
for argument in "$@"; do
  if [[ "$argument" == --gtest_list_tests ]]; then
    cat "$YEE_NATIVE_LISTING"
    exit "$YEE_NATIVE_LIST_STATUS"
  fi
done
print executed >> "$YEE_NATIVE_LOG"
exit "$YEE_NATIVE_RUN_STATUS"
''', encoding='utf-8')
        self.binary.chmod(0o755)
        self.env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1',
                    'YEE_NATIVE_LOG': str(self.log), 'YEE_NATIVE_LISTING': str(self.listing),
                    'YEE_NATIVE_LIST_STATUS': '0', 'YEE_NATIVE_RUN_STATUS': '0'}

    def invoke(self, *arguments, **changes):
        script = '''set -euo pipefail
COMMON_DIR="$1"
source "$COMMON_DIR/lib/build.zsh"
shift
run_native_test_gate "$@"
'''
        return subprocess.run(['zsh', '-c', script, 'gate-test', str(ROOT / 'tools/dev'),
                               str(self.binary), *arguments], env={**self.env, **changes},
                              capture_output=True, text=True)

    def events(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_listing_and_execution_keep_all_flags(self):
        """특성·RTL·배율·창 위치와 실행기 인수는 목록과 실제 실행에 똑같이 전달한다."""
        arguments = ['--enable-features=TabStripUnification', '--gtest_filter=Suite.*',
                     '--force-ui-direction=rtl', '--force-device-scale-factor=1.25',
                     '--window-position=10,20', '--test-launcher-jobs=1',
                     '--test-launcher-retry-limit=0']
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.events(), ['call', *arguments, '--gtest_list_tests',
                                         'call', *arguments, 'executed'])

    def test_failed_listing_preserves_status_and_never_executes(self):
        """유효한 목록을 출력해도 바이너리의 목록 조회 실패를 덮지 않는다."""
        result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_LIST_STATUS='23')
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertNotIn('executed', self.events())
        self.assertEqual(self.events().count('call'), 1)

    def test_empty_missing_and_disabled_only_listings_never_execute(self):
        """발견 검사 실패는 pipefail 경로에서 실제 실행 전에 전파된다."""
        for listing, gtest_filter in (('', '*'), ('Suite.\n  Case\n', 'Suite.*:Absent.*'),
                                      ('Suite.\n  DISABLED_Case\n', 'Suite.*')):
            with self.subTest(listing=listing, gtest_filter=gtest_filter):
                self.log.unlink(missing_ok=True)
                self.listing.write_text(listing, encoding='utf-8')
                result = self.invoke(f'--gtest_filter={gtest_filter}')
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(json.loads(result.stdout)['ok'])
                self.assertNotIn('executed', self.events())

    def test_execution_failure_is_not_changed_after_discovery(self):
        """발견에 성공한 뒤에도 실제 실행의 실패 코드를 그대로 전달한다."""
        result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_RUN_STATUS='17')
        self.assertEqual(result.returncode, 17, result.stderr)
        self.assertIn('executed', self.events())

    def test_missing_or_duplicate_explicit_filter_fails_before_listing(self):
        """명시한 필터가 없거나 둘이면 모호한 실행을 시작하지 않는다."""
        for arguments in ((), ('--gtest_filter=Suite.*', '--gtest_filter=Other.*')):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(self.events(), [])


if __name__ == '__main__':
    unittest.main()
