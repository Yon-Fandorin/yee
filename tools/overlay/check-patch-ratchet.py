#!/usr/bin/env python3
"""Chromium 연결 패치의 결합 지표가 기준선보다 늘지 않았는지 검사한다. 빌드는 실행하지 않는다."""

import argparse
import sys

sys.dont_write_bytecode = True
from lib.overlay_tools import REPO_ROOT
from lib.patch_ratchet import RATCHET_FILE, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--update', action='store_const', const='update', dest='mode',
                      help='줄어든 지표만 기준선에 반영한다. 늘어난 값은 반영하지 않는다.')
    mode.add_argument('--init', action='store_const', const='init', dest='mode',
                      help=f'{RATCHET_FILE}에 없는 패치 역할의 현재 값을 처음 기록한다.')
    parser.add_argument('--role', default='shell',
                        help='build/overlay.json의 패치 역할 (기본: shell)')
    args = parser.parse_args()
    try:
        return run(REPO_ROOT, role=args.role, mode=args.mode or 'check')
    except (OSError, ValueError) as error:
        # 입력 파일·설정 오류는 지표 위반(1)과 구분해 2로 끝낸다.
        print(f'패치 래칫 입력 오류: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
