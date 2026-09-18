"""Verify vendor replacement with tiny, pinned registry archives."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('yee_vendor_test', ROOT / 'tools/overlay/vendor-yee-adblock.py')
vendor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vendor)


class VendorTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-vendor-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.upstream = self.root / 'registry/src/index/adblock-0.0.1'
        self.upstream.mkdir(parents=True)
        files = {'src/lib.rs': b'pub fn fixture() {}\n', 'Cargo.toml': b'[package]\n', 'LICENSE': b'Fixture license\n'}
        archive = self.root / 'registry/cache/index/adblock-0.0.1.crate'
        archive.parent.mkdir(parents=True)
        with tarfile.open(archive, 'w:gz') as output:
            for name, content in files.items():
                path = self.upstream / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
                entry = tarfile.TarInfo('adblock-0.0.1/' + name)
                entry.size = len(content)
                output.addfile(entry, io.BytesIO(content))
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        (self.root / 'Cargo.lock').write_text(
            '[[package]]\nname="adblock"\nversion="0.0.1"\nchecksum="' + checksum + '"\n')
        package = {'id': 'fixture', 'name': 'adblock', 'version': '0.0.1',
                   'manifest_path': str(self.upstream / 'Cargo.toml'), 'edition': '2024',
                   'authors': [], 'license': 'MPL-2.0', 'source': 'registry+fixture',
                   'targets': [{'name': 'adblock', 'kind': ['lib'], 'src_path': str(self.upstream / 'src/lib.rs')}]}
        self.metadata = {'workspace_root': str(self.root), 'packages': [package],
                         'resolve': {'nodes': [{'id': 'fixture', 'features': [], 'deps': []}]}}
        self.destination = self.root / 'vendor'
        self.previous_root = vendor.ROOT
        vendor.ROOT = self.root
        self.addCleanup(setattr, vendor, 'ROOT', self.previous_root)

    def test_revendoring_removes_stale_files_and_does_not_scan_old_gn(self):
        self.assertEqual(vendor.vendor(self.metadata, self.destination), 1)
        stale = self.destination / 'adblock_0_0_1/stale.rs'
        stale.write_text('stale')
        vendor.vendor(self.metadata, self.destination)
        self.assertFalse(stale.exists())
        build = (self.destination / 'adblock_0_0_1/BUILD.gn').read_text()
        self.assertNotIn('stale.rs', build)
        self.assertNotIn('/BUILD.gn"', build)

    def test_source_mismatch_preserves_entire_previous_tree(self):
        vendor.vendor(self.metadata, self.destination)
        before = {p.relative_to(self.destination): p.read_bytes() for p in self.destination.rglob('*') if p.is_file()}
        (self.upstream / 'src/lib.rs').write_text('changed')
        with self.assertRaises(ValueError):
            vendor.vendor(self.metadata, self.destination)
        after = {p.relative_to(self.destination): p.read_bytes() for p in self.destination.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_unverified_registry_file_is_not_compiled_or_published(self):
        (self.upstream / 'unexpected.rs').write_text('not in pinned archive')
        with self.assertRaises(ValueError):
            vendor.vendor(self.metadata, self.destination)
        self.assertFalse(self.destination.exists())

    def test_cargo_extraction_marker_is_preserved(self):
        (self.upstream / '.cargo-ok').write_text('')
        vendor.vendor(self.metadata, self.destination)
        self.assertTrue((self.destination / 'adblock_0_0_1/.cargo-ok').is_file())
