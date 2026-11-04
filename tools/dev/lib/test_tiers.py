"""사례별 manifest로 기존 GTest 필터를 실행 tier별로 나눈다."""

import argparse
import fnmatch
import json
from pathlib import Path
import re


def load_manifest(path):
    def unique_entries(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('중복 tier 항목: ' + key)
            result[key] = value
        return result

    manifest = json.loads(Path(path).read_text(), object_pairs_hook=unique_entries)
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
    return manifest


def case_names(key, entry):
    name = key.split('::')[1]
    if 'parameters' not in entry:
        return [name]
    return [f'{prefix}/{name}/{suffix}'
            for prefix, suffixes in entry['parameters']
            for suffix in (suffixes if suffixes is not None else ['*'])]


def resolve_filter(manifest, pattern, platform, tier, binary):
    positive, _, negative = pattern.partition('-')
    positives = (positive or '*').split(':')
    negatives = [part for part in negative.split(':') if part]
    names, tiers, matched_patterns = set(), set(), set()
    for key, entry in manifest.items():
        if platform not in entry['os'] or binary not in entry['binaries']:
            continue
        for name in case_names(key, entry):
            matches = {part for part in positives if fnmatch.fnmatchcase(name, part)}
            if matches and not any(fnmatch.fnmatchcase(name, part) for part in negatives):
                matched_patterns.update(matches)
                if tier == 'background' and entry['tier'] == 'C':
                    continue
                if tier == 'foreground' and entry['tier'] != 'C':
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
    return (pattern if tier == 'all' else resolved), ''.join(sorted(tiers))


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
