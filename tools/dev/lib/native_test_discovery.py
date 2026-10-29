#!/usr/bin/env python3
"""GTest 목록에서 각 양수 필터가 실행 가능한 사례를 선택하는지 확인한다."""

import argparse
import fnmatch
import json
from pathlib import Path
import re
import sys


def parse_test_listing(listing):
    """일반·타입·값 매개변수 목록을 읽고 GTest의 뒤쪽 설명을 제외한다."""
    tests = set()
    suite = None
    for line in listing.splitlines():
        name = line.partition('#')[0].rstrip()
        if not name:
            continue
        suite_match = re.fullmatch(r'(\S+)\.', name)
        case_match = re.fullmatch(r'  (\S+)', name)
        if suite_match:
            suite = suite_match.group(1)
        elif suite and case_match:
            tests.add(f'{suite}.{case_match.group(1)}')
        else:
            suite = None
    return tests


def matches_pattern(name, pattern):
    """GTest의 *·?만 와일드카드로 해석하고 OS와 무관하게 대소문자를 구분한다."""
    return fnmatch.fnmatchcase(name, pattern.replace('[', '[[]'))


def is_disabled_test(name):
    """GTest와 같이 스위트와 사례 각각에 DISABLED_*·*/DISABLED_*를 적용한다."""
    return any(matches_pattern(part, pattern)
               for part in name.split('.', 1)
               for pattern in ('DISABLED_*', '*/DISABLED_*'))


def check_test_listing(listing, gtest_filter):
    """음수·비활성 사례를 제외한 뒤 모든 양수 패턴의 발견 수를 보고한다."""
    positive, separator, negative = gtest_filter.partition('-')
    positive_patterns = (positive or '*').split(':') if separator else positive.split(':')
    negative_patterns = negative.split(':') if separator else []
    tests = parse_test_listing(listing)
    active_tests = {name for name in tests if not is_disabled_test(name)}
    eligible_tests = {name for name in active_tests
                      if not any(matches_pattern(name, pattern) for pattern in negative_patterns)}
    pattern_counts = {pattern: sum(matches_pattern(name, pattern) for name in eligible_tests)
                      for pattern in positive_patterns}
    missing_patterns = [pattern for pattern, count in pattern_counts.items() if not count]
    selected_tests = {name for name in eligible_tests
                      if any(matches_pattern(name, pattern) for pattern in positive_patterns)}
    return {
        'ok': bool(selected_tests) and not missing_patterns,
        'listed_tests': len(tests),
        'active_tests': len(active_tests),
        'selected_tests': len(selected_tests),
        'pattern_counts': pattern_counts,
        'missing_patterns': missing_patterns,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('listing', nargs='?', default='-', help='목록 파일, 생략하거나 -이면 표준 입력')
    parser.add_argument('--filter', required=True, help='바이너리에 전달한 것과 같은 GTest 필터')
    args = parser.parse_args()
    try:
        listing = (sys.stdin.buffer.read().decode('utf-8-sig') if args.listing == '-'
                   else Path(args.listing).read_text(encoding='utf-8-sig'))
    except (OSError, UnicodeError) as error:
        print(json.dumps({'ok': False, 'error': str(error)}, ensure_ascii=True))
        return 2
    report = check_test_listing(listing, args.filter)
    print(json.dumps(report, ensure_ascii=True, separators=(',', ':')))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
