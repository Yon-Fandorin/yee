"""가짜 ps·번들 목록으로 관찰기의 종료 요구·대기 범위를 검사한다."""

import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/dev'))
from browser_bundle_executables import browser_processes, CURRENT_BUNDLE_ID


@unittest.skipUnless(shutil.which('node') and sys.platform != 'win32', 'Node.js와 POSIX 경로가 필요하다')
class ObservationProcessesTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-observation-processes-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.selected = self.bundle('chromium/src/out/Chosen', 'Orbit Preview (QA+)')
        self.other = self.bundle('chromium/src/out/Other', 'Yee')
        self.worktree = self.bundle('other worktree/out/Chosen', '이전 이름')
        self.installed = self.bundle('Applications', 'Yee', 'io.github.yon-fandorin.yee')
        self.profile = str(self.root / 'profile [review]+')

    def bundle(self, parent, name, identity=CURRENT_BUNDLE_ID):
        app = self.root / parent / (name + '.app')
        binary = app / 'Contents/MacOS' / name
        binary.parent.mkdir(parents=True)
        binary.touch()
        (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleIdentifier': identity, 'CFBundleExecutable': name}))
        return binary

    @staticmethod
    def helper(binary):
        helper = (binary.parents[2] / 'Contents/Frameworks/Helpers/Orbit Helper (Renderer).app'
                  / 'Contents/MacOS/Orbit Helper (Renderer)')
        helper.parent.mkdir(parents=True, exist_ok=True)
        helper.touch()
        return helper

    def inventory(self, rows):
        listing = '\n'.join(f'{pid} {binary}' for pid, binary, _ in rows)
        arguments = {pid: [str(binary), *flags] for pid, binary, flags in rows}
        return [{**entry, 'arguments': arguments[entry['pid']]}
                for entry in browser_processes(listing, include_helpers=True)]

    def run_node(self, source, **values):
        result = subprocess.run(['node', '--input-type=module', '-e', '''
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {inspectDevelopmentProcesses, requireShutdown, waitForShutdown} from RUNTIME;
const input = JSON.parse(readFileSync(0, 'utf8'));
'''.replace('RUNTIME', json.dumps((ROOT / 'tools/dev/content_blocking_test_runtime.mjs').as_uri())) + source],
                                input=json.dumps({'executable': str(self.selected), 'profile': self.profile,
                                                  'root': str(ROOT), **values}),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def classify(self, rows, **values):
        return self.run_node('''
const run = (command, args) => {
  assert.equal(command, 'python3');
  assert.deepEqual(args.slice(1), ['--running', '--details']);
  return JSON.stringify(input.inventory);
};
const report = inspectDevelopmentProcesses(input.executable, input.profile, run);
let blocked = false;
try {requireShutdown(report.owned);} catch {blocked = true;}
console.log(JSON.stringify({report, blocked}));
''', inventory=self.inventory(rows), **values)

    def test_selected_bundle_and_helpers_only_block_the_selected_output(self):
        """선택 출력의 본체·Helper만 막고 다른 out·worktree와 설치판 창은 유지한다."""
        rows = [(index, binary, ['--type=renderer']) for index, binary in enumerate([
            self.selected, self.helper(self.selected), self.other, self.helper(self.other),
            self.worktree, self.helper(self.worktree), self.installed, self.helper(self.installed)], 100)]
        report = self.classify(rows)
        self.assertTrue(report['blocked'])
        self.assertEqual([entry['pid'] for entry in report['report']['owned']], [100, 101])
        self.assertEqual([entry['pid'] for entry in report['report']['unrelated']], [102, 103, 104, 105])
        self.assertNotIn('command', json.dumps(report))
        self.assertNotIn('arguments', json.dumps(report))

    def test_exact_profile_from_another_development_bundle_is_owned(self):
        """다른 개발 앱도 정확한 검수 프로필을 쓰면 종료 대상이며 설치판은 조회에서 빠진다."""
        for arguments in ([f'--user-data-dir={self.profile}'], [f'-user-data-dir={self.profile}'],
                          [f'\t --user-data-dir={self.profile}\r\n']):
            with self.subTest(arguments=arguments):
                report = self.classify([(201, self.worktree, arguments), (202, self.installed, arguments)])
                self.assertEqual([entry['pid'] for entry in report['report']['owned']], [201])
                self.assertEqual(report['report']['unrelated'], [])

    def test_similar_paths_and_other_arguments_do_not_block(self):
        """앱 접두 경로·프로필의 접미사·다른 인수에 적힌 경로로 종료 범위를 넓히지 않는다."""
        sibling = self.bundle(str(self.selected.parents[2].relative_to(self.root)) + '.old', 'Yee')
        rows = [(301, self.other, [f'--user-data-dir={self.profile}-other']),
                (302, self.worktree, [f'--user-data-dir={self.profile}/child']),
                (303, sibling, [f'--log-file={self.profile}', f'--app={self.selected}']),
                (304, self.other, [f'--user-data-dir={self.profile} archived', '--no-first-run']),
                (305, self.worktree, ['--user-data-dir', self.profile + ' archived']),
                (306, self.other, ['--user-data-dir'])]
        report = self.classify(rows)
        self.assertFalse(report['blocked'])
        self.assertEqual(len(report['report']['unrelated']), 6)

    def test_real_arguments_never_fall_back_to_flattened_commands(self):
        """실제 argv가 다른 프로필이면 같은 접두사의 command 문자열로 되돌아가지 않는다."""
        inventory = self.inventory([(350, self.other, [f'--user-data-dir={self.profile} archived'])])
        inventory[0]['command'] = f'{self.other} --user-data-dir={self.profile} archived --no-first-run'
        report = self.run_node('''
const report = inspectDevelopmentProcesses(input.executable, input.profile,
  () => JSON.stringify(input.inventory));
requireShutdown(report.owned);
console.log(JSON.stringify(report));
''', inventory=inventory)
        self.assertEqual(report, {'owned': [], 'unrelated': [{'pid': 350, 'executable': str(self.other)}]})

    def test_profile_uses_the_last_switch_and_stops_at_the_argument_terminator(self):
        """Chromium처럼 '=' 값만 읽고 중복 스위치·빈 값·-- 뒤의 위치 인수를 구분한다."""
        rows = [(360, self.other, [f'--user-data-dir={self.profile}', '--user-data-dir=/tmp/other']),
                (361, self.worktree, ['--', f'--user-data-dir={self.profile}']),
                (362, self.other, ['--user-data-dir', self.profile]),
                (363, self.other, ['--user-data-dir', '--', f'--user-data-dir={self.profile}']),
                (364, self.other, [f'--user-data-dir={self.profile}', '--user-data-dir'])]
        self.assertFalse(self.classify(rows)['blocked'])
        rows = [(365, self.other, ['--user-data-dir=/tmp/other', f'--user-data-dir={self.profile}']),
                (366, self.other, ['--user-data-dir', f'--user-data-dir={self.profile}'])]
        self.assertTrue(self.classify(rows)['blocked'])

    def test_selected_executable_symlink_uses_the_real_bundle(self):
        """선택 실행 파일의 심볼릭 링크도 실제 번들 내부 Helper와 같은 범위로 처리한다."""
        alias = self.root / 'selected-browser'
        alias.symlink_to(self.selected)
        report = self.classify([(401, self.helper(self.selected), ['--type=renderer'])], executable=str(alias))
        self.assertTrue(report['blocked'])
        self.assertEqual(len(report['report']['owned']), 1)

    def test_shutdown_waits_for_owned_helpers_and_ignores_remaining_other_output(self):
        """본체 종료 후 Helper가 사라질 때까지만 기다리며 다른 출력은 대기를 막지 않는다."""
        helper = (501, self.helper(self.selected), ['--type=renderer'])
        unrelated = (502, self.other, ['--no-first-run'])
        report = self.run_node('''
const inventories = [...input.inventories];
let waits = 0;
await waitForShutdown(() => inspectDevelopmentProcesses(input.executable, input.profile,
  () => JSON.stringify(inventories.shift())).owned, async () => {++waits;});
console.log(JSON.stringify({waits, remaining: inventories.length}));
''', inventories=[self.inventory([helper, unrelated]), self.inventory([unrelated])])
        self.assertEqual(report, {'waits': 1, 'remaining': 0})

    def test_both_observation_guards_use_current_scope_and_record_only_count_and_paths(self):
        """두 스크립트의 실제 연결부가 같은 앱·프로필로 분류하고 무관한 개수·경로만 남긴다."""
        unrelated = {'pid': 601, 'executable': str(self.other)}
        result = self.run_node('''
let checks = 0, records = 0;
for (const filename of ['observe-youtube-live.mjs', 'test-content-blocking.mjs']) {
  const source = readFileSync(`${input.root}/tools/dev/${filename}`, 'utf8');
  const observer = filename.startsWith('observe-');
  const start = source.indexOf(observer ? '  const processes = inspectDevelopmentProcesses(' : 'function requireYeeShutdown(');
  const end = source.indexOf(observer ? '  const directory = path.join(output, label)' : 'async function prepareShutdownHelper', start);
  assert.ok(start >= 0 && end > start);
  for (const owned of [[], [{pid: 602, executable: input.executable}]]) {
    const report = {}, unrelatedProcesses = [], messages = [];
    const context = {executable: input.executable, yeeBrowserBin: input.executable,
      profile: input.profile, mode: 'on', label: 'scope-test', report, unrelatedProcesses,
      inspectDevelopmentProcesses(executable, profile) {
        assert.equal(executable, input.executable);
        assert.equal(profile, input.profile);
        ++checks;
        return {owned, unrelated: [input.unrelated]};
      }, requireShutdown, console: {log(...args) {messages.push(args);}},
      async saveReport() {++records;}};
    const code = `(async () => {${source.slice(start, end)}\n${observer ? '' : "requireYeeShutdown(profile, mode);"}})()`;
    if (owned.length) await assert.rejects(vm.runInNewContext(code, context), /Gracefully/);
    else await vm.runInNewContext(code, context);
    const summaries = observer ? report.unrelatedProcesses : unrelatedProcesses;
    assert.equal(summaries.length, 1);
    assert.equal(summaries[0].count, 1);
    assert.equal(JSON.stringify(summaries[0].paths), JSON.stringify([input.unrelated.executable]));
    assert.equal(Object.keys(summaries[0]).sort().join(','), observer ? 'count,label,paths' : 'count,mode,paths');
    assert.equal(messages.length, 1);
  }
}
console.log(JSON.stringify({checks, records}));
''', unrelated=unrelated)
        self.assertEqual(result, {'checks': 4, 'records': 2})


if __name__ == '__main__':
    unittest.main()
