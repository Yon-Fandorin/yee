#!/usr/bin/env python3
"""공통 Chromium 원본을 검사하고 플랫폼별 새 Stable을 조회한다."""

import sys
sys.dont_write_bytecode = True

import argparse
import json
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
from product_version import VERSION_KEYS, parse_version, validate_engine_version

MANIFEST = ROOT / 'build/chromium-version.json'
PRODUCT_VERSION = ROOT / 'branding/VERSION'
PLATFORMS = {'windows': 'Windows', 'macos': 'Mac'}
SOURCE_URL = 'https://chromium.googlesource.com/chromium/src.git'
RELEASES_URL = 'https://chromiumdash.appspot.com/fetch_releases'


def version_parts(value):
    if not isinstance(value, str) or len(value.split('.')) != 4:
        raise ValueError('Chromium 버전은 네 정수로 구성해야 한다.')
    return parse_version(''.join(f'{key}={part}\n' for key, part in zip(VERSION_KEYS, value.split('.'))))


def validate_record(record):
    if not isinstance(record, dict) or set(record) != {'version', 'revision'}:
        raise ValueError('원본 기준에는 version과 revision만 지정한다.')
    version_parts(record['version'])
    if not isinstance(record['revision'], str) or not re.fullmatch('[0-9a-f]{40}', record['revision']):
        raise ValueError('Chromium revision은 40자리 원본 커밋이어야 한다.')
    return record


def load_manifest(filename=MANIFEST, product_version=PRODUCT_VERSION):
    manifest = json.loads(Path(filename).read_text(encoding='utf-8'))
    if (not isinstance(manifest, dict) or
            set(manifest) != {'schema_version', 'channel', 'source_url', 'version', 'revision'} or
            type(manifest['schema_version']) is not int or manifest['schema_version'] != 1 or
            manifest['channel'] != 'Stable' or manifest['source_url'] != SOURCE_URL):
        raise ValueError('지원하지 않는 Chromium 기준 설정이다.')
    pinned = validate_record({key: manifest[key] for key in ('version', 'revision')})
    product = parse_version(Path(product_version).read_text(encoding='utf-8'))
    validate_engine_version(product, version_parts(pinned['version']))
    return manifest


def default_platform():
    if sys.platform == 'win32':
        return 'windows'
    if sys.platform == 'darwin':
        return 'macos'
    raise ValueError('이 호스트에서는 --platform windows 또는 macos를 지정한다.')


def latest_stable(platform):
    query = urllib.parse.urlencode({'channel': 'Stable', 'platform': PLATFORMS[platform], 'num': 1})
    request = urllib.request.Request(f'{RELEASES_URL}?{query}', headers={'Accept': 'application/json'})
    with urllib.request.urlopen(request, timeout=20) as response:
        records = json.load(response)
    if (not isinstance(records, list) or len(records) != 1 or not isinstance(records[0], dict) or
            records[0].get('channel') != 'Stable' or records[0].get('platform') != PLATFORMS[platform]):
        raise ValueError('ChromiumDash가 요청한 플랫폼의 Stable을 반환하지 않았다.')
    release = records[0]
    return validate_record({'version': release['version'], 'revision': release['hashes']['chromium']})


def check_updates(manifest, platforms, fetch=latest_stable):
    results = []
    pinned = {key: manifest[key] for key in ('version', 'revision')}
    for platform in platforms:
        latest = fetch(platform)
        results.append({'platform': platform, 'pinned': pinned, 'latest': latest,
                        'update_available': version_parts(latest['version']) > version_parts(pinned['version']),
                        'same_version_different_revision': latest['version'] == pinned['version'] and
                        latest['revision'] != pinned['revision']})
    return {'platforms': results,
            'same_engine_line': len({version_parts(item['latest']['version'])[:3] for item in results}) == 1,
            'modified': False}


def verify_checkout(manifest, platform, source):
    source = Path(source)
    if not source.is_absolute() or not (source / 'BUILD.gn').is_file():
        raise ValueError('Chromium src의 절대 경로를 지정한다.')
    command = ['git', '-c', f'safe.directory={source.as_posix()}', '-C', str(source)]
    revision = subprocess.run(command + ['rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
    pinned = {key: manifest[key] for key in ('version', 'revision')}
    if revision != pinned['revision']:
        raise ValueError(f'Chromium 기준 불일치 ({platform}): {revision}; 필요한 원본은 {pinned["revision"]}. '
                         '기존 변경을 보존한 뒤 Stable 업그레이드 절차를 진행한다.')
    version = subprocess.run(command + ['show', 'HEAD:chrome/VERSION'], capture_output=True, text=True, check=True).stdout
    if parse_version(version) != version_parts(pinned['version']):
        raise ValueError('기록된 버전과 원본 커밋의 chrome/VERSION이 다르다.')
    return {'platform': platform, **pinned}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    commands = parser.add_subparsers(dest='command', required=True)
    show = commands.add_parser('show', help='현재 기준을 출력한다.')
    show.add_argument('--get', choices=('version', 'revision', 'source_url'))
    check = commands.add_parser('check', help='새 Stable을 조회하며 파일을 바꾸지 않는다.')
    check.add_argument('--platform', choices=(*PLATFORMS, 'all'), default='all')
    verify = commands.add_parser('verify', help='개발 명령 실행 전 원본 일치를 검사한다.')
    verify.add_argument('source', type=Path)
    verify.add_argument('--platform', choices=PLATFORMS)
    args = parser.parse_args()
    try:
        manifest = load_manifest(args.manifest)
        if args.command == 'check':
            platforms = PLATFORMS if args.platform == 'all' else [args.platform]
            output = check_updates(manifest, platforms)
        elif args.command == 'verify':
            output = verify_checkout(manifest, args.platform or default_platform(), args.source)
        else:
            output = {key: manifest[key] for key in ('channel', 'version', 'revision', 'source_url')}
            if args.get:
                print(output[args.get])
                return
        print(json.dumps(output, ensure_ascii=False))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError, urllib.error.URLError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
