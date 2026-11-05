"""백그라운드 검사 자식의 수명과 선택적인 macOS 관찰 기록을 관리한다."""

import errno
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import time


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from browser_bundle_executables import process_arguments


INTERFERENCE_COUNTERS = (
    'max_visible_test_windows', 'test_frontmost_samples',
    'modifier_injections', 'input_injections',
    'unrelated_apps_lost', 'unrelated_windows_lost',
)
ACTIVITY_COUNTERS = (
    'cursor_changes', 'modifier_changes', 'frontmost_changes',
    'non_regular_apps_lost', 'non_regular_windows_lost',
    'user_active_apps_lost', 'user_active_windows_lost',
)


def read_observation(path):
    """누락·표본 부족·손상된 관찰을 성공으로 해석하지 않는다."""
    summary = json.loads(Path(path).read_text())
    if (not isinstance(summary, dict) or type(summary.get('schema_version')) is not int
            or summary['schema_version'] != 1
            or any(type(summary.get(key)) is not int or summary[key] < 0
                   for key in (*INTERFERENCE_COUNTERS, *ACTIVITY_COUNTERS,
                               'sample_count', 'frontmost_sample_count',
                               'frontmost_read_gaps'))
            or type(summary.get('observation_complete')) is not bool):
        raise ValueError('잘못된 background observation JSON')
    summary['passed'] = (summary['observation_complete'] and summary['sample_count'] >= 2
                         and summary['frontmost_sample_count'] >= 2 and summary['frontmost_read_gaps'] == 0
                         and all(summary[key] == 0 for key in INTERFERENCE_COUNTERS))
    return summary


def process_session_snapshot(session, known=None):
    """전용 그룹·세션과 이미 확인한 PID의 자손만 추적하고 좀비는 제외한다."""
    listing = subprocess.check_output(
        ['ps', '-axww', '-o', 'pid=,ppid=,pgid=,stat=,lstart=,comm='], text=True,
        env={**os.environ, 'LC_ALL': 'C'})
    known = known or {}
    current = {}
    found = {}
    for line in listing.splitlines():
        parts = line.split(None, 9)
        if len(parts) < 4 or not all(part.isdigit() for part in parts[:3]):
            raise ValueError('프로세스 그룹 목록을 해석하지 못했다.')
        pid, parent, group = map(int, parts[:3])
        if parts[3].startswith('Z'):
            continue
        if len(parts) != 10:
            raise ValueError('프로세스 시작 시각·실행 파일을 읽지 못했다.')
        identity = (tuple(parts[4:9]), parts[9])
        current[pid] = (parent, identity)
        if pid in known and known[pid] != identity:
            continue
        if group == session or known.get(pid) == identity:
            found[pid] = identity
            continue
        try:
            if session > 0 and os.getsid(pid) == session:
                found[pid] = identity
        except OSError as error:
            if error.errno not in (errno.ESRCH, errno.EINVAL, errno.EPERM, errno.EACCES):
                raise
    if not listing.strip():
        raise ValueError('프로세스 그룹 목록이 비어 있다.')
    changed = True
    while changed:
        changed = False
        for pid, (parent, identity) in current.items():
            if pid not in found and parent in found and (pid not in known or known[pid] == identity):
                found[pid] = identity
                changed = True
    return found


def profile_is_owned(arguments, directory):
    """프로필을 명시한 자식은 이번 실행의 임시 디렉터리 안에 있어야 한다."""
    profile = None
    for argument in arguments[1:]:
        if argument == '--':
            break
        if argument.startswith('--user-data-dir='):
            profile = argument.partition('=')[2]
    return profile is None or (bool(profile) and Path(profile).resolve().is_relative_to(directory.resolve()))


def run_background_command(command, directory, interrupted, *, capture=False, observer=None):
    """격리 프로필·프로세스 세션으로 실행하고 검증한 자기 자식만 정리한다."""
    if interrupted():
        return -signal.SIGINT, '' if capture else None
    directory.mkdir()
    command = [*command, f'--user-data-dir={directory / "profile"}']
    environment = {**os.environ, 'TMPDIR': str(directory) + '/'}
    if sys.platform == 'darwin':
        environment['MAC_CHROMIUM_TMPDIR'] = environment['TMPDIR']
    process_session_snapshot(0)  # 조회 권한이 없으면 검사 프로세스를 시작하지 않는다.
    started_us = time.time_ns() // 1000
    launch_command = command
    release_fd = None
    launch_fd = None
    exec_read_fd = None
    exec_write_fd = None
    try:
        if observer:
            launch_fd, release_fd = os.pipe()
            exec_read_fd, exec_write_fd = os.pipe()
            launch_command = [sys.executable, str(Path(__file__).resolve()),
                              '--launch-background', str(launch_fd), str(exec_write_fd), *command]
        child = subprocess.Popen(launch_command, env=environment, start_new_session=True,
                                 pass_fds=(launch_fd, exec_write_fd) if observer else (),
                                 stdout=subprocess.PIPE if capture else None,
                                 text=True, encoding='utf-8-sig', errors='replace')
    except BaseException:
        for descriptor in (release_fd, exec_read_fd):
            if descriptor is not None:
                os.close(descriptor)
        raise
    finally:
        for descriptor in (launch_fd, exec_write_fd):
            if descriptor is not None:
                os.close(descriptor)
    identities = {}
    output = None
    cleanup_errors = []
    session_registered = False
    released = observer is None

    def remember():
        current = process_session_snapshot(child.pid, identities)
        verified = {}
        for pid, identity in current.items():
            if pid in identities and identities[pid] != identity:
                continue
            # 소유권은 전용 세션·시작 시각으로 확인한다. 프로필 인수는 추가 경계 검사다.
            identities[pid] = identity
            if sys.platform == 'darwin':
                # argv 경계를 커널에서 읽는다. ps의 command 문자열은 쓰지 않는다.
                try:
                    arguments = process_arguments(pid)
                    # argv를 읽는 동안 끝난 자식의 프로필은 더 이상 실행 경계를 침범하지 않는다.
                    if (not profile_is_owned(arguments, directory)
                            and process_session_snapshot(child.pid, identities).get(pid) == identity):
                        cleanup_errors.append(f'격리 프로필 밖의 자식: {pid}')
                except OSError as error:
                    if process_session_snapshot(child.pid, identities).get(pid) != identity:
                        continue
                    if error.errno not in (errno.EINVAL, errno.ESRCH):
                        cleanup_errors.append(f'검사 자식 argv 조회 실패: {pid}: {error}')
            verified[pid] = identity
        return verified

    try:
        if observer:
            session_registered = True
            observer.register_session(child.pid, child.pid, started_us)
            if not interrupted():
                os.write(release_fd, b'1')
                released = True
                # exec가 바꿀 실행 파일을 자식 신원으로 먼저 기록하지 않는다.
                os.read(exec_read_fd, 1)
            os.close(exec_read_fd)
            exec_read_fd = None
            os.close(release_fd)
            release_fd = None
        while True:
            remember()
            if interrupted():
                break
            try:
                output, _ = child.communicate(timeout=0.1)
                break
            except subprocess.TimeoutExpired:
                # 부모가 종료해도 자식이 stdout pipe를 유지할 수 있다.
                if child.poll() is not None:
                    break
    finally:
        for descriptor in (release_fd, exec_read_fd):
            if descriptor is not None:
                os.close(descriptor)
        # Chromium launcher의 정상 종료·timeout 정리 뒤 남은 전용 세션만 회수한다.
        try:
            for signum in (signal.SIGTERM, signal.SIGKILL):
                current = remember()
                for pid, identity in identities.items():
                    if current.get(pid) == identity:
                        try:
                            os.kill(pid, signum)
                        except ProcessLookupError:
                            pass
                deadline = time.monotonic() + 2
                while process_session_snapshot(child.pid, identities) and time.monotonic() < deadline:
                    child.poll()
                    time.sleep(0.1)
            remaining = process_session_snapshot(child.pid, identities)
            if remaining:
                cleanup_errors.append('남은 자식 PID: ' + ','.join(map(str, remaining)))
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            cleanup_errors.append(f'검사 자식 정리 목록 조회 실패: {error}')
        # 목록 조회 실패와 무관하게 직접 생성한 root는 반드시 종료·회수한다.
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        try:
            output, _ = child.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            if child.stdout:
                child.stdout.close()
            cleanup_errors.append('검사 자식의 종료·출력 정리를 확인하지 못했다.')
        if session_registered:
            observer.close_session(child.pid)
    if cleanup_errors:
        raise ValueError('; '.join(sorted(set(cleanup_errors))))
    shutil.rmtree(directory)
    return child.returncode if released else -signal.SIGINT, output


class BackgroundObserver:
    def __init__(self, source, build_root, directory):
        if sys.platform != 'darwin':
            raise ValueError('--observe-background는 macOS 전용이다.')
        cache = build_root / 'background-observer'
        cache.mkdir(exist_ok=True)
        binary = cache / 'observe-background'
        if not binary.exists() or source.stat().st_mtime_ns > binary.stat().st_mtime_ns:
            subprocess.run(['xcrun', 'swiftc', '-warnings-as-errors', '-parse-as-library',
                            '-module-cache-path', str(cache / 'module-cache'),
                            str(source), '-o', str(binary)], check=True)
        self.output = directory / 'observation.json'
        self.stop_file = directory / 'observer.stop'
        self.sessions_file = directory / 'observer-sessions.json'
        self.sessions = []
        self.save_sessions()
        ready = directory / 'observer.ready'
        self.child = subprocess.Popen([str(binary), f'--runner-pid={os.getpid()}',
                                       f'--sessions-file={self.sessions_file}',
                                       f'--output={self.output}', f'--ready-file={ready}',
                                       f'--stop-file={self.stop_file}'], start_new_session=True)
        try:
            self.wait_ready(ready)
        except BaseException:
            self.child.terminate()
            self.child.wait()
            raise

    def wait_ready(self, ready):
        deadline = time.monotonic() + 10
        while not ready.exists():
            if self.child.poll() is not None or time.monotonic() >= deadline:
                raise ValueError(f'background observer의 준비를 확인하지 못했다: {ready.name}')
            time.sleep(0.05)

    def save_sessions(self):
        temporary = self.sessions_file.with_suffix('.tmp')
        try:
            temporary.write_text(json.dumps({'schema_version': 1, 'sessions': self.sessions}))
            temporary.replace(self.sessions_file)
        finally:
            temporary.unlink(missing_ok=True)

    def register_session(self, root_pid, process_group, started_us):
        # Popen(start_new_session=True)의 setsid가 세션·process group을 PID와 같게 만든다.
        if root_pid <= 0 or process_group != root_pid:
            raise ValueError('검사 프로세스의 전용 process group이 필요하다.')
        self.sessions.append({'id': root_pid, 'root_pid': root_pid, 'process_group': process_group,
                              'started_us': started_us, 'ended_us': None})
        self.save_sessions()
        self.wait_ready(self.sessions_file.parent / f'observer-session-{root_pid}.ready')

    def close_session(self, pid):
        session = next(item for item in self.sessions if item['id'] == pid and item['ended_us'] is None)
        session['ended_us'] = time.time_ns() // 1000
        self.save_sessions()

    def finish(self):
        self.stop_file.touch()
        try:
            code = self.child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.child.terminate()
            self.child.wait()
            raise ValueError('background observer가 종료하지 않았다.')
        if code:
            raise ValueError(f'background observer 종료 코드: {code}')
        summary = read_observation(self.output)
        roots = sorted(session['root_pid'] for session in self.sessions)
        groups = sorted({session['process_group'] for session in self.sessions})
        if self.sessions and (summary.get('observed_test_root_pids') != roots
                              or summary.get('observed_test_process_groups') != groups):
            summary['observation_complete'] = summary['passed'] = False
            summary.setdefault('incomplete_reasons', []).append('test process group acknowledgement missing')
        return summary


if __name__ == '__main__':
    # 관찰기가 전용 그룹을 확인할 때까지 exec를 미뤄 짧은 목록 조회도 놓치지 않는다.
    if len(sys.argv) < 5 or sys.argv[1] != '--launch-background':
        sys.exit(2)
    descriptor = int(sys.argv[2])
    released = os.read(descriptor, 1)
    os.close(descriptor)
    if released != b'1':
        sys.exit(1)
    os.set_inheritable(int(sys.argv[3]), False)
    os.execvp(sys.argv[4], sys.argv[4:])
