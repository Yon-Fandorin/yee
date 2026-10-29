#!/usr/bin/env python3
"""제품 배포 버전을 검증하고 Chromium의 공통 버전 입력에 적용한다."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile


VERSION_PATH = Path(__file__).resolve().parents[2] / "branding/VERSION"
VERSION_KEYS = ("MAJOR", "MINOR", "BUILD", "PATCH")


def parse_version(text):
    lines = text.splitlines()
    if len(lines) != len(VERSION_KEYS):
        raise ValueError("VERSION requires MAJOR, MINOR, BUILD, PATCH in order")
    parts = []
    for key, line in zip(VERSION_KEYS, lines):
        match = re.fullmatch(key + r"=(0|[1-9][0-9]{0,4})", line)
        if not match or int(match[1]) > 65535:
            raise ValueError(f"Invalid {key}: expected an integer from 0 to 65535")
        parts.append(int(match[1]))
    return tuple(parts)


def version_string(parts):
    return ".".join(str(part) for part in parts)


def validate_engine_version(version, chromium_version):
    """제품 버전과 원본 엔진의 호환 범위를 확인한다."""
    if version[:3] != chromium_version[:3] or version < chromium_version:
        raise ValueError("branding/VERSION must match the Chromium major/minor/build "
                         "and must not be older than its patch version")


def version_inputs(chromium_src, version_path=VERSION_PATH):
    src = Path(chromium_src)
    if not src.is_absolute() or not (src / "chrome/VERSION").is_file():
        raise ValueError("Expected an absolute Chromium source directory")
    version = parse_version(Path(version_path).read_text(encoding="utf-8"))
    # 적용된 제품 버전이 아닌 checkout 원본과 비교해 반복 적용과 엔진 갱신을 구분한다.
    result = subprocess.run(
        ["git", "-c", f"safe.directory={src.as_posix()}", "-C", str(src),
         "show", "HEAD:chrome/VERSION"], capture_output=True, text=True, check=True)
    chromium_version = parse_version(result.stdout)
    validate_engine_version(version, chromium_version)
    return version, chromium_version


def install_version(chromium_src, version, check_only=False):
    src = Path(chromium_src)
    output = src / "chrome/VERSION"
    for ancestor in (output, output.parent, src):
        attributes = ancestor.lstat()
        if ancestor.is_symlink() or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Version output traverses a link: {ancestor}")
    content = "".join(f"{key}={part}\n" for key, part in zip(VERSION_KEYS, version)).encode("ascii")
    if output.read_bytes() == content:
        return
    if not check_only:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(content)
            temporary.replace(output)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    print(f"{'Would install' if check_only else 'Installed'}: chrome/VERSION ({version_string(version)})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("show", "install"))
    parser.add_argument("chromium_src", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        version, chromium_version = version_inputs(args.chromium_src)
        if args.command == "show":
            print(json.dumps({"version": version_string(version),
                              "chromium_version": version_string(chromium_version)}))
        else:
            install_version(args.chromium_src, version, args.check)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
