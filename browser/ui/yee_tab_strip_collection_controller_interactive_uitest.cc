// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"

#include <memory>
#include <string_view>
#include <utility>

#include "base/cancelable_callback.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/task/current_thread.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/metrics/histogram_tester.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_deletion_dialog_controller.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/base_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/hovercard/tab_hover_card_bubble_view.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/test/vertical_tabs_interactive_test_mixin.h"
#include "chrome/browser/ui/views/yee/applied_wait_kombucha.h"
#include "chrome/browser/ui/views/yee/drag_interactive_test_support.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/tab_group.h"
#include "content/public/test/browser_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "url/url_constants.h"

namespace {

const char kFirstTabName[] = "FirstTab";
const char kSecondTabName[] = "SecondTab";
using namespace yee::test;
using AppliedBase = AppliedWaitInteractiveTestMixin<InteractiveBrowserTest>;

class YeeTabStripCollectionControllerInteractiveUiTest
    : public VerticalTabsInteractiveTestMixin<AppliedBase>,
      public testing::WithParamInterface<TabStripOrientation> {
 public:
  YeeTabStripCollectionControllerInteractiveUiTest() = default;
  ~YeeTabStripCollectionControllerInteractiveUiTest() override = default;

  TabStripOrientation orientation() const { return GetParam(); }
  bool is_horizontal() const {
    return orientation() == TabStripOrientation::kHorizontal;
  }

  void SetUpOnMainThread() override {
    if (!vertical_tab_strip_state_controller()->ShouldDisplayVerticalTabs()) {
      vertical_tab_strip_state_controller()->SetVerticalTabsEnabled(true);
      RunScheduledLayouts();
    }
    AppliedBase::SetUpOnMainThread();
    if (is_horizontal()) {
      vertical_tab_strip_state_controller()->SetVerticalTabsEnabled(false);
      RunScheduledLayouts();
    }
  }
  void TearDownOnMainThread() override {
    drag_.reset();
    applied_.Stop();
    AppliedBase::TearDownOnMainThread();
  }

  const std::vector<base::test::FeatureRefAndParams> GetEnabledFeatures()
      override {
    auto enabled =
        VerticalTabsInteractiveTestMixin<AppliedBase>::GetEnabledFeatures();
    enabled.push_back({tabs::kTabStripUnification, {}});
    return enabled;
  }

  TabStripView* GetTabStripView() {
    auto* base_region = views::AsViewClass<BaseTabStripRegionView>(
        BrowserView::GetBrowserViewForBrowser(browser())->tab_strip_view());
    return base_region ? views::AsViewClass<TabStripView>(
                             base_region->GetTabStripView())
                       : nullptr;
  }

  views::View* GetTabViewAt(int tab_index) {
    TabStripModel* model = browser()->tab_strip_model();
    CHECK(model->ContainsIndex(tab_index));
    tabs::TabInterface* tab = model->GetTabAtIndex(tab_index);
    CHECK(root_node());
    views::View* view = root_node()->GetNodeForHandle(tab->GetHandle())->view();
    CHECK(view);
    return view;
  }

  void ShowContextMenuForTabAt(int tab_index) {
    auto* tab = views::AsViewClass<TabView>(GetTabViewAt(tab_index));
    CHECK(tab);
    static_cast<views::ContextMenuController*>(tab)->ShowContextMenuForView(
        tab, tab->GetBoundsInScreen().CenterPoint(),
        ui::mojom::MenuSourceType::kMouse);
  }

  bool CheckMenuHasStringId(int message_id) {
    ui::SimpleMenuModel* menu_model = vertical_tab_strip_controller()
                                          ->GetTabContextMenuController()
                                          ->GetMenuModel();
    for (size_t i = 0; i < menu_model->GetItemCount(); i++) {
      if (l10n_util::GetStringUTF16(message_id) == menu_model->GetLabelAt(i)) {
        return true;
      }
    }
    return false;
  }

  AppliedConditions CollectionReady() {
    auto* view = BrowserView::GetBrowserViewForBrowser(browser());
    AppliedConditions conditions{
        applied_.Condition(
            applied_.source().ObserveTabCollectionAnimations(*view, 1),
            AnimationMatches{1, false, 1, 1}),
        applied_.Condition(
            applied_.source().ObserveBrowserAnimation(
                *view, *BrowserAnimationController::From(browser()),
                TabStripAnimations::kVerticalTabStrip,
                TabStripAnimations::kTabStripWidth, 2),
            AnimationMatches{2, false, 1, 1})};
    for (int i = 0; i < browser()->GetTabStripModel()->count(); ++i) {
      auto& tab = *GetTabViewAt(i);
      conditions.push_back(
          applied_.View(tab, {.drawn = true}, AppliedMatch::kViewDrawn));
      conditions.push_back(applied_.View(tab, {.bounds = gfx::Rect(0, 0, 1, 1)},
                                         AppliedMatch::kViewNonEmpty));
      conditions.push_back(
          applied_.View(tab, {}, AppliedMatch::kViewCenterVisible));
    }
    return conditions;
  }
  MultiStep AwaitCollection() {
    return ApplyAndWait(
        applied_, [this] { return CollectionReady(); }, [] {},
        AppliedDomain::kLayout, true);
  }
  MultiStep DragTo(int source_index,
                   ElementSpecifier target,
                   RelativePositionCallback position) {
    return Steps(
        Do([this, source_index] {
          ASSERT_TRUE(PrepareNativeDragInput(applied_, *browser()));
          source_handle_ = browser()
                               ->GetTabStripModel()
                               ->GetTabAtIndex(source_index)
                               ->GetHandle()
                               .raw_value();
          drag_ = std::make_unique<DragAppliedObserver>(
              applied_.source(),
              *vertical_tab_strip_controller()
                   ->GetDragHandler()
                   .GetDragContext(),
              *browser(), source_handle_, ++session_, 1);
        }),
        DragInput(Steps(DragMouseTo(target, std::move(position), false)),
                  DragPhaseMatches::Phase::kAttached));
  }
  MultiStep ReleaseDrag() {
    return DragInput(Steps(ReleaseMouse()), DragPhaseMatches::Phase::kInactive);
  }
  MultiStep DragInput(MultiStep input, DragPhaseMatches::Phase phase) {
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kCollectionDrag);
    struct Request {
      AppliedConditions conditions;
      AppliedTicket ticket;
      DispatchDone done;
    };
    auto request = std::make_shared<Request>();
    return Steps(Do([this, phase, request] {
                   drag_->BeginStep(++step_);
                   request->conditions = {applied_.Condition(
                       drag_->ObservePhase(phase),
                       DragPhaseMatches{
                           .session = session_,
                           .step = step_,
                           .source_handle = source_handle_,
                           .phase = phase,
                           .attached_browser =
                               phase == DragPhaseMatches::Phase::kAttached
                                   ? applied_.source().BrowserToken(*browser())
                                   : AppliedOwnerToken{}},
                       AppliedMatch::kDragPhase)};
                   request->ticket = applied_.Begin(request->conditions,
                                                    AppliedDomain::kDrag);
                 }),
                 ArmApplied(
                     kCollectionDrag, applied_.session(),
                     [request] { return request->ticket; },
                     [request] { return request->conditions; }),
                 Do([this, request] {
                   ASSERT_FALSE(applied_.session().DispatchAsync(
                       request->ticket,
                       base::BindLambdaForTesting([request](DispatchDone done) {
                         request->done = std::move(done);
                       })));
                 }),
                 std::move(input), Do([this, request] {
                   drag_->InputAckAtCursor(std::move(request->done)).Run();
                 }),
                 WaitForApplied(kCollectionDrag));
  }
  AppliedTestContext<AppliedBrowserObservers> applied_;
  std::unique_ptr<DragAppliedObserver> drag_;
  uint64_t source_handle_ = 0;
  uint64_t session_ = 0;
  uint64_t step_ = 1;
};

IN_PROC_BROWSER_TEST_P(YeeTabStripCollectionControllerInteractiveUiTest,
                       YeeClosingTabCanReattachWithoutModel) {
  gfx::ScopedAnimationDurationScaleMode animation_mode(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, false);
  views::test::RunScheduledLayout(
      BrowserView::GetBrowserViewForBrowser(browser()));
  auto* const model = browser()->GetTabStripModel();
  auto* const tab = views::AsViewClass<TabView>(GetTabViewAt(0));
  ASSERT_TRUE(tab);
  views::ViewTracker closing(tab);
  model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  ASSERT_EQ(1, model->count());
  ASSERT_TRUE(closing.view());
  ASSERT_TRUE(tab->IsClosing());
  auto* const parent = tab->parent();
  ASSERT_TRUE(parent);
  auto detached = parent->RemoveChildViewT(tab);
  auto* const root = BrowserView::GetBrowserViewForBrowser(browser())
                         ->GetWidget()
                         ->GetRootView();
  auto* const host = root->AddChildView(std::make_unique<views::View>());
  auto* const reattached = host->AddChildView(std::move(detached));
  EXPECT_EQ(tab, reattached);
  EXPECT_TRUE(tab->IsClosing());
  EXPECT_FALSE(tab->IsActive());
  EXPECT_EQ(nullptr, tab->GetTabInterface());
  EXPECT_EQ(1, model->count());
  host->RemoveChildViewT(tab);
  root->RemoveChildViewT(host);
  EXPECT_EQ(nullptr, closing.view());
}

IN_PROC_BROWSER_TEST_P(YeeTabStripCollectionControllerInteractiveUiTest,
                       YeeDragPreviewFollowsStripOrientation) {
  auto animation_mode_reset = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  auto* model = browser()->GetTabStripModel();
  tabs::TabInterface* ordinary_source = nullptr;
  tabs::TabInterface* pinned_source = nullptr;
  TabDragContext* original_context = nullptr;
  gfx::Point last_intended;
#if BUILDFLAG(IS_WIN)
  ui_test_utils::ScopedNativeInputTrace native_trace(
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget());
  const auto record_input = [&](std::string_view phase,
                                const gfx::Point& intended) {
    native_trace.Snapshot(phase, intended);
    RecordProperty("YeeCollectionNativeInput", native_trace.ToJson());
  };
#else
  const auto record_input = [](std::string_view, const gfx::Point&) {};
#endif
  const auto traced_center =
      [&](const char* phase) -> RelativePositionCallback {
    return base::BindLambdaForTesting([&, phase](ui::TrackedElement* element) {
      last_intended = element->GetScreenBounds().CenterPoint();
      record_input(phase, last_intended);
      return last_intended;
    });
  };
  bool observed_unready_layout = false;
  const auto drag_layout_ready = [&] {
    bool ready = !BrowserAnimationController::From(browser())->IsAnimating(
        TabStripAnimations::kVerticalTabStrip);
    if (model->count() != 4) {
      observed_unready_layout = true;
      return false;
    }
    for (int index = 0; index < model->count(); ++index) {
      auto* tab = GetTabViewAt(index);
      ready &=
          tab->IsDrawn() && !tab->GetLocalBounds().IsEmpty() &&
          tab->GetVisibleBounds().Contains(tab->GetLocalBounds().CenterPoint());
      for (auto* ancestor = tab; ancestor; ancestor = ancestor->parent()) {
        ready &=
            !TabCollectionAnimatingLayoutManager::IsViewAnimating(ancestor);
      }
    }
    observed_unready_layout |= !ready;
    return ready;
  };
  const auto center_hits_tab = [&](int index) {
    auto* tab = GetTabViewAt(index);
    auto* root = tab->GetWidget()->GetRootView();
    auto point = tab->GetBoundsInScreen().CenterPoint();
    views::View::ConvertPointFromScreen(root, &point);
    return tab->Contains(root->GetEventHandlerForPoint(point));
  };
  const auto expect_drag_presentation = [&](tabs::TabInterface* source,
                                            bool pinned, int source_index) {
    auto* strip = GetTabStripView();
    ASSERT_TRUE(strip);
    auto* preview = strip->GetViewByID(yee::kSidebarFavoritesDragPreviewViewId);
    EXPECT_EQ(!is_horizontal(), preview && preview->GetVisible());
    auto* controller = vertical_tab_strip_controller();
    ASSERT_TRUE(controller);
    EXPECT_EQ(!is_horizontal() && pinned,
              controller->GetDragHandler().UseFavoritesSlotPreview());
    auto* source_view = GetTabViewAt(source_index);
    ASSERT_TRUE(source_view->layer());
    EXPECT_EQ(is_horizontal() ? 1.0f : 0.0f, source_view->layer()->opacity());
    EXPECT_EQ(pinned, source->IsPinned());
  };
  const auto check_drag_presentation = [&](tabs::TabInterface* source,
                                           bool pinned, const char* phase) {
    const int source_index = model->GetIndexOfTab(source);
    const bool attached = TabDragController::IsAttachedTo(original_context);
    RecordProperty(std::string(phase) + "OriginalIndex", source_index);
    RecordProperty(std::string(phase) + "OriginalModelCount", model->count());
    RecordProperty(std::string(phase) + "OriginalAttached", attached ? 1 : 0);
    record_input(phase, last_intended);
    // 같은 콜백 안에서 원래 소유권과 index를 확인한 뒤 view를 조회한다.
    // false는 InteractionSequence를 중단시켜 잡고 있는 입력도 정리한다.
    if (!attached || !model->ContainsIndex(source_index)) {
      return false;
    }
    expect_drag_presentation(source, pinned, source_index);
    return !testing::Test::HasFatalFailure();
  };
  RunTestSequence(
      WaitForShow(kTabStripRegionElementId), Do([&] {
        for (int index = 1; index < 4; ++index) {
          chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                           /*foreground=*/false);
        }
        model->SetTabPinned(0, true);
        model->SetTabPinned(1, true);
        pinned_source = model->GetTabAtIndex(0);
        ordinary_source = model->GetTabAtIndex(3);
        model->ActivateTabAt(3);
        views::test::RunScheduledLayout(
            BrowserView::GetBrowserViewForBrowser(browser()));
        ASSERT_TRUE(GetTabStripView());
        ASSERT_TRUE(vertical_tab_strip_controller());
        original_context =
            vertical_tab_strip_controller()->GetDragHandler().GetDragContext();
        ASSERT_TRUE(original_context);
        EXPECT_EQ(is_horizontal(), root_node()->orientation() ==
                                       TabStripOrientation::kHorizontal);
        EXPECT_EQ(!is_horizontal(),
                  GetTabStripView()->GetViewByID(
                      yee::kSidebarFavoritesDockViewId) != nullptr);
      }),
      NameView(kFirstTabName,
               base::BindLambdaForTesting([&] { return GetTabViewAt(3); })),
      NameView(kSecondTabName,
               base::BindLambdaForTesting([&] { return GetTabViewAt(2); })),
      // 방향 전환 때 생성된 manager는 기존 애니메이션 시간을 유지할 수 있다.
      // 실제 배치와 입력 대상을 확인한 뒤 화면 좌표를 잡는다.
      Do([&] { drag_layout_ready(); }), AwaitCollection(), Do([&] {
        views::test::RunScheduledLayout(
            BrowserView::GetBrowserViewForBrowser(browser()));
        RecordProperty("YeeOrdinaryDragSourceBounds",
                       GetTabViewAt(3)->GetBoundsInScreen().ToString());
        RecordProperty("YeeOrdinaryDragTargetBounds",
                       GetTabViewAt(2)->GetBoundsInScreen().ToString());
      }),
      CheckResult([&] { return center_hits_tab(3); }, true),
      CheckResult([&] { return center_hits_tab(2); }, true),
      MoveMouseTo(kFirstTabName, traced_center("ordinary-move-request")),
      DragTo(3, kSecondTabName, traced_center("ordinary-drag-request")),
      CheckResult(
          [&] {
            return check_drag_presentation(ordinary_source, false, "ordinary");
          },
          true),
      ReleaseDrag(), Do([&] {
        EXPECT_EQ(2, model->IndexOfFirstNonPinnedTab());
        EXPECT_FALSE(ordinary_source->IsPinned());
        EXPECT_EQ(2, model->GetIndexOfTab(ordinary_source));
      }),
      NameView("PinnedDragSource",
               base::BindLambdaForTesting([&] { return GetTabViewAt(0); })),
      NameView("PinnedDragTarget",
               base::BindLambdaForTesting([&] { return GetTabViewAt(1); })),
      Do([&] { drag_layout_ready(); }), AwaitCollection(), Do([&] {
        views::test::RunScheduledLayout(
            BrowserView::GetBrowserViewForBrowser(browser()));
        RecordProperty("YeePinnedDragSourceBounds",
                       GetTabViewAt(0)->GetBoundsInScreen().ToString());
        RecordProperty("YeePinnedDragTargetBounds",
                       GetTabViewAt(1)->GetBoundsInScreen().ToString());
      }),
      CheckResult([&] { return center_hits_tab(0); }, true),
      CheckResult([&] { return center_hits_tab(1); }, true),
      MoveMouseTo("PinnedDragSource", traced_center("pinned-move-request")),
      DragTo(0, "PinnedDragTarget", traced_center("pinned-drag-request")),
      CheckResult(
          [&] {
            return check_drag_presentation(pinned_source, true, "pinned");
          },
          true),
      ReleaseDrag(), Do([&] {
        EXPECT_EQ(2, model->IndexOfFirstNonPinnedTab());
        EXPECT_TRUE(pinned_source->IsPinned());
        EXPECT_EQ(1, model->GetIndexOfTab(pinned_source));
      }));
  RecordProperty("YeeDragObservedUnreadyLayout",
                 observed_unready_layout ? 1 : 0);
}

IN_PROC_BROWSER_TEST_P(YeeTabStripCollectionControllerInteractiveUiTest,
                       YeeFavoriteContextMenuHonorsCapacityAndAllowsUnpin) {
  if (is_horizontal()) {
    GTEST_SKIP() << "Yee Favorites are the expanded vertical Sidebar dock.";
  }
  auto animation_mode_reset = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  constexpr int kTogglePinned = TabStripModel::CommandTogglePinned;
  const auto inspect_context_menu = [&](int tab_index, bool can_pin) {
    base::RunLoop menu_checked(base::RunLoop::Type::kNestableTasksAllowed);
    bool inspected = false;
    base::CancelableOnceClosure inspect(base::BindLambdaForTesting([&]() {
      auto* const menu =
          vertical_tab_strip_controller()->GetTabContextMenuController();
      base::ScopedClosureRunner close_menu(base::BindLambdaForTesting([&]() {
        if (menu) {
          menu->CloseMenu();
        }
        menu_checked.Quit();
      }));
      ASSERT_TRUE(menu);
      EXPECT_TRUE(CheckMenuHasStringId(IDS_TAB_CXMENU_DUPLICATE));
      EXPECT_EQ(can_pin, menu->IsCommandIdEnabled(kTogglePinned));
      menu->ExecuteCommand(kTogglePinned, /*event_flags=*/0);
      inspected = true;
    }));
#if BUILDFLAG(IS_MAC)
    // native 메뉴의 tracking loop 안에서 검증과 닫기 작업을 실행한다.
    base::CurrentThread::ScopedAllowApplicationTasksInNativeNestedLoop allow;
#endif
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, inspect.callback());
    ShowContextMenuForTabAt(tab_index);
    menu_checked.Run();
    EXPECT_TRUE(inspected);
  };
  RunTestSequence(
      WaitForShow(kTabStripRegionElementId), Do([this]() {
        TabStripModel* model = browser()->tab_strip_model();
        for (int index = 1; index <= yee::kSidebarMetrics.favorites_max_count;
             ++index) {
          chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                           /*foreground=*/false);
        }
        for (int index = 0; index < yee::kSidebarMetrics.favorites_max_count;
             ++index) {
          model->SetTabPinned(index, true);
        }
        model->ActivateTabAt(
            yee::kSidebarMetrics.favorites_max_count,
            TabStripUserGestureDetails(
                TabStripUserGestureDetails::GestureType::kOther));
        views::test::RunScheduledLayout(
            BrowserView::GetBrowserViewForBrowser(browser()));
      }),
      NameView(kSecondTabName, base::BindLambdaForTesting([this]() {
                 return GetTabViewAt(yee::kSidebarMetrics.favorites_max_count);
               })),
      WaitForShow(kSecondTabName), Do([&]() {
        inspect_context_menu(yee::kSidebarMetrics.favorites_max_count, false);
      }),
      Check([this]() {
        TabStripModel* model = browser()->tab_strip_model();
        return model->IndexOfFirstNonPinnedTab() ==
                   yee::kSidebarMetrics.favorites_max_count &&
               !model->IsTabPinned(yee::kSidebarMetrics.favorites_max_count);
      }),
      NameView(kFirstTabName, base::BindLambdaForTesting(
                                  [this]() { return GetTabViewAt(0); })),
      WaitForShow(kFirstTabName), Do([&]() { inspect_context_menu(0, true); }),
      Check([this]() {
        return browser()->tab_strip_model()->IndexOfFirstNonPinnedTab() ==
               yee::kSidebarMetrics.favorites_max_count - 1;
      }));
}

IN_PROC_BROWSER_TEST_P(YeeTabStripCollectionControllerInteractiveUiTest,
                       YeePinnedSplitBorderFollowsStripOrientation) {
  auto animation_mode_reset = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  RunTestSequence(
      WaitForShow(kTabStripRegionElementId), Do([&] {
        auto* model = browser()->GetTabStripModel();
        chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1,
                         /*foreground=*/false);
        model->SetTabPinned(0, true);
        model->SetTabPinned(1, true);
        model->ActivateTabAt(0);
        model->AddToNewSplit({1}, split_tabs::SplitTabVisualData(),
                             split_tabs::SplitTabCreatedSource::kToolbarButton);
        views::test::RunScheduledLayout(
            BrowserView::GetBrowserViewForBrowser(browser()));
        auto* split =
            views::AsViewClass<SplitTabView>(GetTabViewAt(0)->parent());
        ASSERT_TRUE(split);
        EXPECT_TRUE(model->IsTabPinned(0));
        EXPECT_TRUE(model->IsTabPinned(1));
        EXPECT_EQ(is_horizontal(), split->GetBorder() != nullptr);
      }));
}

}  // namespace

INSTANTIATE_TEST_SUITE_P(
    All,
    YeeTabStripCollectionControllerInteractiveUiTest,
    testing::Values(TabStripOrientation::kVertical,
                    TabStripOrientation::kHorizontal),
    [](const testing::TestParamInfo<TabStripOrientation>& info) {
      switch (info.param) {
        case TabStripOrientation::kVertical:
          return "Vertical";
        case TabStripOrientation::kHorizontal:
          return "Horizontal";
      }
    });
