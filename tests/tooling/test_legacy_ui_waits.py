"""Yee UI 테스트의 polling·idle·고정 지연 대기 회귀를 차단한다."""

from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest

from test_gate_coverage import CPP_LITERAL, case_ranges, strip_gn_comments

ROOT = Path(__file__).resolve().parents[2]
ALLOWLIST = ROOT / 'tests/tooling/legacy_ui_wait_allowlist.json'
CLASSIFICATIONS = {'mock_clock', 'callback', 'non_ui', 'deadline'}
NATIVE_WAITS = re.compile(
    r'(?P<poll>\bbase\s*::\s*test\s*::\s*RunUntil\s*\()|'
    r'(?P<idle>\bRunUntilIdle\s*\()|'
    r'(?P<sleep>\bPlatformThread\s*::\s*Sleep\s*\()|'
    r'(?P<delay>\b(?:PostDelayedTask|PostCancelableDelayedTask)\s*\()|'
    r'(?P<fixed_wait>\b(?:WaitForTimeout|sleep_for|sleep_until|WaitForTimeDelta|TimedWait)\s*\()|'
    r'(?P<view_poll>\b(?:PollView(?:Property(?:Impl)?)?|PollState(?:Until)?|'
    r'PollUntil|PollElement|WaitForActiveTabChange|WaitForElementMatching(?:Impl)?)'
    r'\s*(?:<[^;{}()]*>)?\s*\()|'
    r'(?P<state_wait>\bWaitForState\s*(?:<[^;{}()]*>)?\s*\()')
TIMER_DECLARATION = re.compile(
    r'\b(?:OneShotTimer|RepeatingTimer|RetainingOneShotTimer|DelayTimer)'
    r'\s*>?\s+([a-zA-Z_]\w*)')


@dataclass(frozen=True)
class WaitSite:
    file: str
    line: int
    scope: str
    kind: str
    ordinal: int
    fingerprint: str

    @property
    def key(self):
        return self.file, self.scope, self.kind, self.ordinal

    def record(self, classification, reason):
        return dict(file=self.file, scope=self.scope, kind=self.kind,
                    ordinal=self.ordinal, fingerprint=self.fingerprint,
                    classification=classification, reason=reason)


def invocation(text, start, code):
    """주석·문자열을 가린 native 괄호 또는 JS callback의 호출 범위를 읽는다."""
    opening = code.index('(', start)
    depth = 1
    for index in range(opening + 1, len(code)):
        depth += (code[index] == '(') - (code[index] == ')')
        if not depth:
            return text[start:index + 1]
    raise ValueError(f'닫히지 않은 wait 호출: {text[start:start + 60]}')


def scan_text(file, text, timer_names=()):
    code = CPP_LITERAL.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), text)
    matches = [(m.start(), m.lastgroup, code) for m in NATIVE_WAITS.finditer(code)]
    timers = set(timer_names) | set(TIMER_DECLARATION.findall(code))
    for match in re.finditer(
            r'\b(\w+)\s*(?:\.|->)\s*(?:Start|Reset|Again)\s*\(', code):
        if match.group(1) in timers or re.search('timer|deadline', match.group(1), re.I):
            matches.append((match.start(), 'timer', code))
    # JS 대기는 raw/일반 C++ script 문자열 안에서도 검사한다.
    for literal in CPP_LITERAL.finditer(text):
        if literal.group().startswith(('//', '/*')):
            continue
        for match in re.finditer(
                r'\b(setTimeout|setInterval|requestAnimationFrame|'
                r'waitForAnimationFrames?|waitForFrames|waitForNextFrame)\s*\(|'
                r'\bnew\s+Promise\s*\(\s*requestAnimationFrame\s*\)',
                literal.group()):
            kind = {'setTimeout': 'js_delay', 'setInterval': 'js_interval'}.get(
                match.group(1), 'js_frame')
            matches.append((literal.start() + match.start(), kind, text))
    scopes = case_ranges(text)
    counts = Counter()
    sites = []
    for start, kind, masked in sorted(matches):
        line = text.count('\n', 0, start) + 1
        scope = next((name for name, (first, last, _) in scopes.items()
                      if first <= line <= last), '@helpers')
        counts[scope, kind] += 1
        call = invocation(text, start, masked)
        if not kind.startswith('js_'):
            statement = max(masked.rfind(';', 0, start), masked.rfind('{', 0, start), masked.rfind('}', 0, start)) + 1
            call = text[statement:start] + call
        fingerprint = hashlib.sha256(' '.join(call.split()).encode()).hexdigest()
        sites.append(WaitSite(file, line, scope, kind, counts[scope, kind], fingerprint))
    return sites


def testonly_sources(root):
    """GN testonly target과 test 템플릿의 sources/public 헤더를 읽는다."""
    sources = set()
    for gn in (root / 'browser/ui').rglob('BUILD.gn'):
        text = strip_gn_comments(gn.read_text())
        for target in re.finditer(
                r'\b(test|source_set|static_library|executable)\("\w+"\)\s*\{', text):
            start, end, depth = target.end(), target.end(), 1
            while end < len(text) and depth:
                depth += (text[end] == '{') - (text[end] == '}')
                end += 1
            body = text[start:end - 1]
            if target.group(1) != 'test' and not re.search(r'\btestonly\s*=\s*true\b', body):
                continue
            for assignment in re.finditer(r'\b(?:sources|public)\s*\+?=\s*\[([^\]]*)\]', body, re.S):
                for source in re.findall(r'"([^"$]+\.(?:cc|h|mm))"', assignment.group(1)):
                    # //로 시작하는 GN 경로는 Chromium 소스 루트 기준이다.
                    if source.startswith('//'):
                        continue
                    path = (gn.parent / source).resolve()
                    if path.is_relative_to(root.resolve()) and path.is_file():
                        sources.add(path)
    return sources


def scan_tree(root):
    sites = []
    sources = testonly_sources(root)
    sources.update(path for path in (root / 'browser/ui').rglob('*')
                   if path.suffix in {'.cc', '.h', '.mm'} and 'test' in path.stem)
    texts = {path: path.read_text() for path in sorted(sources)}
    # 헤더에 선언하고 .cc에서 시작하는 timer도 같은 검사 범위에 포함한다.
    timers = {name for text in texts.values()
              for name in TIMER_DECLARATION.findall(CPP_LITERAL.sub(' ', text))}
    for path, text in texts.items():
        sites.extend(scan_text(path.relative_to(root).as_posix(), text, timers))
    return sites


def violations(sites, records):
    failures = []
    allowed = {}
    for record in records:
        try:
            key = (record['file'], record['scope'], record['kind'], record['ordinal'])
            if key in allowed:
                raise ValueError(f'중복 wait 예외: {key}')
            if record['classification'] not in CLASSIFICATIONS or not record['reason'].strip():
                raise ValueError(f'분류/사유가 없는 wait 예외: {key}')
            if not re.fullmatch('[0-9a-f]{64}', record['fingerprint']):
                raise ValueError(f'호출 지문이 없는 wait 예외: {key}')
            allowed[key] = record
        except (KeyError, ValueError) as error:
            failures.append(str(error))
    observed = {site.key: site for site in sites}
    for key, site in observed.items():
        record = allowed.get(key)
        if not record or record['fingerprint'] != site.fingerprint:
            failures.append(f'{site.file}:{site.line}: 미분류/변경된 {site.kind} ({site.scope})')
    for key in allowed.keys() - observed.keys():
        failures.append(f'삭제/이전된 wait 예외를 정리해야 함: {key}')
    return failures


class LegacyUiWaitGuardTest(unittest.TestCase):
    def test_all_owned_ui_waits_are_classified(self):
        data = json.loads(ALLOWLIST.read_text())
        self.assertEqual(1, data['schema_version'])
        self.assertEqual([], violations(scan_tree(ROOT), data['sites']))

    def test_rejects_new_poll_idle_sleep_and_delays(self):
        source = '''TEST(Fixture, Waits) {
          base::test::RunUntil([&] { return view->visible(); });
          base::RunLoop().RunUntilIdle();
          base::PlatformThread::Sleep(base::Milliseconds(16));
          runner->PostDelayedTask(FROM_HERE, done, delay);
          PollView(id, element, callback);
          auto js = R"JS(setTimeout(done, 64))JS";
        }'''
        sites = scan_text('browser/ui/sample_unittest.cc', source)
        self.assertEqual({'poll', 'idle', 'sleep', 'delay', 'view_poll', 'js_delay'},
                         {site.kind for site in sites})
        self.assertEqual(6, len(violations(sites, [])))

    def test_exact_site_reason_and_stale_entries_are_checked(self):
        source = 'TEST(Fixture, Signals) { tasks_.RunUntilIdle(); }'
        site, = scan_text('browser/ui/sample_unittest.cc', source)
        record = site.record('mock_clock', 'fake source 평가를 mock TaskEnvironment에서 전달한다.')
        self.assertEqual([], violations([site], [record]))
        self.assertTrue(violations([], [record]))
        self.assertTrue(violations([site], [dict(record, reason='')]))
        changed, = scan_text(site.file, source.replace('tasks_', 'ui_loop_'))
        self.assertNotEqual(site.fingerprint, changed.fingerprint)
        self.assertTrue(violations([changed], [record]))
        self.assertTrue(violations([site], [dict(record, fingerprint='0' * 64)]))

    def test_comments_and_non_script_mentions_are_not_native_calls(self):
        sites = scan_text('browser/ui/sample_unittest.cc', '''
          // base::test::RunUntil(done);
          const char* text = "RunUntilIdle()";
          /* PlatformThread::Sleep(delay); */
        ''')
        self.assertEqual([], sites)

    def test_chromium_polling_mixins_and_state_waits_need_site_reasons(self):
        helpers = ('WaitForActiveTabChange', 'PollState', 'PollStateUntil',
                   'PollUntil', 'PollView', 'PollViewProperty',
                   'PollViewPropertyImpl', 'PollElement',
                   'WaitForElementMatching', 'WaitForElementMatchingImpl',
                   'WaitForState')
        for helper in helpers:
            with self.subTest(helper=helper):
                source = f'void Helper() {{ T::{helper}(id, expected); }}'
                site, = scan_text('browser/ui/adapter.h', source)
                self.assertEqual(1, len(violations([site], [])))
        site, = scan_text('browser/ui/adapter.h',
                          'void Helper() { T::PollView<bool>(id, view, cb); }')
        self.assertEqual('view_poll', site.kind)
        site, = scan_text('browser/ui/adapter.h',
                          'void Helper() { T::WaitForState<Observer>(id, value); }')
        self.assertEqual('state_wait', site.kind)
        self.assertEqual(1, len(violations([site], [])))

    def test_fixed_frame_javascript_waits_are_rejected(self):
        scripts = (
            'R"JS(new Promise(resolve => requestAnimationFrame(() => '
            'requestAnimationFrame(() => resolve(true)))))JS"',
            '"new Promise(resolve => requestAnimationFrame(() => "\n'
            '"requestAnimationFrame(() => resolve(true))))"',
            'R"JS(new Promise(requestAnimationFrame))JS"',
            'R"JS(for (let i = 0; i < 2; ++i) '
            'await new Promise(resolve => requestAnimationFrame(resolve)))JS"',
            '"waitForAnimationFrames(2)"',
            '"waitForFrames(2)"',
        )
        for script in scripts:
            with self.subTest(script=script):
                sites = scan_text('browser/ui/sample_uitest.cc',
                                  f'void Helper() {{ auto js = {script}; }}')
                self.assertTrue(sites)
                self.assertEqual({'js_frame'}, {site.kind for site in sites})
                self.assertEqual(len(sites), len(violations(sites, [])))

    def test_scans_nested_owned_test_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / 'browser/ui/projects'
            folder.mkdir(parents=True)
            (folder / 'sample_browsertest.cc').write_text('void Helper() { task.RunUntilIdle(); }')
            (folder / 'product.cc').write_text('task.RunUntilIdle();')
            site, = scan_tree(root)
            self.assertEqual('browser/ui/projects/sample_browsertest.cc', site.file)

    def test_scans_gn_testonly_adapters_and_public_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / 'browser/ui'
            folder.mkdir(parents=True)
            (folder / 'BUILD.gn').write_text('''
                source_set("adapter") {
                  testonly = true
                  public = [ "applied_wait_kombucha.h" ]
                  sources = [ "applied_wait_browser_observers.cc", "//ui/views/run_all_unittests_main.cc" ]
                  if (is_mac) { sources += [ "observer_mac.mm" ] }
                }
                source_set("product") { sources = [ "product.cc" ] }
            ''')
            for name in ('applied_wait_kombucha.h', 'applied_wait_browser_observers.cc',
                         'observer_mac.mm', 'product.cc'):
                (folder / name).write_text('void Helper() { base::test::RunUntil(done); }')
            self.assertEqual(
                {'browser/ui/applied_wait_kombucha.h',
                 'browser/ui/applied_wait_browser_observers.cc', 'browser/ui/observer_mac.mm'},
                {site.file for site in scan_tree(root)})

    def test_real_gn_includes_every_applied_wait_support_source(self):
        sources = {path.name for path in testonly_sources(ROOT)}
        self.assertTrue({
            'applied_wait_test_support.cc', 'applied_wait_test_support.h',
            'applied_wait_browser_observers.cc', 'applied_wait_browser_observers.h',
            'applied_wait_views_observers.cc', 'applied_wait_views_observers.h',
            'applied_wait_kombucha.cc', 'applied_wait_kombucha.h',
            'applied_wait_test_util.h',
        }.issubset(sources))

    def test_timer_and_javascript_polling_cannot_hide_in_support(self):
        sites = scan_text('browser/ui/applied_wait_views_observers.cc', '''
            base::RepeatingTimer retry;
            retry.Start(FROM_HERE, delay, check);
            timeout_timer_->Start(FROM_HERE, delay, check);
            auto js = R"JS(setInterval(() => check(), 10);
                          setTimeout(() => check(), 10);)JS";
        ''')
        self.assertEqual(['timer', 'timer', 'js_interval', 'js_delay'],
                         [site.kind for site in sites])
        self.assertEqual(4, len(violations(sites, [])))

    def test_internal_page_helper_is_scanned_and_polling_is_rejected(self):
        file = 'browser/ui/internal_page_test_support.h'
        source = (ROOT / file).read_text()
        sites = [site for site in scan_tree(ROOT) if site.file == file]
        self.assertEqual(['js_delay'], [site.kind for site in sites])
        allowlist = json.loads(ALLOWLIST.read_text())['sites']
        self.assertEqual([], violations(scan_tree(ROOT), allowlist))
        for polling in ('setInterval(inspect, 10)', 'setTimeout(inspect, 10)'):
            with self.subTest(polling=polling):
                changed = source.replace('inspect();', polling + ';', 1)
                self.assertTrue(violations(scan_text(file, changed),
                                           [site for site in allowlist if site['file'] == file]))

    def test_timer_declared_in_support_header_is_detected_in_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / 'browser/ui'
            folder.mkdir(parents=True)
            (folder / 'BUILD.gn').write_text('''
                source_set("adapter") {
                  testonly = true
                  public = [ "observer.h" ]
                  sources = [ "observer.cc" ]
                }
            ''')
            (folder / 'observer.h').write_text('base::RepeatingTimer completion_;')
            (folder / 'observer.cc').write_text('completion_.Start(FROM_HERE, delay, check);')
            site, = scan_tree(root)
            self.assertEqual('timer', site.kind)
            self.assertEqual('browser/ui/observer.cc', site.file)

    def test_polling_mixin_calls_in_gn_testonly_support_are_scanned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root / 'browser/ui'
            folder.mkdir(parents=True)
            (folder / 'BUILD.gn').write_text('''
                source_set("adapter") {
                  testonly = true
                  sources = [ "adapter.cc" ]
                  public = [ "adapter.h" ]
                }
            ''')
            (folder / 'adapter.cc').write_text(
                'void Helper() { WaitForActiveTabChange(1); }')
            (folder / 'adapter.h').write_text(
                'void Helper() { T::WaitForState(id, expected); }')
            sites = scan_tree(root)
            self.assertEqual({'view_poll', 'state_wait'},
                             {site.kind for site in sites})
            self.assertEqual(2, len(violations(sites, [])))


if __name__ == '__main__':
    unittest.main()
