#!/usr/bin/env python3
"""독립적인 데스크톱 제품 로고 사용 경로를 브랜드 자산 묶음과 대조한다."""

import argparse
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

from brand_assets import asset_plan
from brand_config import load_brand


NATIVE_ROOTS = (
    'components/vector_icons/chromium',
    'components/omnibox/browser/vector_icons',
    'ui/message_center/vector_icons',
    'chrome/app/vector_icons',
)
NATIVE_NAME = re.compile(r'(?:chrome_product|browser_logo|product)(?:_.*)?\.icon$')
RESOURCE_ROOTS = (
    ('chrome/app/theme/theme_resources.grd', 'chrome/app/theme', True),
    ('chrome/app/theme/chrome_unscaled_resources.grd', 'chrome/app/theme', False),
    ('components/resources/version_ui_scaled_resources.grdp', 'components/resources', True),
)
SHARED_WEBUI = 'ui/webui/resources'
EMBEDDED_MARK = re.compile(
    r'mini-ntp-chrome-logo|<g\b[^>]*\bid\s*=\s*[\"\']chrome[-_]product[\"\']')
DIRECT_ASSETS = (
    'chrome/browser/resources/intro/images/refresh_showcase_illustration_chromium.png',
    'chrome/installer/setup/setup.ico',
    'chrome/installer/mini_installer/mini_installer.ico',
    'chrome/browser/web_applications/chrome_pwa_launcher/chrome_pwa_launcher.ico',
    'chrome/app/theme/mac/apps_folder_16.png',
    'chrome/app/theme/mac/apps_folder_32.png',
    'chrome/app/theme/mac/apps_folder_overlay_128.png',
    'chrome/app/theme/mac/apps_folder_overlay_512.png',
    'chrome/app/theme/chromium/mac/Assets.car',
    'chrome/app/theme/chromium/mac/app.icns',
    'chrome/app/theme/chromium/win/chromium.ico',
    'chrome/app/theme/chromium/win/app_list.ico',
    'chrome/app/theme/chromium/win/chromium_doc.ico',
    'chrome/app/theme/chromium/win/chromium_pdf.ico',
    'chrome/app/theme/chromium/win/tiles/Logo.png',
    'chrome/app/theme/chromium/win/tiles/SmallLogo.png',
)


def discover(src):
    """생성기와 독립적으로 GRIT 연결과 중복 벡터 종류를 조사한다."""
    consumers = {}

    def add(path, consumer):
        if path.is_file():
            relative = path.relative_to(src).as_posix()
            consumers.setdefault(relative, set()).add(consumer)

    for root in NATIVE_ROOTS:
        for path in (src / root).glob('*.icon'):
            if NATIVE_NAME.fullmatch(path.name):
                add(path, root + '/native vector family')
    for resource, root, scaled in RESOURCE_ROOTS:
        path = src / resource
        if not path.is_file():
            continue
        for node in ET.parse(path).iter():
            name = node.get('name', '')
            if not (name.startswith('IDR_PRODUCT_LOGO') or name in ('IDR_PRODUCT_FAVICON', 'IDR_NTP_FAVICON')):
                continue
            filename = node.get('file', '').replace('${branding_path_component}', 'chromium')
            # Google Chrome / CFT와 ChromeOS는 별도 제품 분기다.
            if filename.startswith('google_chrome/') or '/chromeos/' in filename or not filename:
                continue
            if scaled:
                for directory in (src / root).glob('default_*_percent'):
                    add(directory / filename, resource + ':' + name)
            else:
                add(src / root / filename, resource + ':' + name)
    for pattern in ('images/chrome*logo*.svg', 'cr_components/searchbox/icons/chrome_product*.svg'):
        for path in (src / SHARED_WEBUI).glob(pattern):
            add(path, 'shared WebUI resource')
    for relative in DIRECT_ASSETS:
        add(src / relative, 'direct desktop artwork / OS resource')
    for root in ('chrome/browser/resources', SHARED_WEBUI):
        for path in (src / root).rglob('*'):
            if path.suffix in ('.svg', '.ts') and EMBEDDED_MARK.search(path.read_text(encoding='utf-8')):
                add(path, 'embedded WebUI product geometry')
    return {path: sorted(uses) for path, uses in sorted(consumers.items())}


def audit(src, destinations):
    consumers = discover(src)
    missing = sorted(consumers.keys() - set(destinations))
    return {
        'scope': 'Unbranded desktop GRIT product resources, native product vector families, shared WebUI marks and reviewed direct artwork. Runtime and external service logos are separate.',
        'consumer_files': len(consumers),
        'consumers': consumers,
        'unmanaged': missing,
        'complete': not missing,
        'preserved': [
            'Generic page favicons and functional icons; incognito, password, crash and extension symbols.',
            'Google search, Lens, Pay, Chrome Web Store, and Chrome on iOS promotional service marks.',
            'Android/ChromeOS, Chrome for Testing, updater and enterprise companion product branches.',
            'Unused upstream source artwork (AI / XPM), tests and examples.',
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('chromium_src', type=Path)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        src = args.chromium_src
        if not src.is_absolute() or not (src / 'chrome/app/theme/theme_resources.grd').is_file():
            raise ValueError('Expected an absolute Chromium source checkout')
        brand = load_brand(validate_assets=False)
        plan, count = asset_plan(brand, src)
        manifest = json.loads(brand.logo_assets_manifest.read_text(encoding='utf-8'))
        report = audit(src, [item['destination'] for item in manifest['files']])
        report['package_assets'] = count
        report['unapplied'] = [destination.relative_to(src).as_posix() for _, destination in plan]
        if args.json:
            print(json.dumps(report, ensure_ascii=False, indent=2))
        else:
            print(f"Desktop logo consumers: {report['consumer_files']}; "
                  f"unmanaged: {len(report['unmanaged'])}; unapplied: {len(plan)}")
            for path in report['unmanaged']:
                print('  unmanaged: ' + path)
            for path in report['unapplied']:
                print('  unapplied: ' + path)
        return 0 if report['complete'] and not plan else 1
    except (OSError, ValueError, KeyError, ET.ParseError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
