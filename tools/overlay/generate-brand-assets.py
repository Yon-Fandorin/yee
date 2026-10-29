#!/usr/bin/env python3
"""설정한 SVG를 데스크톱 공용 브랜드 자산 묶음으로 렌더링한다."""

import argparse
import copy
import hashlib
from html import escape
from io import BytesIO
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from brand_config import REPO_ROOT, load_brand
from brand_assets import upstream_input_blobs

NS = '{http://www.w3.org/2000/svg}'
ET.register_namespace('', NS[1:-1])
DEFAULT_PALETTE = {
    'primary': '#242824', 'secondary': '#438578',
    'dark_primary': '#F5F6F2', 'dark_secondary': '#8BC4B3',
    'background': '#F7F9F5', 'dark_background': '#202724',
}


def write_if_changed(path, data):
    if isinstance(data, str):
        data = data.encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def paths_from(svg):
    paths = list(svg.iter(NS + 'path'))
    if not paths or any(p.get('transform') for p in paths):
        raise ValueError('Use filled paths with transforms baked into the source SVG')
    if any(node.tag not in {NS + name for name in ('svg', 'g', 'path', 'title', 'desc')}
           or node.get('transform') for node in svg.iter()):
        raise ValueError('Brand SVG must contain only self-contained filled path geometry')
    return paths


def vector_icon(paths, palette=None, *, size=24):
    lines = ['// branding/brand.json과 설정한 SVG에서 생성한다.',
             '// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.',
             '', f'CANVAS_DIMENSIONS, {size},']
    for index, path in enumerate(paths):
        if index:
            lines.append('NEW_PATH,')
        lines.append('FILL_RULE_NONZERO,')
        if palette is not None:
            colour = palette[path.get('data-brand-colour', 'primary')].lstrip('#')
            lines.append('PATH_COLOR_ARGB, 0xFF, ' + ', '.join('0x' + colour[i:i+2] for i in (0,2,4)) + ',')
        tokens = re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?', path.attrib['d'])
        offset = 0
        while offset < len(tokens):
            command = tokens[offset]
            count = {'M': 2, 'L': 2, 'C': 6, 'Z': 0}.get(command)
            if count is None:
                raise ValueError('Native vector export expects absolute M/L/C/Z commands')
            values = tokens[offset+1:offset+1+count]
            if len(values) != count:
                raise ValueError('Incomplete SVG path command')
            values = [format(float(value) * size / 128, '.8g') for value in values]
            name = {'M': 'MOVE_TO', 'L': 'LINE_TO', 'C': 'CUBIC_TO', 'Z': 'CLOSE'}[command]
            lines.append(name + (', ' + ', '.join(values) if count else '') + ',')
            offset += count + 1
    return '\n'.join(lines) + '\n'


def export_svg(source, brand, palette, *, dark=False, mono=False, adaptive=False, size=128):
    svg = copy.deepcopy(source)
    svg.set('width', str(size))
    svg.set('height', str(size))
    svg.set('fill', 'currentColor' if mono else palette['dark_primary' if dark else 'primary'])
    title = svg.find(NS + 'title')
    if title is not None:
        title.text = brand.short_name
    desc = svg.find(NS + 'desc')
    if desc is not None:
        desc.text = 'Interwoven knot mark.'
    for path in svg.iter(NS + 'path'):
        role = path.attrib.pop('data-brand-colour', 'primary')
        if role not in ('primary', 'secondary'):
            raise ValueError(f'Unknown brand colour role: {role}')
        if mono:
            path.attrib.pop('fill', None)
        else:
            path.set('fill', palette[('dark_' if dark else '') + role])
            if adaptive:
                path.set('class', role)
    if adaptive:
        style = ET.Element(NS + 'style')
        style.text = ('@media(prefers-color-scheme:dark){'
                      f'.primary{{fill:{palette["dark_primary"]}}}'
                      f'.secondary{{fill:{palette["dark_secondary"]}}}' + '}')
        svg.insert(0, style)
    return ET.tostring(svg, encoding='unicode') + '\n'


def render(svg, size):
    import cairosvg
    from PIL import Image

    return Image.open(BytesIO(cairosvg.svg2png(bytestring=svg.encode(),
        output_width=size, output_height=size))).convert('RGBA')


def png_bytes(image):
    output = BytesIO()
    image.save(output, format='PNG', optimize=True)
    return output.getvalue()


def wordmark_font(text, size, configured=None):
    from PIL import ImageFont

    if configured is not None:
        return ImageFont.truetype(str(configured), size)
    if any(ord(character) > 255 for character in text):
        return ImageFont.truetype('/System/Library/Fonts/AppleSDGothicNeo.ttc', size, index=6)
    return ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Bold.ttf', size)


def icon_plate(mark, background):
    svg = ET.fromstring(mark)
    svg.set('viewBox', '0 0 128 128')
    rectangle = ET.Element(NS + 'rect', x='8', y='8', width='112', height='112', rx='24', fill=background)
    group = ET.Element(NS + 'g', transform='translate(8 8) scale(.875)')
    for path in list(svg.iter(NS + 'path')):
        group.append(copy.deepcopy(path))
    for child in list(svg):
        svg.remove(child)
    svg.extend((rectangle, group))
    return ET.tostring(svg, encoding='unicode')


def ico_bytes(svg):
    sizes = (16, 24, 32, 48, 64, 128, 256)
    data = [png_bytes(render(svg, size)) for size in sizes]
    header = struct.pack('<HHH', 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    for size, png in zip(sizes, data):
        header += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    return header + b''.join(data)


def document_icon(mark, label):
    paths = ''.join(ET.tostring(p, encoding='unicode') for p in ET.fromstring(mark).iter(NS + 'path'))
    # 종이 윤곽으로 앱 아이콘과 문서 아이콘을 구분한다.
    badge = ('<rect x="36" y="74" width="32" height="15" rx="3" fill="#B84A3A"/>'
             '<text x="52" y="85" text-anchor="middle" fill="white" font-family="Arial" font-size="10" font-weight="bold">PDF</text>') if label == 'PDF' else ''
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">'
            '<path d="M27 7H80L108 35V119H27Z" fill="#F7F9F5" stroke="#7A857E" stroke-width="3"/>'
            '<path d="M80 7V35H108" fill="none" stroke="#7A857E" stroke-width="3"/>'
            '<path d="M40 50H90M40 62H81" stroke="#BCC5BC" stroke-width="4"/>'
            f'<g transform="translate(52 66) scale(.43)">{paths}</g>{badge}</svg>')


def browser_showcase(mark, palette):
    """설정한 표식과 배색으로 브라우저 벡터 삽화를 만든다."""
    paths = ''.join(ET.tostring(p, encoding='unicode')
                    for p in ET.fromstring(mark).iter(NS + 'path'))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 800">'
            f'<rect x="72" y="168" width="656" height="432" rx="28" fill="{palette["background"]}" '
            'stroke="#CBD6CF" stroke-width="4"/>'
            '<path d="M74 224H726" stroke="#CBD6CF" stroke-width="4"/>'
            '<g fill="#CBD6CF"><circle cx="103" cy="197" r="7"/>'
            '<circle cx="127" cy="197" r="7"/><circle cx="151" cy="197" r="7"/></g>'
            f'<g transform="translate(280 292) scale(1.875)">{paths}</g>'
            '<path d="M304 600V644H496V600M240 646H560" fill="none" '
            'stroke="#CBD6CF" stroke-width="8" stroke-linecap="round"/></svg>')


def replace_product_icon_groups(text, geometry, *, expected_count):
    """upstream 아이콘 두 종류에서 제품 표식 그룹만 교체한다."""
    result, count = re.subn(
        r'<g\b[^>]*\bid\s*=\s*([\"\'])chrome-product\1[^>]*>.*?</g>',
        lambda _match: '<g id="chrome-product" viewBox="0 0 128 128" '
                       'fill="currentColor">' + geometry + '</g>',
        text, flags=re.DOTALL)
    if count != expected_count:
        raise ValueError(f'Expected {expected_count} inline product icon groups, found {count}')
    return result


def embedded_logos(src, paths, upstream_blobs=None):
    """제품 표식 주변의 upstream 삽화와 아이콘을 유지한다."""
    upstream_blobs = upstream_blobs or upstream_input_blobs(src)

    def upstream(path):
        return subprocess.check_output(
            ['git', '-C', str(src), 'show', upstream_blobs[path]], text=True)

    geometry = ''.join(f'<path d="{escape(path.attrib["d"], quote=True)}"/>'
                       for path in paths)
    preview_path = 'chrome/browser/resources/side_panel/customize_chrome/icons/mini_new_tab_page.svg'
    preview = ET.fromstring(upstream(preview_path))
    matches = 0
    for parent in preview.iter():
        for child in list(parent):
            if child.get('fill') != 'var(--color-side-panel-customize-chrome-mini-ntp-chrome-logo)':
                continue
            group = ET.fromstring(
                '<g xmlns="http://www.w3.org/2000/svg" '
                'transform="translate(49 13) scale(.109375)" '
                'fill="var(--color-side-panel-customize-chrome-mini-ntp-chrome-logo)">' + geometry + '</g>')
            parent.insert(list(parent).index(child), group)
            parent.remove(child)
            matches += 1
    if matches != 1:
        raise ValueError('Expected one embedded product mark in the theme preview')

    toolbar_path = 'chrome/browser/resources/webui_toolbar/icons.ts'
    toolbar, count = re.subn(
        r"('chrome_product':\s*\(\)\s*=>\s*getTrustedHTML`)\s*<svg>.*?</svg>(`)",
        lambda match: match[1] + '<svg><g id="chrome_product" viewBox="0 0 24 24">'
                      '<g transform="scale(.1875)" fill="currentColor">' + geometry +
                      '</g></g></svg>' + match[2], upstream(toolbar_path),
        flags=re.DOTALL)
    if count != 1:
        raise ValueError('Expected one inline toolbar product icon')
    outputs = {preview_path: ET.tostring(preview, encoding='unicode') + '\n',
               toolbar_path: toolbar}
    # 별도 아이콘 모음은 하이픈이 있는 제품 ID를 쓴다.
    # 기능 아이콘, 서비스 로고와 rounded/legacy 선택 코드는 유지한다.
    for path in ('ui/webui/resources/cr_elements/icons.html.ts',
                 'chrome/browser/resources/settings/icons.html.ts',
                 'chrome/browser/resources/webui_browser/icons.html.ts'):
        outputs[path] = replace_product_icon_groups(
            upstream(path), geometry, expected_count=2)
    return outputs


def main():
    from PIL import Image, ImageDraw

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('chromium_src', type=Path)
    parser.add_argument('--font', type=Path, help='override the automatically selected Latin/Korean wordmark font')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Generate the desktop package on macOS; installation needs no rendering tools')
    brand = load_brand(validate_assets=False)
    if brand.logo_assets_manifest is None:
        parser.error('Set logo_assets_manifest in branding/brand.json')
    source = ET.parse(brand.logo_source).getroot()
    if source.get('viewBox') != '0 0 128 128':
        parser.error('The canonical mark must have viewBox="0 0 128 128"')
    paths = paths_from(source)
    # 묶음 파일을 쓰기 전에 벡터 변환을 검증한다.
    monochrome_icon = vector_icon(paths)
    palette = DEFAULT_PALETTE | brand.logo_palette
    vector_icon(paths, palette)
    upstream_blobs = upstream_input_blobs(args.chromium_src)
    package = brand.logo_assets_manifest.parent
    manifest_files = []

    def output(destination, data):
        relative = 'files/' + destination
        target = package / relative
        write_if_changed(target, data)
        manifest_files.append({'source': relative, 'destination': destination,
                               'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})

    def image(destination, svg, size):
        output(destination, png_bytes(render(svg, size)))

    light = export_svg(source, brand, palette)
    dark = export_svg(source, brand, palette, dark=True)
    mono = export_svg(source, brand, palette, mono=True)
    adaptive = export_svg(source, brand, palette, adaptive=True)
    theme = 'chrome/app/theme/chromium/'
    for name in ('product_logo.svg', 'product_logo_animation.svg'):
        # 애니메이션 경로에도 모양이 변하지 않는 같은 정적 표식을 제공한다.
        output(theme + name, adaptive)
    # 어두운 WebUI 경로에는 CSS와 무관한 흰 단색 표식을 제공한다.
    white = ET.fromstring(mono)
    white.set('fill', palette['dark_primary'])
    output('ui/webui/resources/images/chrome_logo_dark.svg', ET.tostring(white, encoding='unicode') + '\n')
    # 이전 표현까지 각 upstream 아이콘의 기본 캔버스 크기를 유지한다.
    # 크기를 생략한 호출에서도 기존 UI 치수가 바뀌면 안 된다.
    native_icons = {
        'components/vector_icons/chromium/product.icon': (24,),
        'components/vector_icons/chromium/product_refresh.icon': (24,),
        'components/omnibox/browser/vector_icons/chrome_product.icon': (24,),
        'components/omnibox/browser/vector_icons/product_chrome_refresh_old.icon': (15,),
        'components/omnibox/browser/vector_icons/product_old.icon': (32, 16),
        'ui/message_center/vector_icons/chrome_product.icon': (24,),
        'ui/message_center/vector_icons/product_old.icon': (96,),
        'chrome/app/vector_icons/chrome_product.icon': (16,),
        'chrome/app/vector_icons/browser_logo_old.icon': (16,),
    }
    for destination, sizes in native_icons.items():
        output(destination, '\n'.join(vector_icon(paths, size=size) for size in sizes))
    output('ui/webui/resources/cr_components/searchbox/icons/chrome_product_cr23.svg', mono)
    for destination, data in embedded_logos(args.chromium_src, paths, upstream_blobs).items():
        output(destination, data)
    # 이 삽화에는 IDR_PRODUCT_LOGO_SVG 참조 대신 별도 Chromium 로고가 들어 있다.
    # 같은 벡터 경로에서 삽화를 생성한다.
    image('chrome/browser/resources/intro/images/refresh_showcase_illustration_chromium.png',
          browser_showcase(light, palette), 800)
    app_svg = icon_plate(light, palette['background'])
    for size in (16, 24, 32, 48, 64, 128, 256):
        image(theme + f'product_logo_{size}.png', app_svg, size)
        image(theme + f'linux/product_logo_{size}.png', app_svg, size)
    image(theme + 'product_logo_22_mono.png', mono, 22)
    image(theme + 'chromeos/chrome_app_icon_32.png', app_svg, 32)
    image(theme + 'chromeos/chrome_app_icon_192.png', app_svg, 192)
    base_font = wordmark_font(brand.short_name, 19, args.font)
    base_bbox = base_font.getbbox(brand.short_name)
    logical_wordmark_width = 29 + base_bbox[2] - base_bbox[0]
    for scale in (1, 2):
        scaled = f'chrome/app/theme/default_{scale * 100}_percent/chromium/'
        # 탭 테마는 이 자산의 알파를 마스크로 쓴다.
        # 앱 아이콘 배경판이 있으면 일부 화면에서 매듭 대신 사각 타일이 된다.
        image(f'chrome/app/theme/default_{scale * 100}_percent/common/favicon_ntp.png', light, 16 * scale)
        component_scaled = f'components/resources/default_{scale * 100}_percent/chromium/'
        image(component_scaled + 'favicon_product.png', app_svg, 16 * scale)
        image(component_scaled + 'product_logo.png', light, 64 * scale)
        image(component_scaled + 'product_logo_white.png', dark, 64 * scale)
        for size in (16, 32):
            image(scaled + f'product_logo_{size}.png', app_svg, size * scale)
            image(scaled + f'linux/product_logo_{size}.png', app_svg, size * scale)
        font = wordmark_font(brand.short_name, 19 * scale, args.font)
        bbox = font.getbbox(brand.short_name)
        for inverted in (False, True):
            wordmark = Image.new('RGBA', (logical_wordmark_width * scale, 22 * scale))
            icon = render(dark if inverted else light, 22 * scale)
            wordmark.alpha_composite(icon)
            ImageDraw.Draw(wordmark).text((29 * scale - bbox[0], (22 * scale - (bbox[3]-bbox[1]))//2 - bbox[1]),
                brand.short_name, font=font, fill=palette['dark_primary' if inverted else 'primary'])
            suffix = '_white' if inverted else ''
            output(scaled + f'product_logo_name_22{suffix}.png', png_bytes(wordmark))
    for name, mark in (('chromium.ico', app_svg), ('app_list.ico', app_svg),
                       ('chromium_doc.ico', document_icon(light, 'HTML')),
                       ('chromium_pdf.ico', document_icon(light, 'PDF'))):
        output(theme + 'win/' + name, ico_bytes(mark))
    for destination in ('chrome/installer/setup/setup.ico',
                        'chrome/installer/mini_installer/mini_installer.ico',
                        'chrome/browser/web_applications/chrome_pwa_launcher/chrome_pwa_launcher.ico'):
        output(destination, ico_bytes(app_svg))
    for name, canvas, mark_size in (('Logo.png', 600, 284), ('SmallLogo.png', 176, 152)):
        tile = Image.new('RGBA', (canvas, canvas))
        tile.alpha_composite(render(app_svg, mark_size), ((canvas - mark_size)//2,) * 2)
        output(theme + 'win/tiles/' + name, png_bytes(tile))

    with tempfile.TemporaryDirectory(prefix='yee-brand-assets-') as temp:
        work = Path(temp)
        folder_dir = work / 'folders'
        folder_dir.mkdir()
        swift = '''import AppKit
import Foundation
let output = URL(fileURLWithPath: CommandLine.arguments.last!)
let image = NSImage(named: NSImage.folderName)!
for size in [16, 32] {
    let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
    image.draw(in: NSRect(x: 0, y: 0, width: size, height: size))
    NSGraphicsContext.restoreGraphicsState()
    try bitmap.representation(using: .png, properties: [:])!.write(to: output.appendingPathComponent("folder-\\(size).png"))
}'''
        subprocess.run(['xcrun', 'swift', '-module-cache-path', str(work / 'swift-cache'), '-e', swift, str(folder_dir)], check=True)
        for size in (16,32):
            folder = Image.open(folder_dir / f'folder-{size}.png').convert('RGBA')
            logo_size = round(size * .54)
            folder.alpha_composite(render(light, logo_size), ((size-logo_size)//2, round(size * .34)))
            output(f'chrome/app/theme/mac/apps_folder_{size}.png', png_bytes(folder))
        for size in (128,512):
            overlay = Image.new('RGBA', (size,size))
            logo_size = round(size * .51)
            overlay.alpha_composite(render(light, logo_size), ((size-logo_size)//2, round(size * .30)))
            output(f'chrome/app/theme/mac/apps_folder_overlay_{size}.png', png_bytes(overlay))
        mac = work / 'mac'
        app_catalog = mac / 'Assets.xcassets/AppIcon.appiconset'
        badge = mac / 'Assets.xcassets/Icon.iconset'
        app_catalog.mkdir(parents=True)
        badge.mkdir(parents=True)
        write_if_changed(mac / 'Assets.xcassets/Contents.json', json.dumps({'info': {'author': 'xcode', 'version': 1}}))
        images = []
        for size, scale in ((16,1),(16,2),(32,1),(32,2),(128,1),(128,2),(256,1),(256,2),(512,1),(512,2)):
            filename = f'appicon_{size * scale}.png'
            write_if_changed(app_catalog / filename, png_bytes(render(app_svg, size * scale)))
            images.append({'filename': filename, 'idiom': 'mac', 'scale': f'{scale}x', 'size': f'{size}x{size}'})
        write_if_changed(app_catalog / 'Contents.json', json.dumps({'images': images, 'info': {'author': 'xcode', 'version': 1}}, indent=2))
        for scale in (1, 2):
            name = 'icon_256x256' + ('@2x' if scale == 2 else '') + '.png'
            write_if_changed(badge / name, png_bytes(render(light, 256 * scale)))
        composer = mac / 'AppIcon.icon'
        layer_dir = composer / 'Assets'
        for filename, data in (('mark.svg', light), ('mark-dark.svg', dark), ('mark-mono.svg', mono)):
            svg = ET.fromstring(data)
            svg.set('width', '1024'); svg.set('height', '1024')
            if filename == 'mark-mono.svg':
                svg.set('fill', '#000000')
            write_if_changed(layer_dir / filename, ET.tostring(svg, encoding='unicode'))

        def extended(colour):
            return 'srgb:' + ','.join(f'{int(colour[i:i+2],16)/255:.5f}' for i in (1,3,5)) + ',1.00000'

        icon_json = {
            'fill-specializations': [{'value': {'solid': extended(palette['background'])}},
                                     {'appearance': 'dark', 'value': {'solid': extended(palette['dark_background'])}}],
            'groups': [{'layers': [{'name': 'Knot', 'glass': False,
                'image-name-specializations': [{'value': 'mark.svg'}, {'appearance': 'dark', 'value': 'mark-dark.svg'},
                                               {'appearance': 'tinted', 'value': 'mark-mono.svg'}]}],
                'shadow': {'kind': 'none', 'opacity': 0}, 'specular': False,
                'translucency': {'enabled': False, 'value': 0}}],
            'supported-platforms': {'squares': 'shared'},
        }
        write_if_changed(composer / 'icon.json', json.dumps(icon_json, indent=2))
        subprocess.run([sys.executable, str(args.chromium_src / 'tools/mac/icons/compile_car.py'),
                        str(mac / 'Assets.xcassets')], check=True)
        # actool의 ICNS 생성 여부와 관계없이 모든 1x/2x 표현을 직접 패킹한다.
        iconset = work / 'Product.iconset'
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = '@2x' if scale == 2 else ''
                write_if_changed(iconset / f'icon_{size}x{size}{suffix}.png',
                                 png_bytes(render(app_svg, size * scale)))
        packer = work / 'makeicns'
        subprocess.run(['c++', '-Os', str(args.chromium_src / 'tools/mac/icons/additional_tools/makeicns.cc'), '-o', str(packer)], check=True)
        subprocess.run([str(packer), str(iconset), str(mac / 'app.icns')], check=True)
        for file in sorted(mac.rglob('*')):
            if file.is_file():
                output(theme + 'mac/' + file.relative_to(mac).as_posix(), file.read_bytes())

    manifest = {'schema_version': 1, 'product_name': brand.name, 'short_name': brand.short_name,
                'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'source': brand.logo_source.relative_to(REPO_ROOT).as_posix(),
                'source_sha256': hashlib.sha256(brand.logo_source.read_bytes()).hexdigest(),
                'palette': brand.logo_palette, 'upstream_inputs': upstream_blobs,
                'files': manifest_files}
    write_if_changed(brand.logo_assets_manifest, json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    print(f'Generated {len(manifest_files)} {brand.name} assets: {brand.logo_assets_manifest}')


if __name__ == '__main__':
    main()
