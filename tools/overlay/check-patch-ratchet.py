#!/usr/bin/env python3
"""Chromium 연결 패치의 결합 지표를 검사하고 충돌 파일의 관심사를 분류한다."""

import argparse
import sys

sys.dont_write_bytecode = True
from lib.overlay_tools import REPO_ROOT
from lib.patch_ratchet import RATCHET_FILE, classify_paths, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--update', action='store_const', const='update', dest='mode',
                      help='줄어든 지표만 기준선에 반영한다. 늘어난 값은 반영하지 않는다.')
    mode.add_argument('--init', action='store_const', const='init', dest='mode',
                      help=f'{RATCHET_FILE}에 없는 패치 역할의 현재 값을 처음 기록한다.')
    mode.add_argument('--classify', nargs='*', metavar='PATH',
                      help='충돌 경로의 관심사를 출력한다. 경로 인자가 없으면 stdin에서 한 줄씩 읽는다.')
    parser.add_argument('--role', default='shell',
                        help='build/overlay.json의 패치 역할 (기본: shell)')
    args = parser.parse_args()
    if args.classify is not None and args.role != 'shell':
        parser.error('--classify는 shell 패치의 관심사 지도를 사용한다.')
    try:
        if args.classify is not None:
            return classify_paths(REPO_ROOT, args.classify or sys.stdin)
        return run(REPO_ROOT, role=args.role, mode=args.mode or 'check')
    except (OSError, ValueError) as error:
        # 입력 파일·설정 오류는 지표 위반(1)과 구분해 2로 끝낸다.
        print(f'패치 래칫 입력 오류: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
