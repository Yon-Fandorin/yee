#!/usr/bin/env python3
"""공통 원본 고정과 플랫폼별 조회, 개발 명령의 변경 전 중단을 검사한다."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('chromium_version', ROOT/'tools/dev/chromium-version.py')
VERSION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERSION)


class ChromiumVersionFixture:
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-chromium-pin-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()/'project with spaces'
        self.root.mkdir()
        self.product = self.root/'VERSION'
        self.product.write_text((ROOT/'branding/VERSION').read_text(), encoding='utf8')
        self.manifest = json.loads((ROOT/'build/chromium-version.json').read_text())
        engine = ''.join(f'{key}={part}\n' for key,part in zip(VERSION.VERSION_KEYS,self.manifest['version'].split('.')))
        self.source = self.root/'chromium/src'
        (self.source/'chrome').mkdir(parents=True)
        (self.source/'BUILD.gn').touch()
        (self.source/'chrome/VERSION').write_text(engine, encoding='utf8')
        self.git('init')
        self.git('add', 'BUILD.gn', 'chrome/VERSION')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'fixture')
        self.manifest['revision'] = self.git('rev-parse', 'HEAD').strip()
        self.filename = self.root/'pin.json'
        self.save_manifest()

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.source), *args], capture_output=True,
                              text=True, check=True).stdout

    def save_manifest(self):
        self.filename.write_text(json.dumps(self.manifest), encoding='utf8')


class ChromiumVersionTest(ChromiumVersionFixture, unittest.TestCase):
    def test_both_platforms_use_the_same_source_and_ignore_applied_product_version(self):
        """두 OS는 같은 원본을 사용하며 적용된 작업 파일 대신 HEAD 버전을 읽는다."""
        (self.source/'chrome/VERSION').write_text('MAJOR=999\nMINOR=0\nBUILD=1\nPATCH=1\n')
        manifest = VERSION.load_manifest(self.filename, self.product)
        for platform in ('windows', 'macos'):
            result = VERSION.verify_checkout(manifest, platform, self.source)
            self.assertEqual(result['revision'], self.manifest['revision'])
            self.assertEqual(result['version'], self.manifest['version'])

    def test_wrong_head_and_misstated_engine_version_are_rejected(self):
        """다른 원본이나 실제 원본과 다른 버전 표기를 거부한다."""
        wrong = {**self.manifest, 'revision': '0'*40}
        with self.assertRaisesRegex(ValueError, '기준 불일치'):
            VERSION.verify_checkout(wrong, 'windows', self.source)
        wrong = {**self.manifest, 'version': '1.0.0.0'}
        with self.assertRaisesRegex(ValueError, 'chrome/VERSION'):
            VERSION.verify_checkout(wrong, 'windows', self.source)

    def test_manifest_contract_and_product_downgrade_are_rejected(self):
        """임의 원본 URL, 플랫폼별 원본 분기, 잘못된 핀과 제품 버전 역행을 거부한다."""
        variants = [dict(schema_version=True), dict(channel='Beta'), dict(revision='main'),
                    dict(source_url='https://example.invalid/src.git'), dict(platforms={}),
                    dict(version='157.0.8103'), dict(version='157.00.8103.27')]
        for changes in variants:
            with self.subTest(changes=changes):
                self.filename.write_text(json.dumps({**self.manifest, **changes}))
                with self.assertRaises(ValueError):
                    VERSION.load_manifest(self.filename, self.product)
        self.save_manifest()
        self.product.write_text('MAJOR=1\nMINOR=0\nBUILD=1\nPATCH=1\n')
        with self.assertRaises(ValueError):
            VERSION.load_manifest(self.filename, self.product)

    def test_update_check_reports_platform_differences_without_writes(self):
        """서로 다른 최신 마일스톤을 보고하되 기존 기준과 작업 파일을 바꾸지 않는다."""
        before = {name: name.read_bytes() for name in (self.filename, self.product, self.source/'chrome/VERSION')}
        revisions = {'windows': {'version':'999.0.1.1', 'revision':'1'*40},
                     'macos': {'version':self.manifest['version'], 'revision':self.manifest['revision']}}
        result = VERSION.check_updates(self.manifest, ('windows','macos'), fetch=revisions.__getitem__)
        self.assertFalse(result['modified'])
        self.assertFalse(result['same_engine_line'])
        self.assertEqual([item['update_available'] for item in result['platforms']], [True,False])
        self.assertEqual(before, {name:name.read_bytes() for name in before})

    def test_same_version_with_changed_revision_is_not_silently_an_upgrade(self):
        """같은 버전의 다른 커밋을 정상 패치 업그레이드로 처리하지 않는다."""
        result = VERSION.check_updates(self.manifest, ['windows'],
                                       fetch=lambda _: {'version':self.manifest['version'], 'revision':'2'*40})
        self.assertTrue(result['platforms'][0]['same_version_different_revision'])
        self.assertFalse(result['platforms'][0]['update_available'])

    def test_cli_verifies_custom_manifest_from_another_directory(self):
        """공백이 있는 경로와 다른 작업 디렉터리에서도 실제 원본 검증이 동작한다."""
        result = subprocess.run([sys.executable, str(ROOT/'tools/dev/chromium-version.py'),
                                 '--manifest', str(self.filename), 'verify', str(self.source), '--platform','windows'],
                                cwd=self.root, capture_output=True, encoding='utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['revision'], self.manifest['revision'])

    def test_product_patch_can_advance_without_changing_the_engine(self):
        """제품 patch만 올려도 원본 핀이나 검증 fixture가 같이 올라가지는 않는다."""
        parts = list(VERSION.parse_version(self.product.read_text()))
        parts[-1] += 1
        self.product.write_text(''.join(f'{key}={part}\n' for key,part in zip(VERSION.VERSION_KEYS,parts)))
        manifest = VERSION.load_manifest(self.filename,self.product)
        self.assertEqual(VERSION.verify_checkout(manifest,'windows',self.source)['version'],self.manifest['version'])

    def test_cli_does_not_create_bytecode_with_default_python_environment(self):
        """bytecode 억제 환경변수 없이도 조회는 저장소 파일을 만들거나 바꾸지 않는다."""
        repo = self.root/'readonly product'
        for directory in ('tools/dev','tools/overlay','build','branding'):
            (repo/directory).mkdir(parents=True)
        shutil.copy(ROOT/'tools/dev/chromium-version.py',repo/'tools/dev')
        shutil.copy(ROOT/'tools/overlay/product_version.py',repo/'tools/overlay')
        shutil.copy(self.filename,repo/'build/chromium-version.json')
        shutil.copy(self.product,repo/'branding/VERSION')
        before = {path.relative_to(repo):path.read_bytes() for path in repo.rglob('*') if path.is_file()}
        env = dict(os.environ)
        env.pop('PYTHONDONTWRITEBYTECODE',None)
        env.pop('PYTHONPYCACHEPREFIX',None)
        result = subprocess.run([sys.executable,str(repo/'tools/dev/chromium-version.py'),'show'],
                                env=env,capture_output=True,encoding='utf8')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(before,{path.relative_to(repo):path.read_bytes() for path in repo.rglob('*') if path.is_file()})


@unittest.skipUnless(sys.platform == 'win32' and shutil.which('powershell.exe'), 'Windows PowerShell 필요')
class WindowsChromiumPreflightTest(ChromiumVersionFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.repo = self.root/'product'
        self.dev = self.repo/'tools/dev'
        self.dev.mkdir(parents=True)
        shutil.copytree(ROOT/'tools/dev/lib', self.dev/'lib')
        for name in ('common.ps1','configure.ps1','build.ps1','checkout.ps1','sync.ps1','chromium-version.py'):
            shutil.copy(ROOT/'tools/dev'/name, self.dev/name)
        overlay = self.repo/'tools/overlay'
        overlay.mkdir(parents=True)
        for name in ('brand-config.ps1','product_version.py'):
            shutil.copy(ROOT/'tools/overlay'/name, overlay/name)
        (self.repo/'branding').mkdir()
        (self.repo/'branding/VERSION').write_text(self.product.read_text())
        (self.repo/'branding/brand.json').write_text(json.dumps({'name':'Orbit','logo_source':'logo.png'}))
        (self.repo/'build').mkdir()
        self.pin = self.repo/'build/chromium-version.json'
        self.pin.write_text(json.dumps(self.manifest))
        self.log = self.root/'events.log'
        for name in ('apply.ps1','install-branding.ps1','install-brand-assets.ps1','install-yee-ui-sources.ps1'):
            (overlay/name).write_text("Add-Content -LiteralPath $env:YEE_TEST_LOG 'overlay-called'\nthrow 'trap'\n")
        # 단위 검사는 실제 드라이브 용량이나 Chromium 다운로드에 의존하지 않는다.
        with (self.dev/'lib/preflight.ps1').open('a', encoding='utf8') as stream:
            stream.write('\nfunction Assert-FreeGiB { param($RequiredGiB,$Purpose) }\n')
        self.depot = self.root/'depot'
        (self.depot/'.git').mkdir(parents=True)
        (self.depot/'python3.bat').write_text(f'@"{sys.executable}" %*\n')
        (self.depot/'gclient.bat').write_text(
            '@echo off\nif "%~1"=="" exit /b 0\n'
            'echo gclient:%* >>"%YEE_TEST_LOG%"\n'
            'if "%~1"=="config" echo fixture>.gclient\n'
            'if "%~1"=="sync" if "%YEE_TEST_SYNC_FAIL%"=="1" exit /b 9\n'
            'if "%~1"=="sync" if not exist "%YEE_CHROMIUM_ROOT%\\src\\BUILD.gn" '
            'git clone "%YEE_TEST_TEMPLATE%" "%YEE_CHROMIUM_ROOT%\\src"\n'
            'exit /b %errorlevel%\n')
        self.env = {**os.environ, 'YEE_DEPOT_TOOLS_DIR':str(self.depot),
                    'YEE_CHROMIUM_ROOT':str(self.source.parent), 'YEE_LOCAL_BUILD_ROOT':str(self.root/'local'),
                    'YEE_TEST_TEMPLATE':str(self.source), 'YEE_TEST_LOG':str(self.log),
                    'PYTHONDONTWRITEBYTECODE':'1','PYTHONUTF8':'1'}
        self.env.pop('YEE_OUT_NAME', None)
        self.env.pop('YEE_SYNC_JOBS', None)

    def run_script(self, name, **changes):
        return subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(self.dev/name)],
                              env={**self.env,**changes}, cwd=self.root,
                              capture_output=True, encoding='utf8', errors='replace')

    def test_wrong_checkout_stops_all_entries_before_mutation(self):
        """다른 원본이면 설정·빌드·동기화·기존 체크아웃 확인이 변경 전에 중단된다."""
        self.pin.write_text(json.dumps({**self.manifest,'revision':'0'*40}))
        before = (self.source/'chrome/VERSION').read_bytes()
        for name in ('configure.ps1','build.ps1','sync.ps1','checkout.ps1'):
            with self.subTest(name=name):
                result = self.run_script(name)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('0'*40,result.stdout+result.stderr)
                self.assertFalse(self.log.exists())
                self.assertEqual((self.source/'chrome/VERSION').read_bytes(),before)

    def test_fresh_checkout_pins_revision_and_sync_preserves_source(self):
        """처음에는 공통 커밋을 받고 동기화는 검사한 원본과 제품 수정을 유지한다."""
        target = self.root/'fresh chromium'
        result = self.run_script('checkout.ps1', YEE_CHROMIUM_ROOT=str(target))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        events = self.log.read_text()
        self.assertIn('config --name src --unmanaged',events)
        self.assertIn('sync --no-history --revision src@'+self.manifest['revision'],events)
        self.log.unlink()
        extra = self.source/'local-change.txt'
        extra.write_text('keep')
        tracked = self.source/'BUILD.gn'
        tracked.write_text('tracked product changes\n')
        result = self.run_script('sync.ps1')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('src@unmanaged',self.log.read_text())
        self.assertIn('--jobs 2',self.log.read_text())
        self.assertEqual(extra.read_text(),'keep')
        self.assertEqual(tracked.read_text(),'tracked product changes\n')

    def test_configure_preserves_quoted_gn_values_in_the_candidate_output(self):
        """Windows 배치 경계를 거쳐도 코덱 문자열과 주석이 입력 파일에 유지된다."""
        args = 'is_debug = false\n# 코덱 설정\nffmpeg_branding = "Chrome"\n'
        (self.repo/'build/args.gn').write_bytes(args.encode('utf8'))
        (self.repo/'tools/overlay/apply.ps1').write_text(
            'param($ChromiumSrc,[switch]$CheckOnly)\n', encoding='utf8')
        (self.depot/'gn.bat').write_text(
            '@echo off\necho gn:%* >>"%YEE_TEST_LOG%"\nexit /b 0\n')
        result = self.run_script('configure.ps1', YEE_OUT_NAME='StableCandidate')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual((self.source/'out/StableCandidate/args.gn').read_bytes(),args.encode('utf8'))
        events = self.log.read_text()
        self.assertIn('gn:gen out\\StableCandidate',events)
        self.assertNotIn('--args=',events)

    def test_failed_and_partial_checkout_resume_message_keeps_the_pin(self):
        """실패 후와 부분 체크아웃 안내가 핀 없이 main을 받도록 바뀌지 않는다."""
        target = self.root/'partial chromium'
        result = self.run_script('checkout.ps1', YEE_CHROMIUM_ROOT=str(target),YEE_TEST_SYNC_FAIL='1',YEE_SYNC_JOBS='3')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('src@'+self.manifest['revision'],result.stdout+result.stderr)
        self.assertRegex(result.stdout+result.stderr,r'--jobs\s+3')
        self.log.unlink()
        result = self.run_script('checkout.ps1', YEE_CHROMIUM_ROOT=str(target),YEE_SYNC_JOBS='3')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('src@'+self.manifest['revision'],result.stdout+result.stderr)
        self.assertRegex(result.stdout+result.stderr,r'--jobs\s+3')
        self.assertFalse(self.log.exists())

    def test_parent_output_directory_is_rejected(self):
        """후보 빌드 경로로 상위 디렉터리를 사용할 수 없다."""
        result = self.run_script('build.ps1',YEE_OUT_NAME='..')
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(self.log.exists())

    def test_invalid_sync_jobs_fail_before_bootstrap_or_mutation(self):
        """다운로드 병렬 수가 잘못되면 원본 준비와 갱신을 시작하지 않는다."""
        for name in ('checkout.ps1','sync.ps1'):
            result = self.run_script(name,YEE_SYNC_JOBS='0')
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
