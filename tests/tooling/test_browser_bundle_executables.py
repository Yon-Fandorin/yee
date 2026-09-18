#!/usr/bin/env python3
"""Verify rename discovery and shutdown scope using temporary bundle fixtures."""

from pathlib import Path
import plistlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/dev"))

from browser_bundle_executables import browser_executables, browser_processes, CURRENT_BUNDLE_ID


class BundleExecutablesTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        self.output = self.root / "output"
        self.output.mkdir()

    def bundle(self, name, identity=CURRENT_BUNDLE_ID, executable=None, parent=None):
        path = (parent or self.output) / (name + ".app")
        (path / "Contents/MacOS").mkdir(parents=True)
        binary = path / "Contents/MacOS" / name
        binary.touch()
        (path / "Contents/Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": identity, "CFBundleExecutable": executable or name}))
        return binary

    def test_old_and_new_names_are_independent_of_current_config(self):
        old = self.bundle("Yee")
        new = self.bundle("O'Reilly & 새 이름")
        self.assertEqual(browser_executables(self.output), sorted(map(str, (old, new))))

    def test_excludes_other_id_outside_output_and_helper(self):
        self.bundle("Other", identity="com.other.browser")
        outside = self.bundle("Outside", parent=self.root)
        (self.output / "Outside.app").symlink_to(outside.parents[2])
        valid = self.bundle("Orbit")
        self.bundle("Orbit Helper", parent=valid.parents[2] / "Contents")
        self.assertEqual(browser_executables(self.output), [str(valid)])

    def test_rejects_executable_traversal_and_outside_symlink(self):
        self.bundle("Traversal", executable="../escape")
        binary = self.bundle("Link")
        binary.unlink()
        outside = self.root / "external-binary"
        outside.touch()
        binary.symlink_to(outside)
        self.assertEqual(browser_executables(self.output), [])

    def test_skips_invalid_plist_and_missing_binary(self):
        broken = self.bundle("Broken")
        (broken.parents[1] / "Info.plist").write_bytes(b"not a plist")
        self.bundle("Missing").unlink()
        self.assertEqual(browser_executables(self.output), [])

    def test_running_inventory_tracks_identity_across_names_and_build_roots(self):
        old = self.bundle('Yee')
        new = self.bundle('새 이름', parent=self.root)
        other = self.bundle('Other', identity='com.other.browser')
        helper = self.bundle('Helper', identity=CURRENT_BUNDLE_ID + '.helper')
        listing = '\n'.join(f'{pid} {binary}' for pid, binary in enumerate([old, new, other, helper], 100))
        listing += f'\n999 /usr/bin/python {old}\n'
        self.assertEqual(browser_processes(listing), [
            {'pid': 100, 'executable': str(old)}, {'pid': 101, 'executable': str(new)}])


if __name__ == "__main__":
    unittest.main()
