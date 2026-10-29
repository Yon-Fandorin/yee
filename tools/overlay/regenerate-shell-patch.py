#!/usr/bin/env python3
"""Regenerate 0001 including new glue files, without staging Chromium files."""

import argparse
import pathlib
from lib.overlay_tools import chromium_path, load_catalog, shell_exclusions
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PATCH = ROOT / 'patches/0001-integrate-yee-shell.patch'


def git(src, *args, accepted=(0,)):
    result = subprocess.run(['git', *args], cwd=src, capture_output=True)
    if result.returncode not in accepted:
        raise RuntimeError(result.stderr.decode())
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chromium-src', type=chromium_path,
                        default=str(ROOT / '.local-build/chromium/src'),
                        help='패치를 이식한 Chromium 원본의 절대 경로')
    src = parser.parse_args().chromium_src
    overlay_root = ROOT
    catalog = load_catalog(overlay_root)
    patch = git(src, 'diff', 'HEAD', '--ignore-submodules=all', '--', '.',
                *shell_exclusions(catalog, overlay_root))
    tracked = set(git(src, 'ls-files', '-z').decode().split('\0'))
    for path in catalog['new_glue']:
        if path not in tracked:
            if not (src / path).is_file():
                raise RuntimeError(f'Missing required glue: {path}')
            patch += git(src, 'diff', '--no-index', '--', '/dev/null', path,
                         accepted=(1,))
    # Validate before replacing the repository artifact.
    subprocess.run(['git', 'apply', '--reverse', '--check', '-'],
                   cwd=src, input=patch, check=True)
    PATCH.write_bytes(patch)
    print(f'Validated {len(patch)} bytes -> {PATCH.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
