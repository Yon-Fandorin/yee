// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_SUPPORT_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"

namespace yee::test {

// 주소와 독립적인 수명 identity다. 재생성한 owner에는 새 token을 부여한다.
using AppliedOwnerToken = base::UnguessableToken;
enum class AppliedAxis { kHorizontal, kVertical };

struct PageHostsApplied {
  uint64_t pane_id = 0;
  uint64_t tab_handle = 0;
  gfx::Rect bounds;
  gfx::Rect clip;
  gfx::RoundedCornersF radii;
  gfx::Rect target;
  gfx::Rect overlay;
  gfx::Rect applied_bounds;
  gfx::Rect applied_clip;
  gfx::Rect applied_target;
  gfx::Rect applied_overlay;
  bool geometry_applied = false;
  bool operator==(const PageHostsApplied&) const = default;
};

struct PaneLayoutMatches {
  uint64_t split_id = 0;
  AppliedAxis axis = AppliedAxis::kHorizontal;
  gfx::Size parent_extent;
  double ratio = 0.5;
  gfx::Insets insets;
  gfx::Rect divider;
  std::vector<uint64_t> tab_handles;
  gfx::Size start_parent_extent;
  gfx::Size end_parent_extent;
  gfx::Size start_container_extent;
  gfx::Size end_container_extent;
  bool geometry_matches_ratio = false;
  std::optional<double> ratio_min;
  std::optional<double> ratio_max;
  bool operator==(const PaneLayoutMatches&) const = default;
};

struct ViewStateMatches {
  bool drawn = false;
  bool visible = false;
  gfx::Rect bounds;
  AppliedOwnerToken parent;
  std::vector<AppliedOwnerToken> children;
  gfx::Vector2d scroll_offset;
  bool destroyed = false;
  double opacity = 1;
  bool hovered = false;
  bool expanded_on_hover = false;
  bool hover_animation_running = false;
  bool scroll_animation_running = false;
  bool refresh_pending = false;
  bool popup_open = false;
  bool editing = false;
  gfx::Rect screen_bounds;
  gfx::Rect visible_bounds;
  gfx::Rect painted_bounds;
  gfx::Rect contents_bounds_in_screen;
  bool has_background = false;
  int focus_change_reason = 0;
  bool handling_drag = false;
  bool collapsed = false;
  bool vertical_drag_only = false;
  bool popup_transition = false;
  bool autocomplete_empty = true;
  bool presenter_shown = false;
  bool operator==(const ViewStateMatches&) const = default;
};

struct AnimationMatches {
  uint64_t group_id = 0;
  bool running = false;
  // 기대값은 닫힌 구간, snapshot은 [progress, progress]로 표현한다.
  double progress_min = 0;
  double progress_max = 1;
  bool operator==(const AnimationMatches&) const = default;
};

struct SurfaceEpochCommitted {
  enum class Phase { kTransitioning, kStable };
  enum class Destination { kWindowed, kFullscreen, kImmersive };
  uint64_t epoch = 0;
  Phase phase = Phase::kStable;
  uint64_t committed_generation = 0;
  Destination destination = Destination::kWindowed;
  bool operator==(const SurfaceEpochCommitted&) const = default;
};

struct WidgetStateMatches {
  enum class ShowState { kNormal, kMinimized, kMaximized, kFullscreen };
  bool visible = false;
  gfx::Rect bounds;
  bool capture = false;
  ShowState show_state = ShowState::kNormal;
  bool closed = false;
  bool active = false;
  double opacity = 1;
  bool visible_on_screen = false;
  bool paint_as_active = false;
  bool operator==(const WidgetStateMatches&) const = default;
};

struct FocusMatches {
  enum class Kind { kLogicalView, kRendererDocument, kNativeKeyWindow };
  Kind kind = Kind::kLogicalView;
  AppliedOwnerToken target;
  AppliedOwnerToken document;
  bool focused = false;
  bool operator==(const FocusMatches&) const = default;
};

struct NativeWindowMatches {
  bool active = false;
  bool on_active_space = false;
  bool unoccluded = false;
  bool fullscreen = false;
  bool fullscreen_transition_complete = false;
  bool operator==(const NativeWindowMatches&) const = default;
};

// macOS 프레임의 적용값이다. 외부 Toolbar의 AppKit 표시와 Yee 배치를
// 함께 읽되, 관측 중 창 생성·재배치·포인터 입력을 수행하지 않는다.
struct NativeFrameMatches {
  bool opaque = false;
  bool glass_eligible = false;
  bool toolbar_revealed = false;
  double menu_bar_reveal = 0;
  bool toolbar_host_ready = false;
  bool toolbar_on_active_space = false;
  bool toolbar_fully_visible = false;
  bool toolbar_offscreen = false;
  gfx::Rect toolbar_bounds;
  bool operator==(const NativeFrameMatches&) const = default;
};

struct AppliedTabState {
  uint64_t handle = 0;
  bool pinned = false;
  AppliedOwnerToken group;
  uint64_t split_id = 0;
  bool operator==(const AppliedTabState&) const = default;
};

struct AppliedSplitState {
  uint64_t split_id = 0;
  AppliedAxis axis = AppliedAxis::kHorizontal;
  double ratio = 0.5;
  std::vector<uint64_t> tab_handles;
  bool operator==(const AppliedSplitState&) const = default;
};

struct TabModelMatches {
  std::vector<AppliedTabState> tabs;
  std::vector<AppliedSplitState> splits;
  uint64_t active_tab = 0;
  uint64_t project_page = 0;
  uint64_t project_title = 0;
  uint64_t project_live_tab = 0;
  bool operator==(const TabModelMatches&) const = default;
};

struct AppliedBrowserState {
  AppliedOwnerToken browser;
  std::vector<uint64_t> tab_handles;
  bool operator==(const AppliedBrowserState&) const = default;
};

struct BrowserSetMatches {
  std::vector<AppliedBrowserState> browsers;
  std::vector<AppliedOwnerToken> created;
  std::vector<AppliedOwnerToken> closed;
  bool operator==(const BrowserSetMatches&) const = default;
};

struct DragPhaseMatches {
  enum class Phase { kCreated, kStarted, kAttached, kLoopEnded, kInactive };
  uint64_t session = 0;
  uint64_t step = 0;
  uint64_t source_handle = 0;
  uint64_t target_handle = 0;
  Phase phase = Phase::kInactive;
  AppliedOwnerToken attached_browser;
  bool input_acknowledged = false;
  std::optional<gfx::Point> last_input_screen;
  bool operator==(const DragPhaseMatches&) const = default;
};

struct PreviewApplied {
  uint64_t source_handle = 0;
  uint64_t tab_handle = 0;
  bool visible = false;
  bool animation_running = false;
  gfx::Rect screen_bounds;
  int source_screen_x = 0;
  int source_screen_width = 0;
  bool geometry_applied = true;
  bool operator==(const PreviewApplied&) const = default;
};

struct RendererViewportMatches {
  AppliedOwnerToken web_contents;
  AppliedOwnerToken render_frame;
  AppliedOwnerToken document;
  AppliedOwnerToken surface;
  gfx::Size viewport_pixels;
  double scale = 1;
  bool operator==(const RendererViewportMatches&) const = default;
};

struct WebUiStateMatches {
  AppliedOwnerToken document;
  // 페이지 문자열 대신 행 identity와 표시값만 복사한다.
  std::vector<uint64_t> popup_rows;
  int selected_index = -1;
  gfx::RectF rect;
  double opacity = 1;
  bool focused = false;
  int hovered_index = -1;
  int padding_top = 0;
  int results_padding = 0;
  bool forced_colors = false;
  bool prefers_contrast_more = false;
  bool focus_indicator_visible = false;
  uint32_t hover_background = 0;
  std::vector<gfx::RectF> row_bounds;
  bool customization_ready = false;
  int click_count = 0;
  gfx::Rect native_view_bounds_in_screen;
  gfx::Rect native_widget_bounds_in_screen;
  bool operator==(const WebUiStateMatches&) const = default;
};

struct PresentationMatches {
  uint64_t source_id = 0;
  uint64_t presentation_id = 0;
  uint32_t background_color = 0;
  uint32_t foreground_color = 0;
  uint64_t icon_id = 0;
  bool use_native_theme = false;
  uint64_t popup_revision = 0;
  bool icon_ready = false;
  bool sampling_settled = false;
  bool operator==(const PresentationMatches&) const = default;
};

struct PresentedAfterApply {
  AppliedOwnerToken compositor;
  uint64_t geometry_digest = 0;
  uint64_t applied_sample_id = 0;
  bool successful = true;
  bool operator==(const PresentedAfterApply&) const = default;
};

// 임의 predicate나 제품 객체를 조건으로 넘길 수 없다.
using AppliedConditionValue = std::variant<PageHostsApplied,
                                           PaneLayoutMatches,
                                           ViewStateMatches,
                                           AnimationMatches,
                                           SurfaceEpochCommitted,
                                           WidgetStateMatches,
                                           FocusMatches,
                                           NativeWindowMatches,
                                           TabModelMatches,
                                           BrowserSetMatches,
                                           DragPhaseMatches,
                                           PreviewApplied,
                                           RendererViewportMatches,
                                           WebUiStateMatches,
                                           PresentationMatches,
                                           PresentedAfterApply,
                                           NativeFrameMatches>;
enum class AppliedConditionKind {
  kPageHostsApplied,
  kPaneLayoutMatches,
  kViewStateMatches,
  kAnimationMatches,
  kSurfaceEpochCommitted,
  kWidgetStateMatches,
  kFocusMatches,
  kNativeWindowMatches,
  kTabModelMatches,
  kBrowserSetMatches,
  kDragPhaseMatches,
  kPreviewApplied,
  kRendererViewportMatches,
  kWebUiStateMatches,
  kPresentationMatches,
  kPresentedAfterApply,
  kNativeFrameMatches,
};
AppliedConditionKind GetAppliedConditionKind(
    const AppliedConditionValue& value);
std::string FormatAppliedValue(const AppliedConditionValue& value);

enum class AppliedFreshness {
  kNewApply,
  kNewObservation,
  kCurrentAfterDispatch
};
enum class AppliedEvaluationMode { kCurrent, kObservedSinceDispatch };

// 비동기 적용에서 요청한 필드만 비교한다. 임의 predicate는 받지 않는다.
// kExact는 기존 전체 값 비교를 유지한다.
enum class AppliedMatch {
  kExact,
  kPageIdentity,
  kPageGeometry,
  kPaneSplit,
  kPaneSplitCurrentExtent,
  kViewVisible,
  kViewDrawn,
  kViewBounds,
  kViewSize,
  kViewWidth,
  kViewFullyVisible,
  kViewScreenBounds,
  kViewScreenBoundsDifferent,
  kViewHeight,
  kViewParent,
  kViewParentDifferent,
  kViewChildren,
  kViewNonEmpty,
  kViewCenterVisible,
  kViewPointerFocus,
  kViewDestroyed,
  kViewOpacity,
  kViewOpacityPositive,
  kViewHovered,
  kViewChildCount,
  kSidebarHover,
  kTabHoverIdle,
  kViewScrollIdle,
  kViewScrollOffset,
  kViewScrolledDown,
  kViewGroupDrag,
  kOmniboxPopup,
  kOmniboxEditing,
  kOmniboxIdle,
  kOmniboxTransitionComplete,
  kOmniboxShown,
  kWidgetVisible,
  kWidgetOpacity,
  kWidgetBounds,
  kWidgetClosed,
  kWidgetCapture,
  kWidgetActive,
  kWidgetPaintActive,
  kWidgetReady,
  kWidgetShowState,
  kWidgetSizeNear,
  kWidgetSize,
  kFooterMenu,
  kProjectRefreshed,
  kProjectTitle,
  kProjectLive,
  kTabCount,
  kTabOrder,
  kPinnedTabOrder,
  kTabMembership,
  kTabActive,
  kNewTabActive,
  kPresentationRevision,
  kPresentationRevisionChanged,
  kPresentationIconChanged,
  kPresentationIconReady,
  kSamplingSettled,
  kNativeActive,
  kNativeFullscreen,
  kNativeFrameOpaque,
  kNativeFrameGlassEligible,
  kNativeToolbarRevealed,
  kNativeMenuRevealed,
  kNativeToolbarMode,
  kNativeToolbarY,
  kNativeToolbarActiveSpace,
  kPresentationColor,
  kPresentationIcon,
  kPresentationAlpha,
  kPresentationDark,
  kWebUiRowsAtLeast,
  kWebUiRowsFitNativeBounds,
  kWebUiSelection,
  kWebUiOpacity,
  kWebUiHover,
  kWebUiPadding,
  kWebUiResultsPadding,
  kWebUiMedia,
  kWebUiHoverColor,
  kWebUiCustomizationReady,
  kWebUiClickCount,
  kSurfaceDestination,
  kPresentedFrame,
  kDragPhase,
  kPreviewSettled,
  kPreviewAligned,
  kCapsuleTheme,
  kDragInputAcknowledged,
};

struct AppliedCoordinates {
  enum class Space { kLocal, kParent, kScreen };
  enum class Units { kDip, kPixel };
  Space space = Space::kLocal;
  Units units = Units::kDip;
  double device_scale_factor = 1;
  bool rtl = false;
  bool operator==(const AppliedCoordinates&) const = default;
};

struct AppliedCondition {
  AppliedOwnerToken owner;
  AppliedConditionValue expected;
  AppliedFreshness freshness = AppliedFreshness::kNewApply;
  AppliedEvaluationMode mode = AppliedEvaluationMode::kCurrent;
  // 같은 group의 순간 조건은 한 sample 전체에서 충족해야 한다.
  uint64_t observation_group = 0;
  AppliedCoordinates coordinates;
  AppliedMatch match = AppliedMatch::kExact;
  bool operator==(const AppliedCondition&) const = default;
};
using AppliedConditions = std::vector<AppliedCondition>;
bool IsAppliedMatchCompatible(const AppliedCondition& condition);

bool AppliedValueMatches(const AppliedCondition& condition,
                         const AppliedConditionValue& actual);

enum class AppliedDomain {
  kLayout,
  kAnimation,
  kSurface,
  kWidget,
  kFocus,
  kNativeWindow,
  kTabModel,
  kBrowserSet,
  kDrag,
  kPreview,
  kRenderer,
  kWebUi,
  kPresentation,
  kFrame,
};

struct AppliedIntentTarget {
  AppliedOwnerToken owner;
  AppliedConditionValue value;
  bool operator==(const AppliedIntentTarget&) const = default;
};

struct AppliedIntent {
  enum class Operation { kApply, kCollapse, kSplit, kResize, kDrag, kNoOp };
  AppliedDomain domain = AppliedDomain::kLayout;
  Operation operation = Operation::kApply;
  std::vector<AppliedOwnerToken> owner_tokens;
  std::optional<uint64_t> request_epoch;
  // split/tab/resize/drag 목적값은 조건과 같은 닫힌 schema를 쓴다.
  // 비어 있으면 Begin이 conditions에서 채우며, 지정하면 일치를 검사한다.
  std::vector<AppliedIntentTarget> targets;
  bool operator==(const AppliedIntent&) const = default;
};

struct AppliedSignalKey {
  AppliedOwnerToken owner;
  AppliedConditionKind kind = AppliedConditionKind::kPageHostsApplied;
  bool operator==(const AppliedSignalKey&) const = default;
};

struct AppliedSerials {
  uint64_t apply_serial = 0;
  uint64_t native_generation = 0;
  uint64_t observation_sequence = 0;
  bool operator==(const AppliedSerials&) const = default;
};

struct AppliedBaseline {
  AppliedSignalKey key;
  AppliedSerials serials;
  bool operator==(const AppliedBaseline&) const = default;
};

struct AppliedTicket {
  base::UnguessableToken session_id;
  uint64_t request_serial = 0;
  AppliedDomain domain = AppliedDomain::kLayout;
  std::vector<AppliedOwnerToken> owner_tokens;
  std::vector<AppliedBaseline> baselines;
  AppliedIntent intent;
  bool operator==(const AppliedTicket&) const = default;
};

struct AppliedSnapshot {
  AppliedOwnerToken owner;
  AppliedConditionValue value;
  AppliedSerials serials;
  uint64_t sample_id = 0;
  std::optional<uint64_t> request_epoch;
  AppliedCoordinates coordinates;
  // 어댑터의 신호 준비·전달 진단이며 조건 판정과 geometry 지문에는 쓰지 않는다.
  std::string adapter_diagnostics;
  bool operator==(const AppliedSnapshot&) const = default;
};

// frame을 적용 geometry·identity·좌표계에 묶는다. View의 hover/focus 같은
// 비geometry 상태는 각 조건으로 검사하며 frame의 geometry 지문에 넣지 않는다.
uint64_t AppliedFrameGeometryDigest(
    const std::vector<AppliedSnapshot>& snapshots);

struct AppliedSignalSample {
  // 한 callback의 snapshots는 같은 순간을 나타내며 sample_id도 같아야 한다.
  std::vector<AppliedSnapshot> snapshots;
  std::vector<AppliedOwnerToken> destroyed_owners;
};

// 테스트 source와 후속 observer adapter의 경계다. 모든 호출은 세션 sequence에서
// 수행한다. source는 세션보다 오래 살아야 하며 Read는 부작용 없이 현재 값을
// 복사한다.
class AppliedSignalSource {
 public:
  using Observer = base::RepeatingCallback<void(const AppliedSignalSample&)>;
  virtual ~AppliedSignalSource() = default;
  virtual bool Supports(const AppliedCondition& condition) const = 0;
  virtual bool IsOwnerAlive(AppliedOwnerToken owner) const = 0;
  virtual std::optional<AppliedSnapshot> Read(
      const AppliedSignalKey& key) const = 0;
  virtual base::CallbackListSubscription Subscribe(
      const std::vector<AppliedSignalKey>& keys,
      Observer observer) = 0;
};

struct WaitOptions {
  enum class AlreadyApplied { kRequireFresh, kAllowAlreadyApplied };
  enum class Tier { kA, kB, kC };
  enum class Backend { kFake, kHeadless, kNative };
  enum class Platform { kMac, kLinux, kWindows };
  static Platform CurrentPlatform();
  base::TimeTicks deadline;
  AlreadyApplied already_applied = AlreadyApplied::kRequireFresh;
  std::string test_name;
  std::string parameter;
  Tier tier = Tier::kA;
  Backend backend = Backend::kFake;
  Platform platform = CurrentPlatform();
};

enum class AppliedResultKind {
  kApplied,
  kTimedOut,
  kSuperseded,
  kOwnerDestroyed,
  kCancelled,
  kDispatchFailed,
  kUnsupportedCondition,
  kInvalidTicket,
};

enum class AppliedEventKind {
  kBegin,
  kDispatch,
  kDispatchAck,
  kSignal,
  kStaleSignal,
  kOwnerDestroyed,
  kEvaluate,
  kDeadline,
  kTerminal,
  kObservationReleased,
};

struct AppliedWaitEvent {
  base::TimeTicks timestamp;
  AppliedEventKind kind = AppliedEventKind::kBegin;
  AppliedOwnerToken owner;
  std::optional<AppliedConditionKind> condition_kind;
  AppliedSerials serials;
  uint64_t sample_id = 0;
};

enum class AppliedConditionBlocker {
  kNotEvaluated,
  kUnsupported,
  kOwnerDestroyed,
  kDestroyedBeforeDispatch,
  kNoCurrentSnapshot,
  kOwnerMismatch,
  kKindMismatch,
  kValueMismatch,
  kCoordinatesMismatch,
  kRequestEpochMismatch,
  kSerialRegression,
  kApplyNotNew,
  kObservationNotNew,
  kNoSampleIdentity,
  kNoObservedSample,
  kObservationGroupNotLatched,
};

struct AppliedConditionDiagnostics {
  AppliedCondition condition;
  AppliedSerials baseline;
  AppliedSerials dispatch_fence;
  AppliedSerials high_water;
  AppliedSerials event_high_water;
  std::optional<AppliedSnapshot> last;
  std::optional<AppliedSnapshot> observed;
  bool satisfied = false;
  bool supported = true;
  bool owner_destroyed = false;
  std::vector<AppliedConditionBlocker> blockers{
      AppliedConditionBlocker::kNotEvaluated};
};

struct AppliedWaitDiagnostics {
  AppliedTicket ticket;
  WaitOptions options;
  base::TimeTicks began_at;
  base::TimeTicks captured_at;
  bool dispatched = false;
  bool dispatch_acknowledged = false;
  bool deadline_expired = false;
  base::TimeTicks evaluation_queued_at;
  base::TimeTicks evaluated_at;
  base::TimeDelta max_evaluation_delay;
  uint64_t evaluation_count = 0;
  uint64_t wake_count = 0;
  std::vector<AppliedConditionDiagnostics> conditions;
  std::deque<AppliedWaitEvent> events;
  std::string ToString() const;
};

struct AppliedWaitResult {
  AppliedResultKind kind = AppliedResultKind::kInvalidTicket;
  AppliedWaitDiagnostics diagnostics;
  std::string ToString() const;
};

enum class AppliedDispatchStatus { kAccepted, kRejected };
using DispatchDone = base::OnceCallback<void(AppliedDispatchStatus)>;

class AppliedObservation {
 public:
  AppliedObservation();
  AppliedObservation(AppliedObservation&&);
  AppliedObservation& operator=(AppliedObservation&&);
  ~AppliedObservation();
  // callback과 source 구독, 예약 평가를 해제한다. terminal callback은 보내지
  // 않는다.
  void Reset();

 private:
  friend class AppliedWaitSession;
  explicit AppliedObservation(base::OnceClosure release);
  base::ScopedClosureRunner release_;
};

class AppliedWaitSession {
 public:
  explicit AppliedWaitSession(AppliedSignalSource& source);
  AppliedWaitSession(const AppliedWaitSession&) = delete;
  AppliedWaitSession& operator=(const AppliedWaitSession&) = delete;
  ~AppliedWaitSession();

  AppliedTicket BeginRequest(AppliedIntent intent,
                             AppliedConditions conditions,
                             WaitOptions options);
  // 수락하면 nullopt, 잘못된 호출이면 즉시 InvalidTicket을 반환한다.
  // action/submit은 한 번만 실행하며 ACK 전에는 Applied가 될 수 없다.
  std::optional<AppliedWaitResult> Dispatch(AppliedTicket ticket,
                                            base::OnceClosure action);
  std::optional<AppliedWaitResult> DispatchAsync(
      AppliedTicket ticket,
      base::OnceCallback<void(DispatchDone)> submit);
  AppliedWaitResult WaitForApplied(AppliedTicket ticket,
                                   const AppliedConditions& conditions);
  // Observe는 Dispatch 전에도 가능하다. callback은 세션 sequence에 post한다.
  // ticket당 blocking/async 소비자는 하나만 허용하며 conditions 순서도
  // 고정한다.
  AppliedObservation ObserveApplied(
      AppliedTicket ticket,
      const AppliedConditions& conditions,
      base::OnceCallback<void(AppliedWaitResult)> done);
  std::optional<AppliedWaitResult> Cancel(AppliedTicket ticket);
  std::optional<AppliedWaitDiagnostics> GetDiagnostics(
      AppliedTicket ticket) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_SUPPORT_H_
