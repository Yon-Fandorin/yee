#!/usr/bin/env python3
"""Seed only a NEW private test profile; never rewrite a running/existing profile."""
import argparse
import json
import os
from pathlib import Path
import re
import stat


def prepare(bridge, width):
    if type(width) is not int or not 126 <= width <= 400:
        raise ValueError('sidebar width must be within native bounds 126..400')
    if not re.fullmatch(r'/private/tmp/yee-agent\.[A-Za-z0-9]+', str(bridge)):
        raise ValueError('expected explicit private test bridge')
    info = bridge.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError('bridge must be owned, private and non-symlink')
    profile = bridge / 'profile'
    profile.mkdir(mode=0o700, exist_ok=False)
    default = profile / 'Default'
    default.mkdir(mode=0o700)
    fd = os.open(default / 'Preferences', os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump({'vertical_tabs': {'uncollapsed_width': width}}, stream)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bridge', type=Path)
    parser.add_argument('width', type=int)
    args = parser.parse_args()
    prepare(args.bridge, args.width)
