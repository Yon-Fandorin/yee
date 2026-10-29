#!/usr/bin/env python3
"""브랜드 자산 묶음을 준비·검증하고 Chromium에 설치한다."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import tempfile
import subprocess
import sys
import xml.etree.ElementTree as ET

from brand_config import REPO_ROOT, load_brand

UPSTREAM_INPUTS = (
    'chrome/browser/resources/side_panel/customize_chrome/icons/mini_new_tab_page.svg',
    'chrome/browser/resources/webui_toolbar/icons.ts',
    'ui/webui/resources/cr_elements/icons.html.ts',
    'chrome/browser/resources/settings/icons.html.ts',
    'chrome/browser/resources/webui_browser/icons.html.ts',
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def upstream_input_blobs(chromium_src):
    """설치된 생성 파일 대신 HEAD 원본의 Git blob을 갱신 입력으로 읽는다."""
    try:
        blobs = subprocess.check_output(
            ['git', '-C', str(chromium_src), 'rev-parse',
             *('HEAD:' + path for path in UPSTREAM_INPUTS)], text=True).splitlines()
    except subprocess.CalledProcessError as error:
        raise ValueError('Cannot inspect immutable Chromium brand asset inputs') from error
    return dict(zip(UPSTREAM_INPUTS, blobs, strict=True))


def matches_brand_inputs(manifest, brand):
    return (manifest.get('product_name') == brand.name
            and manifest.get('short_name') == brand.short_name
            and manifest.get('source_sha256') == digest(brand.logo_source)
            and manifest.get('palette') == brand.logo_palette
            and manifest.get('generator_sha256') == digest(Path(__file__).with_name('generate-brand-assets.py')))


def matches_inputs(manifest, brand, chromium_src):
    return (matches_brand_inputs(manifest, brand)
            and manifest.get('upstream_inputs') == upstream_input_blobs(chromium_src))


def confined_file(root, value):
    if (not isinstance(value, str) or not value or '\\' in value
            or any(c in value for c in ':*?[]\n\r\0')
            or PurePosixPath(value).is_absolute()
            or any(p in ('', '.', '..') for p in value.split('/'))):
        raise ValueError(f'Expected a literal relative asset path: {value!r}')
    path = root / value
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError(f'Asset paths may not traverse links: {part}')
        if part == root:
            break
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'Asset path escapes its root: {value}')
    return path


def asset_plan(brand, chromium_src, repo_root=REPO_ROOT):
    if brand.logo_assets_manifest is None:
        raise ValueError('Set logo_assets_manifest and run generate-brand-assets.py first')
    package = brand.logo_assets_manifest.parent
    manifest = json.loads(brand.logo_assets_manifest.read_text(encoding='utf-8'))
    if manifest.get('schema_version') != 1:
        raise ValueError('Unsupported brand asset package schema')
    if not matches_inputs(manifest, brand, chromium_src):
        raise ValueError('Brand asset package is stale; run generate-brand-assets.py')
    return package_plan(manifest, package, chromium_src, repo_root)


def package_plan(manifest, package, chromium_src, repo_root):
    catalog = json.loads((Path(repo_root) / 'build/overlay.json').read_text(encoding='utf-8'))
    owned = catalog['generated_roots']
    entries = manifest.get('files')
    if not isinstance(entries, list) or not entries:
        raise ValueError('Brand package contains no outputs')
    plan, destinations = [], set()
    for entry in entries:
        source = confined_file(package, entry['source'])
        relative = entry['destination']
        destination = confined_file(chromium_src, relative)
        if not any(relative == r or relative.startswith(r + '/') for r in owned):
            raise ValueError(f'Asset destination is not an owned generated path: {relative}')
        if relative in destinations:
            raise ValueError(f'Duplicate asset destination: {relative}')
        destinations.add(relative)
        if not source.is_file() or digest(source) != entry['sha256']:
            raise ValueError(f'Missing or modified package asset: {source}')
        if destination.exists() and not destination.is_file():
            raise ValueError(f'Asset destination is not a file: {relative}')
        for parent in destination.parents:
            if parent == chromium_src:
                break
            if parent.exists() and not parent.is_dir():
                raise ValueError(f'Asset destination parent is not a directory: {relative}')
        if not destination.is_file() or digest(destination) != entry['sha256']:
            plan.append((source, destination))
    return plan, len(entries)


def write_file(destination, data):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as out:
            out.write(data)
            temporary = Path(out.name)
        temporary.chmod(0o644)
        temporary.replace(destination)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)


def install(plan):
    for source, destination in plan:
        write_file(destination, source.read_bytes())


def refresh_embeds(brand, chromium_src, repo_root=REPO_ROOT):
    """래스터 출처를 검증한 뒤 Chromium에 내장된 텍스트 자산만 갱신한다."""
    if brand.logo_assets_manifest is None:
        raise ValueError('Set logo_assets_manifest and generate the full package on macOS first')
    manifest_path = brand.logo_assets_manifest
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest.get('schema_version') != 1 or not matches_brand_inputs(manifest, brand):
        raise ValueError('Brand asset package is stale; run generate-brand-assets.py on macOS')
    old_inputs = manifest.get('upstream_inputs')
    if (not isinstance(old_inputs, dict) or set(old_inputs) != set(UPSTREAM_INPUTS)
            or any(not isinstance(blob, str) or len(blob) != 40
                   or any(c not in '0123456789abcdef' for c in blob)
                   for blob in old_inputs.values())):
        raise ValueError('Expected a complete immutable upstream input map')
    package_plan(manifest, manifest_path.parent, chromium_src, repo_root)
    inputs = upstream_input_blobs(chromium_src)
    if old_inputs == inputs:
        return 0

    spec = importlib.util.spec_from_file_location(
        'generate_brand_assets', Path(__file__).with_name('generate-brand-assets.py'))
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    paths = generator.paths_from(ET.parse(brand.logo_source).getroot())
    outputs = generator.embedded_logos(chromium_src, paths, inputs)
    if set(outputs) != set(UPSTREAM_INPUTS):
        raise ValueError('Embedded generator must produce exactly the registered inputs')
    entries = {entry['destination']: entry for entry in manifest['files']}
    sources = [entry['source'] for entry in manifest['files']]
    pending = []
    for destination, content in outputs.items():
        entry = entries[destination]
        source = confined_file(manifest_path.parent, entry['source'])
        if sources.count(entry['source']) != 1 or source == manifest_path:
            raise ValueError(f'Embedded output must have its own package file: {destination}')
        data = content.encode('utf-8')
        entry['sha256'] = hashlib.sha256(data).hexdigest()
        if source.read_bytes() != data:
            pending.append((source, data))
    # 생성 및 전체 경로 검증이 끝난 뒤 묶음 파일을 쓰고, 메타데이터를 마지막에 바꾼다.
    for source, data in pending:
        write_file(source, data)
    manifest['upstream_inputs'] = inputs
    write_file(manifest_path, (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))
    return len(pending)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('chromium_src', type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--refresh', action='store_true', help='regenerate stale inputs on macOS before installing')
    mode.add_argument('--prepare', action='store_true',
                      help='refresh stale inputs and validate without writing the Chromium checkout')
    mode.add_argument('--refresh-embeds', action='store_true',
                      help='같은 브랜드의 내장 SVG·아이콘만 새 Chromium 원본에 맞춘다 (설치하지 않음)')
    args = parser.parse_args()
    try:
        if (not args.chromium_src.is_absolute()
                or not (args.chromium_src / 'chrome/app/theme/chromium/BRANDING').is_file()):
            raise ValueError('Expected an absolute Chromium source checkout')
        brand = load_brand(validate_assets=False)
        if args.refresh_embeds:
            count = refresh_embeds(brand, args.chromium_src)
            print(f'{brand.name} embedded brand assets: {count} updated')
            return
        if args.refresh or args.prepare:
            manifest = brand.logo_assets_manifest
            if manifest is None:
                raise ValueError('Set logo_assets_manifest in branding/brand.json')
            data = json.loads(manifest.read_text(encoding='utf-8')) if manifest.is_file() else {}
            if not matches_inputs(data, brand, args.chromium_src):
                subprocess.run([sys.executable, str(Path(__file__).with_name('generate-brand-assets.py')),
                                str(args.chromium_src)], check=True)
        plan, count = asset_plan(brand, args.chromium_src)
        preview = args.check or args.prepare
        if not preview:
            install(plan)
        print(f'{brand.name} brand assets: {count} verified, {len(plan)} '
              f'{"pending" if preview else "installed"}')
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
