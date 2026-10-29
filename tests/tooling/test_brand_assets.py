#!/usr/bin/env python3
"""자산 묶음의 출처, 경로 제한과 반복 설치를 검증한다."""

import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
import brand_assets

POWERSHELL = shutil.which('pwsh') or shutil.which('powershell')


class PackagedMacIconTest(unittest.TestCase):
    def test_icns_contains_all_standard_and_retina_representations(self):
        manifest = ROOT / 'assets/brand/generated/manifest.json'
        entries = json.loads(manifest.read_text(encoding='utf-8'))['files']
        entry, = [item for item in entries
                  if item['destination'] == 'chrome/app/theme/chromium/mac/app.icns']
        data = (manifest.parent / entry['source']).read_bytes()
        self.assertEqual(struct.unpack_from('>4sI', data), (b'icns', len(data)))
        expected = {
            b'icp4': 16, b'icp5': 32, b'ic07': 128, b'ic08': 256, b'ic09': 512,
            b'ic10': 1024, b'ic11': 32, b'ic12': 64, b'ic13': 256, b'ic14': 512,
        }
        representations = {}
        offset = 8
        while offset < len(data):
            kind, length = struct.unpack_from('>4sI', data, offset)
            self.assertGreaterEqual(length, 32, kind)
            self.assertLessEqual(offset + length, len(data), kind)
            self.assertNotIn(kind, representations)
            png = data[offset + 8:offset + length]
            self.assertEqual(png[:16], b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR', kind)
            representations[kind] = struct.unpack_from('>II', png, 16)
            offset += length
        self.assertEqual(offset, len(data))
        self.assertEqual(representations, {kind: (size, size) for kind, size in expected.items()})


class BrandAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='yee-brand-assets-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        self.src = self.root / 'src'
        self.package = self.repo / 'assets/brand/generated'
        self.package.mkdir(parents=True)
        (self.repo / 'build').mkdir()
        (self.repo / 'build/overlay.json').write_text(json.dumps({'generated_roots': ['chrome/app/theme/chromium']}))
        self.src.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.src)], check=True)
        for relative in brand_assets.UPSTREAM_INPUTS:
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('upstream input fixture\n', encoding='utf-8')
        self.commit_inputs()
        # 작업 트리에 원본이 없어도 HEAD blob을 검증해야 한다.
        for family in ('chrome', 'ui'):
            shutil.rmtree(self.src / family)
        self.source = self.repo / 'assets/brand/mark.svg'
        self.source.write_text('<svg/>')
        file = self.package / 'mark.png'
        file.write_bytes(b'asset output fixture')
        self.manifest = self.package / 'manifest.json'
        self.data = {'schema_version': 1, 'product_name': 'Orbit', 'short_name': 'Orbit',
            'generator_sha256': brand_assets.digest(ROOT / 'tools/overlay/generate-brand-assets.py'),
            'source_sha256': brand_assets.digest(self.source), 'palette': {'primary': '#123456'},
            'upstream_inputs': brand_assets.upstream_input_blobs(self.src),
            'files': [{'source': 'mark.png', 'destination': 'chrome/app/theme/chromium/product_logo_16.png',
                       'sha256': brand_assets.digest(file)}]}
        self.brand = SimpleNamespace(name='Orbit', short_name='Orbit', logo_source=self.source,
            logo_assets_manifest=self.manifest, logo_palette={'primary': '#123456'})

    def commit_inputs(self):
        subprocess.run(['git', '-C', str(self.src), 'add', *brand_assets.UPSTREAM_INPUTS], check=True)
        subprocess.run(['git', '-C', str(self.src), '-c', 'user.name=Fixture',
                        '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'upstream inputs'], check=True)

    def plan(self):
        self.manifest.write_text(json.dumps(self.data))
        return brand_assets.asset_plan(self.brand, self.src, self.repo)

    def prepare_cli(self):
        self.overlay = self.repo / 'tools/overlay'
        self.overlay.mkdir(parents=True)
        for name in ('brand_config.py', 'brand_assets.py', 'install-brand-assets.sh', 'audit_logo_assets.py'):
            shutil.copyfile(ROOT / 'tools/overlay' / name, self.overlay / name)
        generator = self.overlay / 'generate-brand-assets.py'
        generator.write_text('''import hashlib
import json
from pathlib import Path
import sys
from brand_assets import upstream_input_blobs

root = Path(__file__).resolve().parents[2]
config = json.loads((root / 'branding/brand.json').read_text(encoding='utf-8'))
manifest = root / config['logo_assets_manifest']
output = manifest.parent / 'mark.png'
output.write_bytes(b'fresh generated asset')
destination = root / 'generator-output-destination'
destination = destination.read_text(encoding='utf-8') if destination.is_file() else 'chrome/app/theme/chromium/product_logo_16.png'
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
data = {'schema_version': 1, 'product_name': config['name'],
        'short_name': config.get('short_name', config['name']),
        'source_sha256': digest(root / config['logo_source']),
        'generator_sha256': digest(Path(__file__)), 'palette': config['logo_palette'],
        'upstream_inputs': upstream_input_blobs(Path(sys.argv[1])),
        'files': [{'source': 'mark.png', 'destination': destination, 'sha256': digest(output)}]}
manifest.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
with (root / 'generator-runs').open('a', encoding='utf-8') as runs:
    runs.write('generated\\n')
''', encoding='utf-8')
        (self.repo / 'branding').mkdir()
        (self.repo / 'branding/brand.json').write_text(json.dumps({
            'name': self.brand.name, 'short_name': self.brand.short_name,
            'logo_source': self.source.relative_to(self.repo).as_posix(),
            'logo_assets_manifest': self.manifest.relative_to(self.repo).as_posix(),
            'logo_palette': self.brand.logo_palette, 'internal_url_scheme': 'orbit',
        }, ensure_ascii=False), encoding='utf-8')
        self.data['generator_sha256'] = brand_assets.digest(generator)
        self.manifest.write_text(json.dumps(self.data, ensure_ascii=False), encoding='utf-8')
        branding = self.src / 'chrome/app/theme/chromium/BRANDING'
        branding.parent.mkdir(parents=True)
        branding.write_text('PRODUCT_FULLNAME=Orbit\n', encoding='utf-8')

    def run_cli(self, *options, env=None):
        return subprocess.run([
            'zsh', str(self.overlay / 'install-brand-assets.sh'), str(self.src), *options,
        ], capture_output=True, text=True, encoding='utf-8',
            env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1', **(env or {})})

    def snapshot(self, root):
        return {path.relative_to(root).as_posix(): (path.read_bytes(), path.stat().st_mtime_ns)
                for path in root.rglob('*') if path.is_file() and '.git' not in path.parts}

    @unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
    def test_prepare_refreshes_each_stale_input_without_installing(self):
        self.prepare_cli()
        changes = [('product_name', 'Old Orbit'), ('short_name', 'OLD'),
                   ('source_sha256', '0' * 64), ('palette', {'primary': '#654321'}),
                   ('generator_sha256', '0' * 64), ('upstream_inputs', {})]
        runs = self.repo / 'generator-runs'
        for index, (key, value) in enumerate(changes, 1):
            with self.subTest(key=key):
                stale = {**self.data, key: value}
                self.manifest.write_text(json.dumps(stale), encoding='utf-8')
                before = self.snapshot(self.root)
                checked = self.run_cli('--check')
                self.assertNotEqual(checked.returncode, 0)
                self.assertIn('Brand asset package is stale', checked.stderr)
                self.assertEqual(before, self.snapshot(self.root))
                checkout = self.snapshot(self.src)
                prepared = self.run_cli('--prepare')
                self.assertEqual(prepared.returncode, 0, prepared.stderr)
                self.assertEqual(checkout, self.snapshot(self.src))
                self.assertEqual(len(runs.read_text(encoding='utf-8').splitlines()), index)
                installed = self.run_cli('--install-prepared')
                self.assertEqual(installed.returncode, 0, installed.stderr)
                self.assertEqual(len(runs.read_text(encoding='utf-8').splitlines()), index)
                target = self.src / self.data['files'][0]['destination']
                self.assertEqual(target.read_bytes(), b'fresh generated asset')

        # 제품 설정이 같아도 Chromium 원본이 바뀌면 검사·설치 전에 재생성한다.
        for relative in brand_assets.UPSTREAM_INPUTS:
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('new upstream input fixture\n', encoding='utf-8')
        self.commit_inputs()
        checkout = self.snapshot(self.src)
        for option in ('--check', '--install-prepared'):
            rejected = self.run_cli(option)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('Brand asset package is stale', rejected.stderr)
            self.assertEqual(checkout, self.snapshot(self.src))
        prepared = self.run_cli('--prepare')
        self.assertEqual(prepared.returncode, 0, prepared.stderr)
        self.assertEqual(checkout, self.snapshot(self.src))
        self.assertEqual(json.loads(self.manifest.read_text())['upstream_inputs'],
                         brand_assets.upstream_input_blobs(self.src))
        self.assertEqual(len(runs.read_text().splitlines()), len(changes) + 1)

    @unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
    def test_prepare_validates_generated_destinations_before_any_checkout_write(self):
        self.prepare_cli()
        self.data['product_name'] = 'Old Orbit'
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')
        (self.repo / 'generator-output-destination').write_text(
            'chrome/browser/unowned.png', encoding='utf-8')
        before = self.snapshot(self.src)
        prepared = self.run_cli('--prepare')
        self.assertNotEqual(prepared.returncode, 0)
        self.assertIn('not an owned generated path', prepared.stderr)
        self.assertEqual(before, self.snapshot(self.src))
        runs = (self.repo / 'generator-runs').read_bytes()
        installed = self.run_cli('--install-prepared')
        self.assertNotEqual(installed.returncode, 0)
        self.assertEqual(before, self.snapshot(self.src))
        self.assertEqual(runs, (self.repo / 'generator-runs').read_bytes())

    @unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
    def test_default_install_still_refreshes_stale_assets(self):
        self.prepare_cli()
        self.data['product_name'] = 'Old Orbit'
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')
        installed = self.run_cli()
        self.assertEqual(installed.returncode, 0, installed.stderr)
        target = self.src / self.data['files'][0]['destination']
        self.assertEqual(target.read_bytes(), b'fresh generated asset')
        self.assertEqual((self.repo / 'generator-runs').read_text(encoding='utf-8'), 'generated\n')
        for relative in brand_assets.UPSTREAM_INPUTS:
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('new upstream input fixture\n', encoding='utf-8')
        self.commit_inputs()
        installed = self.run_cli()
        self.assertEqual(installed.returncode, 0, installed.stderr)
        self.assertEqual((self.repo / 'generator-runs').read_text(encoding='utf-8'),
                         'generated\ngenerated\n')

    @unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
    def test_utf8_metadata_and_webui_are_independent_of_the_default_locale(self):
        self.brand.name = self.brand.short_name = '새 이름'
        self.data['product_name'] = self.data['short_name'] = '새 이름'
        self.prepare_cli()
        catalog = self.repo / 'build/overlay.json'
        data = json.loads(catalog.read_text(encoding='utf-8'))
        data['description'] = '제품 로고 자산'
        catalog.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
        (self.src / 'chrome/app/theme/theme_resources.grd').write_text('<grit/>', encoding='utf-8')
        webui = self.src / 'chrome/browser/resources/future/messages.ts'
        webui.parent.mkdir(parents=True)
        webui.write_text('export const title = "새 이름";\n', encoding='utf-8')
        env = {'LC_ALL': 'C', 'PYTHONUTF8': '0', 'PYTHONCOERCECLOCALE': '0',
               'PYTHONIOENCODING': 'utf-8', 'PYTHONDONTWRITEBYTECODE': '1'}
        before = self.snapshot(self.root)
        for option in ('--check', '--prepare'):
            result = self.run_cli(option, env=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(before, self.snapshot(self.root))
        installed = self.run_cli('--install-prepared', env=env)
        self.assertEqual(installed.returncode, 0, installed.stderr)
        audited = subprocess.run([
            sys.executable, str(self.overlay / 'audit_logo_assets.py'), str(self.src), '--json',
        ], capture_output=True, text=True, encoding='utf-8', env={**os.environ, **env})
        self.assertEqual(audited.returncode, 0, audited.stderr)
        self.assertTrue(json.loads(audited.stdout)['complete'])

    def test_preview_is_read_only_and_install_is_idempotent(self):
        plan, count = self.plan()
        self.assertEqual(count, 1)
        self.assertFalse(self.snapshot(self.src))
        brand_assets.install(plan)
        target = plan[0][1]
        stamp = target.stat().st_mtime_ns
        self.assertEqual(self.plan(), ([], 1))
        brand_assets.install([])
        self.assertEqual(target.stat().st_mtime_ns, stamp)

    def prepare_embeds(self):
        self.source.write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">'
            '<path d="M0 0L128 0L0 128Z"/></svg>', encoding='utf-8')
        self.data['source_sha256'] = brand_assets.digest(self.source)
        for index, relative in enumerate(brand_assets.UPSTREAM_INPUTS):
            if index == 0:
                text = ('<svg xmlns="http://www.w3.org/2000/svg">'
                        '<path fill="var(--color-side-panel-customize-chrome-mini-ntp-chrome-logo)" '
                        'd="M0 0Z"/></svg>')
            elif index == 1:
                text = "'chrome_product': () => getTrustedHTML`<svg><path/></svg>`"
            else:
                text = '<g id="chrome-product"><path/></g>' * 2
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text + '\n', encoding='utf-8')
            packaged = self.package / 'files' / relative
            packaged.parent.mkdir(parents=True, exist_ok=True)
            packaged.write_text('old embedded output\n', encoding='utf-8')
            self.data['files'].append({
                'source': packaged.relative_to(self.package).as_posix(),
                'destination': relative, 'sha256': brand_assets.digest(packaged),
            })
        self.commit_inputs()
        self.data['upstream_inputs'] = brand_assets.upstream_input_blobs(self.src)
        (self.repo / 'build/overlay.json').write_text(json.dumps({
            'generated_roots': ['chrome/app/theme/chromium', *brand_assets.UPSTREAM_INPUTS],
        }), encoding='utf-8')
        path = self.src / brand_assets.UPSTREAM_INPUTS[1]
        path.write_text(path.read_text(encoding='utf-8') + '// new upstream feature\n', encoding='utf-8')
        self.commit_inputs()
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')

    def test_embedded_refresh_preserves_raster_checkout_and_new_upstream_features(self):
        self.prepare_embeds()
        raster = self.package / 'mark.png'
        raster_before = (raster.read_bytes(), raster.stat().st_mtime_ns)
        checkout = self.snapshot(self.src)
        self.assertEqual(brand_assets.refresh_embeds(self.brand, self.src, self.repo), 5)
        self.assertEqual(checkout, self.snapshot(self.src))
        self.assertEqual(raster_before, (raster.read_bytes(), raster.stat().st_mtime_ns))
        toolbar = self.package / 'files' / brand_assets.UPSTREAM_INPUTS[1]
        self.assertIn('// new upstream feature', toolbar.read_text(encoding='utf-8'))
        self.assertIn('M0 0L128 0L0 128Z', toolbar.read_text(encoding='utf-8'))
        self.assertEqual(brand_assets.asset_plan(self.brand, self.src, self.repo)[1], 6)
        package = self.snapshot(self.package)
        self.assertEqual(brand_assets.refresh_embeds(self.brand, self.src, self.repo), 0)
        self.assertEqual(package, self.snapshot(self.package))

    def test_embedded_refresh_rejects_changed_rendering_inputs_and_corrupt_package(self):
        self.prepare_embeds()
        for key, value in [('product_name', 'Other'), ('generator_sha256', '0' * 64),
                           ('source_sha256', '0' * 64), ('palette', {}), ('upstream_inputs', {})]:
            with self.subTest(key=key):
                self.manifest.write_text(json.dumps({**self.data, key: value}), encoding='utf-8')
                before = self.snapshot(self.root)
                with self.assertRaises(ValueError):
                    brand_assets.refresh_embeds(self.brand, self.src, self.repo)
                self.assertEqual(before, self.snapshot(self.root))
        self.manifest.write_text(json.dumps(self.data), encoding='utf-8')
        (self.package / 'mark.png').write_bytes(b'corrupt raster')
        before = self.snapshot(self.root)
        with self.assertRaisesRegex(ValueError, 'modified'):
            brand_assets.refresh_embeds(self.brand, self.src, self.repo)
        self.assertEqual(before, self.snapshot(self.root))

    def test_name_palette_and_source_changes_require_regeneration(self):
        for key, value in [('product_name', 'Nova'), ('short_name', 'N'),
                           ('product_name', 'ORBIT'), ('short_name', 'ORBIT'),
                           ('palette', {'primary': '#654321'}), ('source_sha256', '0' * 64)]:
            with self.subTest(key=key):
                saved = self.data[key]
                self.data[key] = value
                with self.assertRaisesRegex(ValueError, 'stale'):
                    self.plan()
                self.data[key] = saved

    @unittest.skipUnless(POWERSHELL, 'PowerShell 실행 파일이 필요하다')
    def test_windows_installer_rejects_stale_names_and_upstream_inputs(self):
        overlay = self.repo / 'tools/overlay'
        overlay.mkdir(parents=True)
        for name in ('brand-config.ps1', 'install-brand-assets.ps1', 'generate-brand-assets.py'):
            shutil.copyfile(ROOT / 'tools/overlay' / name, overlay / name)
        (self.repo / 'branding').mkdir()
        (self.repo / 'branding/brand.json').write_text(json.dumps({
            'name': self.brand.name, 'short_name': self.brand.short_name,
            'logo_source': self.source.relative_to(self.repo).as_posix(),
            'logo_assets_manifest': self.manifest.relative_to(self.repo).as_posix(),
            'logo_palette': self.brand.logo_palette,
        }))
        branding = self.src / 'chrome/app/theme/chromium/BRANDING'
        branding.parent.mkdir(parents=True)
        branding.write_text('PRODUCT_FULLNAME=Orbit\n')

        def check():
            self.manifest.write_text(json.dumps(self.data))
            return subprocess.run([
                POWERSHELL, '-NoLogo', '-NoProfile', '-NonInteractive',
                '-ExecutionPolicy', 'Bypass', '-File', str(overlay / 'install-brand-assets.ps1'),
                '-ChromiumSrc', str(self.src), '-CheckOnly',
            ], capture_output=True, text=True, encoding='utf-8')

        valid = check()
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertIn('1 verified, 1 pending', valid.stdout)
        target = branding.with_name('product_logo_16.png')
        for key in ('product_name', 'short_name'):
            with self.subTest(key=key):
                self.data[key] = 'ORBIT'
                stale = check()
                self.assertNotEqual(stale.returncode, 0)
                self.assertIn('Brand asset package is stale', stale.stderr)
                self.assertFalse(target.exists())
                self.data[key] = 'Orbit'
        self.data['upstream_inputs'] = {}
        stale = check()
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn('Brand asset package is stale', stale.stderr)
        self.data['upstream_inputs'] = brand_assets.upstream_input_blobs(self.src)
        for relative in brand_assets.UPSTREAM_INPUTS:
            path = self.src / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('new upstream input fixture\n', encoding='utf-8')
        self.commit_inputs()
        stale = check()
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn('Brand asset package is stale', stale.stderr)
        self.assertFalse(target.exists())

    def test_modified_asset_fails_before_any_output_is_written(self):
        (self.package / 'mark.png').write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'modified'):
            self.plan()
        self.assertFalse(self.snapshot(self.src))

    def test_escape_unowned_and_duplicate_destinations_are_rejected(self):
        original = self.data['files'][0].copy()
        for field, value in [('source', '../mark.svg'), ('destination', '../outside.png'),
                             ('destination', 'chrome/browser/original.cc')]:
            with self.subTest(value=value):
                self.data['files'] = [original.copy(), {**original, field: value}]
                with self.assertRaises(ValueError):
                    self.plan()
        self.data['files'] = [original.copy(), original.copy()]
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.plan()
        self.assertFalse(self.snapshot(self.src))

    def test_symlink_destinations_and_file_parents_are_rejected(self):
        (self.src / 'chrome').symlink_to(self.package, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'links'):
            self.plan()
        (self.src / 'chrome').unlink()
        (self.src / 'chrome').write_text('file, not directory')
        with self.assertRaisesRegex(ValueError, 'parent is not a directory'):
            self.plan()


if __name__ == '__main__':
    unittest.main()
