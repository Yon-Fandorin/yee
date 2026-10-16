#!/usr/bin/env python3
"""대표 앱 아이콘과 별도로 쓰는 로고 경로의 회귀를 검사한다."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
import audit_logo_assets


class LogoAuditTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.src = Path(self.temp.name)

    def write(self, path, data='image fixture'):
        target = self.src / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(data)
        return path

    def test_new_tab_and_version_favicons_are_independent_consumers(self):
        self.write('chrome/app/theme/theme_resources.grd',
                   '<grit><structure name="IDR_NTP_FAVICON" file="common/favicon_ntp.png"/></grit>')
        self.write('components/resources/version_ui_scaled_resources.grdp',
                   '<grit><structure name="IDR_PRODUCT_FAVICON" file="chromium/favicon_product.png"/></grit>')
        paths = [self.write(f'{root}/default_{scale}_percent/{name}')
                 for root, name in [('chrome/app/theme', 'common/favicon_ntp.png'),
                                    ('components/resources', 'chromium/favicon_product.png')]
                 for scale in (100, 200)]
        self.assertEqual(audit_logo_assets.audit(self.src, [])['unmanaged'], sorted(paths))
        self.assertTrue(audit_logo_assets.audit(self.src, paths)['complete'])
        self.assertEqual(audit_logo_assets.audit(self.src, paths[:-1])['unmanaged'], paths[-1:])

    def test_future_native_variant_is_reported_and_service_icons_are_preserved(self):
        path = self.write('components/omnibox/browser/vector_icons/product_future.icon')
        self.write('components/omnibox/browser/vector_icons/google_logo.icon')
        self.write('components/omnibox/browser/vector_icons/productivity.icon')
        self.assertEqual(audit_logo_assets.audit(self.src, [])['unmanaged'], [path])

    def test_embedded_banner_and_webui_search_mark_are_not_hidden_by_app_coverage(self):
        paths = [self.write(path) for path in (
            'chrome/browser/resources/intro/images/refresh_showcase_illustration_chromium.png',
            'ui/webui/resources/cr_components/searchbox/icons/chrome_product_cr23.svg')]
        app = self.write('chrome/app/theme/chromium/mac/app.icns')
        self.assertEqual(audit_logo_assets.audit(self.src, [app])['unmanaged'], sorted(paths))

    def test_inline_and_preview_geometry_are_independent_consumers(self):
        preview = self.write('chrome/browser/resources/future/preview.svg',
                             '<path fill="var(--color-mini-ntp-chrome-logo)"/>')
        inline = self.write('chrome/browser/resources/webui_toolbar/icons.ts',
                            '<svg><g id="chrome_product"><path/></g></svg>')
        self.assertEqual(audit_logo_assets.audit(self.src, [inline])['unmanaged'], [preview])
        self.assertTrue(audit_logo_assets.audit(self.src, [inline, preview])['complete'])

    def test_hyphenated_shared_and_local_product_iconsets_are_independent_consumers(self):
        paths = [self.write(path, '<svg><g viewBox="0 0 24 24" id = "chrome-product"><path/></g></svg>')
                 for path in (
                     'ui/webui/resources/cr_elements/icons.html.ts',
                     'chrome/browser/resources/settings/icons.html.ts',
                     'chrome/browser/resources/webui_browser/icons.html.ts')]
        self.write('ui/webui/resources/cr_elements/functional_icons.ts',
                   '<svg><g id="chrome-extension"/><g id="chrome-web-store"/><g id="google-logo"/></svg>')
        self.assertEqual(audit_logo_assets.audit(self.src, [])['unmanaged'], sorted(paths))
        self.assertTrue(audit_logo_assets.audit(self.src, paths)['complete'])


if __name__ == '__main__':
    unittest.main()
