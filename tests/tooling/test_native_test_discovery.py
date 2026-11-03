#!/usr/bin/env python3
"""가짜 GTest 목록과 실행 파일로 발견 검사와 셸의 실패 전달을 확인한다."""

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
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
        self.directory = Path(temporary.name).resolve()
        self.log = self.directory / 'calls.txt'
        self.listing = self.directory / 'listing.txt'
        self.listing.write_text('Suite.\n  Case\n', encoding='utf-8')
        self.build_root = self.directory / 'local build'
        self.out = self.build_root / 'chromium/src/out/Candidate'
        self.out.mkdir(parents=True)
        self.binary = self.out / 'fake native test'
        self.binary.write_text(f'#!{sys.executable}\n' + '''import os
from pathlib import Path
import sys

arguments = sys.argv[1:]
listing = '--gtest_list_tests' in arguments
with Path(os.environ['YEE_NATIVE_LOG']).open('a') as stream:
    stream.write('call\\n' + ''.join(argument + '\\n' for argument in arguments))
    if not listing:
        stream.write('executed\\n')
output = None
for index, argument in enumerate(arguments):
    if argument.startswith('--gtest_output='):
        output = argument.partition('=')[2]
    elif argument == '--gtest_output':
        output = arguments[index + 1]
if output and (not listing or os.environ.get('YEE_NATIVE_LIST_XML') == '1'):
    if output.startswith('xml') and (listing or os.environ['YEE_NATIVE_XML_MODE'] != 'missing'):
        _, separator, destination = output.partition(':')
        xml = Path(destination if separator else 'test_detail.xml')
        if separator and (not destination or destination.endswith('/')):
            xml = xml / (Path(sys.argv[0]).name + '_1.xml')
        xml.parent.mkdir(parents=True, exist_ok=True)
        xml.write_text(os.environ['YEE_NATIVE_XML'])
if listing and os.environ.get('YEE_NATIVE_SIGINT_PHASE') != 'discovery':
    print(Path(os.environ['YEE_NATIVE_LISTING']).read_text(), end='')
    sys.exit(int(os.environ['YEE_NATIVE_LIST_STATUS']))
if os.environ.get('YEE_NATIVE_WAIT_FOR_SIGINT') == '1':
    import signal
    import subprocess
    import time
    directory = Path(os.environ['YEE_NATIVE_BUILD_ROOT'])
    assert signal.getsignal(signal.SIGINT) != signal.SIG_IGN
    worker = subprocess.Popen([sys.executable, '-c', ''' + repr('''import signal
import sys
import time
from pathlib import Path
directory = Path(sys.argv[1])
def finish(signum, frame):
    time.sleep(0.4)
    (directory / 'worker-cleaned').touch()
    sys.exit(0)
signal.signal(signal.SIGTERM, finish)
(directory / 'worker-ready').touch()
while True:
    time.sleep(0.01)
''') + ''', str(directory)], start_new_session=True)
    def finish(signum, frame):
        os.killpg(worker.pid, signal.SIGTERM)
        try:
            worker.wait(timeout=0.5)
        except subprocess.TimeoutExpired:
            os.killpg(worker.pid, signal.SIGKILL)
            worker.wait()
        time.sleep(0.2)
        (directory / 'launcher-cleaned').touch()
        if os.environ['YEE_NATIVE_RUN_STATUS'] == 'signal':
            signal.signal(signal.SIGINT, signal.SIG_DFL)
            os.kill(os.getpid(), signal.SIGINT)
        sys.exit(int(os.environ['YEE_NATIVE_RUN_STATUS']))
    signal.signal(signal.SIGINT, finish)
    while not (directory / 'worker-ready').exists():
        time.sleep(0.01)
    (directory / 'launcher-ready').touch()
    while True:
        time.sleep(0.01)
sys.exit(int(os.environ['YEE_NATIVE_RUN_STATUS']))
''', encoding='utf-8')
        self.binary.chmod(0o755)
        (self.directory / '.gitignore').write_text('local build/\ncalls.txt\nlisting.txt\n')
        (self.directory / 'tracked.txt').write_text('기준 소스\n')
        for arguments in (('init',), ('add', '.gitignore', 'tracked.txt'),
                          ('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                           'commit', '-qm', 'fixture')):
            subprocess.run(['git', '-C', str(self.directory), *arguments], check=True, capture_output=True)
        self.env = {**{key: value for key, value in os.environ.items() if not key.startswith('YEE_')},
                    'PYTHONDONTWRITEBYTECODE': '1', 'YEE_NATIVE_REPO': str(self.directory),
                    'YEE_NATIVE_BUILD_ROOT': str(self.build_root),
                    'YEE_NATIVE_LOG': str(self.log), 'YEE_NATIVE_LISTING': str(self.listing),
                    'YEE_NATIVE_LIST_STATUS': '0', 'YEE_NATIVE_RUN_STATUS': '0',
                    'YEE_NATIVE_XML_MODE': 'write',
                    'YEE_NATIVE_XML': '<testsuites tests="1" failures="0" errors="0">'
                                      '<testsuite tests="1" failures="0" errors="0"/></testsuites>'}

    def gate_command(self, *arguments):
        script = '''set -euo pipefail
COMMON_DIR="$1"
YEE_ROOT="$YEE_NATIVE_REPO"
LOCAL_BUILD_ROOT="$YEE_NATIVE_BUILD_ROOT"
CHROMIUM_SRC="$LOCAL_BUILD_ROOT/chromium/src"
DEPOT_TOOLS_DIR="$LOCAL_BUILD_ROOT/depot_tools"
YEE_OUT_NAME=Candidate
YEE_OUT_DIR="$CHROMIUM_SRC/out/$YEE_OUT_NAME"
YEE_LOCAL_BUILD_ROOT_EXPLICIT="${YEE_NATIVE_ROOT_EXPLICIT:-true}"
YEE_OUT_NAME_EXPLICIT="${YEE_NATIVE_OUT_EXPLICIT:-true}"
source "$COMMON_DIR/lib/preflight.zsh"
source "$COMMON_DIR/lib/build.zsh"
shift
run_native_test_gate "$@"
'''
        return ['zsh', '-c', script, 'gate-test', str(ROOT / 'tools/dev'), str(self.binary), *arguments]

    def invoke(self, *arguments, **changes):
        return subprocess.run(self.gate_command(*arguments), env={**self.env, **changes},
                              cwd=self.directory, capture_output=True, text=True)

    def events(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def records(self):
        return [json.loads(path.read_text()) for path in sorted(
            (self.build_root / 'logs/native-gates').glob('*/record.json'))]

    def test_listing_and_execution_keep_all_flags(self):
        """특성·RTL·배율·창 위치와 실행기 인수는 목록과 실제 실행에 똑같이 전달한다."""
        arguments = ['--enable-features=TabStripUnification', '--gtest_filter=Suite.*',
                     '--force-ui-direction=rtl', '--force-device-scale-factor=1.25',
                     '--window-position=10,20', '--test-launcher-jobs=1',
                     '--test-launcher-retry-limit=0']
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.records()[0]
        output = '--gtest_output=xml:' + record['xml']['path']
        self.assertEqual(self.events(), ['call', *arguments, output, '--gtest_list_tests',
                                         'call', *arguments, output, 'executed'])

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

    def test_record_contains_source_binary_selection_arguments_and_xml(self):
        """시작·종료, staged·unstaged diff와 미추적 경로·내용, 선택·실행 결과를 함께 기록한다."""
        source = self.directory / 'tracked.txt'
        source.write_text('인덱스 변경\n')
        subprocess.run(['git', '-C', str(self.directory), 'add', 'tracked.txt'], check=True)
        source.write_text('작업 트리 변경\n')
        untracked = self.directory / '미추적 파일.txt'
        untracked.write_text('내용도 지문화한다.\n')
        def git(*args):
            return subprocess.check_output(['git', '-C', str(self.directory), *args])
        expected_head = git('rev-parse', 'HEAD').decode().strip()
        expected_diff = hashlib.sha256(git('diff', 'HEAD', '--binary')).hexdigest()
        expected_untracked = hashlib.sha256(git('ls-files', '--others', '--exclude-standard', '-z')).hexdigest()
        expected_contents = hashlib.sha256(os.fsencode(untracked.name) + b'\0' +
                                           hashlib.sha256(untracked.read_bytes()).digest()).hexdigest()
        result = self.invoke('--gtest_filter=Suite.*', '--test-launcher-jobs=1')
        self.assertEqual(result.returncode, 0, result.stderr)
        record, = self.records()
        self.assertTrue(record['started_at_utc'].endswith('Z'))
        self.assertTrue(record['ended_at_utc'].endswith('Z'))
        self.assertLessEqual(record['started_at_utc'], record['ended_at_utc'])
        self.assertEqual(record['source'], {'head': expected_head,
            'diff_head_binary_sha256': expected_diff, 'untracked_paths_sha256': expected_untracked,
            'untracked_contents_sha256': expected_contents,
            'untracked_paths': ['미추적 파일.txt']})
        self.assertEqual(record['build_root'], str(self.build_root))
        self.assertEqual(record['out_dir'], str(self.out))
        self.assertEqual(record['out_name'], 'Candidate')
        self.assertTrue(record['build_root_explicit'])
        self.assertTrue(record['out_name_explicit'])
        self.assertEqual(record['binary']['path'], str(self.binary))
        self.assertEqual(record['binary']['size_bytes'], self.binary.stat().st_size)
        self.assertEqual(record['binary']['mtime_ns'], self.binary.stat().st_mtime_ns)
        self.assertEqual(record['binary']['sha256'], hashlib.sha256(self.binary.read_bytes()).hexdigest())
        self.assertEqual(record['gtest_filter'], 'Suite.*')
        self.assertEqual(record['discovery']['selected_tests'], 1)
        self.assertEqual(record['xml']['tests'], 1)
        self.assertEqual(record['exit_code'], 0)
        self.assertEqual(record['gate_exit_code'], 0)
        self.assertEqual(self.events()[1:4], record['arguments'])

    def test_untracked_contents_change_without_path_or_size_changes(self):
        """정렬된 경로·내용 지문은 같은 이름·크기의 미추적 파일 수정도 구분한다."""
        sources = {'z.txt': b'zz', 'a.txt': b'aa'}
        for name, contents in sources.items():
            (self.directory / name).write_bytes(contents)
        for _ in range(2):
            result = self.invoke('--gtest_filter=Suite.*')
            self.assertEqual(result.returncode, 0, result.stderr)
        first, second = (record['source'] for record in self.records())
        expected = hashlib.sha256(b''.join(os.fsencode(name) + b'\0' + hashlib.sha256(sources[name]).digest()
                                           for name in sorted(sources))).hexdigest()
        self.assertEqual(first['untracked_contents_sha256'], expected)
        self.assertEqual(first, second)
        (self.directory / 'a.txt').write_bytes(b'bb')
        result = self.invoke('--gtest_filter=Suite.*')
        self.assertEqual(result.returncode, 0, result.stderr)
        changed = self.records()[-1]['source']
        self.assertEqual(first['untracked_paths_sha256'], changed['untracked_paths_sha256'])
        self.assertNotEqual(first['untracked_contents_sha256'], changed['untracked_contents_sha256'])

    @unittest.skipUnless(hasattr(os, 'killpg'), 'POSIX 프로세스 그룹 신호가 필요하다')
    def test_sigint_waits_for_launcher_cleanup_and_never_records_success(self):
        """Ctrl-C 뒤 500ms 정리와 후속 기록을 기다리고 실제 자식 코드·인터럽트를 남긴다."""
        for phase, child_status, exit_status, gate_status in [
                ('execution', '0', 0, 130), ('execution', '37', 37, 37),
                ('execution', 'signal', 130, 130), ('discovery', '0', 0, 130)]:
            with self.subTest(phase=phase, child_status=child_status):
                for name in ('worker-ready', 'launcher-ready', 'worker-cleaned', 'launcher-cleaned'):
                    (self.build_root / name).unlink(missing_ok=True)
                command = [sys.executable, str(ROOT / 'tools/dev/lib/native_gate_record.py'),
                           '--repo-root', str(self.directory), '--build-root', str(self.build_root),
                           '--out-dir', str(self.out), '--out-name', 'Candidate',
                           '--root-explicit', 'true', '--out-explicit', 'true', '--pid', str(os.getpid()),
                           '--', str(self.binary), '--gtest_filter=Suite.*']
                process = subprocess.Popen(command,
                    env={**self.env, 'YEE_NATIVE_WAIT_FOR_SIGINT': '1', 'YEE_NATIVE_RUN_STATUS': child_status,
                         'YEE_NATIVE_SIGINT_PHASE': phase},
                    cwd=self.directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                    start_new_session=True)
                try:
                    deadline = time.monotonic() + 10
                    while not (self.build_root / 'launcher-ready').exists():
                        if process.poll() is not None or time.monotonic() >= deadline:
                            self.fail('가짜 검사 실행기가 SIGINT를 기다리는 상태가 되지 않았다.')
                        time.sleep(0.01)
                    self.assertEqual(self.records()[-1]['gate_exit_code'], 22)
                    os.killpg(process.pid, signal.SIGINT)
                    stdout, stderr = process.communicate(timeout=10)
                    self.assertEqual(process.returncode, gate_status, stdout + stderr)
                    self.assertTrue((self.build_root / 'worker-cleaned').exists())
                    self.assertTrue((self.build_root / 'launcher-cleaned').exists())
                    record = self.records()[-1]
                    self.assertTrue(record['interrupted'])
                    self.assertEqual(record['exit_code'], exit_status)
                    self.assertEqual(record['gate_exit_code'], gate_status)
                finally:
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.communicate(timeout=10)

    def test_sigint_before_child_launch_records_fallback_status(self):
        """자식 종료 코드가 없는 준비 단계의 SIGINT도 130으로 기록하고 새 자식을 실행하지 않는다."""
        script = '''import os
from pathlib import Path
import signal
import sys
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, sys.argv[1])
import native_gate_record as gate
root, build, binary = map(Path, sys.argv[2:])
args = SimpleNamespace(arguments=['--gtest_filter=Suite.*'], binary=binary,
    build_root=build, repo_root=root, out_dir=binary.parent, out_name='Candidate',
    root_explicit='true', out_explicit='true', pid=os.getpid())
def interrupt(root):
    os.kill(os.getpid(), signal.SIGINT)
    return {}
with patch.object(gate, 'source_identity', interrupt):
    sys.exit(gate.run_gate(args))
'''
        result = subprocess.run([sys.executable, '-c', script, str(ROOT / 'tools/dev/lib'),
                                 str(self.directory), str(self.build_root), str(self.binary)],
                                env=self.env, cwd=self.directory, capture_output=True, text=True)
        self.assertEqual(result.returncode, 130, result.stdout + result.stderr)
        record, = self.records()
        self.assertTrue(record['interrupted'])
        self.assertEqual(record['exit_code'], 130)
        self.assertEqual(record['gate_exit_code'], 130)
        self.assertEqual(self.events(), [])

    def test_hash_cache_is_reused_and_invalidated_by_size_mtime_or_path(self):
        """크기·mtime·경로가 같은 바이너리만 캐시를 재사용한다."""
        for _ in range(2):
            self.assertEqual(self.invoke('--gtest_filter=Suite.*').returncode, 0)
        records = self.records()
        self.assertFalse(records[0]['binary']['sha256_cached'])
        self.assertTrue(records[1]['binary']['sha256_cached'])
        stat = self.binary.stat()
        os.utime(self.binary, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000))
        self.assertEqual(self.invoke('--gtest_filter=Suite.*').returncode, 0)
        self.assertFalse(self.records()[-1]['binary']['sha256_cached'])
        self.binary.write_text(self.binary.read_text() + '\n# 바이너리 내용 변경\n')
        self.assertEqual(self.invoke('--gtest_filter=Suite.*').returncode, 0)
        self.assertFalse(self.records()[-1]['binary']['sha256_cached'])
        alternate = self.out / 'another native test'
        shutil.copy2(self.binary, alternate)
        self.binary = alternate
        self.assertEqual(self.invoke('--gtest_filter=Suite.*').returncode, 0)
        self.assertFalse(self.records()[-1]['binary']['sha256_cached'])

    def test_zero_status_requires_nonempty_successful_fresh_xml(self):
        """성공 종료라도 XML 누락·실패·오류·0개·손상은 별도 코드 21로 끝난다."""
        for mode, xml in (('missing', ''), ('write', '<broken'),
                          ('write', '<testsuites tests="0" failures="0" errors="0"/>'),
                          ('write', '<testsuite tests="1" failures="1" errors="0"/>'),
                          ('write', '<testsuite tests="1" failures="0" errors="1"/>'),
                          ('write', '<testsuite tests="1" failures="-1" errors="0"/>')):
            with self.subTest(mode=mode, xml=xml):
                result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_XML_MODE=mode, YEE_NATIVE_XML=xml)
                self.assertEqual(result.returncode, 21, result.stderr)
                record = self.records()[-1]
                self.assertEqual(record['exit_code'], 0)
                self.assertEqual(record['gate_exit_code'], 21)
                self.assertIn('불일치', result.stderr)

    def test_nonzero_execution_status_survives_missing_or_failed_xml(self):
        """실제 실행의 실패 코드는 XML 누락·오류와 무관하게 보존한다."""
        for mode, xml in (('missing', ''), ('write', '<broken'),
                          ('write', '<testsuite tests="1" failures="1" errors="0"/>')):
            with self.subTest(mode=mode, xml=xml):
                result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_RUN_STATUS='37',
                                     YEE_NATIVE_XML_MODE=mode, YEE_NATIVE_XML=xml)
                self.assertEqual(result.returncode, 37, result.stderr)
                self.assertEqual(self.records()[-1]['exit_code'], 37)
                self.assertEqual(self.records()[-1]['gate_exit_code'], 37)

    def test_explicit_xml_path_is_preserved_and_stale_xml_is_rejected(self):
        """호출자의 XML 경로는 유지하고 이전 실행 결과를 새 성공으로 인정하지 않는다."""
        xml = self.directory / 'caller results.xml'
        argument = f'--gtest_output=xml:{xml}'
        result = self.invoke('--gtest_filter=Suite.*', argument)
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.records()[-1]
        self.assertEqual(record['xml']['path'], str(xml))
        self.assertEqual(record['arguments'], ['--gtest_filter=Suite.*', argument])
        result = self.invoke('--gtest_filter=Suite.*', argument, YEE_NATIVE_XML_MODE='missing')
        self.assertEqual(result.returncode, 21, result.stderr)
        self.assertFalse(self.records()[-1]['xml']['fresh'])
        self.assertTrue(xml.exists())

    def test_xml_written_only_during_listing_cannot_prove_execution(self):
        """GTest 목록 조회가 XML을 써도 실제 실행이 갱신하지 않으면 실패한다."""
        result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_LIST_XML='1',
                             YEE_NATIVE_XML_MODE='missing')
        self.assertEqual(result.returncode, 21, result.stderr)
        self.assertTrue(self.records()[-1]['xml']['exists'])
        self.assertFalse(self.records()[-1]['xml']['fresh'])

    def test_relative_directory_default_and_split_xml_arguments_are_preserved(self):
        """호출자의 상대 경로·디렉터리·기본 파일·분리된 출력 인수를 그대로 사용한다."""
        for output in (['--gtest_output=xml:relative results.xml'],
                       ['--gtest_output=xml:xml directory/'], ['--gtest_output=xml'],
                       ['--gtest_output', 'xml:split results.xml']):
            with self.subTest(output=output):
                arguments = ['--gtest_filter=Suite.*', *output]
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 0, result.stderr)
                record = self.records()[-1]
                self.assertEqual(record['arguments'], arguments)
                self.assertTrue(record['xml']['fresh'])
                self.assertEqual(record['xml']['tests'], 1)

    def test_duplicate_or_missing_output_value_is_rejected_before_side_effects(self):
        """모호하거나 값이 없는 출력 인수는 목록·기록 전에 사용법 오류로 처리한다."""
        for output in (['--gtest_output'], ['--gtest_output=xml:a', '--gtest_output=xml:b'],
                       ['--gtest_output=xml:'], ['--gtest_output', 'xml:']):
            with self.subTest(output=output):
                result = self.invoke('--gtest_filter=Suite.*', *output)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(self.events(), [])
                self.assertEqual(self.records(), [])

    def test_non_xml_output_never_silently_passes_or_gets_overridden(self):
        """호출자의 JSON 지정을 덮어쓰지 않으며 성공의 XML 증거가 없으면 21로 끝난다."""
        arguments = ['--gtest_filter=Suite.*', '--gtest_output=json:caller.json']
        result = self.invoke(*arguments)
        self.assertEqual(result.returncode, 21, result.stderr)
        self.assertEqual(self.records()[-1]['arguments'], arguments)

    def test_corrupt_binary_cache_is_recomputed(self):
        """손상되거나 잘못된 자료형의 해시 캐시는 재계산하여 복구한다."""
        self.assertEqual(self.invoke('--gtest_filter=Suite.*').returncode, 0)
        cache, = (self.build_root / 'logs/native-gates/.binary-sha256').glob('*.json')
        for contents in ('[', '[]', '{"sha256":null}'):
            with self.subTest(contents=contents):
                cache.write_text(contents)
                result = self.invoke('--gtest_filter=Suite.*')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(self.records()[-1]['binary']['sha256_cached'])

    def test_missing_or_nonexecutable_binary_stops_before_listing_or_records(self):
        """바이너리 오류는 선택한 출력 경로를 알리고 실행·기록 전에 20으로 끝난다."""
        for missing in (False, True):
            with self.subTest(missing=missing):
                if missing:
                    self.binary.unlink()
                else:
                    self.binary.chmod(0o644)
                result = self.invoke('--gtest_filter=Suite.*')
                self.assertEqual(result.returncode, 20, result.stderr)
                self.assertIn(str(self.out), result.stderr)
                self.assertEqual(self.events(), [])
                self.assertEqual(self.records(), [])

    def test_output_guard_runs_before_binary_or_record_side_effects(self):
        """선택되지 않은 출력은 공통 gate 직접 호출에서도 먼저 18로 막는다."""
        alternate = self.out.parent / 'Existing'
        alternate.mkdir()
        (alternate / 'build.ninja').touch()
        result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_OUT_EXPLICIT='false')
        self.assertEqual(result.returncode, 18, result.stderr)
        self.assertEqual(self.events(), [])
        self.assertEqual(self.records(), [])

    def test_implicit_existing_output_records_false_selection_flags(self):
        """기존 기본 출력의 명시되지 않은 선택도 값과 함께 기록한다."""
        (self.out / 'build.ninja').touch()
        result = self.invoke('--gtest_filter=Suite.*', YEE_NATIVE_ROOT_EXPLICIT='false',
                             YEE_NATIVE_OUT_EXPLICIT='false')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.records()[-1]['build_root_explicit'])
        self.assertFalse(self.records()[-1]['out_name_explicit'])


if __name__ == '__main__':
    unittest.main()
