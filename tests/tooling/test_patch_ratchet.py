#!/usr/bin/env python3
"""패치 래칫의 지표 측정, 기준선 판정과 기록 절차를 임시 파일로 검사한다."""

from collections import Counter
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/overlay'))
from lib.overlay_tools import load_catalog
from lib.patch_ratchet import (FILE_KINDS, METRICS, Measurement, evaluate, file_kind,
                               is_yee_member, load_concerns, load_ratchet, measure, run,
                               statements, validate_concerns, validate_ratchet)


def section(path, body, created=False):
    """접두어가 붙은 본문 줄로 검증 가능한 Git diff 섹션 하나를 만든다."""
    old = sum(line[0] in ' -' for line in body)
    new = sum(line[0] in ' +' for line in body)
    if created:
        header = f'new file mode 100644\n--- /dev/null\n+++ b/{path}\n@@ -0,0 +1,{new} @@\n'
    else:
        header = f'--- a/{path}\n+++ b/{path}\n@@ -1,{old} +1,{new} @@\n'
    return f'diff --git a/{path} b/{path}\n' + header + ''.join(line + '\n' for line in body)


def measurement(**counts):
    """지표별 경로 개수만 지정한 측정값을 만든다."""
    return Measurement({metric: Counter(counts.get(metric, {})) for metric in METRICS},
                       {kind: Counter() for kind in FILE_KINDS})


def config(baseline=None, exceptions=()):
    filled = {metric: {} for metric in METRICS}
    filled.update(baseline or {})
    return {'baseline': filled, 'exceptions': list(exceptions)}


def exception(path, allowed, metric='shell_branches', reason='검토한 연결'):
    return {'metric': metric, 'path': path, 'allowed': allowed, 'reason': reason}


class PatchRatchetTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='yee-patch-ratchet-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.catalog = copy.deepcopy(load_catalog())
        for patch in self.catalog['patches']:
            patch['file'] = f'patches/{patch["role"]}.patch'
        (self.root / 'build').mkdir()
        (self.root / 'patches').mkdir()
        (self.root / 'build/overlay.json').write_text(json.dumps(self.catalog))
        self.output = []

    def write_shell_patch(self, *sections):
        (self.root / 'patches/shell.patch').write_text(''.join(sections))

    def write_concerns(self, paths):
        data = {'schema_version': 1,
                'concerns': {'shell': '셸 연결', 'content_blocking': '콘텐츠 차단 연결'},
                'paths': paths}
        path = self.root / 'build/patch-concerns.json'
        path.write_text(json.dumps(data))
        return path

    def run_ratchet(self, mode='check', **options):
        return run(self.root, mode=mode, out=self.output.append, **options)

    def run_cli(self, *args, stdin=''):
        spec = importlib.util.spec_from_file_location(
            'check_patch_ratchet', ROOT / 'tools/overlay/check-patch-ratchet.py')
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        cli.REPO_ROOT = self.root
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', ['check-patch-ratchet.py', *args]), \
                mock.patch.object(sys, 'stdin', io.StringIO(stdin)), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = cli.main()
        return code, stdout.getvalue(), stderr.getvalue()

    def ratchet(self):
        return load_ratchet(self.root / 'build/patch-ratchet.json')

    def test_file_kind_separates_new_test_and_product_files(self):
        """패치가 만든 파일, 테스트 이름·디렉터리와 제품 파일을 구분한다."""
        cases = {
            ('chrome/browser/ui/views/frame/browser_view.cc', False): 'prod',
            ('chrome/browser/ui/views/frame/layout/layout_interactive_uitest.cc', False): 'test',
            ('chrome/browser/ui/views/frame/immersive_mac_interactive_uitest.mm', False): 'test',
            ('chrome/browser/mac/install_test.cc', False): 'test',
            ('chrome/test/BUILD.gn', False): 'test',
            ('ui/base/test/ui_controls_internal_win.cc', False): 'test',
            ('chrome/browser/attestation/attestation_service.cc', False): 'prod',
            ('chrome/browser/ui/views/frame/layout/geometry_unittest.cc', True): 'new',
        }
        for (path, created), kind in cases.items():
            with self.subTest(path=path):
                self.assertEqual(file_kind(path, created), kind)

    def test_member_detection_accepts_declarations_and_rejects_statements(self):
        """여러 줄로 나뉜 선언까지 Yee 멤버로 세고 반환·대입·호출·별칭·상수는 제외한다."""
        def members(*lines):
            return sum(is_yee_member(statement) for statement in statements(lines))

        accepted = [
            ('  bool yee_header_update_pending_ = false;',),
            ('  std::unique_ptr<yee::PageViewportGeometry> page_viewport_geometry_;',),
            ('  raw_ptr<views::View> yee_resting_text_ = nullptr;  // 설명',),
            ('  yee::FavoritesDragIntent favorite_drop_{};',),
            ('  ContentSettingsForOneType yee_content_blocking_rules;',),
            ('  bool uses_yee_browser_surface_geometry = false;',),
            ('  std::unique_ptr<yee::projects::ProjectNavigationController>',
             '      yee_project_navigation_controller_;'),
            ('  std::array<raw_ptr<yee::BrowserSurfacePresentationBinding>, 2>',
             '      caption_presentation_bindings_ = {};'),
            ('  raw_ptr<yee::BrowserSurfacePresentationBinding> yee_presentation_binding_ =',
             '      nullptr;'),
            ('  yee::SurfaceGeometryTransitionPhase surface_transition_phase =',
             '      yee::SurfaceGeometryTransitionPhase::kStable;'),
            ('  base::RepeatingCallback<void()> yee_callback_;',),
            ('  base::OnceCallback<void(yee::Presentation)> on_presented_;',),
            ('  raw_ptr<yee::WindowState> state_ GUARDED_BY(lock_);',),
            ('  bool yee_pending_ GUARDED_BY_CONTEXT(sequence_checker_) = false;',),
            ('  [[no_unique_address]] yee::Marker marker_;',),
            ('  RAW_PTR_EXCLUSION yee::Binding* binding_ = nullptr;',),
            ('  std::array<raw_ptr<yee::Binding>, 2> bindings_[2];',),
            ('  int yee_flags_ : 4;',),
            # 초기화 문자열·문자와 인라인 주석 속 키워드·괄호는 선언 구조로 보지 않는다.
            ('  std::string yee_status_ = "requires authentication";',),
            ('  std::string yee_mode_ = "using cache (operator)";',),
            ('  const char* yee_label_ = ":)";',),
            ("  char yee_separator_ = '(';",),
            ('  int yee_count_ = /* typedef */ 0;',),
            ("  std::array<int, 1'000> yee_values_ = {1'000};",),
            ("  double yee_scale_ = .000'001;",),
            ("  double yee_exponent_ = .000'001e2;",),
            ("  uint32_t yee_mask_ = 0xFF'FF;",),
            ('  const char* yee_text_ = R"(He said "hello)";',),
            ('  const char* yee_quote_ = u8R"tag(a ")" b)tag";',),
            ("  char yee_mark_ = /* don't translate */ GetChar('x');",),
            ('  const char* yee_path_ = "C:\\\\dir\\"(";',),
            ('  unsigned yee_flags_ : kNumFlags;',),
            ('  unsigned yee_bits_ : sizeof(Storage) * Limits::kBitsPerByte;',),
            ('  raw_ptr<yee::State> state_ GUARDED_BY(GetLock());',),
            ('  int* yee_ptr_ GUARDED_BY(lock_) PT_GUARDED_BY(lock_);',),
            ('  alignas(16) char yee_storage_[64];',),
            ('  base::ScopedObservation<yee::Model, yee::Observer> observation_{',
             '      this};'),
            # offer_notification_bubble_views_test_base.h의 실제 여러 줄 초기화 형태.
            ('  TestAutofillManagerWaiter yee_forms_seen_waiter_{',
             '      *this,',
             '      {AutofillManagerEvent::kFormsSeen}};'),
            ('  METADATA_HEADER(YeeHostView, views::View)',
             '  raw_ptr<yee::Binding> binding_ = nullptr;'),
        ]
        rejected = [
            ('    return yee_sidebar_button_;',),
            ('  void SetPresentation(Value v) {', '    yee_presentation_ = v;', '  }'),
            ('    yee_presentation_ = presentation;',),
            ('  bool enabled_ = false;',),
            ('  // bool yee_note_;',),
            ('    Update(yee_state_);',),
            ('    this->yee_pending_ = true;',),
            ('class YeePresentationOutlet;',),
            ('  using Presentation = yee::BrowserSurfacePresentation;',),
            ('  static constexpr int kYeeRowHeight = 32;',),
            ('    bool operator==(const YeeHeaderLayoutContext&) const = default;',),
            ('  void SetBinding(yee::BrowserSurfacePresentationBinding* binding,',
             '                  bool animate);'),
            ('  void Apply(base::OnceCallback<void(yee::Result)> done) override;',),
            ('  std::string label_ = "Yee";',),
            ('  int width_ = yee::kSidebarMetrics.width;',),
            ('  void Update(yee::State state) EXCLUSIVE_LOCKS_REQUIRED(lock_);',),
            ('  void Apply() override EXCLUSIVE_LOCKS_REQUIRED(yee_lock_);',),
            ('class YeeHost {', '  bool enabled_ = false;', '};'),
            ('  void Reset() {', '    yee_state_ = {};', '  }'),
            ('  std::string label_ = condition ? kYeeLabel : kOther;',),
            # webui_bubble_manager.h의 실제 여러 줄 함수 선언 형태.
            ('  template <typename Controller>',
             '  static std::unique_ptr<WebUIBubbleManager> Create(',
             '      BrowserWindowInterface* browser_window_interface,',
             '      const GURL& webui_url,',
             '      int task_manager_string_id,',
             '      bool yee_force_load_on_create = false);'),
            ('  /* yee::Binding* binding_;', '   * bool yee_ready_ = false;', '   */'),
            # 같은 함수 선언에서 마지막 매개변수 줄만 수정한 경우.
            ('      bool yee_force_load_on_create = false);',),
            ('      yee::Binding* binding,', '      bool yee_animate = true);'),
            ('  YeeHost& operator=(const YeeHost&) = delete;',),
            ('  YeeHost& operator=(YeeHost&&);',),
            ('      yee::Binding* binding) const override;',),
            ('      const override;',),
            ('  static const int kYeeIconSize = 16;',),
            ('  inline const std::string_view kYeeFlow = "flow";',),
            ('extern const char kYeeSwitch[];',),
            ('    auto* yee_view = GetYeeView();',),
            ('    const auto& yee_state = state();',),
            # browser_elements_views.h의 제약 별칭에서 template 줄 아래만 수정한 경우.
            ('    requires std::derived_from<T, yee::SiteControlsButton>',
             '  using RetrievalCallback = base::RepeatingCallback<T*(yee::State)>;'),
        ]
        for lines in accepted:
            with self.subTest(lines=lines):
                self.assertEqual(members(*lines), 1)
        for lines in rejected:
            with self.subTest(lines=lines):
                self.assertEqual(members(*lines), 0)
        # 주석은 선언을 끊으므로 앞 조각이 다음 선언에 붙지 않는다.
        self.assertEqual(members('  raw_ptr<yee::Binding>', '  // 설명', '  bool ready_ = false;'), 0)

    def test_measure_counts_modified_chromium_files_and_reports_new_files(self):
        """수정한 원본 파일의 지표만 세고 새 파일과 수정 위치는 크기로 보고한다."""
        glue = next(path for path in self.catalog['new_glue'] if path.endswith('.h'))
        self.write_shell_patch(
            section('chrome/browser/ui/views/frame/browser_view.cc', [
                ' context',
                '-#include "chrome/browser/ui/views/frame/old.h"',
                '+#include "chrome/browser/ui/views/yee/yee_ui.h"',
                f'+#include "{glue}"',
                '+#include "chrome/browser/ui/views/frame/browser_view_layout.h"',
                ' context',
                '+  if (yee::IsShellEnabled()) GetViewByID(yee::kSurfaceViewId);',
                ' context',
            ]),
            section('chrome/browser/ui/views/frame/browser_view.h', [
                ' class BrowserView {',
                '+  bool yee_pending_ = false;',
                ' };',
            ]),
            section('chrome/browser/ui/views/frame/browser_view_unittest.cc', [
                ' context',
                '+TEST(YeeShell, Branch) {',
                '+  EXPECT_TRUE(yee::IsShellEnabled());',
                '+}',
            ]),
            section('chrome/browser/ui/views/frame/yee_glue.cc', [
                '+#include "chrome/browser/ui/views/yee/yee_ui.h"',
                '+bool Enabled() { return yee::IsShellEnabled(); }',
            ], created=True),
        )
        result = measure(self.root / 'patches/shell.patch', self.catalog)
        view = 'chrome/browser/ui/views/frame/browser_view.cc'
        test = 'chrome/browser/ui/views/frame/browser_view_unittest.cc'
        self.assertEqual({metric: dict(paths) for metric, paths in result.counts.items()}, {
            'upstream_test_lines': {test: 3},
            'shell_branches': {view: 1, test: 1},
            'view_id_lookups': {view: 1},
            'yee_includes': {view: 2},
            'yee_members': {'chrome/browser/ui/views/frame/browser_view.h': 1},
        })
        self.assertEqual(result.sizes['prod'],
                         Counter(files=2, edit_sites=3, added=5, removed=1, hunks=2))
        self.assertEqual(result.sizes['new'],
                         Counter(files=1, edit_sites=1, added=2, removed=0, hunks=1))

    def test_growth_fails_unless_reviewed_exception_allows_same_path(self):
        """기준선보다 늘면 실패하고, 같은 경로의 예외 허용량 안에서만 통과한다."""
        grown = measurement(shell_branches={'a.cc': 3})
        baseline = {'shell_branches': {'a.cc': 2}}
        self.assertEqual([(f.kind, f.path, f.current, f.limit)
                          for f in evaluate(grown, config(baseline))],
                         [('growth', 'a.cc', 3, 2)])
        self.assertEqual(evaluate(grown, config(baseline, [exception('a.cc', 3)])), [])
        moved = evaluate(grown, config(baseline, [exception('b.cc', 3)]))
        self.assertIn(('growth', 'a.cc'), [(f.kind, f.path) for f in moved])
        new_path = evaluate(measurement(view_id_lookups={'c.cc': 1}), config())
        self.assertEqual([(f.kind, f.metric, f.path) for f in new_path],
                         [('growth', 'view_id_lookups', 'c.cc')])

    def test_reduction_and_unused_exception_allowance_must_be_tightened(self):
        """줄어든 기준선과 쓰지 않거나 불필요한 예외 허용량을 위반으로 보고한다."""
        reduced = evaluate(measurement(shell_branches={'a.cc': 2}),
                           config({'shell_branches': {'a.cc': 3}}))
        self.assertEqual([(f.kind, f.current, f.limit) for f in reduced],
                         [('stale_baseline', 2, 3)])
        unused = evaluate(measurement(shell_branches={'a.cc': 4}),
                          config({'shell_branches': {'a.cc': 2}}, [exception('a.cc', 5)]))
        self.assertEqual([(f.kind, f.current, f.limit) for f in unused],
                         [('stale_exception', 4, 5)])
        needless = evaluate(measurement(shell_branches={'a.cc': 3}),
                            config({'shell_branches': {'a.cc': 3}}, [exception('a.cc', 2)]))
        self.assertEqual([f.kind for f in needless], ['stale_exception'])

    def test_run_records_once_lowers_only_and_returns_failure_code(self):
        """기준선은 한 번만 기록하고 --update는 줄어든 값만 반영하며 위반 시 1을 반환한다."""
        def shell_patch(count):
            body = [' context'] + ['+  if (yee::IsShellEnabled()) {}'] * count
            self.write_shell_patch(section('chrome/browser/a.cc', body))

        shell_patch(2)
        self.write_concerns({'chrome/browser/a.cc': ['shell']})
        with self.assertRaisesRegex(ValueError, '--init'):
            self.run_ratchet()
        self.assertEqual(self.run_ratchet(missing_ok=True), 0)
        self.assertFalse((self.root / 'build/patch-ratchet.json').exists())
        self.assertEqual(self.run_ratchet('init'), 0)
        self.assertEqual(self.ratchet()['roles']['shell']['baseline']['shell_branches'],
                         {'chrome/browser/a.cc': 2})
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.run_ratchet('init')

        shell_patch(1)
        self.assertEqual(self.run_ratchet(), 1)
        self.assertIn('[기준선 낮춤] shell_branches chrome/browser/a.cc', self.output[-1])
        self.assertEqual(self.run_ratchet('update'), 0)
        self.assertEqual(self.ratchet()['roles']['shell']['baseline']['shell_branches'],
                         {'chrome/browser/a.cc': 1})

        shell_patch(3)
        self.assertEqual(self.run_ratchet('update'), 1)
        self.assertIn('[증가] shell_branches chrome/browser/a.cc: 현재 3, 허용 1',
                      self.output[-1])
        data = self.ratchet()
        self.assertEqual(data['roles']['shell']['baseline']['shell_branches'],
                         {'chrome/browser/a.cc': 1})
        data['roles']['shell']['exceptions'].append(exception('chrome/browser/a.cc', 3))
        (self.root / 'build/patch-ratchet.json').write_text(json.dumps(data))
        self.assertEqual(self.run_ratchet(), 0)
        self.assertIn('결과: 통과', self.output[-1])

    def test_missing_baseline_does_not_skip_concern_validation(self):
        """기준선이 없어도 지도 파일과 모든 분류를 요구하고 지표 검사만 생략한다."""
        path = 'chrome/browser/a.cc'
        self.write_shell_patch(section(path, [' context', '+if (yee::IsShellEnabled()) {}']))
        with self.assertRaises(FileNotFoundError):
            self.run_ratchet(missing_ok=True)
        code, _, stderr = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn(repr(str(self.root / 'build/patch-concerns.json')), stderr)
        cases = [
            ({}, '미분류 경로: ' + path),
            ({path: ['shell'], 'stale.cc': ['shell']}, '패치에 없는 경로: stale.cc'),
            ({path: ['unknown']}, "정의되지 않은 관심사 'unknown': " + path),
            ({path: []}, '빈 관심사 목록 또는 잘못된 목록: ' + path),
        ]
        for paths, message in cases:
            self.write_concerns(paths)
            with self.subTest(paths=paths), self.assertRaisesRegex(ValueError, message):
                self.run_ratchet(missing_ok=True)
        self.write_concerns({path: ['shell']})
        self.assertEqual(self.run_ratchet(missing_ok=True), 0)
        self.assertIn('지표 검사를 건너뛴다', self.output[-1])
        self.assertFalse((self.root / 'build/patch-ratchet.json').exists())

    def test_concern_errors_fail_check_and_update_without_writing_baseline(self):
        """누락·남은 경로, 미정의 이름과 빈 목록은 설정 오류(2)로 끝나며 기준선을 보존한다."""
        path = 'chrome/browser/a.cc'
        self.write_shell_patch(section(path, [' context', '+added']))
        self.write_concerns({path: ['shell']})
        self.assertEqual(self.run_ratchet('init'), 0)
        ratchet = self.root / 'build/patch-ratchet.json'
        before = ratchet.read_bytes()
        cases = [
            ({}, '미분류 경로: ' + path),
            ({path: ['shell'], 'stale.cc': ['shell']}, '패치에 없는 경로: stale.cc'),
            ({path: ['unknown']}, "정의되지 않은 관심사 'unknown': " + path),
            ({path: []}, '빈 관심사 목록 또는 잘못된 목록: ' + path),
        ]
        for paths, message in cases:
            self.write_concerns(paths)
            for args in ((), ('--update',)):
                with self.subTest(paths=paths, args=args):
                    code, _, stderr = self.run_cli(*args)
                    self.assertEqual(code, 2)
                    self.assertIn('build/patch-concerns.json', stderr)
                    self.assertIn(message, stderr)
                    self.assertEqual(ratchet.read_bytes(), before)

    def test_init_requires_classification_but_other_roles_do_not(self):
        """shell 최초 기록도 분류를 요구하고, 다른 patch role은 기존대로 지도 없이 검사한다."""
        self.write_shell_patch(section('a.cc', [' context', '+added']))
        self.write_concerns({})
        code, _, stderr = self.run_cli('--init')
        self.assertEqual(code, 2)
        self.assertIn('미분류 경로: a.cc', stderr)
        self.assertFalse((self.root / 'build/patch-ratchet.json').exists())
        (self.root / 'build/patch-concerns.json').unlink()
        (self.root / 'patches/branding.patch').write_text(section('brand.cc', ['+brand'], created=True))
        self.assertEqual(self.run_ratchet('init', role='branding'), 0)
        self.assertEqual(self.run_ratchet(role='branding'), 0)

    def test_concern_summary_counts_mixed_paths_once_and_preserves_metrics(self):
        """혼합 목록의 순서와 무관하게 한 행에 합치고, 새 파일은 크기에만 포함한다."""
        paths = {'shell.cc': ['shell'], 'blocking_unittest.cc': ['content_blocking'],
                 'mixed.h': ['shell', 'content_blocking'],
                 'mixed.cc': ['content_blocking', 'shell'], 'new.h': ['content_blocking']}
        self.write_shell_patch(
            section('shell.cc', ['-old', '+plain']),
            section('blocking_unittest.cc', [' context', '+test']),
            section('mixed.h', [' context', '+#include "chrome/browser/ui/views/yee/yee_ui.h"',
                                '+  bool yee_pending_ = false;']),
            section('mixed.cc', [' context', '+if (yee::IsShellEnabled()) GetViewByID(1);']),
            section('new.h', ['+bool yee_new_ = yee::IsShellEnabled();'], created=True),
        )
        concerns = load_concerns(self.write_concerns(paths))
        result = measure(self.root / 'patches/shell.patch', self.catalog, concerns)
        self.assertEqual(set(result.concerns), {'shell', 'content_blocking', 'shell+content_blocking'})
        self.assertEqual(result.concerns['shell+content_blocking'],
                         Counter(files=2, added=3, removed=0, hunks=2, edit_sites=2,
                                 shell_branches=1, view_id_lookups=1, yee_includes=1, yee_members=1))
        self.assertEqual(result.concerns['content_blocking']['files'], 2)
        self.assertEqual(result.concerns['content_blocking']['added'], 2)
        for metric in METRICS:
            self.assertEqual(sum(total[metric] for total in result.concerns.values()),
                             sum(result.counts[metric].values()))
        for size in ('files', 'added', 'removed', 'hunks'):
            self.assertEqual(sum(total[size] for total in result.concerns.values()),
                             sum(total[size] for total in result.sizes.values()))
        self.assertEqual(self.run_ratchet('init'), 0)
        self.assertIn('shell+content_blocking: 파일 2 · +3/-0 · hunk 2', self.output[-1])
        self.assertIn('upstream_test_lines 0 · shell_branches 1 · view_id_lookups 1'
                      ' · yee_includes 1 · yee_members 1', self.output[-1])

    def test_classify_accepts_arguments_and_stdin_without_ratchet_or_patch(self):
        """인자와 stdin은 같은 분류·개수·unclassified를 출력하고 중복·빈 줄은 세지 않는다."""
        self.write_concerns({'s.cc': ['shell'], 'c.cc': ['content_blocking'],
                             'm.cc': ['content_blocking', 'shell']})
        paths = ['c.cc', 's.cc', 'm.cc', 'unknown.cc']
        expected = ('content_blocking\tc.cc\nshell\ts.cc\nshell+content_blocking\tm.cc\n'
                    'unclassified\tunknown.cc\n관심사별 파일 수 (합계 4)\n'
                    '  content_blocking: 1\n  shell: 1\n  shell+content_blocking: 1\n'
                    '  unclassified: 1\n')
        self.assertEqual(self.run_cli('--classify', *paths), (0, expected, ''))
        self.assertEqual(self.run_cli('--classify', stdin='\n'.join(paths + ['', 's.cc', ''])),
                         (0, expected, ''))
        self.assertEqual(self.run_cli('--classify'), (0, '관심사별 파일 수 (합계 0)\n', ''))

    def test_invalid_concern_schema_and_duplicate_keys_are_rejected(self):
        """잘못된 schema·목록·설명과 중복 JSON 경로를 거부한다."""
        data = json.loads(self.write_concerns({'a.cc': ['shell']}).read_text())
        cases = [
            {**data, 'schema_version': True},
            {**data, 'schema_version': 2},
            {**data, 'concerns': {'shell': '  '}},
            {**data, 'paths': {'a.cc': 'shell'}},
            {**data, 'paths': {'a.cc': [[]]}},
            {**data, 'paths': {'a.cc': ['shell', 'shell']}},
            {**data, 'paths': {'../a.cc': ['shell']}},
        ]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_concerns(value)
        path = self.root / 'build/patch-concerns.json'
        path.write_text('{"schema_version": 1, "concerns": {"shell": "셸"}, '
                        '"paths": {"a.cc": ["shell"], "a.cc": ["shell"]}}')
        with self.assertRaisesRegex(ValueError, 'Duplicate keys in build/patch-concerns.json'):
            load_concerns(path)

    def test_invalid_ratchet_files_are_rejected(self):
        """이유 없는 예외, 중복·알 수 없는 지표와 잘못된 기준선 값을 거부한다."""
        def data(**changes):
            role = config({'shell_branches': {'a.cc': 1}}, [exception('a.cc', 2)])
            role.update(changes)
            return {'schema_version': 1, 'roles': {'shell': role}}

        validate_ratchet(data())
        broken_baseline = config()
        del broken_baseline['baseline']['yee_members']
        cases = {
            'missing-reason': data(exceptions=[{'metric': 'shell_branches', 'path': 'a.cc',
                                                'allowed': 2}]),
            'blank-reason': data(exceptions=[exception('a.cc', 2, reason='  ')]),
            'unknown-metric': data(exceptions=[exception('a.cc', 2, metric='hunks')]),
            'duplicate': data(exceptions=[exception('a.cc', 2), exception('a.cc', 3)]),
            'zero-allowed': data(exceptions=[exception('a.cc', 0)]),
            'zero-count': data(baseline=config({'shell_branches': {'a.cc': 0}})['baseline']),
            'absolute-path': data(baseline=config({'shell_branches': {'/a.cc': 1}})['baseline']),
            'missing-metric': data(baseline=broken_baseline['baseline']),
            'unknown-schema': {**data(), 'schema_version': 2},
            'bool-schema': {**data(), 'schema_version': True},
            'float-schema': {**data(), 'schema_version': 1.0},
            'non-string-metric': data(exceptions=[exception('a.cc', 2, metric=[])]),
        }
        for name, value in cases.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_ratchet(value)

    def test_duplicate_json_keys_cannot_replace_limits(self):
        """JSON의 중복 경로나 예외 필드가 앞의 허용량을 조용히 덮어쓰지 못한다."""
        baseline = {metric: '{}' for metric in METRICS}
        exceptions = {
            'duplicate-path': ('{"a.cc": 1, "a.cc": 9}', '[]'),
            'duplicate-field': ('{"a.cc": 1}', '[{"metric": "shell_branches", "path": "a.cc", '
                                '"allowed": 2, "allowed": 9, "reason": "검토"}]'),
        }
        for name, (shell, entries) in exceptions.items():
            fields = {**baseline, 'shell_branches': shell}
            text = ('{"schema_version": 1, "roles": {"shell": {"baseline": {'
                    + ', '.join(f'"{metric}": {value}' for metric, value in fields.items())
                    + '}, "exceptions": ' + entries + '}}}')
            path = self.root / f'{name}.json'
            path.write_text(text)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Duplicate keys'):
                load_ratchet(path)

    def test_cli_reports_input_and_configuration_errors_with_code_two(self):
        """누락된 패치와 잘못된 예외 형식은 지표 위반(1)이 아니라 입력 오류(2)로 끝난다."""
        self.write_concerns({'chrome/browser/a.cc': ['shell']})
        code, _, stderr = self.run_cli('--init')
        self.assertEqual(code, 2)
        self.assertIn('입력 오류', stderr)
        self.write_shell_patch(section('chrome/browser/a.cc', [' context', '+added']))
        self.assertEqual(self.run_cli('--init')[0], 0)
        data = self.ratchet()
        data['roles']['shell']['exceptions'].append(exception('chrome/browser/a.cc', 2, metric=[]))
        (self.root / 'build/patch-ratchet.json').write_text(json.dumps(data))
        code, _, stderr = self.run_cli()
        self.assertEqual(code, 2)
        self.assertIn('입력 오류', stderr)


if __name__ == '__main__':
    unittest.main()
