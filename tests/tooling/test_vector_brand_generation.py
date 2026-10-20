#!/usr/bin/env python3
"""로고 경로 변경이 같은 네이티브 선언을 다시 생성하지 않는지 검증한다."""

import importlib.util
import json
import os
from pathlib import Path
import shlex
import re
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / '.local-build/chromium/src/components/vector_icons'


@unittest.skipUnless(importlib.util.find_spec('PIL'), '브랜드 렌더러 의존성이 필요하다')
class PackagedFaviconTransparencyTest(unittest.TestCase):
    def test_ntp_theming_masks_the_knot_without_an_application_plate(self):
        from PIL import Image

        files = ROOT / 'assets/brand/generated/files'
        for scale in (1, 2):
            with self.subTest(scale=scale):
                ntp_path = files / f'chrome/app/theme/default_{100 * scale}_percent/common/favicon_ntp.png'
                app_path = files / f'chrome/app/theme/default_{100 * scale}_percent/chromium/product_logo_16.png'
                with Image.open(ntp_path) as ntp, Image.open(app_path) as app:
                    self.assertEqual(ntp.size, (16 * scale, 16 * scale))
                    alpha = ntp.convert('RGBA').getchannel('A')
                    self.assertEqual(alpha.getextrema(), (0, 255))
                    # 네이티브 CreateColorMask에서도 매듭의 중앙은 열려 있어야 한다.
                    center = (8 * scale, 8 * scale)
                    # 16px 중앙에는 안티앨리어싱한 가장자리도 일부 포함된다.
                    self.assertLessEqual(alpha.getpixel(center), 64)
                    # 앱·OS 자산은 불투명 배경판을 유지한다.
                    self.assertEqual(app.convert('RGBA').getpixel(center)[3], 255)


@unittest.skipUnless(importlib.util.find_spec('cairosvg') and SOURCE.is_dir(),
                     '브랜드 렌더러와 적용한 Chromium 소스가 필요하다')
class InlineProductIconGenerationTest(unittest.TestCase):
    def test_shared_settings_and_browser_variants_preserve_non_product_icons(self):
        tools = str(ROOT / 'tools/overlay')
        sys.path.insert(0, tools)
        self.addCleanup(sys.path.remove, tools)
        spec = importlib.util.spec_from_file_location(
            'generate_brand_assets_inline_test', ROOT / 'tools/overlay/generate-brand-assets.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        source = generator.load_brand(validate_assets=False).logo_source
        paths = generator.paths_from(ET.parse(source).getroot())
        expected_geometry = [path.attrib['d'] for path in paths]
        chromium = ROOT / '.local-build/chromium/src'
        upstream_blobs = generator.upstream_input_blobs(chromium)
        outputs = generator.embedded_logos(chromium, paths, upstream_blobs)
        manifest = ROOT / 'assets/brand/generated/manifest.json'
        package = json.loads(manifest.read_text(encoding='utf-8'))
        self.assertEqual(package['upstream_inputs'], upstream_blobs)
        for relative, generated in outputs.items():
            self.assertEqual((manifest.parent / 'files' / relative).read_bytes(),
                             generated.encode('utf-8'), relative)
        product_group = re.compile(r'<g id="chrome-product"[^>]*>.*?</g>', re.DOTALL)
        for relative in ('ui/webui/resources/cr_elements/icons.html.ts',
                         'chrome/browser/resources/settings/icons.html.ts',
                         'chrome/browser/resources/webui_browser/icons.html.ts'):
            with self.subTest(path=relative):
                original = subprocess.check_output(
                    ['git', '-C', str(chromium), 'show', 'HEAD:' + relative], text=True)
                generated = outputs[relative]
                groups = product_group.findall(generated)
                self.assertEqual(len(groups), 2)  # rounded·legacy 두 종류다.
                self.assertEqual(product_group.sub('PRODUCT_MARK', original),
                                 product_group.sub('PRODUCT_MARK', generated))
                for group in groups:
                    node = ET.fromstring(group)
                    self.assertEqual(node.attrib['viewBox'], '0 0 128 128')
                    self.assertEqual(node.attrib['fill'], 'currentColor')
                    self.assertEqual([path.attrib['d'] for path in node], expected_geometry)
                    self.assertTrue(all(path.tag == 'path' and set(path.attrib) == {'d'}
                                        for path in node))
        with self.assertRaisesRegex(ValueError, 'Expected 2 inline product icon groups'):
            generator.replace_product_icon_groups(
                '<svg><g id="chrome-product"><path/></g></svg>', '', expected_count=2)


@unittest.skipUnless((SOURCE / 'aggregate_vector_icons.py').is_file(), '적용한 Chromium 소스가 필요하다')
class VectorBrandGenerationTest(unittest.TestCase):
    def test_packaged_native_marks_preserve_upstream_default_sizes(self):
        manifest = ROOT / 'assets/brand/generated/manifest.json'
        data = json.loads(manifest.read_text())
        count = 0
        for entry in data['files']:
            if not entry['destination'].endswith('.icon'):
                continue
            with self.subTest(destination=entry['destination']):
                original = subprocess.check_output(
                    ['git', '-C', str(SOURCE.parents[1]), 'show', 'HEAD:' + entry['destination']], text=True)
                generated = (manifest.parent / entry['source']).read_text()
                sizes = r'CANVAS_DIMENSIONS,\s*(\d+)'
                self.assertEqual(re.findall(sizes, generated), re.findall(sizes, original))
                self.assertNotIn('PATH_COLOR_ARGB', generated)
                count += 1
        self.assertGreaterEqual(count, 9)

    def test_geometry_changes_preserve_header_but_names_change_declarations(self):
        spec = importlib.util.spec_from_file_location('aggregate_vector_icons', SOURCE / 'aggregate_vector_icons.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        with tempfile.TemporaryDirectory(prefix='yee-vector-brand-test-') as temp:
            root = Path(temp)
            icon = root / 'brand.icon'
            icon.write_text('CANVAS_DIMENSIONS, 24,\nMOVE_TO, 1, 1,\nLINE_TO, 12, 12,\nCLOSE,\n')
            file_list = root / 'icons.list'
            file_list.write_text(shlex.quote(str(icon)))
            cc, header, tests = (root / name for name in ('icons.cc', 'icons.h', 'tests.h'))
            def generate():
                generator.AggregateVectorIcons(str(SOURCE), str(file_list), str(cc), str(header), str(tests))
            generate()
            original_header, original_cc = header.read_bytes(), cc.read_bytes()
            stamp = 1_600_000_000_000_000_000
            for path in (cc, header, tests):
                os.utime(path, ns=(stamp, stamp))
            generate()
            self.assertTrue(all(path.stat().st_mtime_ns == stamp for path in (cc, header, tests)))
            icon.write_text(icon.read_text().replace('12, 12', '14, 14'))
            generate()
            self.assertEqual(header.read_bytes(), original_header)
            self.assertEqual(header.stat().st_mtime_ns, stamp)
            self.assertNotEqual(cc.read_bytes(), original_cc)
            file_list.write_text(shlex.quote(str(icon.with_name('new_brand.icon'))))
            icon.rename(icon.with_name('new_brand.icon'))
            generate()
            self.assertNotEqual(header.read_bytes(), original_header)
            self.assertIn('kNewBrandIcon', header.read_text())


if __name__ == '__main__':
    unittest.main()
