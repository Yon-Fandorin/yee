// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_kombucha.h"

#include <algorithm>
#include <memory>
#include <new>
#include <set>
#include <tuple>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab_container.h"
#include "chrome/browser/ui/views/tabs/tab_group_underline.h"
#include "chrome/browser/ui/views/tabs/tab_slot_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/drag_interactive_test_support.h"
#include "chrome/browser/ui/views/yee/favorites.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/interaction/interactive_test_internal.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/layout/animating_layout_manager.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace yee::test {
namespace {

AppliedIntent Intent(const AppliedConditions& conditions,
                     AppliedDomain domain = AppliedDomain::kLayout) {
  AppliedIntent intent{.domain = domain};
  std::set<AppliedOwnerToken> owners;
  for (const auto& condition : conditions) {
    if (owners.insert(condition.owner).second) {
      intent.owner_tokens.push_back(condition.owner);
    }
  }
  return intent;
}

WaitOptions Options(base::TimeDelta budget = TestTimeouts::action_timeout()) {
  const bool headless =
      base::CommandLine::ForCurrentProcess()->HasSwitch("headless");
  return {.deadline = base::TimeTicks::Now() + budget,
          .test_name =
              testing::UnitTest::GetInstance()->current_test_info()->name(),
          .tier = WaitOptions::Tier::kB,
          .backend = headless ? WaitOptions::Backend::kHeadless
                              : WaitOptions::Backend::kNative};
}

PageViewportGeometry Geometry() {
  return ResolvePageViewportGeometry(
      {.page_environment_in_container = gfx::Rect(0, 0, 300, 200),
       .content_and_footer_in_container = gfx::Rect(0, 0, 300, 200),
       .viewport_radii = gfx::RoundedCornersF(8)});
}
void Apply(views::View& host) {
  const auto geometry = Geometry();
  host.SetBoundsRect(geometry.current_viewport_in_container);
  ApplyPageViewportHostGeometry(host, geometry);
}
AppliedCondition PageCondition(AppliedViewsObservers& source,
                               views::View& host,
                               uint64_t pane = 1) {
  const auto owner = source.ObservePageHosts(host, pane, 42 + pane);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kPageHostsApplied});
  return {.owner = owner,
          .expected = snapshot->value,
          .coordinates = snapshot->coordinates};
}

using ProbeViewTree = std::vector<
    std::tuple<const views::View*, const views::View*, gfx::Rect, bool, bool>>;

ProbeViewTree SnapshotProbeViewTree(const views::View& root) {
  ProbeViewTree result;
  const auto visit = [&](auto&& self, const views::View& view) -> void {
    result.emplace_back(&view, view.parent(), view.bounds(), view.GetVisible(),
                        view.needs_layout());
    for (const auto& child : view.children())
      self(self, *child);
  };
  visit(visit, root);
  return result;
}

class YeeAppliedWaitInteractiveTest
    : public AppliedWaitInteractiveTestMixin<InteractiveBrowserTest> {};

class YeeAppliedWaitDragInteractiveTest : public YeeAppliedWaitInteractiveTest {
};

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       RealApplyCallbackBeforeWaitAndSameValueReapply) {
  auto host = CreatePageViewportHostTree();
  Apply(*host);
  AppliedViewsObservers source;
  AppliedWaitSession session(source);
  const AppliedConditions conditions{PageCondition(source, *host)};
  const auto initial = PageViewportAppliedState::From(*host)->GetSnapshot();
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  host->layer()->SetClipRect(gfx::Rect(10, 10, 150, 100));
  base::test::TestFuture<void> callback;
  auto subscription = PageViewportAppliedState::From(*host)->Subscribe(
      base::BindLambdaForTesting(
          [&](const PageViewportAppliedSnapshot&) { callback.SetValue(); }));
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] { Apply(*host); })));
  ASSERT_TRUE(callback.Wait());
  // 실제 신호 뒤 예약된 평가까지 지난 다음 잘못된 native clip을 거부한다.
  base::test::TestFuture<void> evaluated;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, evaluated.GetCallback());
  ASSERT_TRUE(evaluated.Wait());
  EXPECT_FALSE(session.GetDiagnostics(ticket)->conditions[0].satisfied);
  host->layer()->SetClipRect(gfx::Rect());
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_EQ(initial.apply_serial + 1,
            result.diagnostics.conditions[0].last->serials.apply_serial);
  EXPECT_EQ(initial.geometry.generation,
            result.diagnostics.conditions[0].last->serials.native_generation);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       TwoHostsRequireBothApplies) {
  auto first = CreatePageViewportHostTree();
  auto second = CreatePageViewportHostTree();
  Apply(*first);
  Apply(*second);
  AppliedViewsObservers source;
  AppliedWaitSession session(source);
  const AppliedConditions conditions{PageCondition(source, *first, 1),
                                     PageCondition(source, *second, 2)};
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  base::test::TestFuture<void> first_applied;
  auto subscription = PageViewportAppliedState::From(*first)->Subscribe(
      base::BindLambdaForTesting([&](const PageViewportAppliedSnapshot&) {
        first_applied.SetValue();
      }));
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] { Apply(*first); })));
  ASSERT_TRUE(first_applied.Wait());
  EXPECT_FALSE(session.GetDiagnostics(ticket)->conditions[1].satisfied);
  Apply(*second);
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_TRUE(result.diagnostics.conditions[0].satisfied);
  EXPECT_TRUE(result.diagnostics.conditions[1].satisfied);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ExplicitNoOpAndWidgetCurrentState) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  AppliedViewsObservers source;
  const auto owner = source.ObserveWidget(*widget);
  AppliedCondition condition{
      .owner = owner,
      .expected =
          source.Read({owner, AppliedConditionKind::kWidgetStateMatches})
              ->value,
      .freshness = AppliedFreshness::kCurrentAfterDispatch,
      .coordinates = GetAppliedViewCoordinates(
          *widget->GetRootView(), AppliedCoordinates::Space::kScreen)};
  AppliedWaitSession session(source);
  auto intent = Intent({condition}, AppliedDomain::kWidget);
  intent.operation = AppliedIntent::Operation::kNoOp;
  auto options = Options();
  options.already_applied = WaitOptions::AlreadyApplied::kAllowAlreadyApplied;
  auto ticket = session.BeginRequest(intent, {condition}, options);
  ASSERT_FALSE(session.Dispatch(ticket, base::DoNothing()));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(ticket, {condition}).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       SubscriptionReleaseStopsUnrelatedSnapshotReads) {
  class CountingView : public views::View {
   public:
    bool IsDrawn() const override {
      ++reads;
      return views::View::IsDrawn();
    }
    mutable int reads = 0;
  } counted;
  views::View trigger;
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(counted);
  source.ObserveView(trigger);
  const std::vector<AppliedSignalKey> keys{
      {owner, AppliedConditionKind::kViewStateMatches}};
  const auto observe = [&] {
    return source.Subscribe(keys, base::DoNothing());
  };
  auto first = observe();
  auto second = observe();
  first = {};
  counted.reads = 0;
  trigger.SetBoundsRect(gfx::Rect(1, 1, 10, 10));
  EXPECT_GT(counted.reads, 0);
  second = {};
  counted.reads = 0;
  trigger.SetBoundsRect(gfx::Rect(2, 2, 10, 10));
  EXPECT_EQ(counted.reads, 0);
  // 다시 구독하면 새 현재값을 읽고 다음 순간 표본에도 참여한다.
  auto next = observe();
  EXPECT_GT(counted.reads, 0);
  counted.reads = 0;
  trigger.SetBoundsRect(gfx::Rect(3, 3, 10, 10));
  EXPECT_GT(counted.reads, 0);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       OwnerDestructionRejectsAddressReuse) {
  alignas(views::View) unsigned char storage[sizeof(views::View)];
  auto* view = std::construct_at(reinterpret_cast<views::View*>(storage));
  AppliedViewsObservers source;
  const auto old_owner = source.ObserveView(*view);
  auto value = std::get<ViewStateMatches>(
      source.Read({old_owner, AppliedConditionKind::kViewStateMatches})->value);
  value.bounds = gfx::Rect(1, 2, 50, 40);
  const AppliedConditions conditions{{old_owner, value}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  std::destroy_at(view);
                                  view = std::construct_at(
                                      reinterpret_cast<views::View*>(storage));
                                  view->SetBoundsRect(value.bounds);
                                })));
  EXPECT_NE(old_owner, source.ObserveView(*view));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, result.kind)
      << result.ToString();
  std::destroy_at(view);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ExpectedDeletionAndReleasedObservation) {
  auto view = std::make_unique<views::View>();
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(*view);
  const AppliedConditions conditions{
      {owner, ViewStateMatches{.destroyed = true}}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  int calls = 0;
  auto observation = session.ObserveApplied(
      ticket, conditions,
      base::BindLambdaForTesting([&](AppliedWaitResult) { ++calls; }));
  observation.Reset();
  view.reset();
  EXPECT_EQ(0, calls);
  auto second = std::make_unique<views::View>();
  const auto second_owner = source.ObserveView(*second);
  const AppliedConditions deleted{
      {second_owner, ViewStateMatches{.destroyed = true}}};
  const auto closing =
      session.BeginRequest(Intent(deleted), deleted, Options());
  ASSERT_FALSE(session.Dispatch(
      closing, base::BindLambdaForTesting([&] { second.reset(); })));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(closing, deleted).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ChildAddedDuringObservationHasStableIdentity) {
  auto parent = std::make_unique<views::View>();
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(*parent);
  bool saw_child = false;
  auto subscription = source.Subscribe(
      {{owner, AppliedConditionKind::kViewStateMatches}},
      base::BindLambdaForTesting([&](const AppliedSignalSample& sample) {
        for (const auto& snapshot : sample.snapshots) {
          const auto& value = std::get<ViewStateMatches>(snapshot.value);
          if (value.children.size() == 1) {
            saw_child = true;
            EXPECT_FALSE(value.children[0].is_empty());
          }
        }
      }));
  const AppliedConditions conditions{
      {owner, ViewStateMatches{.destroyed = true}}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  parent->AddChildView(
                                      std::make_unique<views::View>());
                                  parent.reset();
                                })));
  EXPECT_TRUE(saw_child);
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(ticket, conditions).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ViewObservationDoesNotVisitSiblingDescendants) {
  views::View root;
  auto* leaf = root.AddChildView(std::make_unique<views::View>());
  auto* sibling = root.AddChildView(std::make_unique<views::View>());
  auto* unrelated = sibling->AddChildView(std::make_unique<views::View>());
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(*leaf);
  int calls = 0;
  auto subscription = source.Subscribe(
      {{owner, AppliedConditionKind::kViewStateMatches}},
      base::BindLambdaForTesting([&](const AppliedSignalSample&) { ++calls; }));
  unrelated->SetBoundsRect(gfx::Rect(1, 2, 30, 40));
  EXPECT_EQ(0, calls);
  leaf->SetBoundsRect(gfx::Rect(2, 3, 40, 50));
  EXPECT_GT(calls, 0);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kViewStateMatches});
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(leaf->bounds(), std::get<ViewStateMatches>(snapshot->value).bounds);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ReentrantViewSignalRetainsLastTransition) {
  views::View trigger;
  views::View target;
  AppliedViewsObservers source;
  const auto trigger_owner = source.ObserveView(trigger);
  const auto owner = source.ObserveView(target);
  auto expected = std::get<ViewStateMatches>(
      source.Read({owner, AppliedConditionKind::kViewStateMatches})->value);
  expected.bounds = gfx::Rect(1, 2, 30, 40);
  // 실제 target은 같은 task에서 원래 bounds로 복원된다. 별도 View에 목표
  // bounds를 적용해 관측 당시의 파생 geometry를 계산한다.
  views::View expected_target;
  expected_target.SetBoundsRect(expected.bounds);
  expected.screen_bounds = expected_target.GetBoundsInScreen();
  expected.visible_bounds = expected_target.GetVisibleBounds();
  expected.painted_bounds = gfx::Rect();
  expected.contents_bounds_in_screen = expected_target.GetContentsBounds();
  views::View::ConvertRectToScreen(&expected_target,
                                   &expected.contents_bounds_in_screen);
  const AppliedConditions conditions{
      {.owner = owner,
       .expected = expected,
       .freshness = AppliedFreshness::kNewObservation,
       .mode = AppliedEvaluationMode::kObservedSinceDispatch,
       .coordinates = GetAppliedViewCoordinates(
           target, AppliedCoordinates::Space::kParent),
       .match = AppliedMatch::kViewBounds}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  bool changed = false;
  auto subscription = source.Subscribe(
      {{trigger_owner, AppliedConditionKind::kViewStateMatches}},
      base::BindLambdaForTesting([&](const AppliedSignalSample&) {
        if (!changed) {
          changed = true;
          target.SetBoundsRect(expected.bounds);
          target.SetBoundsRect(gfx::Rect());
        }
      }));
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  trigger.SetBoundsRect(gfx::Rect(1, 1, 1, 1));
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_EQ(expected, std::get<ViewStateMatches>(
                          result.diagnostics.conditions[0].observed->value));
  EXPECT_TRUE(
      std::get<ViewStateMatches>(result.diagnostics.conditions[0].last->value)
          .bounds.IsEmpty());
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       RealAnimationObservedSurvivesCurrentIdle) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  auto reduced =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* decoration = parent->AddChildView(std::make_unique<views::View>());
  base::ScopedClosureRunner remove(base::BindLambdaForTesting(
      [&] { parent->RemoveChildViewT(decoration); }));
  ASSERT_TRUE(UsesHorizontalTabGroupCapsule(*decoration));
  SetHorizontalTabGroupCapsuleCollapsed(*decoration, true);
  AppliedViewsObservers source;
  const auto owner = source.ObserveCapsule(*decoration, 71);
  AppliedCondition observed{owner, AnimationMatches{71, true, 0, 1}};
  observed.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  observed.coordinates = GetAppliedViewCoordinates(*decoration);
  AppliedCondition current = observed;
  current.mode = AppliedEvaluationMode::kCurrent;
  AppliedWaitSession observed_session(source);
  AppliedWaitSession current_session(source);
  const auto first =
      observed_session.BeginRequest(Intent({observed}), {observed}, Options());
  const auto second = current_session.BeginRequest(
      Intent({current}), {current}, Options(base::Milliseconds(100)));
  ASSERT_FALSE(current_session.Dispatch(second, base::DoNothing()));
  ASSERT_FALSE(observed_session.Dispatch(
      first, base::BindLambdaForTesting([&] {
        SetHorizontalTabGroupCapsuleCollapsed(*decoration, false);
        ASSERT_TRUE(HorizontalTabGroupCapsuleState::From(*decoration)
                        ->GetSnapshot()
                        .running);
        auto reset =
            gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
        SetHorizontalTabGroupCapsuleCollapsed(*decoration, true);
      })));
  const auto result = observed_session.WaitForApplied(first, {observed});
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_TRUE(std::get<AnimationMatches>(
                  result.diagnostics.conditions[0].observed->value)
                  .running);
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            current_session.WaitForApplied(second, {current}).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       HeadlessNativeCapabilityIsExplicit) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  AppliedViewsObservers source;
  const auto owner = source.ObserveNativeWindow(*widget);
  AppliedConditions conditions{
      {owner, NativeWindowMatches{}, AppliedFreshness::kCurrentAfterDispatch}};
#if BUILDFLAG(IS_MAC)
  const bool headless =
      base::CommandLine::ForCurrentProcess()->HasSwitch("headless");
  EXPECT_EQ(!headless, source.Supports(conditions[0]));
#else
  EXPECT_FALSE(source.Supports(conditions[0]));
#endif
  if (!source.Supports(conditions[0])) {
    AppliedWaitSession session(source);
    const auto ticket =
        session.BeginRequest(Intent(conditions), conditions, Options());
    EXPECT_EQ(AppliedResultKind::kUnsupportedCondition,
              session.WaitForApplied(ticket, conditions).kind);
  } else {
    EXPECT_TRUE(
        source.Read({owner, AppliedConditionKind::kNativeWindowMatches}));
  }
  auto detached = CreatePageViewportHostTree();
  Apply(*detached);
  const auto detached_page = PageCondition(source, *detached);
  const auto missing_compositor =
      source.ObservePresentedAfterApply(*detached, {detached_page});
  AppliedWaitSession unavailable(source);
  const auto rejected = unavailable.BeginRequest(
      Intent({missing_compositor}), {missing_compositor}, Options());
  EXPECT_EQ(AppliedResultKind::kUnsupportedCondition,
            unavailable.WaitForApplied(rejected, {missing_compositor}).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       PreviewRequiresItsOwnApplySerial) {
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  views::View strip;
  strip.SetBounds(0, 0, 360, 500);
  auto* row = strip.AddChildView(std::make_unique<views::View>());
  row->SetBounds(4, 100, 340, 36);
  const auto show = [&] {
    ShowFavoritesDragPreview(strip, gfx::Point(120, 120), false, 0, 1,
                             ui::ImageModel(), u"preview",
                             gfx::Vector2d(10, 10), gfx::Size(32, 32));
  };
  show();
  auto* preview = strip.GetViewByID(kSidebarFavoritesDragPreviewViewId);
  ASSERT_TRUE(preview);
  AppliedViewsObservers source;
  const auto owner = source.ObservePreview(*preview, *row, 31, 31);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kPreviewApplied});
  ASSERT_TRUE(snapshot);
  const AppliedConditions conditions{{.owner = owner,
                                      .expected = snapshot->value,
                                      .coordinates = snapshot->coordinates}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting(show)));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_GT(result.diagnostics.conditions[0].last->serials.apply_serial,
            snapshot->serials.apply_serial);
  auto hidden = std::get<PreviewApplied>(snapshot->value);
  hidden.visible = false;
  hidden.animation_running = false;
  const AppliedConditions hide{{.owner = owner,
                                .expected = hidden,
                                .coordinates = snapshot->coordinates}};
  const auto next = session.BeginRequest(Intent(hide), hide, Options());
  ASSERT_FALSE(session.Dispatch(next, base::BindLambdaForTesting([&] {
                                  HideFavoritesDragPreview(strip);
                                })));
  const auto hidden_result = session.WaitForApplied(next, hide);
  EXPECT_EQ(AppliedResultKind::kApplied, hidden_result.kind)
      << hidden_result.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ViewHierarchyScrollAndLogicalFocus) {
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* scroll = parent->AddChildView(std::make_unique<views::ScrollView>());
  base::ScopedClosureRunner remove(
      base::BindLambdaForTesting([&] { parent->RemoveChildViewT(scroll); }));
  scroll->SetBounds(0, 0, 100, 100);
  auto contents = std::make_unique<views::View>();
  contents->SetBounds(0, 0, 500, 500);
  auto* focus = contents->AddChildView(std::make_unique<views::View>());
  focus->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  focus->SetBounds(0, 50, 50, 30);
  scroll->SetContents(std::move(contents));
  // 고정 크기 scroll fixture를 구성하는 layout 자극이다.
  views::test::RunScheduledLayout(scroll);
  AppliedViewsObservers source;
  const auto scroll_owner = source.ObserveScrollView(*scroll);
  const auto focus_owner = source.ObserveView(*focus);
  auto state = std::get<ViewStateMatches>(
      source.Read({scroll_owner, AppliedConditionKind::kViewStateMatches})
          ->value);
  EXPECT_FALSE(state.parent.is_empty());
  EXPECT_FALSE(state.children.empty());
  state.scroll_offset = gfx::Vector2d(0, 50);
  AppliedCondition focused{focus_owner,
                           FocusMatches{.target = focus_owner, .focused = true},
                           AppliedFreshness::kCurrentAfterDispatch};
  focused.coordinates = GetAppliedViewCoordinates(*focus);
  const AppliedConditions conditions{
      {.owner = scroll_owner,
       .expected = state,
       .coordinates = GetAppliedViewCoordinates(
           *scroll, AppliedCoordinates::Space::kParent)},
      focused};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  focus->GetFocusManager()->SetFocusedView(
                                      focus);
                                  scroll->ScrollToOffset(gfx::PointF(0, 50));
                                })));
  EXPECT_EQ(gfx::PointF(0, 50), scroll->CurrentOffset());
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       CaptureReleaseAfterAckWakesCurrentState) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  widget->SetCapture(widget->GetRootView());
  base::ScopedClosureRunner cleanup(
      base::BindLambdaForTesting([&] { widget->ReleaseCapture(); }));
  ASSERT_TRUE(widget->HasCapture());
  AppliedViewsObservers source;
  const auto owner = source.ObserveWidget(*widget);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kWidgetStateMatches});
  auto value = std::get<WidgetStateMatches>(snapshot->value);
  value.capture = false;
  const AppliedConditions conditions{{.owner = owner,
                                      .expected = value,
                                      .coordinates = snapshot->coordinates}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.DispatchAsync(
      ticket, base::BindLambdaForTesting([&](DispatchDone done) {
        std::move(done).Run(AppliedDispatchStatus::kAccepted);
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] {
              widget->ReleaseCapture();
              source.NotifyCaptureChanged(*widget);
            }));
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_GE(result.diagnostics.evaluation_count, 2u);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       FrameGeometryBounceAndAnimationRangeRearm) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  auto reduced =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* host = parent->AddChildView(CreatePageViewportHostTree());
  base::ScopedClosureRunner remove(
      base::BindLambdaForTesting([&] { parent->RemoveChildViewT(host); }));
  Apply(*host);
  auto* decoration = host->AddChildView(std::make_unique<views::View>());
  ASSERT_TRUE(UsesHorizontalTabGroupCapsule(*decoration));
  SetHorizontalTabGroupCapsuleCollapsed(*decoration, true);
  AppliedViewsObservers source;
  const auto page = PageCondition(source, *host);
  const auto animation_owner = source.ObserveCapsule(*decoration, 812);
  AppliedCondition animation{animation_owner,
                             AnimationMatches{812, false, 0, 1},
                             AppliedFreshness::kCurrentAfterDispatch};
  animation.coordinates = GetAppliedViewCoordinates(*decoration);
  const auto frame =
      source.ObservePresentedAfterApply(*host, {page, animation});
  const AppliedConditions conditions{page, animation, frame};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        Apply(*host);
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] {
              const auto bounds = host->bounds();
              host->SetBounds(0, 0, bounds.width() + 17, bounds.height());
              host->SetBoundsRect(bounds);
            }));
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  // 무관한 compositor commit·feedback이 같은 page apply를 재발행하면 안 된다.
  int page_wakes = 0;
  auto page_subscription =
      source.Subscribe({{page.owner, AppliedConditionKind::kPageHostsApplied}},
                       base::BindLambdaForTesting(
                           [&](const AppliedSignalSample&) { ++page_wakes; }));
  auto current_page = page;
  current_page.freshness = AppliedFreshness::kCurrentAfterDispatch;
  const auto next_frame =
      source.ObservePresentedAfterApply(*host, {current_page});
  const AppliedConditions frame_conditions{next_frame};
  const auto frame_ticket =
      session.BeginRequest(Intent(frame_conditions, AppliedDomain::kFrame),
                           frame_conditions, Options());
  ASSERT_FALSE(session.Dispatch(frame_ticket, base::DoNothing()));
  const auto presented = session.WaitForApplied(frame_ticket, frame_conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, presented.kind)
      << presented.ToString();
  EXPECT_EQ(0, page_wakes);
  // 다른 조건이 바뀐 같은 표본에서는 고정된 geometry도 순간 AND에 참여한다.
  auto observed_page = current_page;
  observed_page.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  observed_page.observation_group = 813;
  AppliedCondition observed_motion{
      animation_owner, AnimationMatches{812, true, .1, .9},
      AppliedFreshness::kNewObservation,
      AppliedEvaluationMode::kObservedSinceDispatch, 813};
  observed_motion.coordinates = GetAppliedViewCoordinates(*decoration);
  const AppliedConditions composite{observed_page, observed_motion};
  const auto composite_ticket =
      session.BeginRequest(Intent(composite), composite, Options());
  ASSERT_FALSE(session.Dispatch(
      composite_ticket, base::BindLambdaForTesting([&] {
        SetHorizontalTabGroupCapsuleCollapsed(*decoration, false);
      })));
  const auto observed = session.WaitForApplied(composite_ticket, composite);
  ASSERT_EQ(AppliedResultKind::kApplied, observed.kind) << observed.ToString();
  ASSERT_TRUE(observed.diagnostics.conditions[0].observed);
  ASSERT_TRUE(observed.diagnostics.conditions[1].observed);
  EXPECT_EQ(observed.diagnostics.conditions[0].observed->sample_id,
            observed.diagnostics.conditions[1].observed->sample_id);
  EXPECT_EQ(0, page_wakes);
}

class AppliedLayoutDelegate
    : public TabCollectionAnimatingLayoutManager::Delegate {
 public:
  ~AppliedLayoutDelegate() override = default;
};

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       UnifiedAndLegacyAnimationSourcesUseNativeManagers) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  auto reduced =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* unified_host = parent->AddChildView(std::make_unique<views::View>());
  unified_host->SetBounds(0, 0, 400, 100);
  auto* child = unified_host->AddChildView(std::make_unique<views::View>());
  child->SetPreferredSize(gfx::Size(80, 40));
  AppliedLayoutDelegate delegate;
  TabCollectionAnimatingLayoutManager::AnimationCoordinator coordinator(
      *unified_host, base::Milliseconds(300), gfx::Tween::LINEAR, 0.5);
  coordinator.SetView(nullptr);
  auto* manager = unified_host->SetLayoutManager(
      std::make_unique<TabCollectionAnimatingLayoutManager>(
          std::make_unique<views::FlexLayout>(), delegate, coordinator,
          TabCollectionAnimatingLayoutManager::AnimationAxis::kHorizontal));
  base::ScopedClosureRunner remove(base::BindLambdaForTesting(
      [&] { parent->RemoveChildViewT(unified_host); }));
  views::test::RunScheduledLayout(unified_host);
  ASSERT_TRUE(coordinator.container());
  gfx::AnimationContainerTestApi initial_clock(coordinator.container());
  initial_clock.IncrementTime(base::Seconds(1));
  ASSERT_FALSE(manager->is_animating());
  AppliedBrowserObservers source;
  const auto owner =
      source.ObserveUnifiedAnimation(*unified_host, *manager, 813);
  AppliedCondition progress{owner, AnimationMatches{813, true, 0.05, 0.95}};
  progress.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  progress.coordinates = GetAppliedViewCoordinates(*unified_host);
  ASSERT_TRUE(source.Supports(progress));
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({progress}), {progress}, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        child->SetPreferredSize(gfx::Size(220, 40));
        views::test::RunScheduledLayout(unified_host);
        ASSERT_TRUE(manager->is_animating());
        ASSERT_TRUE(coordinator.container());
        gfx::AnimationContainerTestApi clock(coordinator.container());
        clock.IncrementTime(base::Milliseconds(150));
        EXPECT_NEAR(0.5, coordinator.GetCurrentProgress(), 0.01);
        clock.IncrementTime(base::Milliseconds(300));
        ASSERT_FALSE(manager->is_animating());
      })));
  const auto result = session.WaitForApplied(ticket, {progress});
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_TRUE(std::get<AnimationMatches>(
                  result.diagnostics.conditions[0].observed->value)
                  .running);
  auto idle = progress;
  idle.mode = AppliedEvaluationMode::kCurrent;
  idle.expected = AnimationMatches{813, false, 1, 1};
  idle.freshness = AppliedFreshness::kCurrentAfterDispatch;
  const auto finish = session.BeginRequest(Intent({idle}), {idle}, Options());
  ASSERT_FALSE(session.Dispatch(finish, base::DoNothing()));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(finish, {idle}).kind);
  unified_host->SetLayoutManager(nullptr);
  EXPECT_FALSE(source.IsOwnerAlive(owner));
  EXPECT_FALSE(source.Read({owner, AppliedConditionKind::kAnimationMatches}));
  views::View legacy_host;
  legacy_host.SetBounds(0, 0, 200, 100);
  auto* legacy = legacy_host.SetLayoutManager(
      std::make_unique<views::AnimatingLayoutManager>());
  legacy->SetTargetLayoutManager(std::make_unique<views::FlexLayout>());
  legacy->EnableAnimationForTesting();
  const auto legacy_owner =
      source.views().ObserveLayoutAnimation(legacy_host, *legacy, 814);
  AppliedCondition legacy_idle{legacy_owner, AnimationMatches{814, false, 0, 1},
                               AppliedFreshness::kCurrentAfterDispatch};
  legacy_idle.coordinates = GetAppliedViewCoordinates(legacy_host);
  EXPECT_TRUE(source.Supports(legacy_idle));
  auto unsupported = legacy_idle;
  unsupported.expected = AnimationMatches{814, true, 0.2, 0.8};
  const auto rejected =
      session.BeginRequest(Intent({unsupported}), {unsupported}, Options());
  EXPECT_EQ(AppliedResultKind::kUnsupportedCondition,
            session.WaitForApplied(rejected, {unsupported}).kind);
  const auto current =
      session.BeginRequest(Intent({legacy_idle}), {legacy_idle}, Options());
  ASSERT_FALSE(session.Dispatch(current, base::DoNothing()));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(current, {legacy_idle}).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       LayoutManagerRemovalAndReplacementRetireOwner) {
  for (const bool replace : {false, true}) {
    SCOPED_TRACE(replace);
    views::View host;
    AppliedViewsObservers source;
    auto* manager = host.SetLayoutManager(
        std::make_unique<views::AnimatingLayoutManager>());
    manager->SetTargetLayoutManager(std::make_unique<views::FlexLayout>());
    const auto owner = source.ObserveLayoutAnimation(host, *manager, 815);
    AppliedCondition running{owner, AnimationMatches{815, true, 0, 1}};
    running.coordinates = GetAppliedViewCoordinates(host);
    AppliedWaitSession session(source);
    const auto ticket =
        session.BeginRequest(Intent({running}), {running}, Options());
    ASSERT_FALSE(session.Dispatch(
        ticket, base::BindLambdaForTesting([&] {
          source.WillChangeLayoutManager(host);
          if (replace) {
            host.SetLayoutManager(
                std::make_unique<views::AnimatingLayoutManager>());
          } else {
            host.SetLayoutManager(nullptr);
          }
        })));
    EXPECT_TRUE(source.IsOwnerAlive(source.ObserveView(host)));
    EXPECT_FALSE(source.IsOwnerAlive(owner));
    EXPECT_FALSE(source.Read({owner, AppliedConditionKind::kAnimationMatches}));
    const auto result = session.WaitForApplied(ticket, {running});
    EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, result.kind)
        << result.ToString();
    host.InvalidateLayout();
    if (replace) {
      auto* replacement =
          static_cast<views::AnimatingLayoutManager*>(host.GetLayoutManager());
      replacement->SetTargetLayoutManager(
          std::make_unique<views::FlexLayout>());
      const auto next = source.ObserveLayoutAnimation(host, *replacement, 815);
      EXPECT_NE(owner, next);
      EXPECT_TRUE(source.Read({next, AppliedConditionKind::kAnimationMatches}));
    }
  }
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       LegacyAnimationSameTaskTransitionIsObserved) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  views::View host;
  host.SetBounds(0, 0, 200, 100);
  auto* manager =
      host.SetLayoutManager(std::make_unique<views::AnimatingLayoutManager>());
  manager->SetTargetLayoutManager(std::make_unique<views::FlexLayout>());
  manager->EnableAnimationForTesting();
  views::test::RunScheduledLayout(&host);
  ASSERT_FALSE(manager->is_animating());
  AppliedViewsObservers source;
  const auto owner = source.ObserveLayoutAnimation(host, *manager, 816);
  AppliedCondition observed{owner, AnimationMatches{816, true, 0, 1}};
  observed.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  observed.coordinates = GetAppliedViewCoordinates(host);
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({observed}), {observed}, Options());
  // parent·compositor가 없어 같은 task 안의 시작과 종료 외에는 깨움이 없다.
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  auto child = std::make_unique<views::View>();
                                  child->SetPreferredSize(gfx::Size(40, 20));
                                  host.AddChildView(std::move(child));
                                  ASSERT_TRUE(manager->is_animating());
                                  manager->ResetLayout();
                                  ASSERT_FALSE(manager->is_animating());
                                })));
  const auto result = session.WaitForApplied(ticket, {observed});
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_TRUE(std::get<AnimationMatches>(
                  result.diagnostics.conditions[0].observed->value)
                  .running);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       LayoutManagerRemovalAfterEvaluationWakesWait) {
  views::View host;
  auto* manager =
      host.SetLayoutManager(std::make_unique<views::AnimatingLayoutManager>());
  manager->SetTargetLayoutManager(std::make_unique<views::FlexLayout>());
  AppliedViewsObservers source;
  const auto owner = source.ObserveLayoutAnimation(host, *manager, 817);
  AppliedCondition running{owner, AnimationMatches{817, true, 0, 1}};
  running.coordinates = GetAppliedViewCoordinates(host);
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({running}), {running}, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::DoNothing()));
  // 초기 평가가 끝난 뒤 다른 View·compositor 알림 없이 제거한다.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        source.WillChangeLayoutManager(host);
        host.SetLayoutManager(nullptr);
      }));
  const auto result = session.WaitForApplied(ticket, {running});
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, result.kind)
      << result.ToString();
  EXPECT_FALSE(source.IsOwnerAlive(owner));
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ClosingWidgetReportsOwnerDestroyed) {
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.bounds = gfx::Rect(0, 0, 100, 100);
  auto widget = std::make_unique<views::Widget>();
  widget->Init(std::move(params));
  widget->Close();
  ASSERT_TRUE(widget->IsClosed());
  AppliedViewsObservers source;
  for (const bool native : {false, true}) {
    SCOPED_TRACE(native);
    const auto owner = native ? source.ObserveNativeWindow(*widget)
                              : source.ObserveWidget(*widget);
    EXPECT_FALSE(source.IsOwnerAlive(owner));
    AppliedCondition condition{owner, WidgetStateMatches{.closed = true}};
    if (native) {
      condition.expected = NativeWindowMatches{};
    }
    condition.coordinates = GetAppliedViewCoordinates(
        *widget->GetRootView(), AppliedCoordinates::Space::kScreen);
    EXPECT_TRUE(source.Supports(condition));
    AppliedWaitSession session(source);
    const auto ticket =
        session.BeginRequest(Intent({condition}), {condition}, Options());
    // 이미 폐기된 owner는 시작 시점에 종료되므로 dispatch 없이 결과를 받는다.
    base::test::TestFuture<AppliedWaitResult> completed;
    auto observation =
        session.ObserveApplied(ticket, {condition}, completed.GetCallback());
    EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, completed.Take().kind);
  }
  widget.reset();
}

// 실제 controller를 시작하면 native 창 hit-test를 하므로 C로 분류한다.
// 관측 read 전후 hierarchy·bounds·pending flag와 재진입 알림의 불변을 본다.
IN_PROC_BROWSER_TEST_F(YeeAppliedWaitDragInteractiveTest,
                       DragReadsDoNotCreateSlotsOrReenterHierarchy) {
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto* model = browser()->GetTabStripModel();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  const auto group = model->AddToNewGroup({1, 2});
  model->ActivateTabAt(0);
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  if (!state->ShouldDisplayVerticalTabs())
    state->SetVerticalTabsEnabled(true);
  AppliedTestContext<AppliedBrowserObservers> ready;
  ASSERT_TRUE(PrepareNativeDragInput(ready, *browser()));
  views::test::RunScheduledLayout(browser_view);
  auto* root = browser_view->vertical_tab_strip_region_view_for_testing()
                   ->root_node_for_testing();
  const auto handle = model->GetTabAtIndex(0)->GetHandle();
  auto* node = root->GetNodeForHandle(handle);
  ASSERT_TRUE(node);
  auto* context = browser_view->tab_strip_view()->GetDragContext();
  auto* handler = static_cast<TabDragHandlerImpl*>(context);
  ui::MouseEvent press(
      ui::EventType::kMousePressed,
      gfx::PointF(node->view()->GetLocalBounds().CenterPoint()),
      gfx::PointF(node->view()->GetLocalBounds().CenterPoint()),
      base::TimeTicks::Now(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON);
  handler->InitializeDrag(
      *node, model->selection_model().GetListSelectionModel(), press);
  base::ScopedClosureRunner cancel(base::BindLambdaForTesting(
      [&] { handler->EndDrag(EndDragReason::kCancel); }));
  auto* controller = context->GetDragController();
  ASSERT_TRUE(controller);
  auto destination = node->view()->GetBoundsInScreen().CenterPoint() +
                     gfx::Vector2d(0, kSidebarMetrics.tab_row_height);
  views::View::ConvertPointFromScreen(node->view(), &destination);
  ui::MouseEvent move(ui::EventType::kMouseDragged, gfx::PointF(destination),
                      gfx::PointF(destination), base::TimeTicks::Now(),
                      ui::EF_LEFT_MOUSE_BUTTON, 0);
  ASSERT_TRUE(handler->ContinueDrag(*node->view(), move));
  controller = context->GetDragController();
  ASSERT_TRUE(controller);
  ASSERT_EQ(context, controller->attached_context());
  ASSERT_TRUE(controller->started_drag());

  AppliedBrowserObservers source;
  DragAppliedObserver drag(source, *context, *browser(), handle.raw_value(), 95,
                           1);
  const auto phase = drag.ObservePhase(DragPhaseMatches::Phase::kStarted);
  auto* strip = browser_view->vertical_tab_strip_region_view_for_testing()
                    ->GetTabStripView();
  const auto preview = drag.ObserveVerticalPreview(*strip);
  auto* preview_view = strip->GetViewByID(kSidebarFavoritesDragPreviewViewId);
  ASSERT_TRUE(preview_view);
  const auto aligned = source.views().ObservePreview(
      *preview_view, *root->GetNodeForHandle(handle)->view(),
      handle.raw_value(), handle.raw_value());
  auto* group_view =
      root->GetNodeForHandle(
              model->group_model()->GetTabGroup(group)->GetCollectionHandle())
          ->view();
  const auto group_owner = drag.ObserveGroupDrag(*group_view);
  const std::vector<AppliedSignalKey> keys{
      {phase, AppliedConditionKind::kDragPhaseMatches},
      {preview, AppliedConditionKind::kPreviewApplied},
      {aligned, AppliedConditionKind::kPreviewApplied},
      {group_owner, AppliedConditionKind::kViewStateMatches}};
  int notifications = 0;
  auto subscription = source.Subscribe(
      keys, base::BindLambdaForTesting(
                [&](const AppliedSignalSample&) { ++notifications; }));
  const auto before = SnapshotProbeViewTree(*browser_view);
  const auto outside = browser_view->GetBoundsInScreen().bottom_right() +
                       gfx::Vector2d(100, 100);
  for (const auto& data : controller->GetSessionData().tab_drag_data_)
    ASSERT_FALSE(data.attached_view->GetBoundsInScreen().Contains(outside));
  drag.InputAck(base::BindOnce([](AppliedDispatchStatus status) {
                  EXPECT_EQ(AppliedDispatchStatus::kAccepted, status);
                }),
                outside)
      .Run();
  EXPECT_EQ(before, SnapshotProbeViewTree(*browser_view));
  const int after_ack = notifications;
  for (int i = 0; i < 3; ++i) {
    for (const auto& key : keys)
      ASSERT_TRUE(source.Read(key));
    EXPECT_EQ(before, SnapshotProbeViewTree(*browser_view));
    EXPECT_EQ(after_ack, notifications);
  }
  const auto value = std::get<DragPhaseMatches>(source.Read(keys[0])->value);
  EXPECT_EQ(0u, value.target_handle);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       HorizontalReadsKeepPendingLayoutAndCapsuleState) {
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto* model = browser()->GetTabStripModel();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  const auto group = model->AddToNewGroup({0});
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  if (state->ShouldDisplayVerticalTabs())
    state->SetVerticalTabsEnabled(false);
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::test::RunScheduledLayout(browser_view);
  auto* strip = browser_view->horizontal_tab_strip_for_testing();
  ASSERT_TRUE(strip);
  auto* slot = strip->tab_at(0);
  auto* decoration =
      strip->tab_container_for_testing()->GetGroupViews(group)->underline();
  AppliedBrowserObservers source;
  const auto handle = model->GetTabAtIndex(0)->GetHandle();
  DragAppliedObserver drag(source, *strip->GetDragContext(), *browser(),
                           handle.raw_value(), 96, 1);
  const auto preview = drag.ObserveHorizontalPreview(*strip, *slot);
  const auto theme = drag.ObserveCapsuleTheme(*decoration);
  slot->InvalidateLayout();
  ASSERT_TRUE(slot->needs_layout());
  const auto before = SnapshotProbeViewTree(*browser_view);
  for (int i = 0; i < 3; ++i) {
    const auto snapshot =
        source.Read({preview, AppliedConditionKind::kPreviewApplied});
    ASSERT_TRUE(snapshot);
    const auto& value = std::get<PreviewApplied>(snapshot->value);
    EXPECT_FALSE(value.visible);
    EXPECT_TRUE(value.geometry_applied);
    ASSERT_TRUE(
        source.Read({theme, AppliedConditionKind::kPresentationMatches}));
    EXPECT_EQ(before, SnapshotProbeViewTree(*browser_view));
  }
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       DragLoopEndDoesNotCarryAcrossSteps) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* context = browser_view->tab_strip_view()->GetDragContext();
  ASSERT_TRUE(context);
  auto* widget = context->GetWidget();
  ASSERT_TRUE(widget);
  AppliedBrowserObservers source;
  const auto handle = browser()->tab_strip_model()->GetActiveTab()->GetHandle();
  DragAppliedObserver drag(source, *context, *browser(), handle.raw_value(), 93,
                           1);
  const auto owner = drag.ObservePhase(DragPhaseMatches::Phase::kLoopEnded);
  AppliedWaitSession session(source);
  const gfx::Point screen(100, 100);
  auto loop_done = DragAppliedObserverTestApi::ArmLoopCallback(drag);
  base::ScopedClosureRunner finish_widget_drag(base::BindLambdaForTesting(
      [&] { widget->OnNativeWidgetUserDragEnded(); }));
  widget->OnNativeWidgetUserDragStarted();
  for (uint64_t step : {2, 3, 4}) {
    SCOPED_TRACE(step);
    drag.BeginStep(step);
    AppliedCondition ended{
        owner, DragPhaseMatches{
                   .session = 93,
                   .step = step,
                   .source_handle = static_cast<uint64_t>(handle.raw_value()),
                   .phase = DragPhaseMatches::Phase::kLoopEnded,
                   .input_acknowledged = true,
                   .last_input_screen = screen}};
    ended.coordinates =
        GetAppliedViewCoordinates(*context, AppliedCoordinates::Space::kScreen);
    const auto ticket = session.BeginRequest(
        Intent({ended}, AppliedDomain::kDrag), {ended}, Options());
    base::test::TestFuture<AppliedWaitResult> completed;
    auto observation =
        session.ObserveApplied(ticket, {ended}, completed.GetCallback());
    ASSERT_FALSE(session.DispatchAsync(
        ticket, base::BindLambdaForTesting([&](DispatchDone done) {
          drag.InputAck(std::move(done), screen).Run();
        })));
    base::test::TestFuture<void> evaluated;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, evaluated.GetCallback());
    ASSERT_TRUE(evaluated.Wait());
    EXPECT_FALSE(completed.IsReady());
    EXPECT_NE(DragPhaseMatches::Phase::kLoopEnded,
              std::get<DragPhaseMatches>(
                  source.Read({owner, AppliedConditionKind::kDragPhaseMatches})
                      ->value)
                  .phase);
    if (step > 2) {
      loop_done = DragAppliedObserverTestApi::ArmLoopCallback(drag);
      // 이전 loop의 Widget 알림이 새 callback의 종료를 대신하지 못한다.
      widget->OnNativeWidgetUserDragEnded();
      EXPECT_NE(
          DragPhaseMatches::Phase::kLoopEnded,
          std::get<DragPhaseMatches>(
              source.Read({owner, AppliedConditionKind::kDragPhaseMatches})
                  ->value)
              .phase);
      widget->OnNativeWidgetUserDragStarted();
    }
    std::move(loop_done).Run();
    const auto result = completed.Take();
    EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  }
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       DragLoopCompletionSurvivesContextDestruction) {
  auto* other = CreateBrowser(browser()->GetProfile());
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(other);
  auto* context = browser_view->tab_strip_view()->GetDragContext();
  ASSERT_TRUE(context);
  const auto handle = other->tab_strip_model()->GetActiveTab()->GetHandle();
  AppliedBrowserObservers source;
  DragAppliedObserver drag(source, *context, *other, handle.raw_value(), 94, 1);
  const auto owner = drag.ObservePhase(DragPhaseMatches::Phase::kLoopEnded);
  auto loop_done = DragAppliedObserverTestApi::ArmLoopCallback(drag);
  drag.BeginStep(2);
  const gfx::Point screen(100, 100);
  AppliedCondition ended{
      owner, DragPhaseMatches{
                 .session = 94,
                 .step = 2,
                 .source_handle = static_cast<uint64_t>(handle.raw_value()),
                 .phase = DragPhaseMatches::Phase::kLoopEnded,
                 .input_acknowledged = true,
                 .last_input_screen = screen}};
  ended.coordinates =
      GetAppliedViewCoordinates(*context, AppliedCoordinates::Space::kScreen);
  AppliedWaitSession session(source);
  const auto ticket = session.BeginRequest(
      Intent({ended}, AppliedDomain::kDrag), {ended}, Options());
  views::ViewTracker context_lifetime(context);
  base::test::TestFuture<AppliedWaitResult> completed;
  auto observation =
      session.ObserveApplied(ticket, {ended}, completed.GetCallback());
  ASSERT_FALSE(session.DispatchAsync(
      ticket, base::BindLambdaForTesting([&](DispatchDone done) {
        drag.InputAck(std::move(done), screen).Run();
      })));

  // native loop가 반환하고 once callback만 남은 수명을 검사한다.
  // macOS 닫기 helper의 autorelease pool 재활용은 본문에서 수행한다.
  CloseBrowserSynchronously(other);
  EXPECT_FALSE(context_lifetime.view());
  base::test::TestFuture<void> evaluated;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, evaluated.GetCallback());
  ASSERT_TRUE(evaluated.Wait());
  EXPECT_FALSE(completed.IsReady());

  const auto pending =
      source.Read({owner, AppliedConditionKind::kDragPhaseMatches});
  ASSERT_TRUE(pending);
  EXPECT_NE(DragPhaseMatches::Phase::kLoopEnded,
            std::get<DragPhaseMatches>(pending->value).phase);
  EXPECT_EQ(ended.coordinates, pending->coordinates);
  std::move(loop_done).Run();
  const auto result = completed.Take();
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kDragPhaseMatches});
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(ended.coordinates, snapshot->coordinates);
  EXPECT_EQ(DragPhaseMatches::Phase::kLoopEnded,
            std::get<DragPhaseMatches>(snapshot->value).phase);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       PresentedFrameAndSameGeometryReapply) {
  auto* parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* host = parent->AddChildView(CreatePageViewportHostTree());
  base::ScopedClosureRunner remove(
      base::BindLambdaForTesting([&] { parent->RemoveChildViewT(host); }));
  Apply(*host);
  AppliedViewsObservers source;
  const auto page = PageCondition(source, *host);
  const auto frame = source.ObservePresentedAfterApply(*host, {page});
  const AppliedConditions conditions{page, frame};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] { Apply(*host); })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_TRUE(std::get<PresentedAfterApply>(
                  result.diagnostics.conditions[1].last->value)
                  .successful);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       CompositorSwapInvalidatesOldFeedback) {
  auto* old_parent = BrowserView::GetBrowserViewForBrowser(browser());
  auto* other_browser = CreateBrowser(browser()->GetProfile());
  auto* new_parent = BrowserView::GetBrowserViewForBrowser(other_browser);
  auto* host = old_parent->AddChildView(CreatePageViewportHostTree());
  Apply(*host);
  AppliedViewsObservers source;
  const auto page = PageCondition(source, *host);
  const auto old_frame = source.ObservePresentedAfterApply(*host, {page});
  const AppliedConditions conditions{page, old_frame};
  AppliedWaitSession session(source);
  const auto ticket = session.BeginRequest(Intent(conditions), conditions,
                                           Options(base::Milliseconds(150)));
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        Apply(*host);
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] {
              new_parent->AddChildView(old_parent->RemoveChildViewT(host));
            }));
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind) << result.ToString();
  const auto new_frame = source.ObservePresentedAfterApply(*host, {page});
  EXPECT_NE(std::get<PresentedAfterApply>(old_frame.expected).compositor,
            std::get<PresentedAfterApply>(new_frame.expected).compositor);
  const AppliedConditions rebound{page, new_frame};
  const auto next = session.BeginRequest(Intent(rebound), rebound, Options());
  ASSERT_FALSE(session.Dispatch(
      next, base::BindLambdaForTesting([&] { Apply(*host); })));
  const auto applied = session.WaitForApplied(next, rebound);
  EXPECT_EQ(AppliedResultKind::kApplied, applied.kind) << applied.ToString();
  new_parent->RemoveChildViewT(host);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       KombuchaTerminalFailureAbortsImmediately) {
  views::View view;
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(view);
  auto expected = std::get<ViewStateMatches>(
      source.Read({owner, AppliedConditionKind::kViewStateMatches})->value);
  expected.bounds = gfx::Rect(1, 1, 30, 20);
  const AppliedConditions conditions{{owner, expected}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kApplied);
  base::test::TestFuture<const ui::InteractionSequence::AbortedData&> aborted;
  private_test_impl().set_aborted_callback_for_testing(aborted.GetCallback());
  const auto started = base::TimeTicks::Now();
  EXPECT_FALSE(
      RunTestSequence(ArmApplied(kApplied, session, ticket, conditions),
                      Do([&] { ASSERT_FALSE(session.Cancel(ticket)); }),
                      WaitForApplied(kApplied)));
  EXPECT_TRUE(aborted.IsReady());
  EXPECT_LT(base::TimeTicks::Now() - started, TestTimeouts::action_timeout());
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       KombuchaFailureBeforeArmAbortsAtArm) {
  views::View view;
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(view);
  const AppliedConditions conditions{{owner, ViewStateMatches{}}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kEarlyFailure);
  DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kEarlierAsyncDone);
  base::test::TestFuture<const ui::InteractionSequence::AbortedData&> aborted;
  private_test_impl().set_aborted_callback_for_testing(aborted.GetCallback());
  bool continued = false;
  EXPECT_FALSE(RunTestSequence(
      Do([&] {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] {
              ASSERT_FALSE(session.Cancel(ticket));
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, base::BindLambdaForTesting([&] {
                    views::ElementTrackerViews::GetInstance()
                        ->NotifyCustomEvent(
                            kEarlierAsyncDone,
                            BrowserView::GetBrowserViewForBrowser(browser()));
                  }));
            }));
      }),
      WaitForEvent(kBrowserViewElementId, kEarlierAsyncDone),
      ArmApplied(kEarlyFailure, session, ticket, conditions),
      Do([&] { continued = true; })));
  EXPECT_TRUE(aborted.IsReady());
  EXPECT_FALSE(continued);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitInteractiveTest,
                       ExternalKombuchaAbortReleasesRequestAndIdentifier) {
  views::View view;
  AppliedViewsObservers source;
  const auto owner = source.ObserveView(view);
  const auto initial =
      source.Read({owner, AppliedConditionKind::kViewStateMatches});
  auto expected = std::get<ViewStateMatches>(initial->value);
  expected.bounds = gfx::Rect(4, 5, 30, 20);
  AppliedCondition condition{.owner = owner,
                             .expected = expected,
                             .coordinates = initial->coordinates,
                             .match = AppliedMatch::kViewBounds};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({condition}), {condition}, Options());
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kAbortApplied);
  base::test::TestFuture<const ui::InteractionSequence::AbortedData&> aborted;
  private_test_impl().set_aborted_callback_for_testing(aborted.GetCallback());
  EXPECT_FALSE(RunTestSequence(
      ArmApplied(kAbortApplied, session, ticket, {condition}),
      Do([&] { ASSERT_FALSE(session.Dispatch(ticket, base::DoNothing())); }),
      Check([] { return false; })));
  ASSERT_TRUE(aborted.IsReady());
  const auto before = session.GetDiagnostics(ticket);
  view.SetBoundsRect(expected.bounds);
  base::test::TestFuture<void> barrier;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, barrier.GetCallback());
  ASSERT_TRUE(barrier.Wait());
  EXPECT_EQ(before->wake_count, session.GetDiagnostics(ticket)->wake_count);
  EXPECT_EQ(before->evaluation_count,
            session.GetDiagnostics(ticket)->evaluation_count);
  condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
  const auto next =
      session.BeginRequest(Intent({condition}), {condition}, Options());
  EXPECT_TRUE(RunTestSequence(
      ArmApplied(kAbortApplied, session, next, {condition}),
      Do([&] { ASSERT_FALSE(session.Dispatch(next, base::DoNothing())); }),
      WaitForApplied(kAbortApplied)));
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitDragInteractiveTest,
                       VerticalEmptyCreationCallbackUsesRealInputAck) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* context = browser_view->tab_strip_view()->GetDragContext();
  ASSERT_TRUE(context);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  auto* tab_view = context->GetTabForContents(tab->GetContents());
  ASSERT_TRUE(tab_view);
  const auto origin = tab_view->GetBoundsInScreen().CenterPoint();
  const auto destination = origin + gfx::Vector2d(25, 0);
  const auto native_window = context->GetWidget()->GetNativeWindow();
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(origin, native_window));
  auto allow_input = std::make_shared<bool>(true);
  base::ScopedClosureRunner release(base::BindOnce(
      [](std::shared_ptr<bool> allow_input, gfx::NativeWindow native_window) {
        *allow_input = false;
        // observer와 fixture가 살아 있을 때 실제 release ACK까지 받는다.
        EXPECT_TRUE(ui_test_utils::SendMouseEventsSync(
            ui_controls::LEFT, ui_controls::UP, native_window));
      },
      allow_input, native_window));
  bool creation_callback = false;
  context->SetDragControllerCallbackForTesting(base::BindLambdaForTesting(
      [&](TabDragController*) { creation_callback = true; }));
  AppliedBrowserObservers source;
  DragAppliedObserver drag(source, *context, *browser(),
                           tab->GetHandle().raw_value(), 91, 1);
  const auto owner = drag.ObservePhase(DragPhaseMatches::Phase::kStarted);
  DragPhaseMatches expected{
      .session = 91,
      .step = 1,
      .source_handle = static_cast<uint64_t>(tab->GetHandle().raw_value()),
      .target_handle = static_cast<uint64_t>(tab->GetHandle().raw_value()),
      .phase = DragPhaseMatches::Phase::kStarted,
      .attached_browser = source.BrowserToken(*browser()),
      .input_acknowledged = true,
      .last_input_screen = destination};
  const AppliedConditions conditions{
      {.owner = owner,
       .expected = expected,
       .coordinates = GetAppliedViewCoordinates(
           *context, AppliedCoordinates::Space::kScreen)}};
  AppliedWaitSession session(source);
  auto options = Options();
  options.tier = WaitOptions::Tier::kC;
  const auto ticket = session.BeginRequest(
      Intent(conditions, AppliedDomain::kDrag), conditions, options);
  ASSERT_FALSE(session.DispatchAsync(
      ticket, base::BindLambdaForTesting([&](DispatchDone done) {
        // 생성부터 관측해야 하므로 press도 arm 이후 보낸다. 위치로 창을
        // 다시 추측하지 않고 전용 모니터의 실제 대상 창에 입력한다.
        auto [accepted, rejected] = base::SplitOnceCallback(std::move(done));
        auto [press_rejected, move_rejected] =
            base::SplitOnceCallback(std::move(rejected));
        const bool pressed = ui_controls::SendMouseEventsNotifyWhenDone(
            ui_controls::LEFT, ui_controls::DOWN,
            base::BindLambdaForTesting(
                [allow_input, native_window, destination,
                 rejected = std::move(move_rejected),
                 ack = drag.InputAck(std::move(accepted),
                                     destination)]() mutable {
                  if (!*allow_input) {
                    std::move(rejected).Run(AppliedDispatchStatus::kRejected);
                    return;
                  }
                  const bool moved = ui_controls::SendMouseMoveNotifyWhenDone(
                      destination.x(), destination.y(), std::move(ack),
                      native_window);
                  if (!moved)
                    std::move(rejected).Run(AppliedDispatchStatus::kRejected);
                  ASSERT_TRUE(moved);
                }),
            0, native_window);
        if (!pressed)
          std::move(press_rejected).Run(AppliedDispatchStatus::kRejected);
        ASSERT_TRUE(pressed);
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_FALSE(creation_callback);
  release.RunAndReset();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitDragInteractiveTest,
                       ObservedStartedSurvivesReleaseBeforeInputAck) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* context = browser_view->tab_strip_view()->GetDragContext();
  ASSERT_TRUE(context);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  auto* row = context->GetTabForContents(tab->GetContents());
  ASSERT_TRUE(row);
  const auto origin = row->GetBoundsInScreen().CenterPoint();
  const auto destination = origin + gfx::Vector2d(25, 0);
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(origin));
  auto allow_input = std::make_shared<bool>(true);
  base::ScopedClosureRunner cleanup(base::BindOnce(
      [](std::shared_ptr<bool> allow_input) {
        *allow_input = false;
        ui_controls::SendMouseEvents(ui_controls::LEFT, ui_controls::UP);
      },
      allow_input));
  AppliedBrowserObservers source;
  DragAppliedObserver drag(source, *context, *browser(),
                           tab->GetHandle().raw_value(), 92, 1);
  const auto started_owner =
      drag.ObservePhase(DragPhaseMatches::Phase::kStarted);
  const auto inactive_owner =
      drag.ObservePhase(DragPhaseMatches::Phase::kInactive);
  DragPhaseMatches started{
      .session = 92,
      .step = 1,
      .source_handle = static_cast<uint64_t>(tab->GetHandle().raw_value()),
      .phase = DragPhaseMatches::Phase::kStarted,
      .attached_browser = source.BrowserToken(*browser())};
  auto inactive = started;
  inactive.phase = DragPhaseMatches::Phase::kInactive;
  inactive.attached_browser = {};
  inactive.input_acknowledged = true;
  inactive.last_input_screen = destination;
  const auto coordinates =
      GetAppliedViewCoordinates(*context, AppliedCoordinates::Space::kScreen);
  const AppliedConditions conditions{
      {.owner = started_owner,
       .expected = started,
       .mode = AppliedEvaluationMode::kObservedSinceDispatch,
       .coordinates = coordinates},
      {.owner = inactive_owner,
       .expected = inactive,
       .coordinates = coordinates}};
  AppliedWaitSession session(source);
  auto options = Options();
  options.tier = WaitOptions::Tier::kC;
  const auto ticket = session.BeginRequest(
      Intent(conditions, AppliedDomain::kDrag), conditions, options);
  ASSERT_FALSE(session.DispatchAsync(
      ticket, base::BindLambdaForTesting([&](DispatchDone done) {
        auto pending =
            std::make_shared<std::optional<DispatchDone>>(std::move(done));
        auto finish =
            base::BindLambdaForTesting([pending](AppliedDispatchStatus status) {
              if (*pending) {
                auto done = std::move(**pending);
                pending->reset();
                std::move(done).Run(status);
              }
            });
        auto ack = drag.InputAck(base::BindOnce(finish), destination);
        auto up = base::BindLambdaForTesting(
            [allow_input, finish, ack = std::move(ack)]() mutable {
              if (!*allow_input) {
                finish.Run(AppliedDispatchStatus::kRejected);
                return;
              }
              if (!ui_controls::SendMouseEventsNotifyWhenDone(
                      ui_controls::LEFT, ui_controls::UP, std::move(ack)))
                finish.Run(AppliedDispatchStatus::kRejected);
            });
        auto move = base::BindLambdaForTesting(
            [allow_input, destination, finish, up = std::move(up)]() mutable {
              if (!*allow_input) {
                finish.Run(AppliedDispatchStatus::kRejected);
                return;
              }
              if (!ui_controls::SendMouseMoveNotifyWhenDone(
                      destination.x(), destination.y(), std::move(up)))
                finish.Run(AppliedDispatchStatus::kRejected);
            });
        if (!ui_controls::SendMouseEventsNotifyWhenDone(
                ui_controls::LEFT, ui_controls::DOWN, std::move(move)))
          finish.Run(AppliedDispatchStatus::kRejected);
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_FALSE(std::get<DragPhaseMatches>(
                   result.diagnostics.conditions[0].observed->value)
                   .input_acknowledged);
  EXPECT_FALSE(TabDragController::IsActive());
}

}  // namespace
}  // namespace yee::test
