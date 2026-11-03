"""가짜 ps 목록으로 설치 검사의 앱·프로필 경계를 확인한다. 앱은 실행하지 않는다."""

import json
from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'tools/dev/lib/macos_installer_processes.mjs'


@unittest.skipUnless(shutil.which('node'), 'Node.js가 필요하다')
class MacInstallerProcessesTest(unittest.TestCase):
    application = '/tmp/검사 폴더/Applications/Yee.app'
    profile = '/tmp/검사 폴더/profile'

    def classify(self, listing, **changes):
        source = '''import {readFileSync} from 'node:fs';
import {filterInstallerProcesses} from MODULE;
const input = JSON.parse(readFileSync(0, 'utf8'));
console.log(JSON.stringify(filterInstallerProcesses(input.listing, input.options)));
'''.replace('MODULE', json.dumps(MODULE.as_uri()))
        options = {'application': self.application, 'profile': self.profile, 'productName': 'Yee', **changes}
        result = subprocess.run(['node', '--input-type=module', '-e', source],
                                input=json.dumps({'listing': listing, 'options': options}),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    @staticmethod
    def executable(app, name='Yee'):
        return app + '/Contents/MacOS/' + name

    def helper(self, app):
        return (app + '/Contents/Frameworks/Yee Framework.framework/Versions/157/Helpers/'
                'Yee Helper (Renderer).app/Contents/MacOS/Yee Helper (Renderer)')

    def test_mixed_release_development_test_app_and_helpers(self):
        """별도 설치판·개발판은 유지하고 검사 앱의 본체와 프로필 없는 Helper도 기다린다."""
        release = '/Applications/Yee.app'
        development = '/repo/chromium/src/out/YeeStable157/Yee.app'
        paths = [self.executable(release), self.helper(release),
                 self.executable(development), self.helper(development),
                 self.executable(self.application), self.helper(self.application)]
        listing = '\n'.join(f'  {100 + index} {path} --type=renderer' for index, path in enumerate(paths))
        report = self.classify(listing)
        self.assertEqual(report['owned'], [{'pid': 104, 'executable': paths[4]},
                                            {'pid': 105, 'executable': paths[5]}])
        self.assertEqual([item['executable'] for item in report['unrelated']], paths[:4])

    def test_non_macos_helper_executables_inside_the_test_bundle_are_owned(self):
        """복사한 앱 안의 crashpad 같은 비-MacOS 실행 경로도 종료 대기에서 빠뜨리지 않는다."""
        executable = self.application + '/Contents/Frameworks/Yee Framework.framework/Helpers/chrome_crashpad_handler'
        report = self.classify(f'205 {executable} --monitor-self')
        self.assertEqual(report['owned'], [{'pid': 205, 'executable': executable}])

    def test_exact_profile_matches_even_from_another_product_bundle(self):
        """별도 앱이라도 검사 프로필을 쓰는 본체는 종료 확인 범위에 포함한다."""
        executable = self.executable('/Applications/Yee.app')
        for argument in (f'--user-data-dir={self.profile}', f'--user-data-dir="{self.profile}"',
                         f'"--user-data-dir={self.profile}"', f'--user-data-dir "{self.profile}"'):
            with self.subTest(argument=argument):
                report = self.classify(f'201 {executable} {argument} --no-first-run')
                self.assertEqual(len(report['owned']), 1)
                self.assertEqual(report['unrelated'], [])

    def test_path_prefixes_and_profile_substrings_do_not_acquire_ownership(self):
        """이름이 비슷한 앱·프로필과 다른 인수 안의 경로는 검사 대상이 아니다."""
        release = self.executable('/Applications/Yee.app')
        sibling = self.executable(self.application + '.old/Yee.app')
        listing = '\n'.join((f'301 {release} --user-data-dir={self.profile}-other',
                              f'302 {release} --user-data-dir={self.profile}/child',
                              f'303 {release} --log-file={self.profile}', f'304 {sibling} --no-first-run',
                              f'305 /usr/bin/node --app={self.executable(self.application)}',
                              '306 /Applications/Other.app/Contents/MacOS/Other'))
        report = self.classify(listing)
        self.assertEqual(report['owned'], [])
        self.assertEqual([item['pid'] for item in report['unrelated']], [301, 302, 303, 304])

    def test_product_name_spaces_and_regex_characters_are_literal(self):
        """제품명·프로필의 공백과 정규식 문자를 일반 경로 문자로 해석한다."""
        name = 'Orbit Preview (QA+)'
        app = '/tmp/install/' + name + '.app'
        profile = '/tmp/profile [test]+'
        executable = self.executable(app, name)
        report = self.classify(f'401 {executable} --user-data-dir={profile}',
                               productName=name, application=app, profile=profile)
        self.assertEqual(report['owned'], [{'pid': 401, 'executable': executable}])

    def test_empty_malformed_and_nonproduct_lines_are_ignored(self):
        """ps 헤더·잘못된 행·다른 제품은 종료 요구나 무관한 Yee 집계에 넣지 않는다."""
        for listing in ('', 'PID COMMAND\nnot a pid Yee.app/Contents/MacOS/Yee\n',
                        '12 /Applications/NotYee.app/Contents/MacOS/NotYee\n'):
            with self.subTest(listing=listing):
                self.assertEqual(self.classify(listing), {'owned': [], 'unrelated': []})


if __name__ == '__main__':
    unittest.main()
