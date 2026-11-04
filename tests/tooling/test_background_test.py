"""실제 GUI 없이 background 인수·관찰 JSON·자식 정리 경계를 검사한다."""

from argparse import Namespace
import contextlib
import errno
import io
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
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/dev/lib'))
import background_test
import native_gate_record
from test_tiers import load_manifest, resolve_filter


class BackgroundTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-background-tooling-')
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()

    def summary(self, **changes):
        return dict(schema_version=1, observation_complete=True, sample_count=3,
                    frontmost_sample_count=3, frontmost_read_gaps=0,
                    **{key: 0 for key in background_test.QUIET_COUNTERS}) | changes

    def test_observation_json_requires_complete_zero_counters(self):
        """sampling 부족과 모든 간섭 지표를 각각 실패로 판정한다."""
        path = self.directory / 'observation.json'
        variants = [(self.summary(), True), (self.summary(accessibility_available=False,
                    notes=['AX focused window owner unavailable; optional detail']), True),
                    (self.summary(key_window_changes=4), True),
                    (self.summary(frontmost_sample_count=0), False),
                    (self.summary(frontmost_sample_count=1), False),
                    (self.summary(frontmost_read_gaps=1), False),
                    (self.summary(observation_complete=False), False),
                    (self.summary(sample_count=1), False)]
        variants += [(self.summary(**{key: 1}), False) for key in background_test.QUIET_COUNTERS]
        for summary, passed in variants:
            with self.subTest(summary=summary):
                path.write_text(json.dumps(summary))
                self.assertEqual(background_test.read_observation(path)['passed'], passed)

    def test_observation_json_rejects_missing_wrong_types_and_negative_counts(self):
        path = self.directory / 'observation.json'
        for changes in ({'schema_version': 2}, {'sample_count': -1}, {'sample_count': True},
                        {'modifier_injections': None}, {'observation_complete': 1},
                        {'frontmost_sample_count': True}, {'frontmost_read_gaps': -1}):
            path.write_text(json.dumps(self.summary(**changes)))
            with self.assertRaises(ValueError):
                background_test.read_observation(path)
        for payload in ('[]', '{}', '{broken'):
            path.write_text(payload)
            with self.assertRaises(ValueError):
                background_test.read_observation(path)

    def test_display_exceptions_partition_without_changing_b_classification(self):
        manifest = {'test.cc::Fixture.Case': {'tier': 'B', 'os': ['Darwin', 'Linux'],
                    'binaries': ['small'], 'needs_display': {'Darwin': '기존 Widget runner'}}}
        self.assertEqual(resolve_filter(manifest, 'Fixture.*', 'Darwin', 'background', 'small'), ('', ''))
        self.assertEqual(resolve_filter(manifest, 'Fixture.*', 'Darwin', 'foreground', 'small'),
                         ('Fixture.Case', 'B'))
        self.assertEqual(resolve_filter(manifest, 'Fixture.*', 'Linux', 'background', 'small'),
                         ('Fixture.Case', 'B'))

    def test_manifest_display_exception_requires_b_os_and_reason(self):
        path = self.directory / 'manifest.json'
        entry = dict(tier='B', os=['Darwin'], binaries=['small'], owner='gate', reason='근거')
        for changes in ({'needs_display': ['Darwin']}, {'needs_display': {'Darwin': ''}},
                        {'needs_display': {'Linux': '미지원 OS'}},
                        {'tier': 'A', 'needs_display': {'Darwin': '순수 계산에는 부적합'}}):
            path.write_text(json.dumps({'test.cc::Fixture.Case': entry | changes}))
            with self.assertRaises(ValueError):
                load_manifest(path)

    def test_observer_argument_rejected_before_record_or_binary_access(self):
        command = [sys.executable, str(ROOT / 'tools/dev/lib/native_gate_record.py'),
                   '--repo-root', str(self.directory), '--build-root', str(self.directory / 'missing'),
                   '--out-dir', str(self.directory), '--out-name', 'test', '--root-explicit', 'true',
                   '--out-explicit', 'true', '--pid', '1', '--tier', 'foreground', '--observe-background',
                   '--', str(self.directory / 'missing-binary'), '--gtest_filter=*']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--tier=background', result.stderr)
        self.assertEqual(list(self.directory.iterdir()), [])

    @unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
    def test_gate_observer_options_and_headless_selection_without_native_execution(self):
        """옵션 오류·필터 조회는 개발 환경과 display helper에 접근하지 않는다."""
        for arguments, code in ((['--observe-background', '--print-filters'], 0),
                                (['--observe-background', '--tier=foreground', '--print-filters'], 2),
                                (['--observe-background', '--tier=all'], 2),
                                (['--observe-background=yes'], 2)):
            result = subprocess.run(['zsh', str(ROOT / 'tools/dev/test-header.sh'),
                                     'interactive', *arguments], capture_output=True, text=True,
                                    env={**os.environ, 'YEE_LOCAL_BUILD_ROOT': str(self.directory / 'missing')})
            self.assertEqual(result.returncode, code, result.stdout + result.stderr)
            if code == 0:
                self.assertIn('observe=true', result.stdout)
                self.assertIn('--headless', result.stdout)
                self.assertIn('--screen-info={1920x1080}', result.stdout)
                self.assertIn('--window-size=1280,900', result.stdout)
                self.assertIn('--yee-test-tier=background', result.stdout)
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_background_layout_cases_and_foreground_launch_policy_are_preserved(self):
        """폭을 고친 layout 검사는 background, 표시형 URL 정책 검사는 foreground에서 유지한다."""
        manifest = load_manifest(ROOT / 'tests/tooling/test_tiers.json')
        layout_cases = (
            'YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal',
            'YeeBrowserViewTabbedLayoutImplFeatureOffUiTest.NativeSidePanelRowModesMatchAppliedLayout',
            'YeeBrowserViewTabbedLayoutImplUiTest.YeeNativePlannerResultMatchesAppliedLayout',
        )
        for platform in ('Darwin', 'Linux', 'Windows'):
            for case in layout_cases:
                self.assertEqual(resolve_filter(manifest, case, platform, 'background', 'yee_interactive_ui_tests'),
                                 (case, 'B'))
            case = 'InternalURLsBrowserTest.ExternalLaunchUsesCanonicalPolicy'
            for tier, expected in (('background', ('', '')), ('foreground', (case, 'B')), ('all', (case, 'B'))):
                self.assertEqual(resolve_filter(manifest, case, platform, tier, 'site_controls_browsertests'),
                                 expected)

    def test_macos_upstream_browser_launcher_cases_stay_in_foreground(self):
        """Yee 활성화 차단이 없는 heavy main의 B는 macOS background에서 실행하지 않는다."""
        manifest = load_manifest(ROOT / 'tests/tooling/test_tiers.json')
        cases = [key.split('::')[1] for key, entry in manifest.items()
                 if entry['tier'] == 'B' and 'Darwin' in entry['os'] and 'browser_tests' in entry['binaries']]
        self.assertTrue(cases)
        for case in cases:
            self.assertEqual(resolve_filter(manifest, case, 'Darwin', 'background', 'browser_tests'), ('', ''))
            self.assertEqual(resolve_filter(manifest, case, 'Darwin', 'foreground', 'browser_tests'), (case, 'B'))
            self.assertEqual(resolve_filter(manifest, case, 'Darwin', 'all', 'browser_tests'), (case, 'B'))

    @unittest.skipUnless(hasattr(os, 'killpg'), 'POSIX 세션 신호가 필요하다')
    def test_observer_survives_terminal_sigint_until_cleanup_finishes(self):
        """Python sensor stub은 화면 없이 Ctrl-C와 정리 이후 관찰기 종료를 검증한다."""
        source = self.directory / 'sensor.swift'
        source.touch()
        cache = self.directory / 'background-observer'
        cache.mkdir()
        sensor = cache / 'observe-background'
        sensor.write_text(f'#!{sys.executable}\n' + '''
import json,sys,time
from pathlib import Path
options = dict(arg.split('=',1) for arg in sys.argv[1:])
Path(options['--ready-file']).touch()
deadline = time.monotonic() + 8
while not Path(options['--stop-file']).exists() and time.monotonic() < deadline:
    time.sleep(.02)
Path(options['--output']).write_text(''' + repr(json.dumps(self.summary())) + ''')
''')
        sensor.chmod(0o755)
        harness = '''
import signal,sys,time
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import background_test
background_test.sys.platform = 'darwin'
root = Path(sys.argv[2])
received = []
signal.signal(signal.SIGINT, lambda *args: received.append(True))
observer = background_test.BackgroundObserver(root/'sensor.swift', root, root)
(root/'harness.ready').touch()
deadline = time.monotonic() + 5
while not received and time.monotonic() < deadline: time.sleep(.02)
time.sleep(.2)
assert observer.child.poll() is None
assert observer.finish()['passed']
assert received
'''
        child = subprocess.Popen([sys.executable, '-c', harness, str(ROOT / 'tools/dev/lib'),
                                  str(self.directory)], start_new_session=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not (self.directory / 'harness.ready').exists():
                if child.poll() is not None or time.monotonic() >= deadline:
                    self.fail('Python 관찰기 fixture가 시작되지 않았다.')
                time.sleep(.02)
            os.killpg(child.pid, signal.SIGINT)
            stdout, stderr = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 0, stdout + stderr)
            self.assertTrue((self.directory / 'observer.stop').exists())
        finally:
            (self.directory / 'observer.stop').touch()
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
            child.communicate(timeout=10)

    def test_interrupted_prepare_never_starts_a_native_child(self):
        with mock.patch.object(background_test.subprocess, 'Popen') as launch:
            result = background_test.run_background_command(['unused'], self.directory / 'run',
                                                            lambda: True, capture=True)
        launch.assert_not_called()
        self.assertEqual(result, (-signal.SIGINT, ''))
        self.assertFalse((self.directory / 'run').exists())

    def test_profile_boundary_preserves_argv_and_path_components(self):
        directory = self.directory / 'profile space'
        directory.mkdir()
        for arguments, expected in (
                (['binary'], True), (['binary', f'--user-data-dir={directory}/child'], True),
                (['binary', f'--user-data-dir={directory}-other'], False),
                (['binary', '--user-data-dir='], False),
                (['binary', '--', '--user-data-dir=/other'], True)):
            self.assertEqual(background_test.profile_is_owned(arguments, directory), expected)

    def test_process_session_snapshot_keeps_full_executable_and_start_time(self):
        listing = (' 101 1 101 S Wed Nov  4 12:30:00 2026 /tmp/yee tests/binary\n'
                   ' 102 1 555 S Wed Nov  4 12:30:00 2026 /tmp/yee tests/binary\n')
        with mock.patch.object(background_test.subprocess, 'check_output', return_value=listing), \
             mock.patch.object(background_test.os, 'getsid', side_effect=lambda pid: 101 if pid == 101 else 555):
            self.assertEqual(background_test.process_session_snapshot(101),
                             {101: (('Wed', 'Nov', '4', '12:30:00', '2026'), '/tmp/yee tests/binary')})

    def test_process_group_and_recorded_descendants_survive_unreadable_sessions(self):
        """세션 조회가 막혀도 전용 그룹·자손·기록한 고아 PID를 추적하고 좀비는 제외한다."""
        listing = ('101 1 101 S Wed Nov 4 12:30:00 2026 /tmp/test\n'
                   '102 101 102 S Wed Nov 4 12:30:00 2026 /tmp/helper\n'
                   '103 1 103 S Wed Nov 4 12:30:00 2026 /tmp/orphan\n'
                   '104 1 101 Z Wed Nov 4 12:30:00 2026 /tmp/zombie\n'
                   '106 1 101 Z\n'
                   '105 1 105 S Wed Nov 4 12:30:00 2026 /Applications/Yee\n')
        known = {103: (('Wed', 'Nov', '4', '12:30:00', '2026'), '/tmp/orphan')}
        with mock.patch.object(background_test.subprocess, 'check_output', return_value=listing), \
             mock.patch.object(background_test.os, 'getsid', side_effect=PermissionError(errno.EPERM, 'denied')):
            self.assertEqual(set(background_test.process_session_snapshot(101, known)), {101, 102, 103})

    def test_unreadable_or_empty_process_listing_fails_closed(self):
        for listing in ('', 'unreadable process table'):
            with self.subTest(listing=listing), \
                 mock.patch.object(background_test.subprocess, 'check_output', return_value=listing):
                with self.assertRaises(ValueError):
                    background_test.process_session_snapshot(101)

    def test_exited_child_argv_errors_are_gone_without_cleanup_signals(self):
        """열거 직후 끝난 자식의 EINVAL·ESRCH는 기록 오류나 정리 대상이 아니다."""
        for error in (errno.EINVAL, errno.ESRCH):
            with self.subTest(error=error):
                child = mock.Mock(pid=101, returncode=0)
                child.communicate.return_value = ('Fixture.\n  Case\n', None)
                current = {102: (('start',), '/tmp/helper')}
                def arguments(pid):
                    current.clear()
                    raise OSError(error, 'exited child')
                with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
                     mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
                     mock.patch.object(background_test, 'process_session_snapshot',
                                       side_effect=lambda session, known=None: dict(current) if session else {}), \
                     mock.patch.object(background_test, 'process_arguments', side_effect=arguments), \
                     mock.patch.object(background_test.os, 'kill') as kill:
                    code, output = background_test.run_background_command(
                        ['binary'], self.directory / 'run', lambda: False, capture=True)
                self.assertEqual(code, 0)
                self.assertIn('Fixture.', output)
                kill.assert_not_called()

    def test_live_child_with_einval_cannot_hide_a_cleanup_leftover(self):
        """EINVAL이 반복되어도 정리 뒤 살아 있는 자식을 통과시키지 않는다."""
        child = mock.Mock(pid=101, returncode=0)
        child.communicate.return_value = ('', None)
        current = {102: (('start',), '/tmp/helper')}
        with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
             mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
             mock.patch.object(background_test, 'process_session_snapshot',
                               side_effect=lambda session, known=None: dict(current) if session else {}), \
             mock.patch.object(background_test, 'process_arguments', side_effect=OSError(errno.EINVAL, 'gone')), \
             mock.patch.object(background_test.time, 'monotonic', side_effect=range(0, 30, 3)), \
             mock.patch.object(background_test.os, 'kill'):
            with self.assertRaisesRegex(ValueError, '^남은 자식 PID: 102$'):
                background_test.run_background_command(['binary'], self.directory / 'run', lambda: False)

    def test_live_owned_child_with_einval_is_still_cleaned_by_process_identity(self):
        """argv 오류가 있어도 그룹·PID로 확인한 자기 helper에는 정리 신호를 보낸다."""
        child = mock.Mock(pid=101, returncode=0)
        child.communicate.return_value = ('', None)
        current = {102: (('start',), '/tmp/helper')}
        with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
             mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
             mock.patch.object(background_test, 'process_session_snapshot',
                               side_effect=lambda session, known=None: dict(current) if session else {}), \
             mock.patch.object(background_test, 'process_arguments', side_effect=OSError(errno.EINVAL, 'gone')), \
             mock.patch.object(background_test.os, 'kill', side_effect=lambda *args: current.clear()) as kill:
            code, _ = background_test.run_background_command(['binary'], self.directory / 'run', lambda: False)
        self.assertEqual(code, 0)
        kill.assert_called_once_with(102, signal.SIGTERM)

    def test_cleanup_targets_only_verified_child_identity(self):
        child = mock.Mock(pid=101, returncode=0)
        child.communicate.return_value = ('Fixture.\n  Case\n', None)
        owned = (('Wed', 'Nov', '4', '12:30:00', '2026'), '/tmp/owned binary')
        reused = (('Wed', 'Nov', '4', '12:30:01', '2026'), '/tmp/unrelated binary')
        snapshots = [{}, {101: owned}, {101: reused}, {}, {}, {}, {}]
        with mock.patch.object(background_test.subprocess, 'Popen', return_value=child) as launch, \
             mock.patch.object(background_test, 'process_session_snapshot', side_effect=snapshots), \
             mock.patch.object(background_test, 'process_arguments', return_value=['/tmp/owned binary']), \
             mock.patch.object(background_test.os, 'kill') as kill:
            code, output = background_test.run_background_command(
                ['/tmp/owned binary', '--headless'], self.directory / 'run', lambda: False, capture=True)
        self.assertEqual(code, 0)
        self.assertIn('Fixture.', output)
        kill.assert_not_called()
        self.assertTrue(launch.call_args.kwargs['start_new_session'])
        self.assertIn('--user-data-dir=', launch.call_args.args[0][-1])
        self.assertFalse((self.directory / 'run').exists())

    def test_macos_temp_environment_overrides_inherited_chromium_directory(self):
        """Chromium의 launcher 프로필도 이번 실행의 짧은 임시 경로를 사용한다."""
        child = mock.Mock(pid=101, returncode=0)
        child.communicate.return_value = ('', None)
        directory = self.directory / 'run'
        with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
             mock.patch.dict(os.environ, TMPDIR='/other/tmp', MAC_CHROMIUM_TMPDIR='/other/chromium'), \
             mock.patch.object(background_test.subprocess, 'Popen', return_value=child) as launch, \
             mock.patch.object(background_test, 'process_session_snapshot', return_value={}):
            background_test.run_background_command(['binary'], directory, lambda: False)
        environment = launch.call_args.kwargs['env']
        self.assertEqual(environment['TMPDIR'], str(directory) + '/')
        self.assertEqual(environment['MAC_CHROMIUM_TMPDIR'], environment['TMPDIR'])

    def test_profileless_reparented_helper_is_cleaned_on_exit_and_interrupt(self):
        """부모·프로필 인수가 없는 renderer도 전용 세션의 신원으로 정리한다."""
        helper = (('Wed', 'Nov', '4', '12:30:00', '2026'), '/tmp/Renderer Helper')
        for interrupt in (False, True):
            with self.subTest(interrupt=interrupt):
                child = mock.Mock(pid=101, returncode=0)
                child.communicate.return_value = ('', None)
                current = {102: helper}
                observer = mock.Mock()
                events = []
                observer.register_session.side_effect = lambda *args: events.append('register')
                observer.close_session.side_effect = lambda *args: events.append('close')
                child.communicate.side_effect = lambda **kwargs: (events.append('communicate') or ('', None))
                def kill(pid, signum):
                    events.append('kill')
                    current.pop(pid)
                with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
                     mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
                     mock.patch.object(background_test.os, 'getpgid', side_effect=AssertionError('외부 PID 조회')) as group, \
                     mock.patch.object(background_test.os, 'write', return_value=1), \
                     mock.patch.object(background_test, 'process_session_snapshot',
                                       side_effect=lambda session, known=None: dict(current) if session else {}), \
                     mock.patch.object(background_test, 'process_arguments',
                                       return_value=['/tmp/Renderer Helper', '--type=renderer']), \
                     mock.patch.object(background_test.os, 'kill', side_effect=kill) as terminate:
                    background_test.run_background_command(
                        ['binary'], self.directory / 'run', mock.Mock(side_effect=[False, interrupt, interrupt]),
                        observer=observer)
                terminate.assert_called_once_with(102, signal.SIGTERM)
                self.assertEqual(events[0], 'register')
                self.assertEqual(events[-1], 'close')
                observer.register_session.assert_called_once()
                self.assertEqual(observer.register_session.call_args.args[:2], (101, 101))
                group.assert_not_called()
                observer.close_session.assert_called_once_with(101)

    def test_profile_boundary_error_does_not_disown_session_for_cleanup(self):
        """잘못된 프로필은 실패로 기록하되 자기 세션의 자식은 남기지 않는다."""
        child = mock.Mock(pid=101, returncode=0)
        child.communicate.return_value = ('', None)
        current = {102: (('start',), '/tmp/owned helper')}
        with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
             mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
             mock.patch.object(background_test, 'process_session_snapshot',
                               side_effect=lambda session, known=None: dict(current) if session else {}), \
             mock.patch.object(background_test, 'process_arguments',
                               return_value=['binary', '--user-data-dir=/other/profile']), \
             mock.patch.object(background_test.os, 'kill', side_effect=lambda *args: current.clear()) as kill:
            with self.assertRaisesRegex(ValueError, '격리 프로필 밖의 자식: 102'):
                background_test.run_background_command(['binary'], self.directory / 'run', lambda: False)
        kill.assert_called_once_with(102, signal.SIGTERM)

    def test_observer_publishes_both_session_lifetimes_atomically(self):
        """목록·실행 세션을 종료 후에도 남겨 늦게 관찰한 자식의 소유권을 확인한다."""
        source = self.directory / 'sensor.swift'
        source.touch()
        cache = self.directory / 'background-observer'
        cache.mkdir()
        (cache / 'observe-background').touch()
        (self.directory / 'observer.ready').touch()
        with mock.patch.object(background_test.sys, 'platform', 'darwin'), \
             mock.patch.object(background_test.subprocess, 'Popen') as launch:
            observer = background_test.BackgroundObserver(source, self.directory, self.directory)
        self.assertIn(f'--sessions-file={observer.sessions_file}', launch.call_args.args[0])
        self.assertIn(f'--runner-pid={os.getpid()}', launch.call_args.args[0])
        (self.directory / 'observer-session-101.ready').touch()
        observer.register_session(101, 101, 10)
        observer.close_session(101)
        (self.directory / 'observer-session-202.ready').touch()
        observer.register_session(202, 202, 20)
        registry = json.loads(observer.sessions_file.read_text())
        self.assertEqual(registry['schema_version'], 1)
        self.assertEqual([entry['id'] for entry in registry['sessions']], [101, 202])
        self.assertEqual([entry['root_pid'] for entry in registry['sessions']], [101, 202])
        self.assertEqual([entry['process_group'] for entry in registry['sessions']], [101, 202])
        self.assertGreaterEqual(registry['sessions'][0]['ended_us'], 10)
        self.assertIsNone(registry['sessions'][1]['ended_us'])
        self.assertFalse(observer.sessions_file.with_suffix('.tmp').exists())
        with self.assertRaises(ValueError):
            observer.register_session(303, 304, 30)
        observer.child.wait.return_value = 0
        observer.output.write_text(json.dumps(self.summary()))
        self.assertFalse(observer.finish()['passed'])
        observer.output.write_text(json.dumps(self.summary(
            observed_test_root_pids=[101, 202], observed_test_process_groups=[101, 202])))
        self.assertTrue(observer.finish()['passed'])

    def test_header_site_controls_and_install_receive_both_root_pgid_pairs_before_exec(self):
        """GUI 없는 sensor·바이너리로 세 gate의 목록·실행 경로와 setsid 완료 순서를 확인한다."""
        cache = self.directory / 'background-observer'
        cache.mkdir()
        sensor = cache / 'observe-background'
        sensor.write_text(f'#!{sys.executable}\n' + '''
import json,sys,time
from pathlib import Path
options = dict(arg.split('=',1) for arg in sys.argv[1:])
Path(options['--ready-file']).touch()
registry = Path(options['--sessions-file'])
while not Path(options['--stop-file']).exists():
    for session in json.loads(registry.read_text())['sessions']:
        assert session['root_pid'] == session['id'] == session['process_group']
        (registry.parent / f"observer-session-{session['root_pid']}.ready").touch()
    time.sleep(.01)
sessions = json.loads(registry.read_text())['sessions']
summary = ''' + repr(self.summary()) + '''
summary['observed_test_root_pids'] = sorted(session['root_pid'] for session in sessions)
summary['observed_test_process_groups'] = sorted(session['process_group'] for session in sessions)
Path(options['--output']).write_text(json.dumps(summary))
''')
        sensor.chmod(0o755)
        for name in ('yee_header_unittests', 'site_controls_browsertests', 'install_sh_test'):
            with self.subTest(binary=name):
                binary = self.directory / name
                binary.write_text(f'#!{sys.executable}\n' + '''
import json,os,sys
from pathlib import Path
xml = Path(next(arg.partition('xml:')[2] for arg in sys.argv if arg.startswith('--gtest_output=')))
registry = json.loads((xml.parent/'observer-sessions.json').read_text())
session = next(entry for entry in registry['sessions'] if entry['root_pid'] == os.getpid())
assert session['process_group'] == os.getpgrp() == os.getsid(0) == os.getpid()
assert (xml.parent/f"observer-session-{os.getpid()}.ready").exists()
if '--gtest_list_tests' in sys.argv:
    print('Fixture.\\n  Case')
else:
    xml.write_text('<testsuites tests="1" failures="0" errors="0"/>')
''')
                binary.chmod(0o755)
                args = Namespace(binary=binary, arguments=['--gtest_filter=Fixture.*'],
                                 build_root=self.directory, repo_root=ROOT, out_dir=self.directory,
                                 out_name='test', pid=1, root_explicit='true', out_explicit='true',
                                 tier='background', observe_background=True)
                stderr = io.StringIO()
                with mock.patch.object(native_gate_record, 'source_identity', return_value={}), \
                     mock.patch.object(native_gate_record, 'binary_identity', return_value={}), \
                     mock.patch.object(background_test.os, 'getpgid',
                                       side_effect=AssertionError('외부 PID 그룹 조회')), \
                     mock.patch.object(background_test, 'process_session_snapshot', return_value={}), \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                    self.assertEqual(native_gate_record.run_gate(args), 0, stderr.getvalue())
                records = sorted((self.directory / 'logs/native-gates').glob(f'*-{name}-1/record.json'))
                self.assertEqual(len(records), 1)
                record = json.loads(records[0].read_text())
                sessions = json.loads((records[0].parent / 'observer-sessions.json').read_text())['sessions']
                self.assertEqual(len(sessions), 2)
                self.assertTrue(all(session['ended_us'] is not None for session in sessions))
                self.assertEqual(record['observation']['observed_test_root_pids'],
                                 sorted(session['root_pid'] for session in sessions))
                self.assertEqual(record['observation']['observed_test_process_groups'],
                                 sorted(session['process_group'] for session in sessions))
                self.assertTrue(record['observation']['passed'])

    def test_observer_has_no_foreign_pid_lookup_path(self):
        """Swift는 시스템 앱 PID에 getpgid·getsid·proc_pidinfo를 호출하지 않는다."""
        source = (ROOT / 'tools/dev/observe-background.swift').read_text()
        for lookup in ('getpgid(', 'getsid(', 'proc_pidinfo(', 'resolveWindowProcesses('):
            self.assertNotIn(lookup, source)
        self.assertIn('KERN_PROC_PGRP', source)

    def test_missing_observer_acknowledgement_never_executes_the_test(self):
        """준비 확인이 실패하면 대기 중인 자기 자식만 정리하고 테스트는 실행하지 않는다."""
        marker = self.directory / 'executed'
        observer = mock.Mock()
        observer.register_session.side_effect = ValueError('관찰기 준비 실패')
        children = []
        popen = subprocess.Popen
        def launch(*args, **kwargs):
            child = popen(*args, **kwargs)
            children.append(child)
            return child
        def snapshot(session, known=None):
            if not session or children[0].poll() is not None:
                return {}
            return {session: (('start',), sys.executable)}
        with mock.patch.object(background_test.subprocess, 'Popen', side_effect=launch), \
             mock.patch.object(background_test, 'process_session_snapshot', side_effect=snapshot), \
             mock.patch.object(background_test, 'process_arguments', return_value=[sys.executable]):
            with self.assertRaisesRegex(ValueError, '관찰기 준비 실패'):
                background_test.run_background_command(
                    [sys.executable, '-c', f'from pathlib import Path; Path({str(marker)!r}).touch()'],
                    self.directory / 'run', lambda: False, observer=observer)
        self.assertFalse(marker.exists())
        self.assertIsNotNone(children[0].poll())
        observer.close_session.assert_called_once_with(children[0].pid)

    def test_interruption_while_observer_prepares_never_executes_the_test(self):
        """관찰기 응답 대기 중 받은 중단은 pipe를 해제하지 않고 SIGINT로 반환한다."""
        marker = self.directory / 'executed'
        observer = mock.Mock()
        interrupted = []
        observer.register_session.side_effect = lambda *args: interrupted.append(True)
        with mock.patch.object(background_test, 'process_session_snapshot', return_value={}):
            code, _ = background_test.run_background_command(
                [sys.executable, '-c', f'from pathlib import Path; Path({str(marker)!r}).touch()'],
                self.directory / 'run', lambda: bool(interrupted), observer=observer)
        self.assertEqual(code, -signal.SIGINT)
        self.assertFalse(marker.exists())

    def test_process_identity_snapshot_waits_for_exec_completion(self):
        """launcher 전환을 지연해도 첫 신원 조회는 exec가 끝난 뒤에만 수행한다."""
        active = self.directory / 'launcher.active'
        launcher = self.directory / 'launcher.py'
        launcher.write_text(f'''
import os,sys,time
from pathlib import Path
active = Path({str(active)!r})
active.touch()
assert os.read(int(sys.argv[2]), 1) == b'1'
os.close(int(sys.argv[2]))
time.sleep(.15)
active.unlink()
os.set_inheritable(int(sys.argv[3]), False)
os.execvp(sys.argv[4], sys.argv[4:])
''')
        observer = mock.Mock()
        def register(*args):
            deadline = time.monotonic() + 5
            while not active.exists():
                if time.monotonic() >= deadline:
                    raise ValueError('launcher가 시작되지 않았다')
                time.sleep(.01)
        observer.register_session.side_effect = register
        snapshots = []
        def snapshot(session, known=None):
            if session:
                self.assertFalse(active.exists(), 'exec 전의 launcher를 신원으로 기록했다')
                snapshots.append(session)
            return {}
        with mock.patch.object(background_test, '__file__', str(launcher)), \
             mock.patch.object(background_test, 'process_session_snapshot', side_effect=snapshot):
            code, output = background_test.run_background_command(
                [sys.executable, '-c', 'print("Fixture.")'], self.directory / 'run',
                lambda: False, capture=True, observer=observer)
        self.assertEqual(code, 0)
        self.assertIn('Fixture.', output)
        self.assertTrue(snapshots)

    def test_process_listing_failure_still_reaps_the_direct_child(self):
        """실행 후 목록 권한이 사라져도 직접 생성한 Popen 자식은 회수한다."""
        children = []
        popen = subprocess.Popen
        def launch(*args, **kwargs):
            child = popen(*args, **kwargs)
            children.append(child)
            return child
        def snapshot(session, known=None):
            if session:
                raise PermissionError(errno.EPERM, '목록 조회 거부')
            return {}
        try:
            with mock.patch.object(background_test.subprocess, 'Popen', side_effect=launch), \
                 mock.patch.object(background_test, 'process_session_snapshot', side_effect=snapshot):
                with self.assertRaises(PermissionError):
                    background_test.run_background_command(
                        [sys.executable, '-c', 'import time; time.sleep(10)'],
                        self.directory / 'run', lambda: False)
            self.assertEqual(len(children), 1)
            self.assertIsNotNone(children[0].poll())
        finally:
            for child in children:
                if child.poll() is None:
                    child.kill()
                child.wait(timeout=5)

    def test_killed_root_is_reaped_before_waiting_for_descendant_output(self):
        """root가 TERM을 무시하고 자손이 stdout을 유지해도 KILL 뒤 root부터 회수한다."""
        child = mock.Mock(pid=101, returncode=None)
        child.poll.return_value = None
        child.wait.side_effect = [subprocess.TimeoutExpired('child', 2), 0]
        child.communicate.side_effect = subprocess.TimeoutExpired('child output', 5)
        with mock.patch.object(background_test.subprocess, 'Popen', return_value=child), \
             mock.patch.object(background_test, 'process_session_snapshot', return_value={}):
            with self.assertRaisesRegex(ValueError, '검사 자식의 종료·출력 정리'):
                background_test.run_background_command(
                    ['binary'], self.directory / 'run', mock.Mock(side_effect=[False, True]))
        child.terminate.assert_called_once()
        child.kill.assert_called_once()
        self.assertEqual(child.wait.call_args_list, [mock.call(timeout=2), mock.call(timeout=5)])

    @unittest.skipUnless(hasattr(os, 'fork'), 'POSIX fork가 필요하다')
    def test_exited_listing_parent_does_not_wait_for_a_pipe_holding_descendant(self):
        """목록 부모가 종료하면 stdout을 가진 Python 자식을 먼저 정리하고 출력을 회수한다."""
        worker_pid = self.directory / 'worker.pid'
        script = f'''
import os,time
from pathlib import Path
pid_file = Path({str(worker_pid)!r})
if os.fork() == 0:
    pid_file.write_text(str(os.getpid()))
    time.sleep(10)
    os._exit(0)
while not pid_file.exists(): time.sleep(.01)
print('Fixture.\\n  Case', flush=True)
'''
        terminated = set()
        real_kill = os.kill
        def snapshot(session, known=None):
            if not session or not worker_pid.exists():
                return {}
            pid = int(worker_pid.read_text())
            return {} if pid in terminated else {pid: (('start',), sys.executable)}
        def kill(pid, signum):
            real_kill(pid, signum)
            terminated.add(pid)
        started = time.monotonic()
        with mock.patch.object(background_test, 'process_session_snapshot', side_effect=snapshot), \
             mock.patch.object(background_test, 'process_arguments', return_value=[sys.executable]), \
             mock.patch.object(background_test.os, 'kill', side_effect=kill):
            code, output = background_test.run_background_command(
                [sys.executable, '-c', script], self.directory / 'run', lambda: False, capture=True)
        self.assertEqual(code, 0)
        self.assertIn('Fixture.', output)
        self.assertTrue(terminated)
        self.assertLess(time.monotonic() - started, 5)

    def test_recorder_observes_discovery_execution_and_preserves_native_failure(self):
        binary = self.directory / 'binary'
        binary.touch()
        args = Namespace(binary=binary, arguments=['--headless', '--yee-test-tier=background',
                         '--gtest_filter=Fixture.*'], build_root=self.directory, repo_root=ROOT,
                         out_dir=self.directory, out_name='test', pid=1, root_explicit='true',
                         out_explicit='true', tier='background', observe_background=True)
        for exit_code, expected in ((0, 23), (7, 7)):
            calls = []
            observer = mock.Mock()
            observer.finish.return_value = self.summary(frontmost_changes=1) | {'passed': False}
            def run(command, directory, interrupted, capture=False, observer=None):
                self.assertIs(observer, sensor)
                calls.append(command)
                if capture:
                    return 0, 'Fixture.\n  Case\n'
                xml = next(arg.partition('xml:')[2] for arg in command if arg.startswith('--gtest_output='))
                Path(xml).write_text('<testsuites tests="1" failures="0" errors="0"/>')
                return exit_code, None
            sensor = observer
            with mock.patch.object(native_gate_record, 'source_identity', return_value={}), \
                 mock.patch.object(native_gate_record, 'binary_identity', return_value={}), \
                 mock.patch.object(native_gate_record, 'run_background_command', side_effect=run), \
                 mock.patch.object(native_gate_record, 'BackgroundObserver', return_value=observer) as start, \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(native_gate_record.run_gate(args), expected)
            self.assertEqual(len(calls), 2)
            self.assertIn('--gtest_list_tests', calls[0])
            self.assertNotIn('--gtest_list_tests', calls[1])
            start.assert_called_once()
            observer.finish.assert_called_once()

    def test_recorder_reports_sampling_failure_separately_from_optional_ax_detail(self):
        """AX 참고 정보는 성공을 막지 않고 필수 표본 부족은 원래 실패 코드와 구분한다."""
        binary = self.directory / 'binary'
        binary.touch()
        args = Namespace(binary=binary, arguments=['--gtest_filter=Fixture.*'],
                         build_root=self.directory, repo_root=ROOT, out_dir=self.directory,
                         out_name='test', pid=1, root_explicit='true', out_explicit='true',
                         tier='background', observe_background=True)
        for native_code, xml_tests, complete, expected, status in (
                (0, 1, True, 0, None), (0, 1, False, 23, 'tests passed'),
                (9, 1, False, 9, 'native gate failed (9)'),
                (0, 0, False, 21, 'native gate failed (21)')):
            with self.subTest(native_code=native_code, xml_tests=xml_tests, complete=complete):
                observer = mock.Mock()
                observer.finish.return_value = self.summary(
                    observation_complete=complete, accessibility_available=False,
                    incomplete_reasons=[] if complete else ['sampling coverage incomplete'],
                    notes=['AX focused window owner unavailable; optional detail']) | {'passed': complete}
                def run(command, directory, interrupted, **kwargs):
                    if kwargs.get('capture'):
                        return 0, 'Fixture.\n  Case\n'
                    xml = next(arg.partition('xml:')[2] for arg in command if arg.startswith('--gtest_output='))
                    Path(xml).write_text(f'<testsuites tests="{xml_tests}" failures="0" errors="0"/>')
                    return native_code, None
                stderr = io.StringIO()
                with mock.patch.object(native_gate_record, 'source_identity', return_value={}), \
                     mock.patch.object(native_gate_record, 'binary_identity', return_value={}), \
                     mock.patch.object(native_gate_record, 'run_background_command', side_effect=run), \
                     mock.patch.object(native_gate_record, 'BackgroundObserver', return_value=observer), \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
                    self.assertEqual(native_gate_record.run_gate(args), expected)
                self.assertIn('background observation note: AX focused window owner unavailable', stderr.getvalue())
                self.assertNotIn('Accessibility permission missing', stderr.getvalue())
                if status:
                    self.assertIn(f'{status}; observation incomplete: sampling coverage incomplete', stderr.getvalue())
                if status != 'tests passed':
                    self.assertNotIn('tests passed', stderr.getvalue())


@unittest.skipUnless(sys.platform == 'darwin' and shutil.which('xcrun'), 'macOS Swift 컴파일러가 필요하다')
class BackgroundObserverLogicTest(unittest.TestCase):
    def test_pgrp_ownership_coverage_and_offscreen_survival_without_gui(self):
        """가짜 목록으로 소유권 조회 공백·앱 생명주기·일반 창 손실과 JSON 판정을 검사한다."""
        with tempfile.TemporaryDirectory(prefix='yee-observer-tooling-') as temporary:
            directory = Path(temporary)
            main = directory / 'fixture.swift'
            main.write_text(r'''
import ApplicationServices
import Darwin
import Foundation

@main struct Fixture {
    static func main() throws {
        var transitions = PIDChanges()
        for pid: Int32? in [nil, 705, nil, 705, nil] { transitions.sample(pid) }
        precondition(transitions.changes == 0 && transitions.readGaps == 3 && transitions.validSamples == 2)
        for pid: Int32? in [706, 706, nil, 705, 705] { transitions.sample(pid) }
        precondition(transitions.changes == 2 && transitions.first == 705 && transitions.last == 705)

        let asn = "ASN:0x0-0xe00e:"
        var queries: [[String]] = []
        let front = frontmostApplicationPID(query: { arguments in
            queries.append(arguments)
            return arguments == ["front"] ? asn + "\n" : "\"pid\"=705\n"
        })
        precondition(front == 705 && queries == [["front"], ["info", "-only", "pid", asn]])
        for payload in ["\"LSApplicationPID\"=706\n", " \"pid\" = 706 \n"] {
            precondition(frontmostApplicationPID(query: { $0 == ["front"] ? asn : payload }) == 706)
        }
        for payload in [nil, "", "\"pid\"=0", "\"pid\"=-1", "\"pid\"=text", "\"name\"=705",
                        "\"pid\"=705\n\"pid\"=706", "\"pid\"=999999999999999999"] as [String?] {
            precondition(frontmostApplicationPID(query: { $0 == ["front"] ? asn : payload }) == nil)
        }
        for invalid in [nil, "", "ASN:0x0-0x0:", "Ghostty", asn + "\nextra"] as [String?] {
            precondition(frontmostApplicationPID(query: { arguments in
                precondition(arguments == ["front"] || invalid == "ASN:0x0-0x0:")
                return arguments == ["front"] ? invalid : nil
            }) == nil)
        }

        let root = ProcessIdentity(parent: 1, started: 110)
        let orphan = ProcessIdentity(parent: 1, started: 120)
        let detached = ProcessIdentity(parent: 220, started: 130)
        let unrelated = ProcessIdentity(parent: 1, started: 1)
        let session = RecordedSession(id: 200, root_pid: 200, process_group: 200, started_us: 100, ended_us: nil)
        var ownership = ProcessOwnership(sessions: [session])
        let current: [pid_t: ProcessIdentity] = [200: root, 220: orphan, 230: detached, 900: unrelated]
        precondition(ownership.update(current, groups: [200: [200: root, 220: orphan]], excluding: 999)
                     == [200, 220, 230])
        precondition(ownership.owns(220, orphan) && ownership.complete)
        precondition(!ownership.owns(900, ProcessIdentity(parent: 1, started: 150)))
        let reparented = ProcessIdentity(parent: 1, started: 130)
        precondition(ownership.update([230: reparented], groups: [200: [:]], excluding: 999) == [230])
        precondition(ownership.complete)
        ownership.sessions = [RecordedSession(id: 200, root_pid: 200, process_group: 200,
                                               started_us: 100, ended_us: 300)]
        let reused = ProcessIdentity(parent: 1, started: 400)
        precondition(ownership.update([220: reused], groups: [200: [220: reused]], excluding: 999).isEmpty)
        precondition(!ownership.owns(220, reused))

        var fallback = ProcessOwnership(sessions: [session])
        precondition(fallback.update([200: root, 230: ProcessIdentity(parent: 200, started: 130)],
                                     groups: [:], excluding: 999) == [200, 230])
        precondition(!fallback.complete)
        _ = fallback.update(current, groups: [200: [200: root]], excluding: 999)
        precondition(!fallback.complete && fallback.readGaps == 1)
        _ = fallback.update([:], groups: [:], excluding: 999)
        precondition(!fallback.complete && fallback.readGaps == 2 && fallback.processGroupReadGaps == 2)
        var neverEnumerated = ProcessOwnership(sessions: [session])
        _ = neverEnumerated.update(current, groups: [:], excluding: 999)
        precondition(!neverEnumerated.complete)

        func window(_ id: Int, _ pid: Int32, _ layer: Int = 0) -> [String: Any] {
            [kCGWindowNumber as String: id, kCGWindowOwnerPID as String: pid,
             kCGWindowLayer as String: layer, kCGWindowOwnerName as String: "app-\(pid)"]
        }
        let visible = window(1, 1000), minimized = window(2, 1001), otherSpace = window(3, 1002)
        let onScreen = desktopWindows([visible, window(4, 220)], processes: [:])!
        precondition(visibleTestWindows(onScreen, owned: [220]).count == 1)
        precondition(visibleTestWindows(onScreen, owned: [200]).isEmpty)
        // 프로세스 시작 시각을 읽지 못해도 앱 identity로 창 수를 비교한다.
        let initial = desktopWindows([visible, minimized, otherSpace, window(4, 220), window(5, 999)],
                                     processes: [220: orphan])!
        let latest = desktopWindows([visible, minimized, otherSpace], processes: [:])!
        precondition(unrelatedLosses(initial, latest, ownership: ownership, observer: 999).isEmpty)
        for remaining in [[visible, otherSpace], [visible, minimized]] {
            precondition(unrelatedLosses(initial, desktopWindows(remaining, processes: [:])!,
                                         ownership: ownership, observer: 999).map { $0.count } == [1])
        }
        let initialApps: Set<DesktopItem> = [
            DesktopItem(owner: 220, identity: orphan, application: "name:helper", window: nil, layer: nil),
            DesktopItem(owner: 1000, identity: unrelated, application: "name:editor", window: nil, layer: nil)]
        let unreadableApp: Set<DesktopItem> = [
            DesktopItem(owner: 1000, identity: nil, application: "name:editor", window: nil, layer: nil)]
        precondition(unrelatedLosses(initialApps, unreadableApp, ownership: ownership, observer: 999).isEmpty)
        precondition(unrelatedLosses(unreadableApp, initialApps, ownership: ownership, observer: 999).isEmpty)
        precondition(unrelatedLosses(initialApps, [], ownership: ownership, observer: 999).map { $0.count } == [1])
        precondition(unrelatedLosses([DesktopItem(owner: 220, identity: reused, application: "name:editor",
                                                 window: nil, layer: nil)], [],
                                     ownership: ownership, observer: 999).map { $0.count } == [1])
        precondition(desktopWindows([[:]], processes: [:]) == nil)
        var missingLayer = visible
        missingLayer.removeValue(forKey: kCGWindowLayer as String)
        precondition(desktopWindows([missingLayer], processes: [:]) == nil)
        var missingIdentity = visible
        missingIdentity.removeValue(forKey: kCGWindowOwnerName as String)
        precondition(desktopWindows([missingIdentity], processes: [:]) == nil)
        missingIdentity[kCGWindowLayer as String] = 25
        let anonymousMenu = desktopWindows([missingIdentity], processes: [:])!
        precondition(unrelatedLosses(anonymousMenu, [], ownership: ownership, observer: 999).isEmpty)
        precondition(visibleTestWindows(anonymousMenu, owned: [1000]).count == 1)

        let agent = applicationIdentity(bundleIdentifier: "test.exosphere", executablePath: "/agent",
                                        name: "엑소스피어")!
        precondition(agent == "bundle:test.exosphere")
        precondition(applicationIdentity(bundleIdentifier: " ", executablePath: "/agent", name: "UA")
                     == "executable:/agent")
        precondition(applicationIdentity(bundleIdentifier: nil, executablePath: "", name: "Exosphere UA")
                     == "name:Exosphere UA")
        precondition(applicationIdentity(bundleIdentifier: nil, executablePath: nil, name: " ") == nil)
        func app(_ pid: pid_t, _ identity: String) -> DesktopItem {
            DesktopItem(owner: pid, identity: ProcessIdentity(parent: 1, started: UInt64(pid)),
                        application: identity, window: nil, layer: nil)
        }
        func save(_ name: String, _ observation: Observation) throws {
            let data = try JSONSerialization.data(withJSONObject: observation.summary(), options: [.sortedKeys])
            try data.write(to: URL(fileURLWithPath: CommandLine.arguments[1]).deletingLastPathComponent()
                .appendingPathComponent("\(name).json"))
        }
        func observed(sessionsFile: String = "unused") -> Observation {
            let observation = Observation(sessionsFile: sessionsFile)
            observation.ownership.sessions = [session]
            _ = observation.ownership.update(current, groups: [200: [200: root, 220: orphan]], excluding: 999)
            observation.samples = 2
            observation.frontmost.sample(705)
            observation.frontmost.sample(705)
            return observation
        }
        func application(_ pid: pid_t, _ identity: String?, terminated: Bool = false) -> ApplicationSample {
            ApplicationSample(owner: pid, terminated: terminated, tracksLoss: true, readIdentity: { identity })
        }
        let present = application(3000, agent)
        let transient = observed()
        _ = transient.sampleApplications([present])
        _ = transient.sampleApplications([present, application(6000, nil), application(6001, "name:transient")])
        _ = transient.sampleApplications([present, application(6000, nil, terminated: true)])
        _ = transient.sampleApplications([present])
        try save("transient-unrelated", transient)

        let helper = observed()
        for terminated in [false, true] {
            if terminated {
                _ = helper.ownership.update([200: root], groups: [200: [200: root]], excluding: 999)
            }
            let applications = helper.sampleApplications([present,
                ApplicationSample(owner: 220, terminated: terminated, tracksLoss: true, readIdentity: {
                    precondition(!terminated, "종료 중인 자체 helper의 identity는 읽지 않는다")
                    return "bundle:test.helper"
                })])
            if !terminated {
                var anonymousOwnWindow = window(6, 220)
                anonymousOwnWindow.removeValue(forKey: kCGWindowOwnerName as String)
                let snapshot = desktopWindows([anonymousOwnWindow], processes: helper.currentProcesses,
                                               applications: applications)!
                precondition(visibleTestWindows(snapshot, owned: [220]).count == 1)
            }
        }
        try save("terminating-own-helper", helper)

        let unreadableLater = observed()
        _ = unreadableLater.sampleApplications([present])
        let retained = unreadableLater.sampleApplications([application(3000, nil)])
        precondition(retained[3000] == agent)
        try save("initial-identity-retained", unreadableLater)
        let downgraded = observed()
        _ = downgraded.sampleApplications([present])
        precondition(downgraded.sampleApplications([application(3000, "name:엑소스피어")])[3000] == agent)
        try save("initial-identity-downgraded", downgraded)
        let reusedPID = observed()
        _ = reusedPID.sampleApplications([present])
        _ = reusedPID.ownership.update(current.merging([3000: reused]) { _, replacement in replacement },
                                        groups: [200: [200: root, 220: orphan]], excluding: 999)
        _ = reusedPID.sampleApplications([application(3000, "bundle:other.app")])
        try save("initial-pid-reused", reusedPID)
        for (name, remaining) in [("initial-app-gone", [ApplicationSample]()),
                                   ("initial-app-terminated", [application(3000, nil, terminated: true)])] {
            let gone = observed()
            _ = gone.sampleApplications([present])
            _ = gone.sampleApplications(remaining)
            try save(name, gone)
        }
        let unreadableInitially = observed()
        _ = unreadableInitially.sampleApplications([application(6000, nil)])
        _ = unreadableInitially.sampleApplications([present])
        precondition(unreadableInitially.initialApps.map { $0.owner } == [6000])
        try save("initial-identity-unreadable", unreadableInitially)

        func record(_ name: String, apps: Set<DesktopItem>, latestApps: Set<DesktopItem>,
                    windows: Set<DesktopItem>, latestWindows: Set<DesktopItem>) throws {
            let observation = Observation(sessionsFile: "unused")
            observation.ownership = ownership
            observation.samples = 2
            observation.frontmost.sample(705)
            observation.frontmost.sample(705)
            observation.initialApps = apps
            observation.latestApps = latestApps
            observation.initialWindows = windows
            observation.latestWindows = latestWindows
            try save(name, observation)
        }
        // PID·시작 시각·창 번호가 바뀌어도 같은 앱 identity와 창 수가 남으면 손실이 아니다.
        let relaunchedBefore = desktopWindows([window(10, 3000), window(11, 3001)], processes: [:],
                                              applications: [3000: agent, 3001: agent])!
        let relaunchedAfter = desktopWindows([window(20, 4000), window(21, 4000)], processes: [:],
                                             applications: [4000: agent])!
        try record("relaunch", apps: [app(3000, agent), app(3001, agent)], latestApps: [app(4000, agent)],
                   windows: relaunchedBefore, latestWindows: relaunchedAfter)
        for identity in ["executable:/agent", "name:Exosphere UA"] {
            precondition(unrelatedLosses([app(3000, identity)], [app(4000, identity)],
                                         ownership: ownership, observer: 999).isEmpty)
        }

        // 제어 센터의 layer 25 창 교체·소실은 일반 창 손실에 포함하지 않는다.
        let menuBefore = desktopWindows([window(45854, 757, 25), window(45855, 757, 25),
                                         window(45856, 757, 25)], processes: [:])!
        let menuAfter = desktopWindows([window(45962, 757, 25), window(45963, 757, 25),
                                        window(45964, 757, 25)], processes: [:])!
        try record("menu-replacement", apps: [], latestApps: [], windows: menuBefore, latestWindows: menuAfter)
        precondition(unrelatedLosses(menuBefore, [], ownership: ownership, observer: 999).isEmpty)
        let otherLayers = desktopWindows([window(30, 757, -1), window(31, 757, 3), window(32, 757, 24)],
                                         processes: [:])!
        precondition(unrelatedLosses(otherLayers, [], ownership: ownership, observer: 999).isEmpty)

        // 다른 앱의 창이 늘거나 메뉴바 창이 남아도 해당 앱의 일반 창 감소는 실패한다.
        let fewerWindows = desktopWindows([window(22, 4000), window(23, 5000), window(24, 4000, 25)],
                                           processes: [:], applications: [4000: agent])!
        try record("normal-window-loss", apps: [app(3000, agent)], latestApps: [app(4000, agent)],
                   windows: relaunchedBefore.union(menuBefore), latestWindows: fewerWindows.union(menuAfter))
        try record("app-loss", apps: [app(3000, agent), app(3001, agent)], latestApps: [],
                   windows: relaunchedBefore, latestWindows: [])
        // 자체 실행 앱은 종료뿐 아니라 같은 identity의 대체 표본에서도 제외한다.
        precondition(unrelatedLosses([app(3000, "name:helper")],
            [DesktopItem(owner: 220, identity: orphan, application: "name:helper", window: nil, layer: nil)],
            ownership: ownership, observer: 999).map { $0.count } == [1])

        for count in [0, 1, 2, 20] {
            let observation = Observation(sessionsFile: "unused")
            observation.ownership = ownership
            observation.samples = count
            for _ in 0..<count { observation.frontmost.sample(705) }
            observation.maxGapMS = 5000
            let summary = observation.summary()
            precondition(summary["observation_complete"] as! Bool == (count >= 2))
            precondition(summary["sample_count"] as! Int == count)
        }
        for pids: [Int32?] in [[nil, nil], [705, nil, 705], [705], [705, 706, 705]] {
            let observation = Observation(sessionsFile: "unused")
            observation.ownership = ownership
            observation.samples = 3
            for pid in pids { observation.frontmost.sample(pid) }
            let summary = observation.summary()
            precondition(summary["frontmost_source"] as! String == "launchservices")
            precondition(summary["observation_complete"] as! Bool == (pids == [705, 706, 705]))
            precondition(summary["frontmost_changes"] as! Int == (pids == [705, 706, 705] ? 2 : 0))
        }
        let failedWindows = Observation(sessionsFile: "unused")
        failedWindows.ownership = ownership
        failedWindows.samples = 10
        failedWindows.incompleteReasons.insert("window enumeration unavailable")
        precondition(!(failedWindows.summary()["observation_complete"] as! Bool))
        let failedGroup = Observation(sessionsFile: "unused")
        failedGroup.ownership = neverEnumerated
        failedGroup.samples = 10
        precondition(!(failedGroup.summary()["observation_complete"] as! Bool))

        let file = URL(fileURLWithPath: CommandLine.arguments[1])
        let valid = #"{"schema_version":1,"sessions":[{"id":200,"root_pid":200,"process_group":200,"started_us":100,"ended_us":300}]}"#
        try Data(valid.utf8).write(to: file)
        let registry = try SessionRegistry.read(file.path)
        precondition(registry.sessions[0].root_pid == 200 && registry.sessions[0].process_group == 200)
        for (name, listFails, groupFails) in [("enumeration-empty", false, false),
                                             ("enumeration-list-failed", true, false),
                                             ("enumeration-group-failed", false, true),
                                             ("enumeration-both-failed", true, true)] {
            let enumeration = observed(sessionsFile: file.path)
            var selections: [pid_t?] = []
            precondition(enumeration.updateTree(query: { group in
                selections.append(group)
                return group == nil ? current : [200: root, 220: orphan]
            }) == [200, 220, 230])
            precondition(selections == [nil, 200])
            for _ in 0..<2 {
                _ = enumeration.updateTree(query: { group in
                    if group == nil { return listFails ? nil : [:] }
                    return groupFails ? nil : [:]
                })
            }
            precondition(enumeration.currentProcesses.isEmpty)
            // 성공한 후속 조회도 앞선 관측 공백을 지우지 않는다.
            _ = enumeration.updateTree(query: { group in group == nil ? current : [200: root, 220: orphan] })
            try save(name, enumeration)
        }
        let acknowledgement = Observation(sessionsFile: file.path)
        acknowledgement.ownership = ownership
        acknowledgement.acknowledgeSessions()
        precondition(FileManager.default.fileExists(atPath: file.deletingLastPathComponent()
            .appendingPathComponent("observer-session-200.ready").path))
        for invalid in [valid.replacingOccurrences(of: #""ended_us":300"#, with: #""ended_us":99"#),
                        valid.replacingOccurrences(of: #""root_pid":200"#, with: #""root_pid":201"#),
                        valid.replacingOccurrences(of: #""process_group":200"#, with: #""process_group":201"#),
                        #"{"schema_version":2,"sessions":[]}"#, "{}", "broken"] {
            try Data(invalid.utf8).write(to: file)
            do {
                _ = try SessionRegistry.read(file.path)
                preconditionFailure("손상된 세션 기록을 허용했다")
            } catch {}
        }

        var entry = kinfo_proc()
        entry.kp_proc.p_pid = 220
        entry.kp_eproc.e_ppid = 1
        entry.kp_proc.p_un.__p_starttime = timeval(tv_sec: 0, tv_usec: 120)
        let group = processes(group: 200, query: { mib in
            precondition(mib == [CTL_KERN, KERN_PROC, KERN_PROC_PGRP, 200])
            return [entry]
        })!
        precondition(group[220] == orphan)
        precondition(processes(query: { mib in
            precondition(mib == [CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0])
            return [entry]
        }) == group)
        precondition(processes(group: 200, query: { _ in nil }) == nil)
        precondition(processes(group: 200, query: { _ in [] })?.isEmpty == true)
    }
}
''')
            binary = directory / 'fixture'
            compile_result = subprocess.run(
                ['xcrun', 'swiftc', '-warnings-as-errors', '-parse-as-library',
                 '-D', 'YEE_BACKGROUND_OBSERVER_LIBRARY',
                 '-module-cache-path', str(directory / 'module-cache'),
                 str(ROOT / 'tools/dev/observe-background.swift'), str(main), '-o', str(binary)],
                capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
            result = subprocess.run([str(binary), str(directory / 'sessions.json')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for scenario, apps_lost, windows_lost in (
                    ('relaunch', 0, 0), ('menu-replacement', 0, 0),
                    ('normal-window-loss', 0, 1), ('app-loss', 1, 2)):
                with self.subTest(scenario=scenario):
                    summary = background_test.read_observation(directory / f'{scenario}.json')
                    self.assertTrue(summary['observation_complete'])
                    self.assertEqual(summary['unrelated_apps_lost'], apps_lost)
                    self.assertEqual(summary['unrelated_windows_lost'], windows_lost)
                    self.assertEqual(summary['passed'], apps_lost == windows_lost == 0)
                    self.assertEqual(summary['unrelated_apps_lost_details'],
                                     [dict(identity='bundle:test.exosphere', before=1, after=0, lost=1)]
                                     if apps_lost else [])
                    self.assertEqual(summary['unrelated_windows_lost_details'],
                                     [dict(identity='bundle:test.exosphere', before=2,
                                           after=2 - windows_lost, lost=windows_lost)]
                                     if windows_lost else [])
            for scenario, unreadable in (
                    ('transient-unrelated', [6000]), ('terminating-own-helper', []),
                    ('initial-identity-retained', [3000]), ('initial-identity-downgraded', [])):
                with self.subTest(scenario=scenario):
                    summary = background_test.read_observation(directory / f'{scenario}.json')
                    self.assertTrue(summary['passed'])
                    self.assertTrue(summary['observation_complete'])
                    self.assertEqual(summary['incomplete_reasons'], [])
                    self.assertEqual(summary['unrelated_apps_lost'], 0)
                    self.assertEqual(summary['application_identity_unreadable_pids'], unreadable)
                    if scenario == 'transient-unrelated':
                        self.assertTrue(any('pid=6000 identity=unavailable' in note for note in summary['notes']))
                        self.assertTrue(any('pid=6001 identity=name:transient' in note for note in summary['notes']))
            for scenario in ('initial-app-gone', 'initial-app-terminated', 'initial-pid-reused'):
                with self.subTest(scenario=scenario):
                    summary = background_test.read_observation(directory / f'{scenario}.json')
                    self.assertFalse(summary['passed'])
                    self.assertTrue(summary['observation_complete'])
                    self.assertEqual(summary['unrelated_apps_lost'], 1)
                    self.assertEqual(summary['unrelated_apps_lost_details'],
                                     [dict(identity='bundle:test.exosphere', before=1, after=0, lost=1)])
            summary = background_test.read_observation(directory / 'initial-identity-unreadable.json')
            self.assertFalse(summary['passed'])
            self.assertFalse(summary['observation_complete'])
            self.assertIn('initial application identity unavailable', summary['incomplete_reasons'])
            self.assertEqual(summary['application_identity_unreadable_pids'], [6000])
            for scenario, list_gaps, group_gaps in (
                    ('enumeration-empty', 0, 0), ('enumeration-list-failed', 2, 0),
                    ('enumeration-group-failed', 0, 2), ('enumeration-both-failed', 2, 2)):
                with self.subTest(scenario=scenario):
                    summary = background_test.read_observation(directory / f'{scenario}.json')
                    self.assertEqual(summary['passed'], list_gaps == group_gaps == 0)
                    self.assertEqual(summary['observation_complete'], list_gaps == group_gaps == 0)
                    self.assertEqual(summary['ownership_complete'], list_gaps == group_gaps == 0)
                    self.assertEqual(summary['ownership_read_gaps'], max(list_gaps, group_gaps))
                    self.assertEqual(summary['process_list_read_gaps'], list_gaps)
                    self.assertEqual(summary['process_group_read_gaps'], group_gaps)
                    self.assertEqual('process list enumeration unavailable' in summary['incomplete_reasons'],
                                     list_gaps > 0)
                    self.assertEqual('test process group enumeration unavailable' in summary['incomplete_reasons'],
                                     group_gaps > 0)


if __name__ == '__main__':
    unittest.main()
