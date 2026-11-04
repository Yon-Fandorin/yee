"""Yee 테스트 선언의 소스·빌드 타깃·gate 필터 연결을 검사한다."""

import fnmatch
import contextlib
import difflib
from dataclasses import dataclass
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/dev/lib'))
from test_tiers import case_names, load_manifest, resolve_filter

PATCH = ROOT / 'patches/0001-integrate-yee-shell.patch'
# 괄호와 선언을 찾을 때만 C++ 주석·문자열을 가린다. C++ 문법은 해석하지 않는다.
CPP_LITERAL = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]*)\(.*?\)\1"|'
                         r'"(?:\\.|[^"\\])*"|(?<![\w])(?:u8|u|U|L)?\'(?:\\.|[^\'\\])*\'', re.S)
TEST = re.compile(r'^[ \t]*((?:IN_PROC_BROWSER_)?TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\('
                  r'\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{', re.M)
GATE_MODES = {
    'test-agent-browser.sh': [()],
    'test-agent-prompt.sh': [()],
    'test-browser-surface-layout.sh': [('fast',), ('horizontal',), ('interactive',), ('browser',), ('all',)],
    'test-content-blocking-native.sh': [()],
    'test-header.sh': [('unit',), ('interactive',), ('all',)],
    'test-internal-pages.sh': [()],
    'test-macos-install.sh': [()],
    'test-omnibox-accessibility.sh': [()],
    'test-projects.sh': [('unit',), ('browser',), ('all',)],
    'test-sidebar.sh': [('unit',), ('interactive',), ('all',)],
    'test-site-controls.sh': [()],
}
TIERED_GATES = {'test-header.sh', 'test-sidebar.sh', 'test-browser-surface-layout.sh',
               'test-projects.sh', 'test-site-controls.sh', 'test-omnibox-accessibility.sh'}


@dataclass
class GateRun:
    script: str
    platform: str
    binary: str
    arguments: list
    built: set
    mode: tuple = ()
    tier: str = 'background'
    display: bool = False
    shutdown: bool = False


def case_ranges(text):
    """테스트 선언에서 닫는 괄호까지의 checkout 줄 범위를 반환한다."""
    code = CPP_LITERAL.sub(lambda match: re.sub(r'[^\n]', ' ', match.group()), text)
    result = {}
    for declaration in TEST.finditer(code):
        depth = 1
        for brace in re.finditer(r'[{}]', code[declaration.end():]):
            depth += 1 if brace.group() == '{' else -1
            if not depth:
                end = declaration.end() + brace.end()
                break
        else:
            raise ValueError('닫히지 않은 테스트: ' + declaration.group(2) + '.' + declaration.group(3))
        name = declaration.group(2) + '.' + declaration.group(3).removeprefix('MAYBE_')
        result[name] = (text.count('\n', 0, declaration.start()) + 1,
                        text.count('\n', 0, end) + 1,
                        declaration.group(1).endswith('_P'))
    return result


def code_without_spacing(text):
    """포맷·주석만 바뀐 hunk는 제외하되 문자열 내부 공백은 보존한다."""
    output, cursor = [], 0
    for match in CPP_LITERAL.finditer(text):
        output.append(re.sub(r'\s+', '', text[cursor:match.start()]))
        if not match.group().startswith(('//', '/*')):
            output.append(match.group())
        cursor = match.end()
    return ''.join(output) + re.sub(r'\s+', '', text[cursor:])


def patch_changes(patch):
    """기본 문맥의 patch에서 변경한 적용 후 줄 범위와 검증용 줄을 읽는다."""
    for section in patch.split('diff --git ')[1:]:
        path = re.search(r'^\+\+\+ b/(.+)$', section, re.M)
        if not path:
            continue
        changed, expected = [], {}
        cursor, start = None, None
        removed, added = [], []

        def flush():
            if start is not None and code_without_spacing('\n'.join(removed)) != code_without_spacing('\n'.join(added)):
                changed.append((start, max(start, cursor - 1)))

        for line in section.splitlines():
            hunk = re.match(r'@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@', line)
            if hunk:
                flush()
                cursor, start = int(hunk.group(1)), None
                removed, added = [], []
            elif cursor is not None and line.startswith(('+', '-')):
                if start is None:
                    start = cursor
                if line.startswith('+'):
                    added.append(line[1:])
                    expected[cursor] = line[1:]
                    cursor += 1
                else:
                    removed.append(line[1:])
            elif cursor is not None and line.startswith(' '):
                flush()
                start, removed, added = None, [], []
                expected[cursor] = line[1:]
                cursor += 1
        flush()
        yield path.group(1), changed, expected


def chromium_checkout(root, environ):
    explicit = environ.get('YEE_LOCAL_BUILD_ROOT')
    candidates = ([Path(explicit) / 'chromium/src'] if explicit else
                  sorted((root / '.local-build').glob('*/chromium/src')) +
                  [root / '.local-build/chromium/src'])
    return next((path for path in candidates if (path / 'BUILD.gn').is_file()), None)


def overlay_inventory(root):
    found = {}
    for directory in ('browser', 'renderer', 'components'):
        for source in (root / directory).rglob('*'):
            if not re.search(r'(?:test|tests)\.(?:cc|mm)$', source.name):
                continue
            text = source.read_text()
            instances = instantiations(text)
            for name, (_, _, parameterized) in case_ranges(text).items():
                found[(source.relative_to(root).as_posix(), name)] = (
                    instances.get(name.split('.')[0], []) if parameterized else False)
    return found


def upstream_inventory(chromium, patch):
    found = {}
    for path, changed, expected in patch_changes(patch):
        if not re.search(r'(?:test|tests)\.(?:cc|mm)$', path):
            continue
        text = (chromium / path).read_text()
        instances = instantiations(text)
        lines = text.splitlines()
        if any(number > len(lines) or lines[number - 1] != value for number, value in expected.items()):
            raise ValueError(f'0001과 checkout 줄이 다르다. 패치 적용 상태를 확인하라: {path}')
        for name, (start, end, parameterized) in case_ranges(text).items():
            if any(first <= end and last >= start for first, last in changed):
                found[(path, name)] = instances.get(name.split('.')[0], []) if parameterized else False
    return found


def cpp_arguments(text, start):
    """시작 괄호 다음부터 중첩 괄호·리터럴을 보존하며 인수를 나눈다."""
    code = CPP_LITERAL.sub(lambda match: ' ' * len(match.group()), text)
    arguments, first, depth = [], start, 0
    for index in range(start, len(code)):
        char = code[index]
        if char == ')' and depth == 0:
            return arguments + [text[first:index].strip()]
        if char == ',' and depth == 0:
            arguments.append(text[first:index].strip())
            first = index + 1
        elif char in '({[':
            depth += 1
        elif char in ')}]':
            depth -= 1
    raise ValueError('닫히지 않은 인스턴스화 인수')


def parameter_count(generator):
    """현재 gate의 기본 숫자 suffix를 만드는 GTest generator를 읽는다."""
    generator = re.sub(r'/\*.*?\*/|//[^\n]*|^[ \t]*#[^\n]*', '', generator, flags=re.S | re.M)
    match = re.fullmatch(r'(?:::)?testing::(Bool|Values|Combine)\s*\((.*)\)', generator, re.S)
    if not match:
        return None
    if match.group(1) == 'Bool':
        return 2
    arguments = cpp_arguments(generator, generator.index('(') + 1)
    if match.group(1) == 'Values':
        return len(arguments)
    count = 1
    for argument in arguments:
        size = parameter_count(argument)
        if size is None:
            return None
        count *= size
    return count


def instantiations(text):
    """필터와 독립적으로 실제 suite prefix와 확인 가능한 suffix를 수집한다."""
    code = CPP_LITERAL.sub(lambda match: ' ' * len(match.group()), text)
    result = {}
    for match in re.finditer(r'\bINSTANTIATE_(?:TEST_SUITE_P|TYPED_TEST_SUITE_P)\s*\(', code):
        arguments = cpp_arguments(text, match.end())
        prefix, suite = arguments[:2]
        suffixes = None
        if len(arguments) == 3:
            count = parameter_count(arguments[2])
            if count is not None:
                suffixes = tuple(str(index) for index in range(count))
        elif len(arguments) == 4:
            # 단순 switch의 이름은 정적으로 확인한다. 임의 C++ 이름 함수는
            # 평가하지 않으며 이때는 전체 suffix(*)를 고르는 필터만 인정한다.
            returns = re.findall(r'\breturn\s+([^;]+);', arguments[3])
            if returns and all(re.fullmatch(r'"\w+"', value.strip()) for value in returns):
                suffixes = tuple(value.strip()[1:-1] for value in returns)
        result.setdefault(suite, []).append((prefix, suffixes))
    return result


def strip_gn_comments(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|#[^\n]*',
                  lambda match: '' if match.group().startswith('#') else match.group(), text)


def gn_targets(root, patch, chromium=None):
    """overlay·checkout의 sources/deps를 읽고 GN 조건은 OS의 합집합으로 취급한다."""
    catalog = json.loads((root / 'build/overlay.json').read_text())
    files = {}
    for entry in catalog['source_roots']:
        for source in (root / entry['source']).rglob('BUILD.gn'):
            destination = PurePosixPath(entry['destination']) / source.relative_to(root / entry['source'])
            files[destination.as_posix()] = source.read_text()
    if chromium:
        paths = {path for path, _, _ in patch_changes(patch) if path.endswith('BUILD.gn')}
        paths.add('chrome/browser/ui/animation/BUILD.gn')
        for path in paths:
            files[path] = (chromium / path).read_text()
    # 기존 heavy browser gate의 원본 fixture는 overlay 밖 조립 지점이다.
    targets = {'browser_tests': [
        'chrome/browser/glic/selection/selection_overlay_browsertest.cc',
        'chrome/browser/ui/lens/lens_overlay_controller_browsertest.cc',
        'chrome/browser/ui/read_anything/read_anything_controller_browsertest.cc',
        'chrome/browser/ui/views/frame/browser_view_browsertest.cc',
        'chrome/browser/ui/views/sad_tab_split_view_browsertest.cc',
        'chrome/browser/ui/views/side_panel/side_panel_coordinator_browsertest.cc',
    ]}
    if chromium is None:
        # checkout이 없을 때도 overlay 검사는 유지한다. 이 native GN 연결의 실제
        # 검증은 checkout이 필요한 upstream 검사와 함께 건너뛴다.
        header_sources = [
            'browser_surface_color_controller_unittest.cc', 'native_presentation_unittest.cc',
            'sidebar_footer_unittest.cc', 'split_pane_controls_unittest.cc', 'yee_ui_unittest.cc',
        ]
        for target, sources in (
                ('yee_header_unittests', header_sources),
                ('yee_layout_unittests', header_sources + ['favorites_unittest.cc', 'group_mark_signals_unittest.cc',
                                                          'yee_tab_group_header_view_unittest.cc'])):
            targets['chrome/browser/ui/views/tabs/common:' + target] = [
                'chrome/browser/ui/views/yee/' + source for source in sources]
    for path, text in files.items():
        parent = PurePosixPath(path).parent
        text = strip_gn_comments(text)
        for match in re.finditer(r'\b(test|source_set|static_library|group)\("([\w]+)"\)\s*\{', text):
            start, depth, end = match.end(), 1, match.end()
            while end < len(text) and depth:
                depth += (text[end] == '{') - (text[end] == '}')
                end += 1
            body = text[start:end - 1]
            references = []
            for assignment in re.finditer(r'\b(sources|deps|public_deps)\s*\+?=\s*\[([^\]]*)\]', body, re.S):
                references.extend(re.findall(r'"([^"$]+)"', assignment.group(2)))
            targets[f'{parent}:{match.group(2)}'] = [normalize_label(parent, value) if ':' in value or not value.endswith(('.cc', '.mm', '.h'))
                                                     else normalize_path(parent, value) for value in references]
    return targets, catalog


def normalize_path(parent, value):
    return os.path.normpath(value[2:] if value.startswith('//') else str(parent / value))


def normalize_label(parent, value):
    if value.startswith(':'):
        return str(parent) + value
    value = normalize_path(parent, value)
    if ':' not in value:
        value += ':' + PurePosixPath(value).name
    return value


def target_sources(targets, target, seen=None):
    seen = set() if seen is None else seen
    if target in seen:
        return set()
    seen.add(target)
    found = set()
    for value in targets.get(target, []):
        if value.endswith(('.cc', '.mm')):
            found.add(value)
        else:
            found.update(target_sources(targets, value, seen))
    return found


def selected(name, parameterized, arguments):
    if (any(part.startswith('DISABLED_') for part in name.split('.'))
            and '--gtest_also_run_disabled_tests' not in arguments):
        return False
    filters = [arg.split('=', 1)[1] for arg in arguments if arg.startswith('--gtest_filter=')]
    pattern = filters[-1] if filters else '*'
    positive, _, negative = pattern.partition('-')
    positive = positive or '*'
    candidates = [name]
    if parameterized is not False:
        candidates = []
        for prefix, suffixes in parameterized:
            if suffixes is None:
                # 이름 함수를 해석할 수 없어도 올바른 prefix의 전체 선택은 확인한다.
                patterns = positive.split(':')
                base = f'{prefix}/{name}'
                if any(part.endswith('*') and fnmatch.fnmatchcase(base + '/', part)
                       for part in patterns) and not negative:
                    return True
            else:
                candidates.extend(f'{prefix}/{name}/{suffix}' for suffix in suffixes)
    return any(any(fnmatch.fnmatchcase(case, part) for part in positive.split(':'))
               and not any(fnmatch.fnmatchcase(case, part) for part in negative.split(':') if part)
               for case in candidates)


def covered_by_gate(destination, name, parameterized, targets, runs):
    for run in runs:
        binary, arguments, built = run.binary, run.arguments, run.built
        if not selected(name, parameterized, arguments):
            continue
        for target in built:
            if (target.rsplit(':', 1)[-1] == binary
                    and destination in target_sources(targets, target)):
                return True
    return False


def destination_path(path, catalog):
    for source in catalog['source_roots']:
        if path.startswith(source['source'] + '/'):
            return source['destination'] + path[len(source['source']):]
    return path


def tier_inventory(root, chromium, patch, targets, catalog, runs):
    """Yee 선언과 gate가 선택한 원본 선언을 함께 검사한다."""
    found = overlay_inventory(root)
    if chromium:
        found.update(upstream_inventory(chromium, patch))
    sources = {path for run in runs for target in run.built
               for path in target_sources(targets, target)
               if re.search(r'(?:test|tests)\.(?:cc|mm)$', path)}
    for destination in sorted(sources):
        path = destination
        for source in catalog['source_roots']:
            if destination.startswith(source['destination'] + '/'):
                path = source['source'] + destination[len(source['destination']):]
                break
        file = root / path if (root / path).is_file() else chromium / path if chromium else None
        if file is None or not file.is_file():
            continue
        text = file.read_text()
        instances = instantiations(text)
        for name, (_, _, parameterized) in case_ranges(text).items():
            parameters = instances.get(name.split('.')[0], []) if parameterized else False
            if covered_by_gate(destination, name, parameters, targets, runs):
                found[path, name] = parameters
    return {path + '::' + name: parameters for (path, name), parameters in found.items()}


def tier_violations(inventory, manifest, targets, catalog, runs, allowlist=()):
    errors = ['tier 없는 선언: ' + key for key in inventory.keys() - manifest.keys()]
    errors += ['삭제되거나 gate에서 빠진 tier 항목: ' + key for key in manifest.keys() - inventory.keys()]
    for key in inventory.keys() & manifest.keys():
        entry = manifest[key]
        path, name = key.split('::')
        destination = destination_path(path, catalog)
        matching = [run for run in runs
                    if covered_by_gate(destination, name, inventory[key], targets, [run])]
        if entry['tier'] == 'C':
            if any(run.tier == 'background' for run in matching):
                errors.append('background가 C를 선택한다: ' + key)
            for platform in entry['os']:
                for tier in ('foreground', 'all'):
                    if not any(run.tier == tier
                               and ('Windows' if run.platform.startswith(('MINGW', 'MSYS', 'CYGWIN')) else run.platform) == platform
                               and (key in allowlist or run.script == entry['owner'])
                               for run in matching):
                        errors.append(f'{tier}에 C가 없다 ({platform}): ' + key)
    return sorted(errors)


def gate_runs(root, tier_values=(None, 'background', 'foreground', 'all')):
    """복사한 gate만 가짜 빌드·실행 함수로 평가한다. 실제 개발 환경은 읽지 않는다."""
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        dev, out, bin_dir = (temp / part for part in ('dev', 'out', 'bin'))
        for part in (dev, out, bin_dir):
            part.mkdir()
        scripts = [root / 'tools/dev' / name for name in GATE_MODES]
        (dev / 'lib').mkdir()
        for name in ('test_tiers.zsh', 'test_tiers.py'):
            shutil.copy(ROOT / 'tools/dev/lib' / name, dev / 'lib' / name)
        manifest_dir = temp / 'tests/tooling'
        manifest_dir.mkdir(parents=True)
        shutil.copy(ROOT / 'tests/tooling/test_tiers.json', manifest_dir)
        native_scripts = {path.name for path in (root / 'tools/dev').glob('test-*.sh')
                          if 'YEE_OUT_DIR/' in path.read_text()}
        if native_scripts != set(GATE_MODES) | {'test-youtube-live.sh'}:
            raise AssertionError('native gate 모드 등록을 갱신해야 한다: ' + str(sorted(native_scripts)))
        capture = temp / 'capture.py'
        capture.write_text('import json,os,sys\nwith open(os.environ["CAPTURE"],"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n')
        (dev / 'common.zsh').write_text('''YEE_OUT_DIR="$FIXTURE/out"
LOCAL_BUILD_ROOT="$FIXTURE"
YEE_ROOT="$FIXTURE"
COMMON_DIR="$FIXTURE/dev"
source "$COMMON_DIR/lib/test_tiers.zsh"
YEE_UI_TEST_WINDOW_ARGS=()
require_depot_tools() { :; }
require_chromium_src() { :; }
require_yee_output_selection() { :; }
require_free_gib() { :; }
sync_yee_ui_sources() { :; }
sync_yee_branding() { :; }
prepare_yee_build_inputs() { :; }
prepare_ui_test_display() { python3 "$FIXTURE/capture.py" display; }
gracefully_quit_yee() { python3 "$FIXTURE/capture.py" shutdown; }
build_regression_targets() { shift; python3 "$FIXTURE/capture.py" build "$@"; }
run_native_test_gate() {
  local count=0 argument
  for argument in "$@"; do
    [[ "$argument" == --gtest_filter=* ]] && (( count += 1 ))
  done
  if [[ "$count" != 1 ]]; then
    print -u2 'run_native_test_gate에는 명시적 필터 하나가 필요하다'
    return 2
  fi
  python3 "$FIXTURE/capture.py" run "$@"
}
''')
        (dev / 'test-run-preflight.sh').write_text('#!/bin/zsh\nexit 0\n')
        (dev / 'test-run-preflight.sh').chmod(0o755)
        (bin_dir / 'uname').write_text('#!/bin/zsh\nprint -r -- "$GATE_PLATFORM"\n')
        (bin_dir / 'uname').chmod(0o755)
        for script in scripts:
            shutil.copy(script, dev / script.name)
            for binary in re.findall(r'\$YEE_OUT_DIR/(\w+)', script.read_text()):
                fake = out / binary
                fake.write_text('''#!/bin/zsh
if [[ " $* " == *' --gtest_list_tests '* ]]; then
  print -rl -- AgentBrowserContractTest. AgentInputRequestTest. AgentTabActivityTest. AgentRequestLedgerTest. AgentTaskPermissionsTest. AgentBridgePromptTest.
  exit 0
fi
python3 "$FIXTURE/capture.py" run "$0" "$@"
''')
                fake.chmod(0o755)
        runs = []
        for platform in ('Darwin', 'Linux', 'MSYS_NT-10.0'):
            for script in scripts:
                if script.name == 'test-macos-install.sh' and platform != 'Darwin':
                    continue
                scenarios = [(mode, tier) for mode in GATE_MODES[script.name]
                             for tier in (tier_values if script.name in TIERED_GATES else (None,))]
                for mode, tier in scenarios:
                    log = temp / 'capture.jsonl'
                    log.unlink(missing_ok=True)
                    env = {**os.environ, 'FIXTURE': str(temp), 'CAPTURE': str(log),
                           'GATE_PLATFORM': platform, 'YEE_TEST_UI': 'off',
                           'PATH': str(bin_dir) + os.pathsep + os.environ['PATH']}
                    tier_args = [f'--tier={tier}'] if tier else []
                    result = subprocess.run(['zsh', str(dev / script.name), *mode, *tier_args],
                                            env=env, capture_output=True, text=True)
                    if result.returncode:
                        raise AssertionError(f'{script.name} {mode} {tier} {platform}: {result.stderr}')
                    events = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
                    built = {arg.removeprefix('//') for event in events if event[0] == 'build' for arg in event[1:]}
                    display, shutdown = False, False
                    for event in events:
                        if event[0] == 'display':
                            display = True
                        elif event[0] == 'shutdown':
                            shutdown = True
                        elif event[0] == 'run':
                            binary = Path(event[1]).name
                            resolved_tier = tier or 'background'
                            runs.append(GateRun(script.name, platform, binary, event[2:], built,
                                                mode, resolved_tier, display, shutdown))
                            display, shutdown = False, False
        return runs


class GateCoverageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.patch = PATCH.read_text()
        cls.chromium = chromium_checkout(ROOT, os.environ)
        cls.targets, cls.catalog = gn_targets(ROOT, cls.patch, cls.chromium)
        cls.runs = gate_runs(ROOT) if shutil.which('zsh') else None
        cls.allowlist = json.loads((ROOT / 'tests/tooling/gate_coverage_allowlist.json').read_text())
        cls.manifest = load_manifest(ROOT / 'tests/tooling/test_tiers.json')

    def assert_covered(self, found, overlay):
        if self.runs is None:
            self.skipTest('gate 평가에 zsh가 필요하다')

        def is_overlay(path):
            return any(path.startswith(source['source'] + '/') for source in self.catalog['source_roots'])

        remaining = {key for key in self.allowlist if is_overlay(key.split('::')[0]) == overlay}
        missing = []
        for (path, name), parameterized in sorted(found.items()):
            key = path + '::' + name
            if key in self.allowlist:
                entry = self.allowlist[key]
                self.assertTrue(entry['reason'].strip(), key)
                self.assertTrue(entry['owner'].strip(), key)
                self.assertIn(entry['kind'], ('opt-in', 'windows'))
                self.assertEqual(entry['tier'], self.manifest[key]['tier'], key)
                remaining.remove(key)
                continue
            destination = path
            for source in self.catalog['source_roots']:
                if path.startswith(source['source'] + '/'):
                    destination = source['destination'] + path[len(source['source']):]
                    break
            if not covered_by_gate(destination, name, parameterized, self.targets, self.runs):
                missing.append(key)
        self.assertFalse(remaining, '삭제하거나 연결한 allowlist 항목: ' + str(sorted(remaining)))
        self.assertFalse(missing, 'gate가 선택하지 않는 Yee 사례:\n' + '\n'.join(missing))

    def test_overlay_cases_have_a_gate(self):
        self.assert_covered(overlay_inventory(ROOT), overlay=True)

    def test_case_tiers_and_gate_tiers(self):
        if self.runs is None:
            self.skipTest('gate 평가에 zsh가 필요하다')
        inventory = tier_inventory(ROOT, self.chromium, self.patch, self.targets, self.catalog, self.runs)
        manifest = self.manifest
        if self.chromium is None:
            manifest = {key: entry for key, entry in manifest.items() if (ROOT / key.split('::')[0]).is_file()}
        self.assertEqual(tier_violations(inventory, manifest, self.targets, self.catalog,
                                         self.runs, self.allowlist), [])
        compiled = {target.rsplit(':', 1)[-1]: target_sources(self.targets, target)
                    for run in self.runs for target in run.built}
        for key, parameters in inventory.items():
            entry = manifest[key]
            destination = destination_path(key.split('::')[0], self.catalog)
            binaries = sorted(binary for binary, sources in compiled.items() if destination in sources)
            if binaries:
                self.assertEqual(entry['binaries'], binaries, key)
            self.assertEqual(entry.get('parameters', False),
                             json.loads(json.dumps(parameters)), key)
            if key not in self.allowlist:
                path, name = key.split('::')
                self.assertTrue(any(run.script == entry['owner'] and covered_by_gate(
                    destination_path(path, self.catalog), name, parameters, self.targets, [run])
                                    for run in self.runs), 'owner gate가 선택하지 않는다: ' + key)

    def test_tier_failures_are_not_covered_by_other_runs(self):
        """다른 tier의 통과가 미분류·stale·C 혼입·foreground 누락을 가리지 않는다."""
        key = 'area/test.cc::Fixture.Case'
        inventory = {key: False}
        manifest = {key: {'tier': 'C', 'os': ['Darwin'], 'owner': 'test-area.sh'}}
        targets, catalog = {'area:small': ['area/test.cc']}, {'source_roots': []}
        run = GateRun('test-area.sh', 'Darwin', 'small', ['--gtest_filter=Fixture.Case'], {'area:small'})
        self.assertIn('tier 없는 선언: ' + key,
                      tier_violations(inventory, {}, targets, catalog, [run]))
        self.assertIn('삭제되거나 gate에서 빠진 tier 항목: ' + key,
                      tier_violations({}, manifest, targets, catalog, [run]))
        self.assertEqual(tier_violations(inventory, manifest, targets, catalog, [run]), sorted([
            'background가 C를 선택한다: ' + key,
            'foreground에 C가 없다 (Darwin): ' + key, 'all에 C가 없다 (Darwin): ' + key]))
        run.tier = 'all'
        self.assertEqual(tier_violations(inventory, manifest, targets, catalog, [run]),
                         ['foreground에 C가 없다 (Darwin): ' + key])
        manifest[key]['os'].append('Windows')
        mac_foreground = GateRun('test-area.sh', 'Darwin', 'small', run.arguments, run.built, tier='foreground')
        win_foreground = GateRun('test-area.sh', 'MSYS_NT-10.0', 'small', run.arguments, run.built, tier='foreground')
        runs = [run, mac_foreground, win_foreground]
        self.assertEqual(tier_violations(inventory, manifest, targets, catalog, runs),
                         ['all에 C가 없다 (Windows): ' + key])
        runs.append(GateRun('test-other.sh', 'MSYS_NT-10.0', 'small', run.arguments, run.built, tier='all'))
        self.assertEqual(tier_violations(inventory, manifest, targets, catalog, runs),
                         ['all에 C가 없다 (Windows): ' + key])

    def test_background_and_foreground_partition_each_run(self):
        if self.runs is None:
            self.skipTest('gate 평가에 zsh가 필요하다')
        for run in self.runs:
            if run.script not in TIERED_GATES:
                continue
            platform = 'Windows' if run.platform.startswith('MSYS') else run.platform
            pattern = next(arg.split('=', 1)[1] for arg in run.arguments if arg.startswith('--gtest_filter='))
            expected = {name for key, entry in self.manifest.items() if platform in entry['os']
                        and covered_by_gate(destination_path(key.split('::')[0], self.catalog),
                                            key.split('::')[1], entry.get('parameters', False), self.targets, [run])
                        for name in case_names(key, entry) if selected(name, False, run.arguments)}
            positive = pattern.partition('-')[0] or '*'
            for part in positive.split(':'):
                self.assertTrue(any(fnmatch.fnmatchcase(name, part) for name in expected),
                                f'바이너리에서 발견할 수 없는 필터: {run.binary} {part}')
            if run.tier != 'all':
                continue
            partitions = []
            for tier in ('background', 'foreground'):
                arguments = [arg for arg in run.arguments if not arg.startswith('--gtest_filter=')]
                candidates = [other for other in self.runs
                              if (other.script, other.platform, other.mode, other.binary, other.tier) ==
                              (run.script, run.platform, run.mode, run.binary, tier)
                              and [arg for arg in other.arguments if not arg.startswith('--gtest_filter=')] == arguments]
                partitions.append({name for name in expected
                                   if any(selected(name, False, other.arguments) for other in candidates)})
            self.assertFalse(partitions[0] & partitions[1], (run.script, run.mode, pattern))
            self.assertEqual(partitions[0] | partitions[1], expected, (run.script, run.mode, pattern))

    def test_tier_resolution_preserves_os_parameters_and_negative_filters(self):
        manifest = {
            'area/test.cc::Fixture.Case': {'os': ['Darwin'], 'tier': 'B', 'binaries': ['small'],
                                          'parameters': [['All', ['0', '1']]]},
            'area/test.cc::Fixture.Native': {'os': ['Darwin'], 'tier': 'C', 'binaries': ['small']},
            'area/browser.cc::Fixture.Browser': {'os': ['Darwin'], 'tier': 'B', 'binaries': ['browser_tests']},
        }
        pattern = 'All/Fixture.Case/*:Fixture.Native-All/Fixture.Case/1'
        self.assertEqual(resolve_filter(manifest, pattern, 'Darwin', 'background', 'small'),
                         ('All/Fixture.Case/0-All/Fixture.Case/1', 'B'))
        self.assertEqual(resolve_filter(manifest, pattern, 'Darwin', 'foreground', 'small'),
                         ('Fixture.Native-All/Fixture.Case/1', 'C'))
        self.assertEqual(resolve_filter(manifest, 'Fixture.*', 'Darwin', 'background', 'small'),
                         ('', ''))
        with self.assertRaisesRegex(ValueError, '선택하지 못한 양수 패턴'):
            resolve_filter(manifest, pattern, 'Linux', 'all', 'small')
        with self.assertRaisesRegex(ValueError, 'Fixture.New'):
            resolve_filter(manifest, 'Fixture.New', 'Darwin', 'background', 'small')

    def test_tier_resolution_keeps_negative_patterns_with_unknown_parameter_suffixes(self):
        """suffix를 열거할 수 없어도 각 tier는 제외한 인스턴스를 실행하지 않는다."""
        pattern = 'All/F.C/*-All/F.C/Skip'
        for tier, case_tier in (('background', 'B'), ('foreground', 'C')):
            with self.subTest(tier=tier):
                manifest = {'area/test.cc::F.C': {
                    'os': ['Darwin'], 'tier': case_tier, 'binaries': ['small'],
                    'parameters': [['All', None]],
                }}
                resolved, tiers = resolve_filter(manifest, pattern, 'Darwin', tier, 'small')
                self.assertEqual((resolved, tiers), (pattern, case_tier))
                arguments = ['--gtest_filter=' + resolved]
                self.assertTrue(selected('All/F.C/Keep', False, arguments))
                self.assertFalse(selected('All/F.C/Skip', False, arguments))

    def test_a_only_runs_preserve_desktop(self):
        if self.runs is None:
            self.skipTest('gate 평가에 zsh가 필요하다')
        checked = 0
        for run in self.runs:
            platform = 'Windows' if run.platform.startswith('MSYS') else run.platform
            tiers = {entry['tier'] for key, entry in self.manifest.items() if platform in entry['os']
                     and covered_by_gate(destination_path(key.split('::')[0], self.catalog),
                                         key.split('::')[1], entry.get('parameters', False), self.targets, [run])}
            if tiers == {'A'}:
                checked += 1
                self.assertFalse(run.display or run.shutdown, (run.script, run.mode, run.tier))
        self.assertGreater(checked, 0)

    def test_aura_guard_is_built_and_run_only_on_windows(self):
        if self.runs is None:
            self.skipTest('gate 평가에 zsh가 필요하다')
        target = 'chrome/browser/ui/views/yee:yee_aura_unittests'
        for run in self.runs:
            script, platform, binary, built = run.script, run.platform, run.binary, run.built
            if script != 'test-browser-surface-layout.sh':
                continue
            if platform in ('Darwin', 'Linux'):
                self.assertNotIn(target, built)
                self.assertNotEqual(binary, 'yee_aura_unittests')
        self.assertTrue(any(run.platform.startswith('MSYS') and run.binary == 'yee_aura_unittests'
                            and target in run.built for run in self.runs))

    def test_upstream_cases_have_a_gate(self):
        if self.chromium is None:
            reason = 'Chromium checkout 없음: YEE_LOCAL_BUILD_ROOT 또는 .local-build/*/chromium/src가 필요하다. upstream 사례·GN 검사를 건너뛴다.'
            print('SKIP: ' + reason, file=sys.stderr)
            self.skipTest(reason)
        self.assert_covered(upstream_inventory(self.chromium, self.patch), overlay=False)

    def test_patch_ranges_select_case_without_function_context(self):
        """짧은 hunk의 본문 수정·삭제는 고르고 fixture·포맷 변경은 제외한다."""
        old = ('void Helper() {\n  Before();\n}\n'
               'TEST_F(Fixture, Changed) {\n  Before();\n}\n'
               'TEST_P(Fixture, Formatting) {\n  Before();\n}\n')
        new = old.replace('Before();', 'After();', 2).replace('  Before();', '    Before();')
        patch = ('diff --git a/sample_unittest.cc b/sample_unittest.cc\n' + ''.join(
            difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                 fromfile='a/sample_unittest.cc', tofile='b/sample_unittest.cc', n=0)))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'sample_unittest.cc'
            source.write_text(new)
            self.assertEqual(upstream_inventory(root, patch),
                             {('sample_unittest.cc', 'Fixture.Changed'): False})
            source.write_text(old)
            with self.assertRaisesRegex(ValueError, 'checkout 줄이 다르다'):
                upstream_inventory(root, patch)
        literal = 'TEST_P(Fixture, Raw) { auto s = R"js( } " )js"; }'
        self.assertEqual(case_ranges('// TEST_F(Fake, Case) {}\n' + literal),
                         {'Fixture.Raw': (2, 2, True)})
        self.assertNotEqual(code_without_spacing('"a b"'), code_without_spacing('"a  b"'))
        with self.assertRaises(ValueError):
            case_ranges('TEST_F(Fixture, Cut) { EXPECT_TRUE(true);')

    def test_checkout_discovery_and_missing_checkout_skip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertIsNone(chromium_checkout(root, {}))
            src = root / '.local-build/stable/chromium/src'
            src.mkdir(parents=True)
            (src / 'BUILD.gn').touch()
            self.assertEqual(chromium_checkout(root, {}), src)
            self.assertEqual(chromium_checkout(root, {'YEE_LOCAL_BUILD_ROOT': str(src.parents[1])}), src)
            self.assertIsNone(chromium_checkout(root, {'YEE_LOCAL_BUILD_ROOT': str(root / 'missing')}))
        case = type(self)('test_upstream_cases_have_a_gate')
        case.chromium = None
        result = unittest.TestResult()
        with contextlib.redirect_stderr(io.StringIO()) as stderr:
            case.run(result)
        self.assertEqual(len(result.skipped), 1)
        self.assertIn('Chromium checkout 없음', stderr.getvalue())
        # native GN 연결을 읽지 못해도 overlay 사례 검사는 계속한다.
        targets, _ = gn_targets(ROOT, self.patch)
        with mock.patch.object(self, 'targets', targets):
            self.assert_covered(overlay_inventory(ROOT), overlay=True)

    def test_commented_gn_source_or_dependency_is_not_compiled(self):
        """GN 주석 안의 소스와 의존성은 타깃 연결로 세지 않는다."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'build').mkdir()
            (root / 'area').mkdir()
            (root / 'build/overlay.json').write_text(json.dumps({'source_roots': [
                {'source': 'area', 'destination': 'area'}]}))
            (root / 'area/BUILD.gn').write_text('''
test("small") {
  # sources = [ "commented.cc" ]
  sources = [
    "real.cc"
    # "commented_entry.cc"
  ]
  # deps = [ ":commented" ]
  deps = [ ":cases" ]
}
source_set("cases") { sources = [ "dependency.cc" ] }
''')
            targets, _ = gn_targets(root, '')
            self.assertEqual(target_sources(targets, 'area:small'),
                             {'area/real.cc', 'area/dependency.cc'})

    def test_gate_requires_the_selected_binary_and_source(self):
        """필터가 맞아도 소스·의존·실행 바이너리가 다르면 실패한다."""
        targets = {'area:small': ['area:cases'], 'area:cases': ['area/test.cc']}
        runs = [GateRun('test-area.sh', 'Darwin', 'small', ['--gtest_filter=Fixture.*'], {'area:small'})]
        self.assertTrue(covered_by_gate('area/test.cc', 'Fixture.Case', False, targets, runs))
        self.assertFalse(covered_by_gate('area/other.cc', 'Fixture.Case', False, targets, runs))
        self.assertFalse(covered_by_gate('area/test.cc', 'Other.Case', False, targets, runs))
        self.assertFalse(covered_by_gate('area/test.cc', 'Fixture.Case', False,
                                        {'area:small': []}, runs))
        self.assertFalse(covered_by_gate('area/test.cc', 'Fixture.Case', False, targets,
                                        [GateRun('test-area.sh', 'Darwin', 'large', [], {'area:small'})]))

    def test_gtest_negative_filter_and_parameterized_selection(self):
        instances = instantiations('INSTANTIATE_TEST_SUITE_P(All, Fixture, testing::Bool());')['Fixture']
        self.assertTrue(selected('Fixture.Case', instances, ['--gtest_filter=All/Fixture.Case/*']))
        self.assertFalse(selected('Fixture.Case', instances, ['--gtest_filter=All/Fixture.*/*-*/Fixture.Case/*']))
        self.assertFalse(selected('Fixture.Case', False, ['--gtest_filter=Other.*']))
        self.assertTrue(selected('Fixture.Case', False, ['--gtest_filter=-Other.*']))
        self.assertFalse(selected('Fixture.Case', instances, ['--gtest_filter=-*/Fixture.Case/*']))
        self.assertFalse(selected('Fixture.Case', instances, ['--gtest_filter=*-*/Fixture.Case/*']))
        self.assertFalse(selected('Fixture.Case', instances, ['--gtest_filter=Fixture.Case']))
        self.assertFalse(selected('Fixture.DISABLED_Case', False, ['--gtest_filter=*']))
        self.assertTrue(selected('Fixture.DISABLED_Case', False,
                                 ['--gtest_filter=*', '--gtest_also_run_disabled_tests']))

    def test_parameterized_filter_cannot_invent_prefix_or_suffix(self):
        """실제 인스턴스화에 없는 prefix·suffix로는 gate 연결을 만족하지 못한다."""
        text = '''
// INSTANTIATE_TEST_SUITE_P(Typo, TabSearchPageHandlerTest, testing::Bool());
INSTANTIATE_TEST_SUITE_P(All, TabSearchPageHandlerTest, testing::Bool());
INSTANTIATE_TEST_SUITE_P(Orientations, NamedTest,
    testing::Values(A, B), [](const auto& info) {
      if (info.param == A) return "Vertical";
      return "Horizontal";
    });
'''
        targets = {'area:small': ['area/test.cc']}
        for suite, prefix, suffix, expected in (
                ('TabSearchPageHandlerTest', 'All', '*', True),
                ('TabSearchPageHandlerTest', 'All', '1', True),
                ('TabSearchPageHandlerTest', 'Typo', '*', False),
                ('TabSearchPageHandlerTest', 'All', 'Typo', False),
                ('TabSearchPageHandlerTest', 'All', '2', False),
                ('NamedTest', 'Orientations', 'Vertical', True),
                ('NamedTest', 'Orientations', 'Typo', False)):
            with self.subTest(suite=suite, prefix=prefix, suffix=suffix):
                name = suite + '.RemoveSplit_NTP'
                runs = [GateRun('test-area.sh', 'Darwin', 'small',
                         [f'--gtest_filter={prefix}/{name}/{suffix}'], {'area:small'})]
                self.assertEqual(covered_by_gate('area/test.cc', name, instantiations(text)[suite],
                                                 targets, runs), expected)


if __name__ == '__main__':
    unittest.main()
