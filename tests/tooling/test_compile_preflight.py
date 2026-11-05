#!/usr/bin/env python3
"""임시 overlay와 가짜 빌드 도구로 사전 컴파일의 목록·종료 코드 계약을 검사한다."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('zsh'), 'zsh가 필요하다')
class CompilePreflightTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='yee-compile-preflight-')
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name).resolve() / 'repo with spaces'
        dev = self.repo / 'tools/dev'
        dev.mkdir(parents=True)
        shutil.copytree(ROOT / 'tools/dev/lib', dev / 'lib')
        for name in ('common.zsh', 'check-compile.sh'):
            shutil.copy(ROOT / 'tools/dev' / name, dev / name)
        overlay = self.repo / 'tools/overlay'
        shutil.copytree(ROOT / 'tools/overlay/lib', overlay / 'lib')
        shutil.copy(ROOT / 'tools/overlay/install-yee-ui-sources.sh', overlay)
        (overlay / 'brand_config.py').write_text('print("Yee")\n')
        (dev / 'chromium-version.py').write_text(
            'import os, sys\n'
            'sys.exit(int(os.environ.get("PIN_EXIT", "0")))\n')
        self.local = self.repo / 'local'
        self.src = self.local / 'chromium/src'
        self.out = self.src / 'out/YeeStable157'
        self.out.mkdir(parents=True)
        (self.out / 'build.ninja').write_text('# 가짜 graph\n')
        (self.src / 'BUILD.gn').touch()
        installed = self.src / 'chrome/browser/ui/views/yee'
        installed.mkdir(parents=True)
        (installed / 'BUILD.gn').write_text('owned\n')
        (self.src / 'chrome/browser/ui/BUILD.gn').touch()
        self.write_tool(self.local / 'depot_tools/gclient', '#!/bin/sh\nexit 0\n')
        self.bin = self.repo / 'fake tools'
        self.write_tool(self.bin / 'df', '#!/bin/sh\nprintf "header\\nfixture 0 0 104857600 0 /\\n"\n')
        self.write_tool(self.bin / 'caffeinate', '#!/bin/sh\nshift\nexec "$@"\n')
        self.write_tool(self.bin / 'nice', '#!/bin/sh\nshift 2\nexec "$@"\n')
        self.events = self.repo / 'events.jsonl'
        self.write_tool(self.bin / 'autoninja', f'''#!{sys.executable}
import json, os, sys
from pathlib import Path
with open(os.environ['EVENTS'], 'a') as stream:
    stream.write(json.dumps({{'args': sys.argv[1:], 'cwd': os.getcwd(),
                             'cache': os.environ.get('XDG_CACHE_HOME')}}) + '\\n')
assert (Path(os.environ['SRC']) / 'chrome/browser/ui/views/yee/main.cc').read_text() == 'owned\\n'
if os.environ.get('BUILD_EXIT'):
    print("FAILED: obj/chrome/one/patched.o compiler's diagnostic" if os.environ.get('MALFORMED_FAILURE')
          else 'FAILED: obj/chrome/one/patched.o')
    print('FAILED: uuid "./obj/yee/native.o" CXX obj/yee/native.o')
    print('FAILED: obj/dependency.o')
    sys.exit(int(os.environ['BUILD_EXIT']))
''')
        (self.repo / 'build').mkdir()
        (self.repo / 'build/overlay.json').write_text(json.dumps({
            'schema_version': 2,
            'patches': [{'role': 'shell', 'file': 'patches/0001.patch', 'strategy': 'whole'}],
            'source_roots': [{'source': 'browser/ui', 'destination': 'chrome/browser/ui/views/yee'}],
            'generated_roots': [], 'new_glue': [],
            'ignored_source_names': ['.DS_Store', '__pycache__']}))
        owned = self.repo / 'browser/ui'
        owned.mkdir(parents=True)
        for name in ('main.cc', 'native.mm', 'platform_win.cc', 'header.h', 'BUILD.gn',
                     '__pycache__/ignored.cc', '.DS_Store/ignored.mm'):
            target = owned / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('owned\n')
        (self.repo / 'browser/README.md').write_text('소유권 안내\n')
        patches = self.repo / 'patches'
        patches.mkdir()
        patch = ''
        for name in ('patched.cc', 'platform_win.cc', 'disabled.cc', 'header_like.mm',
                     'added.mm', 'mode.cc', 'deleted.cc', 'empty_deleted.mm', 'header.h'):
            path = 'chrome/' + name
            patch += f'diff --git a/{path} b/{path}\n'
            if name == 'deleted.cc':
                patch += f'deleted file mode 100644\n--- a/{path}\n+++ /dev/null\n@@ -1 +0,0 @@\n-old\n'
            elif name == 'empty_deleted.mm':
                patch += 'deleted file mode 100644\nindex e69de29..0000000\n'
            elif name == 'mode.cc':
                patch += 'old mode 100644\nnew mode 100755\n'
            elif name == 'added.mm':
                patch += f'new file mode 100644\n--- /dev/null\n+++ b/{path}\n@@ -0,0 +1 @@\n+new\n'
            else:
                patch += f'--- a/{path}\n+++ b/{path}\n@@ -1 +1 @@\n'
                patch += '--- old\n+++ /dev/null\n' if name == 'header_like.mm' else '-old\n+new\n'
        (patches / '0001.patch').write_text(patch)
        self.nodes = {
            '../../chrome/patched.cc': ['obj/chrome/one/patched.o', 'obj/chrome/two/patched.o'],
            '../../chrome/header_like.mm': ['obj/chrome/header_like.o'],
            '../../chrome/added.mm': ['obj/chrome/added.o'],
            '../../chrome/mode.cc': ['obj/chrome/mode.o'],
            '../../chrome/disabled.cc': ['phony/disabled'],
            '../../chrome/browser/ui/views/yee/main.cc': ['obj/yee/main.o'],
            '../../chrome/browser/ui/views/yee/native.mm': ['obj/yee/native.o'],
        }
        for outputs in self.nodes.values():
            for output in outputs:
                if Path(output).suffix == '.o':
                    obj = self.out / output
                    obj.parent.mkdir(parents=True, exist_ok=True)
                    obj.write_text('가짜 object\n')
        self.graph = self.repo / 'graph.json'
        self.graph.write_text(json.dumps(self.nodes))
        self.write_tool(self.src / 'third_party/ninja/ninja', f'''#!{sys.executable}
import json, os, sys
from pathlib import Path
if os.environ.get('QUERY_EXIT'):
    print('가짜 graph 조회 실패', file=sys.stderr)
    sys.exit(int(os.environ['QUERY_EXIT']))
nodes = json.loads(Path(os.environ['GRAPH']).read_text())
args = sys.argv[sys.argv.index('-t') + 1:]
if args == ['targets', 'rule']:
    print('\\n'.join(nodes))
elif args[0] == 'query':
    for path in args[1:]:
        if path == 'obj/dependency.o':
            print(path + ':\\n  input: cxx\\n    ../../base/dependency.cc\\n  outputs:')
        else:
            print(path + ':\\n  outputs:')
            for output in nodes[path]:
                print('    ' + output)
else:
    sys.exit(99)
''')
        self.env = {**os.environ, 'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
                    'YEE_LOCAL_BUILD_ROOT': str(self.local), 'YEE_OUT_NAME': 'YeeStable157',
                    'YEE_BUILD_JOBS': '3', 'PYTHONDONTWRITEBYTECODE': '1',
                    'EVENTS': str(self.events), 'SRC': str(self.src), 'GRAPH': str(self.graph)}
        (self.out / 'build.ninja.d').write_text(
            'build.ninja.stamp: ../../BUILD.gn ../../chrome/browser/ui/views/yee/BUILD.gn\n')
        (self.out / 'build.ninja.stamp').touch()

    def write_tool(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        path.chmod(0o755)

    def run_check(self, *args, **env):
        return subprocess.run(['zsh', str(self.repo / 'tools/dev/check-compile.sh'), *args],
                              env={**self.env, **env}, capture_output=True, text=True)

    def test_list_extracts_live_patch_and_mapped_owned_sources_without_building(self):
        """삭제·파일 헤더처럼 생긴 본문을 구분하고 overlay 제외 규칙과 graph 분류를 확인한다."""
        result = self.run_check('--list')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('0001 .cc/.mm (삭제 제외): 6', result.stdout)
        self.assertIn('owned .cc/.mm (목적지): 3', result.stdout)
        self.assertIn('실제 컴파일 대상: 6개 파일 / 7개 object', result.stdout)
        self.assertIn('이 플랫폼에서 미컴파일: 3개 파일', result.stdout)
        self.assertIn('다른 플랫폼·빌드 구성에서 추가 점검', result.stdout)
        excluded = result.stdout.split('이 플랫폼에서 미컴파일:')[1]
        for path in ('chrome/platform_win.cc', 'chrome/disabled.cc',
                     'chrome/browser/ui/views/yee/platform_win.cc'):
            self.assertIn(path, excluded)
        for omitted in ('deleted.cc', 'empty_deleted.mm', 'ignored.cc', 'ignored.mm', 'header.h'):
            self.assertNotIn(omitted, result.stdout)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())
        self.assertFalse((self.src / 'chrome/browser/ui/views/yee/main.cc').exists())

    def test_build_uses_one_keep_going_call_with_shared_cache_and_all_objects(self):
        """동기화 후 캐시·출력·작업 수를 유지하며 모든 object를 한 번에 전달한다."""
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in self.events.read_text().splitlines()]
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(event['args'][:8], ['-C', 'out/YeeStable157', '-j', '3', '-k', '0',
                                           '../../chrome/added.mm^',
                                           '../../chrome/browser/ui/views/yee/main.cc^'])
        self.assertIn('obj/chrome/one/patched.o', event['args'])
        self.assertIn('obj/chrome/two/patched.o', event['args'])
        self.assertNotIn('../../chrome/patched.cc^', event['args'])
        self.assertEqual(len(event['args'][6:]), 7)
        self.assertEqual(event['cwd'], str(self.src))
        self.assertEqual(event['cache'], str(self.local / 'cache'))
        self.assertEqual(sorted(p.name for p in (self.src / 'out').iterdir()), ['YeeStable157'])

    def test_failure_propagates_autoninja_status_and_reports_sources(self):
        """Ninja·Siso 형식의 실패와 의존 object 실패를 소스로 보고하며 원래 코드를 반환한다."""
        result = self.run_check(BUILD_EXIT='37')
        self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
        logs = list((self.local / 'logs').glob('compile-preflight.*'))
        self.assertEqual(len(logs), 1)
        self.assertEqual((logs[0] / 'failed-sources.txt').read_text().splitlines(),
                         ['base/dependency.cc', 'chrome/browser/ui/views/yee/native.mm', 'chrome/patched.cc'])
        self.assertTrue((logs[0] / 'build.log').exists())

    def test_failure_with_unmatched_quote_falls_back_for_only_that_line(self):
        """설명의 닫히지 않은 따옴표 뒤에도 다른 줄의 인용 경로와 실패 소스를 보존한다."""
        result = self.run_check(BUILD_EXIT='37', MALFORMED_FAILURE='1')
        self.assertEqual(result.returncode, 37, result.stdout + result.stderr)
        directory = next((self.local / 'logs').glob('compile-preflight.*'))
        self.assertEqual((directory / 'failed-sources.txt').read_text().splitlines(),
                         ['base/dependency.cc', 'chrome/browser/ui/views/yee/native.mm', 'chrome/patched.cc'])
        self.assertNotIn('No closing quotation', result.stderr)

    def test_unbuilt_objects_are_excluded_and_partial_coverage_is_reported(self):
        """미빌드 object만 빼고 다중 출력의 나머지를 빌드하며 불완전한 점검임을 표시한다."""
        missing = {'chrome/patched.cc': ['obj/chrome/one/patched.o'],
                   'chrome/browser/ui/views/yee/native.mm': ['obj/yee/native.o']}
        for outputs in missing.values():
            for output in outputs:
                (self.out / output).unlink()
        for args in [('--list',), ()]:
            with self.subTest(args=args):
                result = self.run_check(*args)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('실제 컴파일 대상: 5개 파일 / 5개 object', result.stdout)
                self.assertIn('현재 출력에서 미빌드: 2개 파일 / 2개 object', result.stdout)
                self.assertIn('2개 미빌드로 생략 — 완전한 통과 아님', result.stdout)
                self.assertNotIn('사전 컴파일 통과.', result.stdout)
                if not args:
                    summary = result.stdout.split('선택한 대상의 사전 컴파일 완료.')[1]
                    self.assertIn('현재 출력에서 미빌드: 2개 파일 / 2개 object (생략)', summary)
                    self.assertIn('이 플랫폼에서 미컴파일: 3개 파일', summary)
                    self.assertIn('완전한 통과 아님', summary)
        events = [json.loads(line) for line in self.events.read_text().splitlines()]
        self.assertEqual(len(events), 1)
        targets = events[0]['args'][6:]
        self.assertEqual(len(targets), 5)
        self.assertIn('obj/chrome/two/patched.o', targets)
        for target in ('obj/chrome/one/patched.o', '../../chrome/patched.cc^',
                       '../../chrome/browser/ui/views/yee/native.mm^'):
            self.assertNotIn(target, targets)
        directory = next((self.local / 'logs').glob('compile-preflight.*'))
        plan = json.loads((directory / 'plan.json').read_text())
        self.assertEqual(plan['unbuilt_objects'], missing)
        self.assertFalse(plan['include_unbuilt'])
        self.assertEqual(plan['compiled']['chrome/patched.cc'], ['obj/chrome/two/patched.o'])
        self.assertNotIn('chrome/browser/ui/views/yee/native.mm', plan['compiled'])

    def test_include_unbuilt_lists_in_either_option_order_and_builds_all_objects(self):
        """비어 있는 출력에서도 명시한 옵션은 미빌드를 포함하고 목록 모드와 순서 없이 결합한다."""
        for obj in self.out.rglob('*.o'):
            obj.unlink()
        for args in [('--list', '--include-unbuilt'), ('--include-unbuilt', '--list')]:
            result = self.run_check(*args)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('실제 컴파일 대상: 6개 파일 / 7개 object', result.stdout)
            self.assertIn('현재 출력에서 미빌드: 6개 파일 / 7개 object', result.stdout)
            self.assertIn('7개 미빌드 object를 --include-unbuilt로 컴파일 대상에 포함', result.stdout)
            self.assertNotIn('미빌드로 생략', result.stdout)
        self.assertFalse(self.events.exists())
        result = self.run_check('--include-unbuilt')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        events = [json.loads(line) for line in self.events.read_text().splitlines()]
        self.assertEqual(len(events), 1)
        self.assertEqual(len(events[0]['args'][6:]), 7)
        directory = next((self.local / 'logs').glob('compile-preflight.*'))
        plan = json.loads((directory / 'plan.json').read_text())
        self.assertTrue(plan['include_unbuilt'])
        self.assertEqual(len(plan['unbuilt_objects']), 6)
        self.assertEqual(sum(map(len, plan['unbuilt_objects'].values())), 7)

    def test_success_summary_distinguishes_included_unbuilt_and_full_source_coverage(self):
        """모든 소스를 점검한 완료 요약은 미빌드 포함과 플랫폼 생략 0개를 계획에서 다시 읽는다."""
        for index, source in enumerate(('chrome/platform_win.cc', 'chrome/disabled.cc',
                                        'chrome/browser/ui/views/yee/platform_win.cc')):
            self.nodes['../../' + source] = [f'obj/extra-{index}.o']
        self.graph.write_text(json.dumps(self.nodes))
        for obj in self.out.rglob('*.o'):
            obj.unlink()
        result = self.run_check('--include-unbuilt')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        summary = result.stdout.split('선택한 대상의 사전 컴파일 완료.')[1]
        self.assertIn('현재 출력에서 미빌드: 9개 파일 / 10개 object (컴파일 대상에 포함)', summary)
        self.assertIn('이 플랫폼에서 미컴파일: 0개 파일', summary)
        self.assertIn('선택한 컴파일 단위 전체 완료', summary)
        self.assertNotIn('완전한 통과 아님', summary)

    def test_all_objects_unbuilt_stops_with_distinct_status_and_preserves_plan(self):
        """기본 모드에서 전부 미빌드이면 목록과 실행 모두 종료 코드 16으로 안내하고 빌드하지 않는다."""
        for obj in self.out.rglob('*.o'):
            obj.unlink()
        for args in [('--list',), ()]:
            result = self.run_check(*args)
            self.assertEqual(result.returncode, 16, result.stdout + result.stderr)
            self.assertIn('출력이 비어 있다 — 이식 점검이면 --include-unbuilt로 실행', result.stderr)
            self.assertIn('실제 컴파일 대상: 0개 파일 / 0개 object', result.stdout)
        self.assertFalse(self.events.exists())
        directory = next((self.local / 'logs').glob('compile-preflight.*'))
        plan = json.loads((directory / 'plan.json').read_text())
        self.assertEqual(plan['compiled'], {})
        self.assertEqual(plan['targets'], [])
        self.assertEqual(sum(map(len, plan['unbuilt_objects'].values())), 7)
        self.assertFalse((directory / 'build.log').exists())

    def test_missing_graph_invalid_jobs_and_pin_stop_before_sync_or_build(self):
        """출력 생성·동기화 전에 기존 graph·작업 수·버전 사전 검사를 강제한다."""
        for env, expected in [({'YEE_BUILD_JOBS': '0'}, 14), ({'PIN_EXIT': '29'}, 29)]:
            with self.subTest(env=env):
                self.assertEqual(self.run_check(**env).returncode, expected)
        (self.out / 'build.ninja').unlink()
        for args in [(), ('--list',)]:
            self.assertEqual(self.run_check(*args).returncode, 6)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())
        self.assertFalse((self.src / 'chrome/browser/ui/views/yee/main.cc').exists())

    def test_graph_lookup_error_is_not_a_platform_exclusion(self):
        """조회 도구 실패를 플랫폼 미컴파일 목록이나 성공으로 숨기지 않는다."""
        result = self.run_check('--list', QUERY_EXIT='9')
        self.assertEqual(result.returncode, 3)
        self.assertIn('가짜 graph 조회 실패', result.stderr)
        self.assertNotIn('이 플랫폼에서 미컴파일:', result.stdout)
        self.assertFalse(self.events.exists())

    def test_helper_imports_do_not_create_bytecode_without_environment_opt_out(self):
        """환경 변수 없이도 overlay import가 tools 아래 새 바이트코드를 만들지 않는다."""
        for cache in self.repo.glob('tools/**/__pycache__'):
            shutil.rmtree(cache)
        env = {key: value for key, value in self.env.items() if key != 'PYTHONDONTWRITEBYTECODE'}
        result = subprocess.run([sys.executable, str(self.repo / 'tools/dev/lib/compile_preflight.py'),
                                 'plan', str(self.src), str(self.out), '--list'],
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list(self.repo.glob('tools/**/__pycache__')), [])

    def test_stale_graph_warns_in_list_and_stops_compilation_before_query(self):
        """새 BUILD.gn이 기존 graph에 없는 소스를 플랫폼 미컴파일로 확정하지 못하게 한다."""
        build_file = self.src / 'BUILD.gn'
        newer = (self.out / 'build.ninja.stamp').stat().st_mtime_ns + 1_000_000
        os.utime(build_file, ns=(newer, newer))
        listed = self.run_check('--list')
        self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
        self.assertIn('오래된 빌드 graph', listed.stderr)
        self.assertIn('../../BUILD.gn', listed.stderr)
        self.assertIn('플랫폼 미컴파일 분류를 확정할 수 없다', listed.stderr)
        self.assertFalse((self.local / 'logs').exists())
        compiled = self.run_check(QUERY_EXIT='9')
        self.assertEqual(compiled.returncode, 19, compiled.stdout + compiled.stderr)
        self.assertIn('빌드 설정을 먼저 갱신하라: ./tools/dev/configure.sh', compiled.stderr)
        self.assertNotIn('이 플랫폼에서 미컴파일:', compiled.stdout)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())

    def test_graph_is_checked_after_owned_build_file_sync(self):
        """동기화가 BUILD.gn과 새 소스를 바꾸면 그 직후 재생성 필요 상태를 감지한다."""
        (self.repo / 'browser/ui/BUILD.gn').write_text('새 소스 추가\n')
        (self.repo / 'browser/ui/new.cc').write_text('새 소스\n')
        result = self.run_check()
        self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
        self.assertIn('../../chrome/browser/ui/views/yee/BUILD.gn', result.stderr)
        self.assertTrue((self.src / 'chrome/browser/ui/views/yee/new.cc').is_file())
        self.assertNotIn('이 플랫폼에서 미컴파일:', result.stdout)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())

    def test_depfile_escapes_and_stamp_timestamp_prevent_false_stale_warning(self):
        """공백·따옴표·달러·역슬래시 경로를 풀고 manifest보다 최신인 stamp를 기준으로 삼는다."""
        dependency = self.out / "args with 'quotes' #$\\name.gn"
        dependency.touch()
        escaped = ''.join('\\' + char if char in ' \\#:' else '$$' if char == '$' else char
                          for char in dependency.name)
        with (self.out / 'build.ninja.d').open('a') as depfile:
            depfile.write('  \\\n  ' + escaped + '\n')
        generated_at = dependency.stat().st_mtime_ns + 1_000_000
        os.utime(self.out / 'build.ninja.stamp', ns=(generated_at, generated_at))
        listed = self.run_check('--list')
        self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
        self.assertNotIn('오래된 빌드 graph', listed.stderr)
        os.utime(dependency, ns=(generated_at + 1, generated_at + 1))
        listed = self.run_check('--list')
        self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
        self.assertIn(dependency.name, listed.stderr)
        self.assertEqual(self.run_check().returncode, 19)
        self.assertFalse(self.events.exists())

    def test_missing_depfile_or_dependency_requires_configuration(self):
        """의존 파일 누락과 읽을 수 없는 최신 상태를 조용히 통과시키지 않는다."""
        dependency = self.out / 'removed.gni'
        (self.out / 'build.ninja.d').write_text('build.ninja.stamp: removed.gni\n')
        for missing in (dependency, self.out / 'build.ninja.d'):
            with self.subTest(missing=missing.name):
                missing.unlink(missing_ok=True)
                result = self.run_check()
                self.assertEqual(result.returncode, 19, result.stdout + result.stderr)
                self.assertIn('누락', result.stderr)
                self.assertIn('./tools/dev/configure.sh', result.stderr)
                self.assertFalse((self.local / 'logs').exists())
                listed = self.run_check('--list')
                self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
                self.assertIn('경고:', listed.stderr)
        self.assertFalse(self.events.exists())

    def test_manifest_timestamp_is_used_when_stamp_is_missing(self):
        """stamp가 없는 기존 graph는 build.ninja의 mtime으로 최신 여부를 확인한다."""
        (self.out / 'build.ninja.stamp').unlink()
        # 소유 BUILD.gn은 루트보다 나중에 생성되므로 모든 의존 파일 뒤로 맞춘다.
        generated_at = max((self.src / path).stat().st_mtime_ns for path in (
            'BUILD.gn', 'chrome/browser/ui/views/yee/BUILD.gn')) + 1_000_000
        os.utime(self.out / 'build.ninja', ns=(generated_at, generated_at))
        listed = self.run_check('--list')
        self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
        self.assertNotIn('오래된 빌드 graph', listed.stderr)
        os.utime(self.src / 'BUILD.gn', ns=(generated_at + 1, generated_at + 1))
        self.assertEqual(self.run_check().returncode, 19)

    def test_list_does_not_require_free_disk_space(self):
        """목록만 읽을 때는 컴파일용 15 GiB 공간 검사나 동기화가 필요하지 않다."""
        self.write_tool(self.bin / 'df', '#!/bin/sh\nprintf "header\\nfixture 0 0 0 0 /\\n"\n')
        listed = self.run_check('--list')
        self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
        self.assertNotIn('Disk guard:', listed.stdout)
        self.assertEqual(self.run_check().returncode, 4)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())
        self.assertFalse((self.src / 'chrome/browser/ui/views/yee/main.cc').exists())

    def test_unknown_option_returns_usage_status_without_effects(self):
        """알 수 없는 옵션은 목록과 조합해도 사용법 오류 2로 부작용 전에 끝난다."""
        for args in [('--unknown',), ('--list', '--unknown')]:
            result = self.run_check(*args)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('사용법:', result.stderr)
        self.assertFalse(self.events.exists())
        self.assertFalse((self.local / 'logs').exists())

    def test_no_objects_never_invokes_autoninja_without_targets(self):
        """모든 파일이 미컴파일이면 옵션과 관계없이 종료 코드 16으로 미점검 사실을 알린다."""
        self.graph.write_text('{}')
        for args in [(), ('--list',), ('--include-unbuilt',), ('--list', '--include-unbuilt')]:
            result = self.run_check(*args)
            self.assertEqual(result.returncode, 16, result.stdout + result.stderr)
            self.assertIn('실제 컴파일 대상: 0개 파일 / 0개 object', result.stdout)
            self.assertIn('이 플랫폼에서 미컴파일: 9개 파일', result.stdout)
            self.assertIn('이 graph에서 컴파일할 대상이 없다.', result.stderr)
        self.assertFalse(self.events.exists())


if __name__ == '__main__':
    unittest.main()
