#!/usr/bin/env python3
"""Regenerate 0001 including new glue files, without staging Chromium files."""

import pathlib
from lib.overlay_tools import load_catalog, shell_exclusions
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / '.local-build/chromium/src'
PATCH = ROOT / 'patches/0001-integrate-yee-shell.patch'
NEW_GLUE = load_catalog()['new_glue']


def git(*args, accepted=(0,)):
    result = subprocess.run(['git', *args], cwd=SRC, capture_output=True)
    if result.returncode not in accepted:
        raise RuntimeError(result.stderr.decode())
    return result.stdout


def main():
    overlay_root = ROOT
    catalog = load_catalog(overlay_root)
    patch = git('diff', 'HEAD', '--', '.', *shell_exclusions(catalog, overlay_root))
    tracked = set(git('ls-files', '-z').decode().split('\0'))
    for path in NEW_GLUE:
        if path not in tracked:
            if not (SRC / path).is_file():
                raise RuntimeError(f'Missing required glue: {path}')
            patch += git('diff', '--no-index', '--', '/dev/null', path,
                         accepted=(1,))
    # Validate before replacing the repository artifact.
    subprocess.run(['git', 'apply', '--reverse', '--check', '-'],
                   cwd=SRC, input=patch, check=True)
    PATCH.write_bytes(patch)
    print(f'Validated {len(patch)} bytes -> {PATCH.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
