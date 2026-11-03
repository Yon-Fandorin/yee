#!/usr/bin/env python3
"""임시 번들·가짜 ps 목록으로 이름 변경 탐색과 종료 범위를 검사한다."""

from contextlib import redirect_stdout
import ctypes
import errno
import io
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/dev"))

from browser_bundle_executables import (browser_executables, browser_processes, CURRENT_BUNDLE_ID,
                                        main, parse_process_arguments, process_arguments)


class BundleExecutablesTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.output = self.root / "output"
        self.output.mkdir()

    def bundle(self, name, identity=CURRENT_BUNDLE_ID, executable=None, parent=None):
        path = (parent or self.output) / (name + ".app")
        (path / "Contents/MacOS").mkdir(parents=True)
        binary = path / "Contents/MacOS" / name
        binary.touch()
        (path / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": identity, "CFBundleExecutable": executable or name}))
        return binary

    def test_old_and_new_names_are_independent_of_current_config(self):
        old = self.bundle("Yee")
        new = self.bundle("O'Reilly & 새 이름")
        self.assertEqual(browser_executables(self.output), sorted(map(str, (old, new))))

    def test_excludes_other_id_outside_output_and_helper(self):
        self.bundle("Other", identity="com.other.browser")
        outside = self.bundle("Outside", parent=self.root)
        (self.output / "Outside.app").symlink_to(outside.parents[2])
        valid = self.bundle("Orbit")
        self.bundle("Orbit Helper", parent=valid.parents[2] / "Contents")
        self.assertEqual(browser_executables(self.output), [str(valid)])

    def test_rejects_executable_traversal_and_outside_symlink(self):
        self.bundle("Traversal", executable="../escape")
        binary = self.bundle("Link")
        binary.unlink()
        outside = self.root / "external-binary"
        outside.touch()
        binary.symlink_to(outside)
        self.assertEqual(browser_executables(self.output), [])

    def test_skips_invalid_plist_and_missing_binary(self):
        broken = self.bundle("Broken")
        (broken.parents[1] / "Info.plist").write_bytes(b"not a plist")
        self.bundle("Missing").unlink()
        self.assertEqual(browser_executables(self.output), [])

    @unittest.skipIf(sys.platform == 'win32', 'macOS ps의 POSIX 실행 경로를 검사한다.')
    def test_running_inventory_tracks_identity_across_names_and_build_roots(self):
        """기본 조회는 출력·이름과 무관한 개발 본체만 반환하며 설치판과 Helper를 제외한다."""
        old = self.bundle('Yee')
        new = self.bundle('새 이름', parent=self.root)
        other = self.bundle('Other', identity='com.other.browser')
        installed = self.bundle('Installed Yee', identity='io.github.yon-fandorin.yee')
        helper = self.bundle('Helper', identity=CURRENT_BUNDLE_ID + '.helper')
        listing = '\n'.join(f'{pid} {binary}' for pid, binary in enumerate([old, new, other, installed, helper], 100))
        listing += f'\n999 /usr/bin/python {old}\n'
        self.assertEqual(browser_processes(listing), [
            {'pid': 100, 'executable': str(old)}, {'pid': 101, 'executable': str(new)}])

    @unittest.skipIf(sys.platform == 'win32', 'macOS ps의 POSIX 실행 경로를 검사한다.')
    def test_details_include_helpers_without_a_running_main_and_exclude_installed_bundles(self):
        """개발 본체가 없어도 내부 Helper를 찾으며 설치판 본체·Helper는 포함하지 않는다."""
        selected = self.bundle('Orbit Preview (QA+)')
        other = self.bundle('이전 이름', parent=self.root)
        installed = self.bundle('Yee', identity='io.github.yon-fandorin.yee', parent=self.root)
        helpers = []
        for binary in (selected, other, installed):
            helper = (binary.parents[2] / 'Contents/Frameworks/Helpers/Orbit Helper (Renderer).app'
                      / 'Contents/MacOS/Orbit Helper (Renderer)')
            helper.parent.mkdir(parents=True)
            helper.touch()
            helpers.append(helper)
        crashpad = selected.parents[2] / 'Contents/Frameworks/Helpers/chrome_crashpad_handler'
        crashpad.touch()
        listing = '\n'.join(f'{pid} {binary}' for pid, binary in enumerate(
            [*helpers, installed, crashpad], 200))
        self.assertEqual(browser_processes(listing, include_helpers=True), [
            {'pid': 200, 'executable': str(helpers[0])},
            {'pid': 201, 'executable': str(helpers[1])},
            {'pid': 204, 'executable': str(crashpad)}])

    @unittest.skipIf(sys.platform == 'win32', 'macOS ps의 POSIX 실행 경로를 검사한다.')
    def test_cli_output_and_running_remain_compatible_and_details_add_arguments(self):
        """기존 CLI의 출력 형식을 보존하고 상세 조회에서만 Helper·정확한 argv를 반환한다."""
        binary = self.bundle('Yee')
        helper = binary.parents[2] / 'Contents/Helpers/chrome_crashpad_handler'
        helper.parent.mkdir()
        helper.touch()
        inventory = f'101 {binary}\n102 {helper}\n'
        process_args = {101: [str(binary), '--user-data-dir=/tmp/yee-review'],
                        102: [str(helper), '--monitor-self']}
        for arguments, expected, ps_calls, argument_calls in (
                ([str(self.output)], str(binary) + '\n', 0, 0),
                (['--running'], [{'pid': 101, 'executable': str(binary)}], 1, 0),
                (['--running', '--details'], [
                    {'pid': 101, 'executable': str(binary), 'arguments': process_args[101]},
                    {'pid': 102, 'executable': str(helper), 'arguments': process_args[102]}], 1, 2)):
            with (self.subTest(arguments=arguments), patch.object(sys, 'argv', ['bundles', *arguments]),
                  patch('browser_bundle_executables.subprocess.run', return_value=
                        subprocess.CompletedProcess([], 0, inventory)) as run,
                  patch('browser_bundle_executables.process_arguments', side_effect=process_args.get) as read_args,
                  redirect_stdout(io.StringIO()) as output):
                main()
                self.assertEqual(run.call_count, ps_calls)
                self.assertEqual(read_args.call_count, argument_calls)
                if ps_calls:
                    self.assertEqual(json.loads(output.getvalue()), expected)
                    self.assertEqual(run.call_args_list[0].args[0][-1], 'pid=,comm=')
                else:
                    self.assertEqual(output.getvalue(), expected)

    @unittest.skipIf(sys.platform == 'win32', 'macOS ps의 POSIX 실행 경로를 검사한다.')
    def test_details_skip_exited_pids_and_propagate_argument_read_errors(self):
        """끝난 PID만 생략하며 권한 오류는 명령줄 추정으로 대체하지 않고 전파한다."""
        binary = self.bundle('Yee')
        for error in (ProcessLookupError(errno.ESRCH, '종료됨'), PermissionError(errno.EPERM, '권한 없음')):
            with (self.subTest(error=error), patch.object(sys, 'argv', ['bundles', '--running', '--details']),
                  patch('browser_bundle_executables.subprocess.run', return_value=
                        subprocess.CompletedProcess([], 0, f'101 {binary}\n')),
                  patch('browser_bundle_executables.process_arguments', side_effect=error),
                  redirect_stdout(io.StringIO()) as output):
                if isinstance(error, ProcessLookupError):
                    main()
                    self.assertEqual(json.loads(output.getvalue()), [])
                else:
                    with self.assertRaises(PermissionError):
                        main()
                    self.assertEqual(output.getvalue(), '')


class ProcessArgumentsTest(unittest.TestCase):
    @staticmethod
    def kernel_data(arguments):
        return (len(arguments).to_bytes(4, sys.byteorder, signed=True) + b'/tmp/Yee\0\0\0'
                + b'\0'.join(argument.encode() for argument in arguments)
                + b'\0ENV=--user-data-dir=/tmp/profile\0')

    def test_kernel_argv_preserves_spaces_empty_arguments_and_excludes_environment(self):
        """커널의 NUL 인수 경계·공백·빈 인수를 보존하고 환경 변수는 읽지 않는다."""
        arguments = ['/tmp/Yee', '--user-data-dir=/tmp/profile archived', '', '--no-first-run']
        self.assertEqual(parse_process_arguments(self.kernel_data(arguments)), arguments)

    def test_malformed_kernel_buffers_are_rejected(self):
        """헤더·실행 경로·인수 종결이 누락되면 공백 문자열로 복구하지 않고 실패한다."""
        for data in (b'', b'123', (1).to_bytes(4, sys.byteorder) + b'no-null',
                     (2).to_bytes(4, sys.byteorder) + b'/tmp/Yee\0\0argv-without-null'):
            with self.subTest(data=data), self.assertRaises(ValueError):
                parse_process_arguments(data)

    def test_sysctl_reads_only_the_requested_pid_and_passes_kernel_errors(self):
        """가짜 sysctl로 읽기 전용 PID 조회·버퍼 해석·실패 전파를 확인한다."""
        data = self.kernel_data(['/tmp/Yee', '--user-data-dir', '/tmp/profile archived'])

        def sysctl(mib, length, buffer, size, new_value, new_length):
            self.assertEqual(list(mib), [1, 49, 123])
            self.assertEqual(length, 3)
            self.assertIsNone(new_value)
            self.assertEqual(new_length, 0)
            ctypes.memmove(buffer, data, len(data))
            size._obj.value = len(data)
            return 0

        with (patch('browser_bundle_executables.sys.platform', 'darwin'),
              patch('browser_bundle_executables.os.sysconf', return_value=1024),
              patch('browser_bundle_executables.ctypes.CDLL') as library):
            library.return_value.sysctl = sysctl
            self.assertEqual(process_arguments(123), ['/tmp/Yee', '--user-data-dir', '/tmp/profile archived'])
            library.assert_called_once_with(None, use_errno=True)
            library.return_value.sysctl = lambda *args: -1
            with patch('browser_bundle_executables.ctypes.get_errno', return_value=errno.EPERM):
                with self.assertRaises(PermissionError):
                    process_arguments(123)


if __name__ == "__main__":
    unittest.main()
