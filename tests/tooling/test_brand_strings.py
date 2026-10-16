#!/usr/bin/env python3
"""Chromium의 실제 GRIT 리더로 제품명이 바뀐 번역을 검증한다."""

from dataclasses import replace
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / '.local-build/chromium/src'
sys.path.insert(0, str(ROOT / 'tools/overlay'))
import brand_strings
from brand_config import Brand, product_parts

HAS_GRIT = (SRC / 'tools/grit/grit/grd_reader.py').is_file()
if HAS_GRIT:
    sys.path.insert(0, str(SRC / 'tools/grit'))
    from grit import constants, grd_reader, util
    from grit.node import message


@unittest.skipUnless(HAS_GRIT, 'Chromium GRIT 필요')
class BrandStringsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='yee-brand-strings-')
        self.addCleanup(self.temp.cleanup)
        self.src = Path(self.temp.name) / 'src'
        self.src.mkdir()
        self.policy = Path(self.temp.name) / 'strings.json'
        policy = json.loads((ROOT / 'branding/strings.json').read_text(encoding='utf-8'))
        policy['roots'] = ['chrome/app/chromium_strings.grd']
        self.policy.write_text(json.dumps(policy, ensure_ascii=False), encoding='utf-8')
        self.brand = Brand('Orbit', 'Orb', self.policy, 128, 'orbit', strings_policy=self.policy)
        self.fragments = [
            '<message name="IDS_PRODUCT_NAME">Chromium</message>',
            '<message name="IDS_SHORT_PRODUCT_NAME">Chromium</message>',
            '<message name="IDS_APP_MENU_PRODUCT_NAME">Chromium</message>',
            '<message name="IDS_WELCOME" meaning="welcome">Welcome to Chromium, <ph name="USER">$1<ex>Chrome User</ex></ph>.</message>',
            '<message name="IDS_LITERAL">Open <ph name="PRODUCT">Chromium<ex>Chromium</ex></ph>.</message>',
            '<message name="IDS_SPACED">\'\'\' Start Chromium. \'\'\'</message>',
            '<message name="IDS_COPYRIGHT">The Chromium Authors</message>',
            '<message name="IDS_IMPORT_CHROME">Import from Chrome</message>',
            '<message name="IDS_STORE">Chrome Web Store for Chromium</message>',
            '<message name="IDS_URL">Chromium: https://example.com/Chrome</message>',
            '<message name="IDS_VERSION_UI_LICENSE">Chromium uses <ph name="BEGIN_LINK_CHROMIUM">&lt;a href="$1"&gt;</ph>Chromium<ph name="END_LINK_CHROMIUM">&lt;/a&gt;</ph>.</message>',
            '<message name="IDS_LINK">Use &lt;a href="https://example.com/Chrome?x=1&amp;y=2"&gt;Chromium&lt;/a&gt;.</message>',
        ]
        grd = ('<grit base_dir="." source_lang_id="en" latest_public_release="0" current_release="1">'
               '<outputs/><translations><file path="resources/chromium_strings_ko.xtb" lang="ko"/></translations>'
               '<release seq="1"><messages><part file="parts/messages.grdp"/></messages></release></grit>')
        # 핵심 제품명 연결부는 루트에 두고 나머지는 재귀 part로 읽는다.
        grd = grd.replace('<part file="parts/messages.grdp"/>',
                          ''.join(self.fragments[:3]) + '<part file="parts/messages.grdp"/>')
        self.write('chrome/app/chromium_strings.grd', grd)
        self.write('chrome/app/parts/messages.grdp', '<grit-part>' + ''.join(self.fragments[3:]) + '</grit-part>')
        translations = [
            ('IDS_WELCOME', 'Chromium에 오신 것을 환영합니다, <ph name="USER"/>.'),
            ('IDS_LITERAL', '<ph name="PRODUCT"/> 열기.'),
            ('IDS_SPACED', 'Chromium 시작.'),
            ('IDS_COPYRIGHT', 'The Chromium Authors'),
            ('IDS_IMPORT_CHROME', 'Chrome에서 가져오기'),
            ('IDS_STORE', 'Chromium용 Chrome 웹 스토어'),
            ('IDS_URL', 'Chromium: https://example.com/Chrome'),
            ('IDS_VERSION_UI_LICENSE', 'Chromium은 <ph name="BEGIN_LINK_CHROMIUM"/>Chromium<ph name="END_LINK_CHROMIUM"/>을 사용합니다.'),
            ('IDS_LINK', '&lt;a href="https://example.com/Chrome?x=1&amp;y=2"&gt;Chromium&lt;/a&gt; 사용.'),
        ]
        nodes = {ET.fromstring(text).get('name'): ET.fromstring(text) for text in self.fragments}
        xtb = '<translationbundle lang="ko">' + ''.join(
            f'<translation id="{brand_strings.fingerprint(nodes[name])}">{text}</translation>'
            for name, text in translations) + '</translationbundle>'
        self.write('chrome/app/resources/chromium_strings_ko.xtb', xtb)
        subprocess.run(['git', 'init', '-q', str(self.src)], check=True)
        subprocess.run(['git', '-C', str(self.src), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.src), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture'], check=True)
        self.original_xtb = self.read('chrome/app/resources/chromium_strings_ko.xtb')
        self.parts(self.brand)

    def write(self, path, text):
        target = self.src / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')

    def read(self, path):
        return (self.src / path).read_text(encoding='utf-8')

    def parts(self, brand):
        for name, data in product_parts(brand).items():
            self.write('chrome/app/' + name, data)

    def install(self, brand=None, check=False):
        with contextlib.redirect_stdout(io.StringIO()):
            brand_strings.install(self.src, brand or self.brand, check)

    def translations(self):
        tree = grd_reader.Parse(str(self.src / 'chrome/app/chromium_strings.grd'))
        tree.SetOutputLanguage('ko')
        tree.RunGatherers()
        return {node.attrs['name']: node.Translate('ko', constants.DEFAULT_GENDER)
                for node in tree.GetChildrenOfType(message.MessageNode)}

    def snapshot(self):
        return {p.relative_to(self.src).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
                for p in self.src.rglob('*') if p.is_file() and '.git' not in p.parts}

    def test_translated_names_placeholders_and_external_names(self):
        self.install()
        self.assertTrue(brand_strings.check_inputs(self.src, self.brand)['applied'])
        ko = self.translations()
        self.assertEqual(ko['IDS_PRODUCT_NAME'], 'Orbit')
        self.assertEqual(ko['IDS_SHORT_PRODUCT_NAME'], 'Orb')
        self.assertEqual(ko['IDS_WELCOME'], 'Orbit에 오신 것을 환영합니다, $1.')
        self.assertEqual(ko['IDS_LITERAL'], 'Orbit 열기.')
        self.assertEqual(ko['IDS_SPACED'], 'Orbit 시작.')
        self.assertEqual(ko['IDS_COPYRIGHT'], 'The Chromium Authors')
        self.assertEqual(ko['IDS_IMPORT_CHROME'], 'Chrome에서 가져오기')
        self.assertEqual(ko['IDS_STORE'], 'Orbit용 Chrome 웹 스토어')
        self.assertEqual(ko['IDS_URL'], 'Orbit: https://example.com/Chrome')
        self.assertEqual(ko['IDS_VERSION_UI_LICENSE'], 'Orbit은 <a href="$1">Chromium</a>을 사용합니다.')
        self.assertEqual(ko['IDS_LINK'], '<a href="https://example.com/Chrome?x=1&y=2">Orbit</a> 사용.')
        self.assertEqual(self.read('chrome/app/resources/chromium_strings_ko.xtb'), self.original_xtb)

    def test_rename_starts_from_upstream_and_install_is_idempotent(self):
        self.install()
        before = self.snapshot()
        self.install(check=True)
        self.install()
        self.assertEqual(before, self.snapshot())
        brand = replace(self.brand, name='새 이름', short_name='새이름')
        self.parts(brand)
        self.install(brand)
        self.assertEqual(self.translations()['IDS_WELCOME'], '새 이름에 오신 것을 환영합니다, $1.')
        generated = self.read('chrome/app/yee_branding_strings/inputs/chrome/app/resources/chromium_strings_ko.xtb')
        self.assertNotIn('Orbit', generated)

    def test_utf8_policy_and_manifest_ignore_the_locale_encoding(self):
        brand = replace(self.brand, name='새 이름', short_name='새이름')
        self.parts(brand)
        original_open = Path.open
        for locale_encoding in ('cp949', 'cp1252'):
            with self.subTest(encoding=locale_encoding):
                def locale_open(path, mode='r', buffering=-1, encoding=None,
                                errors=None, newline=None):
                    if 'b' not in mode and encoding in (None, 'locale'):
                        encoding = locale_encoding
                    return original_open(path, mode, buffering, encoding, errors, newline)

                with mock.patch.object(Path, 'open', locale_open):
                    self.install(brand)
                    self.install(brand)
                    self.assertTrue(brand_strings.check_inputs(self.src, brand)['applied'])
        self.assertEqual(self.translations()['IDS_WELCOME'],
                         '새 이름에 오신 것을 환영합니다, $1.')

    def test_unmanaged_edits_fail_before_any_output_changes(self):
        self.install()
        path = self.src / 'chrome/app/chromium_strings.grd'
        path.write_text(path.read_text(encoding='utf-8').replace(
            'latest_public_release="0"', 'latest_public_release="2"'), encoding='utf-8')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Unmanaged change'):
            self.install(replace(self.brand, name='Nova'))
        self.assertEqual(before, self.snapshot())

    def test_output_parents_are_validated_before_any_write(self):
        (self.src / brand_strings.GENERATED).write_bytes(b'blocked output directory')
        before = self.snapshot()
        for check_only in (True, False):
            with self.subTest(check_only=check_only):
                with self.assertRaisesRegex(ValueError, 'parent is not a directory'):
                    self.install(check=check_only)
                self.assertEqual(before, self.snapshot())

    def test_fingerprints_match_grit_including_quoted_whitespace(self):
        for fragment in self.fragments:
            tree = util.ParseGrdForUnittest('<messages>' + fragment + '</messages>')
            node = tree.GetChildrenOfType(message.MessageNode)[0]
            self.assertEqual(brand_strings.fingerprint(ET.fromstring(fragment)),
                             str(node.GetCliques()[0].GetMessage().GetId()))

    def test_local_translation_edit_is_not_silently_bypassed(self):
        self.install()
        path = self.src / 'chrome/app/resources/chromium_strings_ko.xtb'
        path.write_text(path.read_text(encoding='utf-8').replace(
            'Chromium 시작.', '수정된 번역.'), encoding='utf-8')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Unmanaged change'):
            self.install(replace(self.brand, name='Nova'))
        self.assertEqual(before, self.snapshot())

    def test_read_only_audit_detects_stale_generated_translations(self):
        self.install()
        path = self.src / 'chrome/app/yee_branding_strings/inputs/chrome/app/resources/chromium_strings_ko.xtb'
        path.write_text(path.read_text(encoding='utf-8').replace('Orbit', 'Wrong Name'), encoding='utf-8')
        before = self.snapshot()
        status = brand_strings.check_inputs(self.src, self.brand)
        self.assertFalse(status['applied'])
        self.assertIn(path.relative_to(self.src).as_posix(), status['mismatches'])
        self.assertEqual(before, self.snapshot())


if __name__ == '__main__':
    unittest.main()
