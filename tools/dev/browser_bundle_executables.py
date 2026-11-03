#!/usr/bin/env python3
"""빌드 출력에서 현재·이전 이름의 브라우저 번들을 찾는다.

실행·종료 없이 번들 ID로 식별한다. --running은 프로세스 목록만 읽는다.
--details를 함께 쓰면 Helper와 실제 프로세스 인수를 포함한다.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys


CURRENT_BUNDLE_ID = "org.chromium.Chromium"


def browser_processes(process_list, bundle_id=CURRENT_BUNDLE_ID, *, include_helpers=False):
    matches = []
    pattern = (r'\s*(\d+)\s+(/.+)\s*' if include_helpers
               else r'\s*(\d+)\s+(/.+\.app/Contents/MacOS/[^/]+)\s*')
    outputs = {}
    for line in process_list.splitlines():
        match = re.fullmatch(pattern, line)
        if not match:
            continue
        binary = Path(match.group(2).rstrip()).resolve()
        if not binary.is_file():
            continue
        for bundle in binary.parents:
            if bundle.suffix != '.app' or not binary.is_relative_to(bundle / 'Contents'):
                continue
            if bundle.parent not in outputs:
                outputs[bundle.parent] = browser_executables(bundle.parent, bundle_id)
            executables = outputs[bundle.parent]
            if str(binary) in executables or (include_helpers and any(
                    Path(executable).parents[2] == bundle for executable in executables)):
                matches.append({'pid': int(match.group(1)), 'executable': str(binary)})
                break
    return matches


def browser_executables(output, bundle_id=CURRENT_BUNDLE_ID):
    root = Path(output).resolve()
    executables = set()
    for candidate in root.glob("*.app"):
        bundle = candidate.resolve()
        if bundle.parent != root or any(ord(char) < 32 for char in str(bundle)):
            continue
        try:
            info = plistlib.loads((bundle / "Contents/Info.plist").read_bytes())
        except (OSError, ValueError, plistlib.InvalidFileException):
            continue
        if not isinstance(info, dict):
            continue
        name = info.get("CFBundleExecutable")
        if (info.get("CFBundleIdentifier") != bundle_id or not isinstance(name, str)
                or not name or name in (".", "..") or "/" in name
                or any(ord(char) < 32 for char in name)):
            continue
        binary = (bundle / "Contents/MacOS" / name).resolve()
        if binary.parent == bundle / "Contents/MacOS" and binary.is_file():
            executables.add(str(binary))
    return sorted(executables)


def parse_process_arguments(data):
    """KERN_PROCARGS2의 인수 개수·실행 경로·NUL 구분 인수를 읽고 환경 변수는 제외한다."""
    if len(data) < 4:
        raise ValueError('프로세스 인수 헤더가 없다')
    count = int.from_bytes(data[:4], sys.byteorder, signed=True)
    start = data.index(b'\0', 4) + 1
    arguments = data[start:].lstrip(b'\0').split(b'\0')
    if count < 1 or len(arguments) <= count:
        raise ValueError('프로세스 인수 개수와 NUL 경계가 맞지 않는다')
    return [os.fsdecode(argument) for argument in arguments[:count]]


def process_arguments(pid):
    """ps가 공백으로 합친 문자열 대신 macOS 커널에서 정확한 argv 경계를 읽는다."""
    if sys.platform != 'darwin':
        raise OSError('--details의 프로세스 인수 조회는 macOS용이다')
    library = ctypes.CDLL(None, use_errno=True)
    sysctl = library.sysctl
    sysctl.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint, ctypes.c_void_p,
                      ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p, ctypes.c_size_t]
    sysctl.restype = ctypes.c_int
    # CTL_KERN, KERN_PROCARGS2, PID. 새 값을 쓰지 않는 읽기 전용 조회다.
    mib = (ctypes.c_int * 3)(1, 49, pid)
    size = ctypes.c_size_t(os.sysconf('SC_ARG_MAX'))
    buffer = ctypes.create_string_buffer(size.value)
    if sysctl(mib, len(mib), buffer, ctypes.byref(size), None, 0) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), f'PID {pid}')
    return parse_process_arguments(buffer.raw[:size.value])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, nargs='?')
    parser.add_argument('--running', action='store_true')
    parser.add_argument('--details', action='store_true', help='--running 목록에 Helper와 정확한 argv를 포함한다')
    args = parser.parse_args()
    if args.details and not args.running:
        parser.error('--details는 --running과 함께 사용해야 한다')
    if args.running:
        listing = subprocess.run(['ps', '-axww', '-o', 'pid=,comm='], check=True,
                                 capture_output=True, text=True).stdout
        processes = browser_processes(listing, include_helpers=args.details)
        if args.details:
            detailed = []
            for process in processes:
                try:
                    process['arguments'] = process_arguments(process['pid'])
                except ProcessLookupError:
                    continue
                detailed.append(process)
            processes = detailed
        print(json.dumps(processes))
        return
    if args.output is None:
        parser.error('output is required unless --running is used')
    for binary in browser_executables(args.output):
        print(binary)


if __name__ == "__main__":
    main()
