// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller_interactive_uitest.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller_interactive_test_mixin.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_group_underline.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/drag_interactive_test_support.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/base/hit_test.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/compositor.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/canvas.h"
#include "ui/native_theme/native_theme.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include "ui/base/ui_base_features.h"
#endif
#if BUILDFLAG(IS_CHROMEOS)
#include "ui/events/gesture_detection/gesture_configuration.h"
#endif

namespace {
using namespace yee::test;
using test::GetTabStripForBrowser;
using test::IDString;
using ui_test_utils::GetCenterInScreenCoordinates;

// native 생성/닫힘 callback에서는 입력을 기다리지 않는다. 두 번 post하여
// VisibilityWaiter와 창 파괴 stack을 빠져나온 뒤 release/attach를 실행한다.
class DragBrowserChangeObserver : public BrowserCollectionObserver {
 public:
  explicit DragBrowserChangeObserver(
      base::OnceCallback<void(BrowserWindowInterface*)> callback)
      : callback_(std::move(callback)) {
    observation_.Observe(GlobalBrowserCollection::GetInstance());
  }
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    if (!callback_ || pending_)
      return;
    pending_ = browser;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::OnceClosure action) {
              base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, std::move(action));
            },
            base::BindOnce(&DragBrowserChangeObserver::RunPending,
                           weak_.GetWeakPtr())));
  }
  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    if (pending_ == browser)
      pending_ = nullptr;
  }

 private:
  void RunPending() {
    if (pending_ && callback_) {
      auto* browser = pending_.get();
      pending_ = nullptr;
      std::move(callback_).Run(browser);
    }
  }
  raw_ptr<BrowserWindowInterface> pending_ = nullptr;
  base::OnceCallback<void(BrowserWindowInterface*)> callback_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
  base::WeakPtrFactory<DragBrowserChangeObserver> weak_{this};
};

gfx::Point GetLeftCenterInScreenCoordinates(const views::View* view) {
  auto point = view->GetLocalBounds().CenterPoint();
  point.set_x(point.x() - view->width() / 4);
  views::View::ConvertPointToScreen(view, &point);
  return point;
}
gfx::Point GetRightCenterInScreenCoordinates(const views::View* view) {
  auto point = view->GetLocalBounds().CenterPoint();
  point.set_x(point.x() + view->width() / 4);
  views::View::ConvertPointToScreen(view, &point);
  return point;
}

class YeeDetachToBrowserTabDragControllerTest
    : public TabDragControllerInteractiveTestMixin<TabDragControllerTest>,
      public testing::WithParamInterface<testing::tuple<bool, const char*>> {
 public:
  using Input = TabDragControllerInteractiveTestMixin<TabDragControllerTest>;
  using Phase = DragPhaseMatches::Phase;
  using Submit = std::function<bool(base::OnceClosure)>;
  YeeDetachToBrowserTabDragControllerTest() {
    std::vector<base::test::FeatureRefAndParams> enabled;
    std::vector<base::test::FeatureRef> disabled;
#if BUILDFLAG(IS_WIN)
    disabled.push_back(features::kCalculateNativeWinOcclusion);
#endif
    if (std::get<0>(GetParam()))
      enabled.push_back({features::kTearOffWebAppTabOpensWebAppWindow, {}});
    features_.InitWithFeaturesAndParameters(enabled, disabled);
  }
  void SetUpOnMainThread() override {
    TabDragControllerTest::SetUpOnMainThread();
    if (auto* state = tabs::VerticalTabStripStateController::From(browser());
        state && state->ShouldDisplayVerticalTabs())
      state->SetVerticalTabsEnabled(false);
#if BUILDFLAG(IS_WIN)
    BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
#endif
#if BUILDFLAG(IS_CHROMEOS)
    ui::GestureConfiguration::GetInstance()->set_min_fling_velocity(
        std::numeric_limits<float>::max());
#endif
#if BUILDFLAG(IS_MAC)
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
#endif
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kSplitViewDragAndDropEnabled, false);
  }
  void TearDownOnMainThread() override {
    browser_change_.reset();
    drag_.reset();
    applied_.Stop();
    TabDragControllerTest::TearDownOnMainThread();
  }
  InputSource input_source() override {
    return std::string_view(std::get<1>(GetParam())).find("mouse") !=
                   std::string_view::npos
               ? InputSource::INPUT_SOURCE_MOUSE
               : InputSource::INPUT_SOURCE_TOUCH;
  }

 protected:
  void StopAnimating(TabStrip* strip) {
    TabDragControllerTest::StopAnimating(strip);
#if BUILDFLAG(IS_WIN)
    // 모드 전환 뒤 Header 예약 폭도 적용한 다음 입력 좌표를 읽는다.
    strip->GetWidget()->LayoutRootViewIfNecessary();
    TabDragControllerTest::StopAnimating(strip);
#endif
  }
  bool BeginPress(TabSlotView& slot) {
    const auto ready = PrepareNativeDragInput(applied_, *browser());
    if (!ready) {
      ADD_FAILURE() << ready.message();
      return false;
    }
    auto* strip = GetTabStripForBrowser(browser());
    auto* model = browser()->GetTabStripModel();
    int index = model->active_index();
    if (slot.GetTabSlotViewType() == TabSlotView::ViewType::kTabGroupHeader) {
      CHECK(slot.group());
      index =
          model->group_model()->GetTabGroup(*slot.group())->ListTabs().start();
    } else {
      for (int i = 0; i < model->count(); ++i) {
        if (strip->tab_at(i) == &slot)
          index = i;
      }
    }
    source_handle_ = model->GetTabAtIndex(index)->GetHandle().raw_value();
    drag_ = std::make_unique<DragAppliedObserver>(
        applied_.source(), *strip->GetDragContext(), *browser(), source_handle_,
        ++session_, 1);
    step_ = 1;
    preview_ = drag_->ObserveHorizontalPreview(*strip, slot);
    drag_->ObserveHorizontalControllerCreation();
    // 선택 guard는 원래 창 내부에 붙어 있어야 한다. 뜻밖의 detach도 실제
    // release로 native loop를 끝낸 뒤 phase/model 조건으로 실패시킨다.
    browser_change_ =
        std::make_unique<DragBrowserChangeObserver>(base::BindOnce(
            &YeeDetachToBrowserTabDragControllerTest::ReleaseDetachedWindow,
            weak_.GetWeakPtr(), session_));
    return true;
  }
  void ReleaseDetachedWindow(uint64_t session,
                             BrowserWindowInterface* detached) {
    if (session != session_ || !drag_)
      return;
    auto* model = detached->GetTabStripModel();
    bool owns_source = false;
    for (int i = 0; i < model->count(); ++i)
      owns_source |= static_cast<uint64_t>(
                         model->GetTabAtIndex(i)->GetHandle().raw_value()) ==
                     source_handle_;
    if (!owns_source)
      return;
    auto* strip = GetTabStripForBrowser(detached);
    if (strip)
      drag_->AddContext(*strip->GetDragContext(), *detached);
    if (input_source() == InputSource::INPUT_SOURCE_MOUSE) {
      EXPECT_TRUE(
          ui_controls::SendMouseEvents(ui_controls::LEFT, ui_controls::UP));
    }
#if BUILDFLAG(IS_CHROMEOS)
    else {
      EXPECT_TRUE(
          ui_controls::SendTouchEvents(ui_controls::kTouchRelease, 0, 0, 0));
    }
#endif
  }
  bool ApplyNative(std::optional<Phase> phase,
                   gfx::Point screen,
                   Submit submit,
                   AppliedConditions conditions = {},
                   bool first = false) {
    CHECK(drag_);
    if (!first)
      drag_->BeginStep(++step_);
    TabModelMatches count;
    count.tabs.resize(browser()->GetTabStripModel()->count());
    conditions.push_back(applied_.Condition(
        applied_.source().ObserveTabModel(*browser()->GetTabStripModel()),
        count, AppliedMatch::kTabCount));
    conditions.push_back(applied_.Condition(
        drag_->ObservePhase(phase.value_or(Phase::kInactive)),
        DragPhaseMatches{
            .session = session_,
            .step = step_,
            .source_handle = source_handle_,
            .phase = phase.value_or(Phase::kInactive),
            .attached_browser = phase == Phase::kAttached
                                    ? applied_.source().BrowserToken(*browser())
                                    : AppliedOwnerToken{},
            .last_input_screen = screen},
        phase ? AppliedMatch::kDragPhase
              : AppliedMatch::kDragInputAcknowledged));
    if (phase) {
      conditions.push_back(applied_.Condition(
          preview_,
          PreviewApplied{.source_handle = source_handle_,
                         .visible = phase == Phase::kAttached},
          AppliedMatch::kPreviewSettled));
    }
    const auto ticket = applied_.Begin(conditions, AppliedDomain::kDrag);
    const auto error = applied_.session().DispatchAsync(
        ticket, base::BindLambdaForTesting([&](DispatchDone done) {
          auto [ack, reject] = base::SplitOnceCallback(std::move(done));
          auto input_ack = input_source() == InputSource::INPUT_SOURCE_MOUSE
                               ? drag_->InputAckAtCursor(std::move(ack))
                               : drag_->InputAck(std::move(ack), screen);
          if (!submit(std::move(input_ack)))
            std::move(reject).Run(AppliedDispatchStatus::kRejected);
        }));
    if (error) {
      ADD_FAILURE() << error->ToString();
      return false;
    }
    const auto result = applied_.Wait(ticket, conditions);
    if (!result)
      ADD_FAILURE() << result.message();
    last_input_ = screen;
    return bool(result);
  }
  // 이전 input mixin처럼 첫 누름은 ACK까지 기다린다. focus 확보/버블 닫기는
  // controller를 만들지 않을 수 있고, 실제 이동은 kAttached를 계속 요구한다.
  bool PressInputAtCenter(const views::View* view, int id = 0) {
    if (!BeginPress(
            *const_cast<TabSlotView*>(static_cast<const TabSlotView*>(view))))
      return false;
    const auto location = GetCenterInScreenCoordinates(view);
    const auto hint = GetWindowHint(view);
    return ApplyNative(
        std::nullopt, location,
        [&, location, hint, id](base::OnceClosure done) {
          const bool accepted = Input::PressInput(location, hint, id);
          if (accepted)
            std::move(done).Run();
          return accepted;
        },
        {}, true);
  }
  bool PressInput(const gfx::Point& location,
                  gfx::NativeWindow hint,
                  int id = 0) {
    auto* strip = GetTabStripForBrowser(browser());
    TabSlotView* source = nullptr;
    for (int i = 0; i < strip->GetTabCount(); ++i) {
      if (strip->tab_at(i)->GetBoundsInScreen().Contains(location))
        source = strip->tab_at(i);
    }
    CHECK(source);
    if (!BeginPress(*source))
      return false;
    return ApplyNative(
        std::nullopt, location,
        [&, location, hint, id](base::OnceClosure done) {
          const bool accepted = Input::PressInput(location, hint, id);
          if (accepted)
            std::move(done).Run();
          return accepted;
        },
        {}, true);
  }
  bool DragInputTo(const gfx::Point& location,
                   gfx::NativeWindow hint,
                   AppliedConditions conditions = {}) {
    return ApplyNative(
        Phase::kAttached, location,
        [this, location, hint](base::OnceClosure done) {
          if (input_source() == InputSource::INPUT_SOURCE_MOUSE)
            return ui_controls::SendMouseMoveNotifyWhenDone(
                location.x(), location.y(), std::move(done), hint);
#if BUILDFLAG(IS_CHROMEOS)
          return ui_controls::SendTouchEventsNotifyWhenDone(
              ui_controls::kTouchMove, 0, location.x(), location.y(),
              std::move(done));
#else
          NOTREACHED();
#endif
        },
        std::move(conditions));
  }
  bool DragInputToCenter(const views::View* view,
                         gfx::Vector2d offset = {},
                         AppliedConditions conditions = {}) {
    return DragInputTo(GetCenterInScreenCoordinates(view) + offset,
                       GetWindowHint(view), std::move(conditions));
  }
  bool ReleaseInput(int id = 0) {
    return ApplyNative(
        Phase::kInactive, last_input_, [this, id](base::OnceClosure done) {
          static_cast<void>(id);
          if (input_source() == InputSource::INPUT_SOURCE_MOUSE)
            return ui_controls::SendMouseEventsNotifyWhenDone(
                ui_controls::LEFT, ui_controls::UP, std::move(done));
#if BUILDFLAG(IS_CHROMEOS)
          return ui_controls::SendTouchEventsNotifyWhenDone(
              ui_controls::kTouchRelease, id, 0, 0, std::move(done));
#else
        NOTREACHED();
#endif
        });
  }
  bool EscapeInput() {
    return ApplyNative(
        Phase::kInactive, last_input_, [this](base::OnceClosure done) {
          return ui_controls::SendKeyPressNotifyWhenDone(
              browser()->GetWindow()->GetNativeWindow(), ui::VKEY_ESCAPE, false,
              false, false, false, std::move(done));
        });
  }
  void EnsureFocusToTabStrip(TabStrip* strip) {
    ASSERT_TRUE(PressInputAtCenter(strip->tab_at(0)));
    ASSERT_TRUE(ReleaseInput());
  }
  AppliedConditions PositionConditions(uint64_t handle,
                                       int position,
                                       tab_groups::TabGroupId group) {
    const auto owner =
        applied_.source().ObserveTabModel(*browser()->GetTabStripModel());
    auto expected = std::get<TabModelMatches>(
        applied_.source()
            .Read({owner, AppliedConditionKind::kTabModelMatches})
            ->value);
    const auto it =
        std::ranges::find(expected.tabs, handle, &AppliedTabState::handle);
    CHECK(it != expected.tabs.end());
    auto tab = *it;
    expected.tabs.erase(it);
    tab.group = *AppliedOwnerToken::Deserialize(group.token().high(),
                                                group.token().low());
    expected.tabs.insert(expected.tabs.begin() + position, tab);
    return {applied_.Condition(owner, expected, AppliedMatch::kTabOrder),
            applied_.Condition(owner, TabModelMatches{.tabs = {tab}},
                               AppliedMatch::kTabMembership)};
  }

  AppliedTestContext<AppliedBrowserObservers> applied_;
  std::unique_ptr<DragAppliedObserver> drag_;
  std::unique_ptr<DragBrowserChangeObserver> browser_change_;
  AppliedOwnerToken preview_;
  uint64_t source_handle_ = 0;
  uint64_t session_ = 0;
  uint64_t step_ = 0;
  gfx::Point last_input_;
  base::test::ScopedFeatureList features_;
  gfx::AnimationTestApi::RenderModeResetter animation_mode_reset_{
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED)};
  base::WeakPtrFactory<YeeDetachToBrowserTabDragControllerTest> weak_{this};
};
IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragCollapsedGroupHeaderRemainsCollapsed) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({2, 3});
  ASSERT_FALSE(model->IsGroupCollapsed(group));
  tab_strip->ToggleTabGroupCollapsedState(group);
  StopAnimating(tab_strip);
  ASSERT_TRUE(model->IsGroupCollapsed(group));
  EnsureFocusToTabStrip(tab_strip);

  ASSERT_EQ(4, model->count());
  ASSERT_EQ(2u, group_model->GetTabGroup(group)->ListTabs().length());

  // Drag group1, group should remain collapsed.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->group_header(group)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(TabDragController::IsActive());
  EXPECT_TRUE(model->IsGroupCollapsed(group));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);
  EXPECT_TRUE(model->IsGroupCollapsed(group));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragGroupHeaderDragsGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({1, 2});
  StopAnimating(tab_strip);
  EnsureFocusToTabStrip(tab_strip);

  ASSERT_EQ(4, model->count());
  ASSERT_EQ(2u, group_model->GetTabGroup(group)->ListTabs().length());

  // Drag the entire group right by its header.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->group_header(group)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(3)));
  EXPECT_TRUE(group_model->GetTabGroup(group)->visual_data()->is_collapsed());
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("0 3 1 2", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group)->ListTabs(), gfx::Range(2, 4));
  EXPECT_FALSE(group_model->GetTabGroup(group)->visual_data()->is_collapsed());
  EXPECT_EQ(std::nullopt, model->GetTabGroupForTab(0));
  EXPECT_EQ(std::nullopt, model->GetTabGroupForTab(1));

  // Drag the entire group left by its header.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->group_header(group)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(0)));
  EXPECT_TRUE(group_model->GetTabGroup(group)->visual_data()->is_collapsed());
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 2 0 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group)->ListTabs(), gfx::Range(0, 2));
  EXPECT_FALSE(group_model->GetTabGroup(group)->visual_data()->is_collapsed());
  EXPECT_EQ(std::nullopt, model->GetTabGroupForTab(2));
  EXPECT_EQ(std::nullopt, model->GetTabGroupForTab(3));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragOnlyTabInGroupRetainsGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({0});
  StopAnimating(tab_strip);

  EXPECT_EQ(4, model->count());
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 1));

  // Dragging the tab in the zero-th index to the tab in the first index
  // switches the tabs and removes the group of the zero-th tab.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(0)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 0 2 3", IDString(model));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(1, 2));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragSingleTabLeftIntoGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group1 = model->AddToNewGroup({0});
  tab_groups::TabGroupId group4 = model->AddToNewGroup({3});
  StopAnimating(tab_strip);
  EnsureFocusToTabStrip(tab_strip);

  // Dragging the tab in the first index toward the tab in the zero-th index
  // switches the tabs and adds the dragged tab to the group.
  // Drag only half the tab length, to ensure that the tab in the first index
  // lands in the slot between the tab in the zero-th index and the group
  // header to the left of the zero-th index.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(DragInputToCenter(
      tab_strip->tab_at(1), gfx::Vector2d((base::i18n::IsRTL() ? 1 : -1) *
                                              tab_strip->tab_at(0)->width() / 2,
                                          0)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 0 2 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(0, 2));

  // Dragging the tab in the second index to the tab in the first index switches
  // the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 2 0 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(0, 3));

  // Dragging the tab in the third index to the tab in the second index switches
  // the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(3)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 2 0 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(0, 3));
  EXPECT_EQ(group_model->GetTabGroup(group4)->ListTabs(), gfx::Range(3, 4));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragSingleTabRightIntoGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 4);
  tab_groups::TabGroupId group1 = model->AddToNewGroup({3});
  tab_groups::TabGroupId group2 = model->AddToNewGroup({0});
  StopAnimating(tab_strip);
  EnsureFocusToTabStrip(tab_strip);

  // Dragging the tab in the second index to the tab in the third index switches
  // the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(3)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("0 1 3 2 4", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(2, 4));

  // Dragging the tab in the first index to the tab in the second index switches
  // the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("0 3 1 2 4", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(1, 4));

  // Dragging the tab in the zero-th index to the tab in the first index
  // switches the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(0)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("0 3 1 2 4", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(1, 4));
  EXPECT_EQ(group_model->GetTabGroup(group2)->ListTabs(), gfx::Range(0, 1));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragSingleTabRightOfRightmostGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 4);
  tab_groups::TabGroupId group = model->AddToNewGroup({3, 4});
  StopAnimating(tab_strip);
  EnsureFocusToTabStrip(tab_strip);

  // Dragging the first tab past the last slot brings it to the end of the
  // tabstrip, where it should not be in a group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(0)));
  // 화면 좌표의 끝 슬롯은 RTL에서 왼쪽이므로 이동 방향도 반전한다.
  ASSERT_TRUE(DragInputToCenter(
      tab_strip->tab_at(4), gfx::Vector2d(base::i18n::IsRTL() ? -1 : 1, 0)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 2 3 4 0", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group)->ListTabs(), gfx::Range(2, 4));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragTabLeftPastCollapsedGroupHeader) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({1, 2});
  StopAnimating(tab_strip);

  EXPECT_EQ(4, model->count());
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(1, 3));
  EXPECT_FALSE(model->IsGroupCollapsed(group));
  tab_strip->ToggleTabGroupCollapsedState(group);
  StopAnimating(tab_strip);
  EXPECT_TRUE(model->IsGroupCollapsed(group));

  // Dragging the last tab to the left should cause it to swap places with the
  // collapsed group header.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(3)));
  const gfx::Point leading =
      base::i18n::IsRTL()
          ? GetRightCenterInScreenCoordinates(tab_strip->tab_at(3))
          : GetLeftCenterInScreenCoordinates(tab_strip->tab_at(3));
  ASSERT_TRUE(DragInputTo(leading, GetWindowHint(tab_strip)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("0 3 1 2", IDString(model));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(2, 4));
  EXPECT_TRUE(model->IsGroupCollapsed(group));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragTabRightPastCollapsedGroupHeader) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({1, 2});
  StopAnimating(tab_strip);

  EXPECT_EQ(4, model->count());
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(1, 3));
  EXPECT_FALSE(model->IsGroupCollapsed(group));
  tab_strip->ToggleTabGroupCollapsedState(group);
  StopAnimating(tab_strip);
  EXPECT_TRUE(model->IsGroupCollapsed(group));

  // Dragging the first tab to the right should cause it to swap places with the
  // collapsed group header.
  const gfx::NativeWindow window_hint = GetWindowHint(tab_strip);
  const gfx::Point leading =
      base::i18n::IsRTL()
          ? GetRightCenterInScreenCoordinates(tab_strip->tab_at(0))
          : GetLeftCenterInScreenCoordinates(tab_strip->tab_at(0));
  const gfx::Point trailing =
      base::i18n::IsRTL()
          ? GetLeftCenterInScreenCoordinates(tab_strip->tab_at(0))
          : GetRightCenterInScreenCoordinates(tab_strip->tab_at(0));
  ASSERT_TRUE(PressInput(leading, window_hint));
  ASSERT_TRUE(DragInputTo(trailing, window_hint));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  EXPECT_EQ("1 2 0 3", IDString(model));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 2));
  EXPECT_TRUE(model->IsGroupCollapsed(group));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       DragTabWithinGroupDoesNotModifyGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({0, 1, 2});
  StopAnimating(tab_strip);

  EXPECT_EQ(4, model->count());
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 3));

  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(0)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  // Dragging the tab in the zero-th index to the tab in the first index
  // switches the tabs but group membership does not change.
  EXPECT_EQ("1 0 2 3", IDString(model));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 3));

  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));
  ASSERT_TRUE(ReleaseInput());
  StopAnimating(tab_strip);

  // Dragging the tab in the second index to the tab in the first index switches
  // the tabs but group membership does not change.
  EXPECT_EQ("1 2 0 3", IDString(model));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 3));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       RevertDragSingleTabIntoGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  TabGroupModel* group_model = model->group_model();

  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group1 = model->AddToNewGroup({0, 1});
  StopAnimating(tab_strip);

  // Dragging the tab in the second index to the tab in the first index switches
  // the tabs and adds the dragged tab to the group.
  ASSERT_TRUE(PressInputAtCenter(tab_strip->tab_at(2)));
  ASSERT_TRUE(DragInputToCenter(tab_strip->tab_at(1)));

  EXPECT_EQ("0 2 1 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(0, 3));

  ASSERT_TRUE(TabDragController::IsActive());

  // Pressing escape will revert the tabs to original state before the drag.
  ASSERT_TRUE(EscapeInput());
  EXPECT_EQ("0 1 2 3", IDString(model));
  EXPECT_EQ(group_model->GetTabGroup(group1)->ListTabs(), gfx::Range(0, 2));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       RevertTabDragOutOfGroup) {
  ASSERT_TRUE(browser()->GetTabStripModel()->SupportsTabGroups());

  TabStrip* tab_strip = GetTabStripForBrowser(browser());
  TabStripModel* model = browser()->GetTabStripModel();
  AddTabsAndResetBrowser(browser(), 3);
  tab_groups::TabGroupId group = model->AddToNewGroup({0, 1, 2});
  StopAnimating(tab_strip);
  EnsureFocusToTabStrip(tab_strip);
  const auto* source = model->GetTabAtIndex(0);
  const auto* original_context = tab_strip->GetDragContext();
#if BUILDFLAG(IS_WIN)
  ui_test_utils::ScopedNativeInputTrace native_trace(tab_strip->GetWidget());
#endif
  const auto record_phase = [&](const char* phase,
                                [[maybe_unused]] const gfx::Point& intended) {
    const std::string prefix = std::string("YeeGroupRevert-") + phase;
    const int source_index = model->GetIndexOfTab(source);
    RecordProperty(prefix + "-source-index", source_index);
    RecordProperty(prefix + "-model-count", model->count());
    RecordProperty(prefix + "-drag-active",
                   TabDragController::IsActive() ? 1 : 0);
    RecordProperty(prefix + "-original-attached",
                   TabDragController::IsAttachedTo(original_context) ? 1 : 0);
    if (model->ContainsIndex(source_index)) {
      RecordProperty(prefix + "-source-in-original-group",
                     model->GetTabGroupForTab(source_index) == group ? 1 : 0);
      if (source_index < tab_strip->GetTabCount()) {
        RecordProperty(
            prefix + "-view-in-original-group",
            tab_strip->tab_at(source_index)->group() == group ? 1 : 0);
      }
    }
    RecordProperty(prefix + "-ids", IDString(model));
    RecordProperty(
        prefix + "-group-range",
        model->group_model()->ContainsTabGroup(group)
            ? model->group_model()->GetTabGroup(group)->ListTabs().ToString()
            : "missing");
#if BUILDFLAG(IS_WIN)
    native_trace.Snapshot(phase, intended);
    RecordProperty("YeeGroupRevertNativeInput", native_trace.ToJson());
#endif
  };

  // Drag the first tab in the group to after the group.
  const auto press_point = GetCenterInScreenCoordinates(tab_strip->tab_at(0));
  record_phase("before-press", press_point);
  const bool pressed = PressInputAtCenter(tab_strip->tab_at(0));
  record_phase("after-press", press_point);
  ASSERT_TRUE(pressed);
  const auto drag_point = GetCenterInScreenCoordinates(tab_strip->tab_at(3));
  record_phase("before-drag", drag_point);
  const bool dragged = DragInputToCenter(tab_strip->tab_at(3));
  record_phase("after-drag", drag_point);
  ASSERT_TRUE(dragged);
  // 실제 드래그와 그룹 이탈을 확인한 뒤 Escape 복원을 검사한다.
  EXPECT_TRUE(TabDragController::IsActive());
  EXPECT_TRUE(TabDragController::IsAttachedTo(original_context));
  const int dragged_index = model->GetIndexOfTab(source);
  EXPECT_TRUE(model->ContainsIndex(dragged_index));
  if (model->ContainsIndex(dragged_index)) {
    EXPECT_FALSE(model->GetTabGroupForTab(dragged_index).has_value());
  }

  record_phase("before-escape", drag_point);
  const bool escaped = EscapeInput();
  record_phase("after-escape", drag_point);
  ASSERT_TRUE(escaped);
  StopAnimating(tab_strip);
  record_phase("after-layout", drag_point);

  EXPECT_EQ("0 1 2 3", IDString(model));
  ASSERT_TRUE(model->group_model()->ContainsTabGroup(group));
  EXPECT_EQ(model->group_model()->GetTabGroup(group)->ListTabs(),
            gfx::Range(0, 3));
}

IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       YeeGroupDragKeepsSingleCapsuleAtEveryPosition) {
  // 탭바 끝에서 그룹을 벗어나는 native 경계와 그룹 내부의 끝 슬롯을 구분한다.
  AddTabsAndResetBrowser(browser(), 4);
  auto* const strip = GetTabStripForBrowser(browser());
  auto* const model = browser()->tab_strip_model();
  const auto group = model->AddToNewGroup({0, 1, 2});
  auto* const first = model->GetWebContentsAt(0);
  auto* const middle = model->GetWebContentsAt(1);
  auto* const last = model->GetWebContentsAt(2);
  auto* const moving = model->GetWebContentsAt(3);
  StopAnimating(strip);
  EnsureFocusToTabStrip(strip);
  strip->GetWidget()->LayoutRootViewIfNecessary();
  StopAnimating(strip);
  auto* const group_views =
      strip->tab_container_for_testing()->GetGroupViews(group);
  auto* const background = group_views->underline();
  auto* const drag_background = group_views->drag_underline();
  ASSERT_TRUE(yee::UsesHorizontalTabGroupCapsule(*background));

  const auto maximum_alpha = [](const SkBitmap& bitmap) {
    SkAlpha alpha = SK_AlphaTRANSPARENT;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        alpha = std::max(
            alpha, static_cast<SkAlpha>(SkColorGetA(bitmap.getColor(x, y))));
      }
    }
    return alpha;
  };
  const auto check_background = [&] {
    EXPECT_TRUE(background->GetVisible());
    gfx::Canvas canvas(background->size(), 1.f, false);
    background->OnPaint(&canvas);
    const SkAlpha base_alpha = maximum_alpha(canvas.GetBitmap());
    EXPECT_GT(base_alpha, SK_AlphaTRANSPARENT);
    EXPECT_LT(base_alpha, SK_AlphaOPAQUE);
    if (drag_background->GetVisible()) {
      gfx::Canvas drag_canvas(drag_background->size(), 1.f, false);
      drag_background->OnPaint(&drag_canvas);
      EXPECT_EQ(SK_AlphaTRANSPARENT, maximum_alpha(drag_canvas.GetBitmap()));
      gfx::Point origin;
      views::View::ConvertPointToTarget(drag_background, background, &origin);
      canvas.Save();
      canvas.Translate(gfx::Vector2d(origin.x(), origin.y()));
      drag_background->OnPaint(&canvas);
      canvas.Restore();
    }
    EXPECT_EQ(base_alpha, maximum_alpha(canvas.GetBitmap()));
  };
  const auto capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-group-marker-capture-dir");
  const auto capture = [&](const std::string& phase) {
    if (capture_directory.empty()) {
      return;
    }
    ASSERT_TRUE(base::CreateDirectory(capture_directory));
    auto* const widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    ASSERT_TRUE(applied_.WaitForPresentedFrame(*widget));
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
    ui::GrabWindowSnapshotAsPNG(
        widget->GetNativeWindow(),
        gfx::Rect(widget->GetWindowBoundsInScreen().size()),
        snapshot.GetCallback());
    const auto png = snapshot.Take();
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(base::WriteFile(
        capture_directory.AppendASCII("group-drag-" + phase + ".png"),
        static_cast<base::span<const uint8_t>>(*png)));
  };
  const auto check_native_roundtrip = [&] {
    auto* const native_theme = strip->GetNativeTheme();
    const auto original_contrast = native_theme->preferred_contrast();
    base::ScopedClosureRunner restore_theme(base::BindLambdaForTesting([&] {
      native_theme->set_preferred_contrast(original_contrast);
      native_theme->NotifyOnNativeThemeUpdated();
    }));
    const auto theme = drag_->ObserveCapsuleTheme(*background);
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(theme,
                            PresentationMatches{.use_native_theme = true},
                            AppliedMatch::kCapsuleTheme),
         applied_.View(*background, {.visible = true},
                       AppliedMatch::kViewVisible),
         applied_.View(*drag_background, {.visible = true},
                       AppliedMatch::kViewVisible)},
        [&] {
          native_theme->set_preferred_contrast(
              ui::NativeTheme::PreferredContrast::kMore);
          native_theme->NotifyOnNativeThemeUpdated();
        },
        AppliedDomain::kPresentation));
    gfx::Canvas marker(drag_background->size(), 1.f, false);
    drag_background->OnPaint(&marker);
    EXPECT_GT(maximum_alpha(marker.GetBitmap()), SK_AlphaTRANSPARENT);
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(theme,
                            PresentationMatches{.use_native_theme = false},
                            AppliedMatch::kCapsuleTheme)},
        [&] { restore_theme.RunAndReset(); }, AppliedDomain::kPresentation));
    check_background();
  };

  const std::array<content::WebContents*, 3> targets = {middle, first, last};
  const std::array<int, 3> expected_positions = {1, 0, 3};
  const std::array<const char*, 3> phases = {"middle", "first", "last"};
  for (size_t i = 0; i < targets.size(); ++i) {
    SCOPED_TRACE(phases[i]);
    strip->GetWidget()->LayoutRootViewIfNecessary();
    StopAnimating(strip);
    ASSERT_TRUE(PressInputAtCenter(
        strip->tab_at(model->GetIndexOfWebContents(moving))));
    base::ScopedClosureRunner release_input(
        base::BindLambdaForTesting([&] { EXPECT_TRUE(ReleaseInput()); }));
    const auto expected_drag = PositionConditions(
        tabs::TabInterface::GetFromContents(moving)->GetHandle().raw_value(),
        expected_positions[i], group);
    if (i == 1) {
      // 첫 멤버의 이전 중앙은 그룹 바깥 슬롯이 될 수 있다. 기존 편입 검사처럼
      // 반 칸만 이동해 그룹 안쪽의 첫 위치를 선택한다.
      const int distance =
          strip->tab_at(model->GetIndexOfWebContents(first))->width() / 2;
      ASSERT_TRUE(DragInputToCenter(
          strip->tab_at(model->GetIndexOfWebContents(moving)),
          gfx::Vector2d((base::i18n::IsRTL() ? 1 : -1) * distance, 0),
          expected_drag));
    } else {
      ASSERT_TRUE(DragInputToCenter(
          strip->tab_at(model->GetIndexOfWebContents(targets[i])), {},
          expected_drag));
    }
    ASSERT_TRUE(HasDragStarted(strip));
    SCOPED_TRACE("order=" + IDString(model) + " moving_index=" +
                 base::NumberToString(model->GetIndexOfWebContents(moving)) +
                 " grouped=" +
                 base::NumberToString(
                     model->GetTabGroupForTab(
                         model->GetIndexOfWebContents(moving)) == group));
    ASSERT_EQ(expected_positions[i], model->GetIndexOfWebContents(moving));
    ASSERT_EQ(group, model->GetTabGroupForTab(expected_positions[i]));
    EXPECT_EQ(4u,
              model->group_model()->GetTabGroup(group)->ListTabs().length());
    check_background();
    capture(phases[i]);
    if (i == targets.size() - 1) {
      check_native_roundtrip();
    }
    release_input.RunAndReset();
    // 다음 목표의 좌표는 native 드롭 애니메이션이 끝난 뒤에 읽는다.
    StopAnimating(strip);
    check_background();
    EXPECT_EQ(moving, model->GetActiveWebContents());
  }
  EXPECT_EQ(5, model->count());

  // 헤더가 drag container로 옮겨진 뒤 포인터를 멈춘 상태에서도 native marker로
  // 복귀해야 한다. 처음부터 남긴 그룹 밖 탭으로 전체 창 drag도 방지한다.
  StopAnimating(strip);
  EnsureFocusToTabStrip(strip);
  ASSERT_TRUE(PressInputAtCenter(strip->group_header(group)));
  base::ScopedClosureRunner release_header(
      base::BindLambdaForTesting([&] { EXPECT_TRUE(ReleaseInput()); }));
  ASSERT_TRUE(
      DragInputToCenter(strip->group_header(group),
                        gfx::Vector2d(base::i18n::IsRTL() ? -20 : 20, 0)));
  ASSERT_TRUE(HasDragStarted(strip));
  check_background();
  check_native_roundtrip();
  capture("header-stationary");
  release_header.RunAndReset();
  StopAnimating(strip);
  check_background();
  EXPECT_EQ(5, model->count());
  EXPECT_EQ(4u, model->group_model()->GetTabGroup(group)->ListTabs().length());
  for (auto* member : {first, middle, last, moving}) {
    EXPECT_EQ(group,
              model->GetTabGroupForTab(model->GetIndexOfWebContents(member)));
  }
}

#if BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_P(YeeDetachToBrowserTabDragControllerTest,
                       YeeGroupMembershipDragSurvivesLayoutSwitch) {
  auto* const prefs_service = browser()->GetProfile()->GetPrefs();
  const bool original_split_drag =
      prefs_service->GetBoolean(prefs::kSplitViewDragAndDropEnabled);
  // 일반 fixture가 끈 분할 드래그도 켜서 실제 제품의 입력 경합을 검사한다.
  prefs_service->SetBoolean(prefs::kSplitViewDragAndDropEnabled, true);
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(state);
  state->SetVerticalTabsEnabled(true);
  state->SetVerticalTabsEnabled(false);
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  view->GetWidget()->LayoutRootViewIfNecessary();
  auto* const strip = GetTabStripForBrowser(browser());
  ASSERT_TRUE(strip && strip->IsDrawn());
  AddTabsAndResetBrowser(browser(), 3);
  auto* const model = browser()->tab_strip_model();
  const auto group = model->AddToNewGroup({1, 2});
  auto* const outside = model->GetWebContentsAt(0);
  auto* const first_member = model->GetWebContentsAt(1);
  auto* const second_member = model->GetWebContentsAt(2);
  auto* const moving = model->GetWebContentsAt(3);
  model->ActivateTabAt(model->GetIndexOfWebContents(outside));

  const auto capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-group-marker-capture-dir");
  const auto capture = [&](const std::string& name) {
    if (capture_directory.empty()) {
      return;
    }
    ASSERT_TRUE(base::CreateDirectory(capture_directory));
    auto* const widget = view->GetWidget();
    ASSERT_TRUE(applied_.WaitForPresentedFrame(*widget));
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
    ui::GrabWindowSnapshotAsPNG(
        widget->GetNativeWindow(),
        gfx::Rect(widget->GetWindowBoundsInScreen().size()),
        snapshot.GetCallback());
    const auto png = snapshot.Take();
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(base::WriteFile(capture_directory.AppendASCII(name + ".png"),
                                static_cast<base::span<const uint8_t>>(*png)));
  };

  for (bool maximized : {false, true}) {
    const std::string window_state = maximized ? "maximized" : "restored";
    SCOPED_TRACE(window_state);
    if (maximized) {
      view->GetWidget()->Maximize();
    }
    StopAnimating(strip);
    EnsureFocusToTabStrip(strip);
    auto* const frame = view->browser_widget()->GetFrameView();
    gfx::Point moving_center =
        strip->tab_at(model->GetIndexOfWebContents(moving))
            ->GetBoundsInScreen()
            .CenterPoint();
    views::View::ConvertPointFromScreen(frame->parent(), &moving_center);
    EXPECT_EQ(HTCLIENT, frame->NonClientHitTest(moving_center));
    // 출발 탭을 빼면 그 뒤 멤버가 한 칸 이동한다. 이동 후 두 멤버 사이에
    // 삽입되도록 출발 방향에 따라 기존 멤버의 중앙을 선택한다.
    auto* const target_member =
        model->GetIndexOfWebContents(moving) <
                model->GetIndexOfWebContents(first_member)
            ? first_member
            : second_member;
    ASSERT_TRUE(PressInputAtCenter(
        strip->tab_at(model->GetIndexOfWebContents(moving))));
    ASSERT_EQ(moving, model->GetActiveWebContents());
    ASSERT_TRUE(DragInputToCenter(
        strip->tab_at(model->GetIndexOfWebContents(target_member))));
    ASSERT_TRUE(HasDragStarted(strip));
    auto* const moving_view =
        strip->tab_at(model->GetIndexOfWebContents(moving));
    auto* const member_view =
        strip->tab_at(model->GetIndexOfWebContents(target_member));
    SCOPED_TRACE(
        "drag source=" + moving_view->bounds().ToString() +
        " source_screen=" + moving_view->GetBoundsInScreen().ToString() +
        " member=" + member_view->bounds().ToString() +
        " member_screen=" + member_view->GetBoundsInScreen().ToString() +
        " cursor=" + display::Screen::Get()->GetCursorScreenPoint().ToString() +
        " order=" + IDString(model));
    ASSERT_TRUE(ReleaseInput());
    StopAnimating(strip);
    EXPECT_EQ(group,
              model->GetTabGroupForTab(model->GetIndexOfWebContents(moving)));
    EXPECT_EQ(3u,
              model->group_model()->GetTabGroup(group)->ListTabs().length());
    EXPECT_EQ(model->GetIndexOfWebContents(first_member) + 1,
              model->GetIndexOfWebContents(moving));
    EXPECT_EQ(model->GetIndexOfWebContents(moving) + 1,
              model->GetIndexOfWebContents(second_member));
    capture("horizontal-group-joined-" + window_state);

    ASSERT_TRUE(PressInputAtCenter(
        strip->tab_at(model->GetIndexOfWebContents(moving))));
    ASSERT_TRUE(DragInputToCenter(
        strip->tab_at(model->GetIndexOfWebContents(outside))));
    ASSERT_TRUE(ReleaseInput());
    StopAnimating(strip);
    EXPECT_FALSE(model->GetTabGroupForTab(model->GetIndexOfWebContents(moving))
                     .has_value());
    EXPECT_EQ(2u,
              model->group_model()->GetTabGroup(group)->ListTabs().length());
    EXPECT_EQ(moving, model->GetActiveWebContents());
    EXPECT_EQ(4, model->count());
    EXPECT_EQ(0, model->GetIndexOfWebContents(moving));
    EXPECT_EQ(group, model->GetTabGroupForTab(
                         model->GetIndexOfWebContents(first_member)));
    EXPECT_EQ(group, model->GetTabGroupForTab(
                         model->GetIndexOfWebContents(second_member)));
    capture("horizontal-group-removed-" + window_state);
  }
  view->GetWidget()->Restore();
  prefs_service->SetBoolean(prefs::kSplitViewDragAndDropEnabled,
                            original_split_drag);
}

#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_CHROMEOS)
INSTANTIATE_TEST_SUITE_P(TabDragging,
                         YeeDetachToBrowserTabDragControllerTest,
                         testing::Combine(testing::Values(false),
                                          testing::Values("mouse", "touch")));
#else
INSTANTIATE_TEST_SUITE_P(TabDragging,
                         YeeDetachToBrowserTabDragControllerTest,
                         testing::Combine(testing::Bool(),
                                          testing::Values("mouse")));
#endif
}  // namespace
