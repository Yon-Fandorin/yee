#!/usr/bin/env python3
"""Test overlay boundaries and all-before-write preflight with disposable files."""

import contextlib
import copy
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
from lib.overlay_tools import (apply_patch_plan, load_catalog, patch_paths, patch_plan,
                               shell_exclusions, source_plan, sync_sources)


def git(src, *args):
    return subprocess.run(['git', '-C', str(src), *args], check=True,
                          capture_output=True).stdout


def patch_format_cases():
    """지원하는 Git diff와 파일 헤더 위장 형식의 사례를 제공한다."""
    header = 'diff --git a/fixture.cc b/fixture.cc\n'
    files = '--- a/fixture.cc\n+++ b/fixture.cc\n'
    modified = files + '@@ -1 +1 @@\n-upstream\n+overlay\n'
    return {
        'standard': (header + modified, True),
        'header-like-source': (header + files + '@@ -1,2 +1,2 @@\n--- old\n+++ new\n context\n', True),
        'embedded-diff-text': (header + files + '@@ -1 +1 @@\n-upstream\n+diff --git a/text b/text\n', True),
        'unicode-source': (header + files + '@@ -1 +1 @@\n-upstream\n+before\u2028--- after\n', True),
        'no-final-newline': (header + files + '@@ -1 +1 @@\n-old\n\\ No newline at end of file\n+new\n\\ No newline at end of file\n', True),
        'added': (header + 'new file mode 100644\n--- /dev/null\n+++ b/fixture.cc\n@@ -0,0 +1 @@\n+new\n', True),
        'deleted': (header + 'deleted file mode 100644\n--- a/fixture.cc\n+++ /dev/null\n@@ -1 +0,0 @@\n-old\n', True),
        'mode-only': (header + 'old mode 100644\nnew mode 100755\n', True),
        'empty-added': (header + 'new file mode 100644\nindex 0000000..e69de29\n', True),
        'wrong-old-header': (header + modified.replace('--- a/fixture.cc', '--- a/generated.cc'), False),
        'wrong-new-header': (header + modified.replace('+++ b/fixture.cc', '+++ b/generated.cc'), False),
        'both-null': (header + modified.replace('a/fixture.cc', '/dev/null').replace('b/fixture.cc', '/dev/null'), False),
        'rename': (header + 'similarity index 100%\nrename from fixture.cc\nrename to generated.cc\n', False),
        'copy': (header + 'copy from fixture.cc\ncopy to generated.cc\n', False),
        'binary': (header + 'index 1234567..7654321 100644\nGIT binary patch\nliteral 1\nX\n', False),
        'binary-summary': (header + 'Binary files a/fixture.cc and b/generated.cc differ\n', False),
        'traditional-prefix': ('--- a/generated.cc\n+++ b/generated.cc\n@@ -1 +1 @@\n-old\n+new\n' + header + modified, False),
        'second-file-headers': (header + modified + '--- a/generated.cc\n+++ b/generated.cc\n@@ -1 +1 @@\n-old\n+new\n', False),
        'incomplete-hunk': (header + files + '@@ -1 +1 @@\n-upstream\n', False),
        'missing-old-header': (header + '+++ b/fixture.cc\n@@ -1 +1 @@\n-old\n+new\n', False),
        'nul-path': ((header + modified).replace('fixture.cc', 'fixture.cc\0extra'), False),
        'duplicate-case-section': (header + modified + (header + modified).replace('fixture.cc', 'FIXTURE.CC'), False),
    }


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

    def test_regenerate_uses_explicit_source_and_preserves_patch_ownership(self):
        """별도 후보 원본에서 새 glue까지 수집하고 제품·생성·다른 패치 파일은 제외한다."""
        tool = self.overlay / 'tools/overlay'
        tool.mkdir(parents=True)
        shutil.copy(ROOT/'tools/overlay/regenerate-shell-patch.py', tool)
        shutil.copytree(ROOT/'tools/overlay/lib', tool/'lib',
                        ignore=shutil.ignore_patterns('__pycache__'))
        self.catalog['new_glue'] = ['new-glue.cc']
        (self.overlay/'build/overlay.json').write_text(json.dumps(self.catalog))
        owned = self.src/self.catalog['source_roots'][0]['destination']/'fixture.cc'
        generated = self.src/'chrome/VERSION'
        for path in (owned, generated):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('upstream\n')
        dependency = self.src/'engine-dependency'
        dependency.mkdir()
        git(dependency, 'init', '-q')
        (dependency/'source.cc').write_text('dependency upstream\n')
        git(dependency, 'add', '.')
        git(dependency, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'dependency')
        revision = git(dependency, 'rev-parse', 'HEAD').decode().strip()
        git(self.src, 'update-index', '--add', '--cacheinfo', f'160000,{revision},engine-dependency')
        git(self.src, 'add', '.')
        git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'owned and generated inputs')
        self.apply(patch_plan(self.src, self.catalog, self.overlay))
        owned.write_text('owned change\n')
        generated.write_text('generated change\n')
        (dependency/'source.cc').write_text('dependency tool patch\n')
        (self.src/'new-glue.cc').write_text('new glue\n')
        index_before = git(self.src, 'diff', '--cached', '--binary')
        result = subprocess.run([sys.executable, str(tool/'regenerate-shell-patch.py'),
                                 '--chromium-src', str(self.src)],
                                cwd=self.base, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        patch = self.overlay/'patches/0001-integrate-yee-shell.patch'
        self.assertEqual(patch_paths(patch), ['shell.cc', 'new-glue.cc'])
        git(self.src, 'apply', '--reverse', '--check', str(patch))
        self.assertEqual(index_before, git(self.src, 'diff', '--cached', '--binary'))

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
        text = (self.overlay / self.catalog['patches'][0]['file']).read_text()
        for path in ('shell.cc', 'SHELL.CC'):
            (self.overlay / self.catalog['patches'][-1]['file']).write_text(text.replace('shell.cc', path))
            with self.subTest(path=path), mock.patch('lib.overlay_tools.git_apply') as query:
                with self.assertRaisesRegex(ValueError, 'must own disjoint files'):
                    patch_plan(self.src, self.catalog, self.overlay)
                query.assert_not_called()
        with self.assertRaises(ValueError):
            patch_plan(self.src, self.catalog, self.overlay, role='missing')

    def test_patch_cannot_claim_generated_or_mirrored_source_paths(self):
        """전체 파일 패치와 부분 패치가 생성기·제품 소스의 경로를 차지하지 못한다."""
        paths = ('chrome/app/chromium_strings.grd', 'chrome/app/theme/chromium/BRANDING',
                 'CHROME/APP/CHROMIUM_STRINGS.GRD', 'CHROME/APP/THEME/CHROMIUM/BRANDING',
                 'chrome/app/yee_product_names.grdp', 'chrome/app/yee_app_menu_name.grdp',
                 self.catalog['source_roots'][0]['destination'] + '/fixture.cc',
                 self.catalog['source_roots'][0]['destination'].upper() + '/fixture.cc')
        for role in ('branding', 'windows_tools'):
            patch = next(p for p in self.catalog['patches'] if p['role'] == role)
            for path in paths:
                with self.subTest(role=role, path=path):
                    (self.overlay / patch['file']).write_text(
                        f'diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n'
                        '@@ -1 +1 @@\n-upstream\n+overlay\n')
                    before = git(self.src, 'diff')
                    with self.assertRaisesRegex(ValueError, 'must not own generated or mirrored source paths'):
                        patch_plan(self.src, self.catalog, self.overlay, role=role)
                    self.assertEqual(before, git(self.src, 'diff'))

    @unittest.skipUnless(shutil.which('pwsh') or shutil.which('powershell'), 'PowerShell 필요')
    def test_powershell_rejects_the_same_generated_and_mirrored_patch_owners(self):
        """Windows의 패치 검사도 생성 GRD와 복사한 소스의 재진입을 막는다."""
        shell = shutil.which('pwsh') or shutil.which('powershell')
        env = {**os.environ, 'YEE_TEST_SRC': str(self.src), 'YEE_TEST_ROOT': str(self.overlay),
               'YEE_TEST_LIBRARY': str(ROOT / 'tools/overlay/lib/overlay-tools.ps1')}
        script = '''$ErrorActionPreference = 'Stop'
. $env:YEE_TEST_LIBRARY
try {
    $null = @(Get-YeeOverlayPatchPlan -ChromiumSrc $env:YEE_TEST_SRC -RepoRoot $env:YEE_TEST_ROOT -Role $env:YEE_TEST_ROLE)
} catch {
    if ($_.Exception.Message -like '*must not own generated or mirrored source paths*') { exit 0 }
    Write-Error $_
    exit 1
}
exit 2
'''
        paths = ('chrome/app/chromium_strings.grd', 'chrome/app/theme/chromium/BRANDING',
                 'CHROME/APP/CHROMIUM_STRINGS.GRD', 'CHROME/APP/THEME/CHROMIUM/BRANDING',
                 'chrome/app/yee_product_names.grdp', 'chrome/app/yee_app_menu_name.grdp',
                 self.catalog['source_roots'][0]['destination'] + '/fixture.cc',
                 self.catalog['source_roots'][0]['destination'].upper() + '/fixture.cc')
        for role in ('branding', 'windows_tools'):
            patch = next(p for p in self.catalog['patches'] if p['role'] == role)
            for path in paths:
                with self.subTest(role=role, path=path):
                    (self.overlay / patch['file']).write_text(
                        f'diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n'
                        '@@ -1 +1 @@\n-upstream\n+overlay\n')
                    before = git(self.src, 'diff')
                    result = subprocess.run([shell, '-NoProfile', '-NonInteractive', '-Command', script],
                                            env={**env, 'YEE_TEST_ROLE': role}, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(before, git(self.src, 'diff'))

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

    def test_patch_parser_handles_real_headers_and_hunk_payloads(self):
        """추가·삭제·권한 변경을 읽되 실제 대상이 선언과 다른 파일 헤더는 거부한다."""
        patch = self.overlay / 'format.patch'
        for name, (text, accepted) in patch_format_cases().items():
            patch.write_bytes(text.encode('utf-8'))
            with self.subTest(format=name):
                if accepted:
                    self.assertEqual(patch_paths(patch), ['fixture.cc'])
                else:
                    with self.assertRaises(ValueError):
                        patch_paths(patch)

    @unittest.skipUnless(shutil.which('pwsh') or shutil.which('powershell'), 'PowerShell 필요')
    def test_powershell_patch_parser_matches_supported_and_rejected_formats(self):
        """PowerShell도 hunk 본문을 보존하고 같은 파일 헤더·메타데이터 계약을 적용한다."""
        cases = []
        for name, (text, accepted) in patch_format_cases().items():
            patch = self.overlay / (name + '.patch')
            patch.write_bytes(text.encode('utf-8'))
            cases.append({'file': str(patch), 'accepted': accepted})
        cases_path = self.base / 'parser-cases.json'
        cases_path.write_text(json.dumps(cases), encoding='utf-8')
        env = {**os.environ, 'YEE_TEST_CASES': str(cases_path),
               'YEE_TEST_LIBRARY': str(ROOT / 'tools/overlay/lib/overlay-tools.ps1')}
        script = '''$ErrorActionPreference = 'Stop'
. $env:YEE_TEST_LIBRARY
foreach ($case in (Get-Content -LiteralPath $env:YEE_TEST_CASES -Raw | ConvertFrom-Json)) {
    $rejected = $false
    try { $actual = @(Get-YeePatchPaths $case.file) } catch { $rejected = $true }
    if ($case.accepted) {
        if ($rejected -or $actual.Count -ne 1 -or $actual[0] -cne 'fixture.cc') {
            throw "Valid patch format rejected: $($case.file)"
        }
    } elseif (-not $rejected) { throw "Invalid patch format accepted: $($case.file)" }
}
'''
        result = subprocess.run([shutil.which('pwsh') or shutil.which('powershell'),
                                 '-NoProfile', '-NonInteractive', '-Command', script],
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mismatched_actual_targets_fail_before_any_git_query_or_write(self):
        """Git이 적용할 수 있는 위장 헤더도 전체·부분 패치의 사전 검사에서 거부한다."""
        targets = ('chrome/app/theme/chromium/BRANDING',
                   self.catalog['source_roots'][0]['destination'] + '/fixture.cc')
        for target in targets:
            path = self.src / target
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('upstream\n')
        for role in ('branding', 'windows_tools'):
            patch = next(p for p in self.catalog['patches'] if p['role'] == role)
            patch_file = self.overlay / patch['file']
            original = patch_file.read_bytes()
            for target in targets:
                with self.subTest(role=role, target=target):
                    try:
                        patch_file.write_text(
                            f'diff --git a/safe.cc b/safe.cc\n--- a/{target}\n+++ b/{target}\n'
                            '@@ -1 +1 @@\n-upstream\n+overlay\n')
                        git(self.src, 'apply', '--check', str(patch_file))
                        before = {p: p.read_bytes() for p in self.src.rglob('*')
                                  if p.is_file() and '.git' not in p.relative_to(self.src).parts}
                        with mock.patch('lib.overlay_tools.git_apply') as query:
                            with self.assertRaisesRegex(ValueError, 'matching patch file headers'):
                                self.apply(patch_plan(self.src, self.catalog, self.overlay))
                            query.assert_not_called()
                        self.assertEqual(before, {p: p.read_bytes() for p in before})
                    finally:
                        patch_file.write_bytes(original)

    @unittest.skipUnless(shutil.which('pwsh') or shutil.which('powershell'), 'PowerShell 필요')
    def test_powershell_mismatched_targets_fail_before_any_git_query_or_write(self):
        """Windows 사전 검사도 생성·복사 대상의 위장 파일 헤더를 Git 호출 전에 거부한다."""
        env = {**os.environ, 'YEE_TEST_SRC': str(self.src), 'YEE_TEST_ROOT': str(self.overlay),
               'YEE_TEST_LIBRARY': str(ROOT / 'tools/overlay/lib/overlay-tools.ps1')}
        script = '''$ErrorActionPreference = 'Stop'
. $env:YEE_TEST_LIBRARY
$script:queryCount = 0
function Invoke-YeeGitApply { $script:queryCount++; throw 'Unexpected Git query' }
try {
    $plan = @(Get-YeeOverlayPatchPlan -ChromiumSrc $env:YEE_TEST_SRC -RepoRoot $env:YEE_TEST_ROOT)
    Invoke-YeeOverlayPatchPlan -ChromiumSrc $env:YEE_TEST_SRC -Plan $plan
} catch {
    if ($script:queryCount -eq 0 -and $_.Exception.Message -like '*matching patch file headers*') { exit 0 }
    Write-Error $_
    exit 1
}
exit 2
'''
        targets = ('chrome/app/theme/chromium/BRANDING',
                   self.catalog['source_roots'][0]['destination'] + '/fixture.cc')
        for target in targets:
            path = self.src / target
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('upstream\n')
        for role in ('branding', 'windows_tools'):
            patch = next(p for p in self.catalog['patches'] if p['role'] == role)
            patch_file = self.overlay / patch['file']
            original = patch_file.read_bytes()
            for target in targets:
                with self.subTest(role=role, target=target):
                    try:
                        patch_file.write_text(
                            f'diff --git a/safe.cc b/safe.cc\n--- a/{target}\n+++ b/{target}\n'
                            '@@ -1 +1 @@\n-upstream\n+overlay\n')
                        before = {p: p.read_bytes() for p in self.src.rglob('*')
                                  if p.is_file() and '.git' not in p.relative_to(self.src).parts}
                        result = subprocess.run([shutil.which('pwsh') or shutil.which('powershell'),
                                                 '-NoProfile', '-NonInteractive', '-Command', script],
                                                env=env, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertEqual(before, {p: p.read_bytes() for p in before})
                    finally:
                        patch_file.write_bytes(original)

    def test_added_glue_deleted_files_and_header_like_source_apply_and_reapply(self):
        """정상 파일 헤더 뒤의 ---/+++ 소스와 /dev/null 추가·삭제를 실제로 적용한다."""
        (self.src / 'hunk.cc').write_bytes(b'-- old\ncontext\n')
        (self.src / 'deleted.cc').write_bytes(b'deleted\n')
        git(self.src, 'add', '.')
        git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'header content fixture')
        patch = self.overlay / self.catalog['patches'][1]['file']
        patch.write_bytes(
            b'diff --git a/hunk.cc b/hunk.cc\n--- a/hunk.cc\n+++ b/hunk.cc\n'
            b'@@ -1,2 +1,2 @@\n--- old\n+++ new\n context\n'
            b'diff --git a/new-glue.cc b/new-glue.cc\nnew file mode 100644\n'
            b'--- /dev/null\n+++ b/new-glue.cc\n@@ -0,0 +1 @@\n+new glue\n'
            b'diff --git a/deleted.cc b/deleted.cc\ndeleted file mode 100644\n'
            b'--- a/deleted.cc\n+++ /dev/null\n@@ -1 +0,0 @@\n-deleted\n')
        self.assertEqual(patch_paths(patch), ['hunk.cc', 'new-glue.cc', 'deleted.cc'])
        self.apply(patch_plan(self.src, self.catalog, self.overlay, role='branding'))
        self.assertEqual((self.src / 'hunk.cc').read_bytes(), b'++ new\ncontext\n')
        self.assertEqual((self.src / 'new-glue.cc').read_bytes(), b'new glue\n')
        self.assertFalse((self.src / 'deleted.cc').exists())
        self.assertFalse(any(step.pending for step in patch_plan(
            self.src, self.catalog, self.overlay, role='branding')))

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
        individual_assets = [self.src / value for value in self.catalog['generated_roots']
                             if Path(value).suffix]
        for asset in individual_assets:
            asset.parent.mkdir(parents=True, exist_ok=True)
            asset.write_bytes(b'original generated asset')
        git(self.src, 'add', '.')
        git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-qm', 'owned sources fixture')
        self.apply(patch_plan(self.src, self.catalog, self.overlay))
        for binding in self.catalog['source_roots']:
            (self.src / binding['destination'] / 'fixture.cc').write_text('changed source\n')
        (theme / 'logo.png').write_bytes(b'changed')
        for asset in individual_assets:
            asset.write_bytes(b'changed generated asset')
        diff = git(self.src, 'diff', 'HEAD', '--', '.',
                   *shell_exclusions(self.catalog, self.overlay)).decode()
        self.assertIn('shell.cc', diff)
        for path in ('brand.cc', 'version.cc', 'windows.bat', *[b['destination'] for b in self.catalog['source_roots']]):
            self.assertNotIn(path, diff)
        self.assertNotIn('logo.png', diff)
        for asset in individual_assets:
            self.assertNotIn(asset.relative_to(self.src).as_posix(), diff)


if __name__ == '__main__':
    unittest.main()
