#!/usr/bin/env python3
"""Find current and previously named browser bundles in one build output.

Read-only: never launches or signals processes. --running reads process inventory.
The bundle identity is intentionally independent of the display name.
"""

import argparse
import json
from pathlib import Path
import plistlib
import re
import subprocess


CURRENT_BUNDLE_ID = "org.chromium.Chromium"


def browser_processes(process_list, bundle_id=CURRENT_BUNDLE_ID):
    matches = []
    for line in process_list.splitlines():
        match = re.fullmatch(r'\s*(\d+)\s+(/.+\.app/Contents/MacOS/[^/]+)\s*', line)
        if not match:
            continue
        binary = Path(match.group(2).rstrip()).resolve()
        bundle = binary.parents[2]
        if str(binary) in browser_executables(bundle.parent, bundle_id):
            matches.append({'pid': int(match.group(1)), 'executable': str(binary)})
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, nargs='?')
    parser.add_argument('--running', action='store_true')
    args = parser.parse_args()
    if args.running:
        listing = subprocess.run(['ps', '-axww', '-o', 'pid=,comm='], check=True,
                                 capture_output=True, text=True).stdout
        print(json.dumps(browser_processes(listing)))
        return
    if args.output is None:
        parser.error('output is required unless --running is used')
    for binary in browser_executables(args.output):
        print(binary)


if __name__ == "__main__":
    main()
