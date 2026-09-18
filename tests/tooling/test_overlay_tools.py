#!/usr/bin/env python3
"""Test overlay boundaries and all-before-write preflight with disposable files."""

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
from lib.overlay_tools import (apply_patch_plan, load_catalog, patch_paths, patch_plan,
                               shell_exclusions, source_plan, sync_sources)


def git(src, *args):
    return subprocess.run(['git', '-C', str(src), *args], check=True,
                          capture_output=True).stdout


class OverlayToolsTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='yee-overlay-structure-')
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.overlay = self.base / 'overlay'
        self.overlay.mkdir()
        self.src = self.base / 'chromium src'
        self.src.mkdir()
        git(self.src, 'init', '-q')
        for name in ('shell.cc', 'brand.cc', 'version.cc', 'windows.bat'):
            (self.src / name).write_text('upstream\n')
        git(self.src, 'add', '.')
        git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'upstream')
        self.catalog = copy.deepcopy(load_catalog())
        self.catalog['new_glue'] = []
        for patch, paths in zip(self.catalog['patches'],
                                [('shell.cc',), ('brand.cc', 'version.cc'), ('windows.bat',)]):
            patch['file'] = 'patches/' + patch['role'] + '.patch'
            output = self.overlay / patch['file']
            output.parent.mkdir(exist_ok=True)
            text = ''
            for path in paths:
                text += (f'diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n'
                         '@@ -1 +1 @@\n-upstream\n+overlay\n')
            output.write_text(text)
        (self.overlay / 'build').mkdir()
        (self.overlay / 'build/overlay.json').write_text(json.dumps(self.catalog))
        for binding in self.catalog['source_roots']:
            owned = self.overlay / binding['source']
            owned.mkdir(parents=True)
            (owned / 'fixture.cc').write_text('owned source\n')

    def apply(self, plan, check_only=False):
        with contextlib.redirect_stdout(io.StringIO()):
            apply_patch_plan(self.src, plan, check_only)

    def sync(self, plan, check_only=False):
        with contextlib.redirect_stdout(io.StringIO()):
            sync_sources(plan, check_only)

    def test_fresh_preview_apply_and_reapply(self):
        before = git(self.src, 'diff')
        plan = patch_plan(self.src, self.catalog, self.overlay)
        self.apply(plan, check_only=True)
        self.assertEqual(git(self.src, 'diff'), before)
        self.apply(plan)
        self.assertTrue(all((self.src / p).read_text() == 'overlay\n'
                            for p in ('shell.cc', 'brand.cc', 'version.cc', 'windows.bat')))
        self.assertFalse(any(s.pending for s in patch_plan(self.src, self.catalog, self.overlay)))

    def test_late_patch_conflict_is_rejected_before_any_write(self):
        (self.src / 'windows.bat').write_text('conflict\n')
        before = git(self.src, 'diff')
        with self.assertRaises(RuntimeError):
            self.apply(patch_plan(self.src, self.catalog, self.overlay))
        self.assertEqual(git(self.src, 'diff'), before)
        self.assertEqual((self.src / 'shell.cc').read_text(), 'upstream\n')

    def test_partial_branding_sections_migrate_without_reapplying_earlier_file(self):
        (self.src / 'brand.cc').write_text('overlay\n')
        plan = patch_plan(self.src, self.catalog, self.overlay, role='branding')
        self.assertEqual([(p.include, p.pending) for p in plan],
                         [('brand.cc', False), ('version.cc', True)])
        self.apply(plan)
        self.assertEqual((self.src / 'shell.cc').read_text(), 'upstream\n')

    def test_late_branding_section_conflict_does_not_apply_first_section(self):
        (self.src / 'version.cc').write_text('conflict\n')
        with self.assertRaises(RuntimeError):
            patch_plan(self.src, self.catalog, self.overlay, role='branding')
        self.assertEqual((self.src / 'brand.cc').read_text(), 'upstream\n')

    def test_patch_ownership_overlap_and_unknown_role_are_rejected(self):
        (self.overlay / self.catalog['patches'][-1]['file']).write_text(
            (self.overlay / self.catalog['patches'][0]['file']).read_text())
        with self.assertRaises(ValueError):
            patch_plan(self.src, self.catalog, self.overlay)
        with self.assertRaises(ValueError):
            patch_plan(self.src, self.catalog, self.overlay, role='missing')

    def test_catalog_rejects_overlapping_or_escaping_paths(self):
        for root in ('browser/ui/nested', '../outside', '/absolute'):
            catalog = copy.deepcopy(self.catalog)
            catalog['source_roots'].append({'source': root, 'destination': 'chrome/browser/extra_owner'})
            (self.overlay / 'build/overlay.json').write_text(json.dumps(catalog))
            with self.subTest(root=root), self.assertRaises(ValueError):
                load_catalog(self.overlay)

    def test_patch_parser_refuses_renamed_and_malformed_sections(self):
        patch = self.overlay / 'bad.patch'
        for text in ('diff --git a/old.cc b/new.cc\n', 'diff --git "a/a b" "b/a b"\n',
                     'diff --git a/../outside b/../outside\n'):
            patch.write_text(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                patch_paths(patch)

    def test_sources_copy_all_owned_roots_and_preserve_unchanged_mtimes(self):
        source_root = self.overlay
        vendor = source_root / self.catalog['source_roots'][-1]['source']
        (vendor / '.cargo').mkdir()
        (vendor / '.cargo/checksum.json').write_text('{}')
        (vendor / '.DS_Store').write_text('metadata')
        (vendor / '__pycache__').mkdir()
        (vendor / '__pycache__/cached.pyc').write_bytes(b'cache')
        for family in {binding['source'].split('/')[0] for binding in self.catalog['source_roots']}:
            (source_root / family / 'README.md').write_text('Repository documentation\n')
        (vendor / 'README.md').write_text('Original vendor documentation\n')
        plan = source_plan(self.src, self.catalog, self.overlay)
        self.assertEqual(len(plan), len(self.catalog['source_roots']) + 2)
        self.assertEqual([destination.relative_to(self.src).as_posix()
                          for source, destination in plan if source.name == 'README.md'],
                         [self.catalog['source_roots'][-1]['destination'] + '/README.md'])
        self.sync(plan, check_only=True)
        self.assertFalse((self.src / 'chrome').exists())
        self.sync(plan)
        mtimes = {destination: destination.stat().st_mtime_ns for _, destination in plan}
        self.assertEqual(source_plan(self.src, self.catalog, self.overlay), [])
        self.assertEqual(mtimes, {p: p.stat().st_mtime_ns for p in mtimes})
        self.assertFalse((self.src / '.DS_Store').exists())
        self.assertFalse(any(self.src.rglob('*.pyc')))

    def test_equal_size_and_mtime_content_changes_are_still_synchronized(self):
        self.sync(source_plan(self.src, self.catalog, self.overlay))
        source = self.overlay / self.catalog['source_roots'][0]['source'] / 'fixture.cc'
        saved = source.stat()
        source.write_text('other source\n')
        os.utime(source, ns=(saved.st_atime_ns, saved.st_mtime_ns))
        self.assertEqual(len(source_plan(self.src, self.catalog, self.overlay)), 1)

    def test_unregistered_source_and_missing_owner_fail_before_copy(self):
        extra = self.overlay / 'browser/unowned.cc'
        extra.write_text('unowned\n')
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        self.assertFalse((self.src / 'chrome').exists())
        extra.unlink()
        extra = self.overlay / 'browser/unowned/README.md'
        extra.parent.mkdir()
        extra.write_text('Unregistered directory\n')
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        extra.unlink()
        catalog = copy.deepcopy(self.catalog)
        catalog['source_roots'].append({'source': 'browser/missing_owner', 'destination': 'chrome/browser/missing_owner'})
        with self.assertRaises(ValueError):
            source_plan(self.src, catalog, self.overlay)

    def test_destination_file_directory_conflicts_fail_before_copy(self):
        (self.src / 'chrome').write_text('blocked parent')
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        self.assertFalse((self.src / 'components').exists())

    def test_deleted_sources_are_previewed_and_pruned_only_inside_owned_roots(self):
        self.sync(source_plan(self.src, self.catalog, self.overlay))
        binding = self.catalog['source_roots'][0]
        original = self.overlay / binding['source'] / 'fixture.cc'
        destination = self.src / binding['destination'] / 'fixture.cc'
        original.unlink()
        untouched = self.src / 'chrome/upstream.cc'
        untouched.write_text('Chromium original')
        generated = self.src / self.catalog['generated_roots'][0] / 'logo.png'
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.write_bytes(b'generated')
        plan = source_plan(self.src, self.catalog, self.overlay)
        self.assertEqual(plan, [(None, destination)])
        self.sync(plan, check_only=True)
        self.assertTrue(destination.is_file())
        self.sync(plan)
        self.assertFalse(destination.exists())
        self.assertEqual(untouched.read_text(), 'Chromium original')
        self.assertEqual(generated.read_bytes(), b'generated')
        self.assertEqual(source_plan(self.src, self.catalog, self.overlay), [])

    def test_stale_destination_link_is_rejected_before_any_mutation(self):
        self.sync(source_plan(self.src, self.catalog, self.overlay))
        binding = self.catalog['source_roots'][0]
        destination = self.src / binding['destination'] / 'stale.cc'
        destination.symlink_to(self.src / 'shell.cc')
        original = self.overlay / binding['source'] / 'fixture.cc'
        original.write_text('changed')
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        self.assertEqual((self.src / binding['destination'] / 'fixture.cc').read_text(), 'owned source\n')
        self.assertEqual((self.src / 'shell.cc').read_text(), 'upstream\n')

    def test_owned_source_root_links_are_rejected(self):
        binding = self.catalog['source_roots'][0]
        owned = self.overlay / binding['source']
        moved = self.base / 'moved-owner'
        owned.rename(moved)
        owned.symlink_to(moved, target_is_directory=True)
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)

    def test_source_or_destination_links_cannot_escape_ownership(self):
        owner = self.overlay / self.catalog['source_roots'][0]['source']
        (owner / 'link.cc').symlink_to(self.src / 'shell.cc')
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        (owner / 'link.cc').unlink()
        outside = self.base / 'outside'
        outside.mkdir()
        (self.src / 'chrome').symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            source_plan(self.src, self.catalog, self.overlay)
        self.assertEqual(list(outside.iterdir()), [])

    def test_shell_patch_excludes_every_owned_root_and_other_patch(self):
        for binding in self.catalog['source_roots']:
            destination = self.src / binding['destination'] / 'fixture.cc'
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text('upstream source\n')
        theme = self.src / 'chrome/app/theme/chromium'
        theme.mkdir(parents=True)
        (theme / 'logo.png').write_bytes(b'original')
        git(self.src, 'add', '.')
        git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'owned sources fixture')
        self.apply(patch_plan(self.src, self.catalog, self.overlay))
        for binding in self.catalog['source_roots']:
            (self.src / binding['destination'] / 'fixture.cc').write_text('changed source\n')
        (theme / 'logo.png').write_bytes(b'changed')
        diff = git(self.src, 'diff', 'HEAD', '--', '.',
                   *shell_exclusions(self.catalog, self.overlay)).decode()
        self.assertIn('shell.cc', diff)
        for path in ('brand.cc', 'version.cc', 'windows.bat', *[b['destination'] for b in self.catalog['source_roots']]):
            self.assertNotIn(path, diff)
        self.assertNotIn('logo.png', diff)


if __name__ == '__main__':
    unittest.main()
