"""사례별 manifest로 기존 GTest 필터를 실행 tier별로 나눈다."""

import argparse
import json
from pathlib import Path
import re

from native_test_discovery import matches_pattern


def load_manifest(path):
    def unique_entries(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('중복 tier 항목: ' + key)
            result[key] = value
        return result

    manifest = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique_entries)
    for key, entry in manifest.items():
        if not re.fullmatch(r'[\w/.-]+::\w+\.\w+', key):
            raise ValueError('정확한 소스::Suite.Case가 필요하다: ' + key)
        if entry['tier'] not in ('A', 'B', 'C'):
            raise ValueError('잘못된 tier: ' + key)
        if (not entry['os'] or not set(entry['os']) <= {'Darwin', 'Linux', 'Windows'}
                or not entry['owner'].strip() or not entry['reason'].strip()):
            raise ValueError('OS·owner·근거가 필요하다: ' + key)
        if not entry['binaries'] or not all(re.fullmatch(r'\w+', binary) for binary in entry['binaries']):
            raise ValueError('실행 바이너리 이름이 필요하다: ' + key)
        needs_display = entry.get('needs_display', {})
        if (not isinstance(needs_display, dict) or not set(needs_display) <= set(entry['os'])
                or any(not isinstance(reason, str) or not reason.strip()
                       for reason in needs_display.values())
                or (needs_display and entry['tier'] != 'B')):
            raise ValueError('B의 OS별 needs_display 근거가 필요하다: ' + key)
        exclusions = entry.get('parameter_exclusions', {})
        if not isinstance(exclusions, dict) or not set(exclusions) <= set(entry['os']):
            raise ValueError('매개변수 제외는 선언의 OS에 속해야 한다: ' + key)
        if exclusions:
            parameter_ids = {f'{prefix}/{suffix}'
                             for prefix, suffixes in entry.get('parameters', ())
                             for suffix in suffixes or ()}
            for excluded in exclusions.values():
                if (not isinstance(excluded, list) or not excluded
                        or any(not isinstance(value, str) for value in excluded)
                        or not set(excluded) <= parameter_ids or len(set(excluded)) != len(excluded)):
                    raise ValueError('정확한 매개변수 prefix/suffix가 필요하다: ' + key)
    return manifest


def case_names(key, entry, platform=None):
    name = key.split('::')[1]
    if 'parameters' not in entry:
        return [name]
    excluded = entry.get('parameter_exclusions', {}).get(platform, ())
    return [f'{prefix}/{name}/{suffix}'
            for prefix, suffixes in entry['parameters']
            for suffix in (suffixes if suffixes is not None else ['*'])
            if f'{prefix}/{suffix}' not in excluded]


def resolve_filter(manifest, pattern, platform, tier, binary, *, preserve_all=True):
    positive, _, negative = pattern.partition('-')
    positives = (positive or '*').split(':')
    negatives = [part for part in negative.split(':') if part]
    names, tiers, matched_patterns = set(), set(), set()
    for key, entry in manifest.items():
        if platform not in entry['os'] or binary not in entry['binaries']:
            continue
        for name in case_names(key, entry, platform):
            matches = {part for part in positives if matches_pattern(name, part)}
            if matches and not any(matches_pattern(name, part) for part in negatives):
                matched_patterns.update(matches)
                foreground = entry['tier'] == 'C' or platform in entry.get('needs_display', {})
                if tier == 'background' and foreground:
                    continue
                if tier == 'foreground' and not foreground:
                    continue
                names.add(name)
                tiers.add(entry['tier'])
    missing = set(positives) - matched_patterns
    if missing:
        raise ValueError('manifest에서 선택하지 못한 양수 패턴: ' + ':'.join(sorted(missing)))
    if not names:
        return '', ''
    # suffix가 '*'이면 일부 인스턴스만 제외할 수 있으므로 음수 패턴을 유지한다.
    resolved = ':'.join(sorted(names))
    if negative:
        resolved += '-' + negative
    # all에서는 기존 양수 패턴별 native 발견 검사도 그대로 유지한다.
    return (pattern if tier == 'all' and preserve_all else resolved), ''.join(sorted(tiers))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--platform', choices=('Darwin', 'Linux', 'Windows'), required=True)
    parser.add_argument('--tier', choices=('background', 'foreground', 'all'), required=True)
    parser.add_argument('--filter', required=True)
    args = parser.parse_args()
    try:
        pattern, tiers = resolve_filter(load_manifest(args.manifest), args.filter, args.platform, args.tier, args.binary)
    except (KeyError, TypeError, ValueError) as error:
        parser.error(str(error))
    print(tiers or 'none')
    print(pattern)


if __name__ == '__main__':
    main()
