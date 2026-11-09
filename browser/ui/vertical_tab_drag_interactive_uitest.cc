// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include "chrome/browser/ui/views/yee/applied_wait_kombucha.h"
#include "chrome/browser/ui/views/yee/drag_interactive_test_support.h"

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/base_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_drag_handler.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_interactive_test_mixin.h"
#include "chrome/browser/ui/views/yee/favorites.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "chrome/browser/yee_projects/project_service_factory.h"
#include "chrome/browser/yee_projects/project_tab_coordinator.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/test/ui_controls.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/interactive_views_test.h"
#include "ui/views/interaction/mouse/interaction_test_util_mouse.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace {
using namespace yee::test;
using GroupHandles = std::vector<uint64_t>;
using TabOrder = std::vector<std::variant<uint64_t, GroupHandles>>;
using PinnedHandles = std::vector<uint64_t>;
enum class DragPosition { kAbove, kBelow };

TabOrder ReadTabOrder(const TabStripModel& model) {
  TabOrder order;
  std::optional<tab_groups::TabGroupId> last;
  for (int i = 0; i < model.count(); ++i) {
    const auto* tab = model.GetTabAtIndex(i);
    const uint64_t handle = tab->GetHandle().raw_value();
    if (tab->GetGroup()) {
      if (last == tab->GetGroup())
        std::get<GroupHandles>(order.back()).push_back(handle);
      else
        order.push_back(GroupHandles{handle});
    } else {
      order.push_back(handle);
    }
    last = tab->GetGroup();
  }
  return order;
}
PinnedHandles ReadPinnedHandles(const TabStripModel& model) {
  PinnedHandles handles;
  for (int i = 0; i < model.IndexOfFirstNonPinnedTab(); ++i)
    handles.push_back(model.GetTabAtIndex(i)->GetHandle().raw_value());
  return handles;
}
int TabSelectModifier() {
#if BUILDFLAG(IS_MAC)
  return ui_controls::kCommand;
#elif BUILDFLAG(IS_CHROMEOS)
  return ui_controls::kShift;
#else
  return ui_controls::kControl;
#endif
}
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kSecondTab);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kThirdTab);

class YeeVerticalTabDragTest
    : public VerticalTabsInteractiveTestMixin<
          yee::test::AppliedWaitInteractiveTestMixin<InteractiveBrowserTest>> {
 public:
  YeeVerticalTabDragTest() = default;
  ~YeeVerticalTabDragTest() override = default;

  void SetUpOnMainThread() override {
    // Chromium normally starts these tests in horizontal mode, while Yee's
    // profile default is already vertical. Avoid asking the state controller
    // to enter its current mode: that would tear down an uninitialized
    // horizontal TabStrip before the test body starts.
    if (!vertical_tab_strip_state_controller()->ShouldDisplayVerticalTabs()) {
      vertical_tab_strip_state_controller()->SetVerticalTabsEnabled(true);
      RunScheduledLayouts();
    }
    yee::test::AppliedWaitInteractiveTestMixin<
        InteractiveBrowserTest>::SetUpOnMainThread();
    initial_handles_.push_back(browser()
                                   ->GetTabStripModel()
                                   ->GetTabAtIndex(0)
                                   ->GetHandle()
                                   .raw_value());

    // 실제 입력을 시작할 작업 영역을 보장하되 명시한 모니터 위치는 유지한다.
    gfx::Rect window_bounds = browser()->GetWindow()->GetBounds();
    auto* const screen = display::Screen::Get();
    const auto display = base::CommandLine::ForCurrentProcess()->HasSwitch(
                             switches::kWindowPosition)
                             ? screen->GetDisplayMatching(window_bounds)
                             : screen->GetPrimaryDisplay();
    window_bounds.AdjustToFit(display.work_area());
    browser()->GetWindow()->SetBounds(window_bounds);
  }

  const std::vector<base::test::FeatureRefAndParams> GetEnabledFeatures()
      override {
    auto enabled = VerticalTabsInteractiveTestMixin<
        yee::test::AppliedWaitInteractiveTestMixin<InteractiveBrowserTest>>::
        GetEnabledFeatures();
    enabled.push_back({features::kCollapseTabGroupDuringDrag, {}});
    return enabled;
  }

  const std::vector<base::test::FeatureRef> GetDisabledFeatures() override {
    // TODO(crbug.com/452061489): Fix tests that fail when the WebUI Omnibox is
    // enabled and then remove this.
    return {omnibox::internal::kWebUIOmniboxPopup,
            omnibox::internal::kWebUIOmniboxAimPopup};
  }

 protected:
  void DragUngroupedTabIntoExpandedGroupMiddle(DragPosition position);

  BrowserView& GetBrowserView() {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    EXPECT_TRUE(browser_view != nullptr);
    return *browser_view;
  }

  TabStripView& GetTabStripView() {
    return *static_cast<TabStripView*>(
        GetBrowserView()
            .vertical_tab_strip_region_view_for_testing()
            ->GetTabStripView());
  }

  views::View& GetFavoritesDropZone() {
    views::View* drop_zone =
        GetTabStripView().GetViewByID(yee::kSidebarFavoritesDropZoneViewId);
    CHECK(drop_zone);
    return *drop_zone;
  }

  views::View* GetFavoritesDragPreview() {
    return GetTabStripView().GetViewByID(
        yee::kSidebarFavoritesDragPreviewViewId);
  }

  void ExpectDragPreviewAlignedWithTab(int tab_index) {
    views::View* preview = GetFavoritesDragPreview();
    ASSERT_TRUE(preview);
    ASSERT_TRUE(preview->GetVisible());
    const gfx::Rect preview_bounds = preview->GetBoundsInScreen();
    const gfx::Rect tab_bounds = GetTabViewAt(tab_index)->GetBoundsInScreen();
    SCOPED_TRACE(
        ::testing::Message()
        << "preview=" << preview->bounds().ToString()
        << " screen=" << preview_bounds.ToString()
        << " strip=" << GetTabStripView().GetBoundsInScreen().ToString()
        << " tab=" << GetTabViewAt(tab_index)->bounds().ToString()
        << " screen=" << tab_bounds.ToString() << " parent="
        << GetTabViewAt(tab_index)->parent()->GetBoundsInScreen().ToString());
    EXPECT_EQ(tab_bounds.x(), preview_bounds.x());
    EXPECT_EQ(tab_bounds.width(), preview_bounds.width());
  }

  auto RunScheduledLayout() {
    return Do([&]() { views::test::RunScheduledLayout(&GetBrowserView()); });
  }

  views::View* GetTabViewAt(int tab_index) {
    TabStripModel* tab_strip_model = browser()->GetTabStripModel();
    tabs::TabInterface* tab = tab_strip_model->GetTabAtIndex(tab_index);
    RootTabCollectionNode* root_node =
        GetBrowserView()
            .vertical_tab_strip_region_view_for_testing()
            ->root_node_for_testing();
    return root_node ? root_node->GetNodeForHandle(tab->GetHandle())->view()
                     : nullptr;
  }

  auto NameTabViewAt(std::string_view tab_name, int tab_index) {
    return NameView(tab_name, base::BindLambdaForTesting([&, tab_index]() {
                      return GetTabViewAt(tab_index);
                    }));
  }

  auto StartDragBetweenTabs(int from_tab_index, int to_tab_index) {
    const char kTabToDragFrom[] = "Tab to drag";
    const char kTabToDragTo[] = "Tab to drag to";
    return Steps(Log("Start drag from " + base::NumberToString(from_tab_index) +
                     " to " + base::NumberToString(to_tab_index)),
                 NameTabViewAt(kTabToDragFrom, from_tab_index),
                 NameTabViewAt(kTabToDragTo, to_tab_index),
                 MoveDragMouseTo(kTabToDragFrom),
                 ClickMouse(ui_controls::LEFT, /*release=*/false),
                 MoveDragMouseTo(kTabToDragTo), RunScheduledLayout());
  }

  auto StartDragTabWithinSidebar(int from_tab_index) {
    const char kTabToDragFrom[] = "Lone tab to drag";
    return Steps(
        Log("Start lone-tab Sidebar organization drag"),
        NameTabViewAt(kTabToDragFrom, from_tab_index),
        MoveDragMouseTo(kTabToDragFrom),
        ClickMouse(ui_controls::LEFT, /*release=*/false),
        MoveDragMouseTo(base::BindLambdaForTesting([this, from_tab_index]() {
          TabStripModel* model = browser()->GetTabStripModel();
          tabs::TabInterface* tab = model->GetTabAtIndex(from_tab_index);
          RootTabCollectionNode* root_node =
              GetBrowserView()
                  .vertical_tab_strip_region_view_for_testing()
                  ->root_node_for_testing();
          gfx::Point point = root_node->GetNodeForHandle(tab->GetHandle())
                                 ->view()
                                 ->GetBoundsInScreen()
                                 .CenterPoint();
          point.Offset(0, yee::kSidebarMetrics.tab_row_height);
          return point;
        })),
        RunScheduledLayout());
  }

  auto StartDragFromGroupToTab(int from_group_index, int to_tab_index) {
    const char kGroupToDragFrom[] = "Group to drag";
    const char kTabToDragTo[] = "Tab to drag to";
    return Steps(
        Log("Start drag from group" + base::NumberToString(from_group_index) +
            " to " + base::NumberToString(to_tab_index)),
        NameDescendantViewByType<TabGroupHeaderView>(
            kBrowserViewElementId, kGroupToDragFrom, from_group_index),
        NameTabViewAt(kTabToDragTo, to_tab_index),
        MoveDragMouseTo(kGroupToDragFrom),
        ClickMouse(ui_controls::LEFT, /*release=*/false),
        MoveDragMouseTo(kTabToDragTo), RunScheduledLayout());
  }

  auto ContinueDragToTab(int to_tab_index) {
    const char kTabToDragTo[] = "Tab to drag to";
    return Steps(
        Log("Continue drag to tab at " + base::NumberToString(to_tab_index)),
        NameTabViewAt(kTabToDragTo, to_tab_index),
        MoveDragMouseTo(kTabToDragTo), RunScheduledLayout());
  }

  auto ContinueDragToFavoritesDropZone() {
    return Steps(Log("Continue drag to the Favorites drop zone"),
                 MoveDragMouseTo(base::BindLambdaForTesting([this]() {
                   auto* tab_strip_view = static_cast<TabStripView*>(
                       GetBrowserView()
                           .vertical_tab_strip_region_view_for_testing()
                           ->GetTabStripView());
                   views::View* drop_zone = tab_strip_view->GetViewByID(
                       yee::kSidebarFavoritesDropZoneViewId);
                   CHECK(drop_zone);
                   CHECK(drop_zone->GetVisible());
                   return drop_zone->GetBoundsInScreen().CenterPoint();
                 })),
                 RunScheduledLayout());
  }

  auto ContinueDragToFavoriteSlot(int target_item_count, int slot) {
    return Steps(
        Log("Continue drag to Favorite slot " + base::NumberToString(slot)),
        MoveDragMouseTo(base::BindLambdaForTesting([this, target_item_count,
                                                    slot]() {
          auto* scroll = views::AsViewClass<views::ScrollView>(
              GetTabStripView().GetViewByID(yee::kSidebarFavoritesDockViewId));
          CHECK(scroll);
          CHECK(scroll->contents());
          const views::View& dock = *scroll->contents();
          const gfx::Size tile =
              yee::FavoritesTileSizeForItemCount(dock, target_item_count);
          const gfx::Rect logical_slot(
              yee::kSidebarMetrics.tab_strip_horizontal_padding +
                  slot *
                      (tile.width() + yee::kSidebarMetrics.favorites_cell_gap),
              0, tile.width(), tile.height());
          // 논리 슬롯을 물리 dock 좌표로 한 번만 미러링한 뒤 화면으로 옮긴다.
          gfx::Point point = dock.GetMirroredRect(logical_slot).CenterPoint();
          views::View::ConvertPointToScreen(&dock, &point);
          return point;
        })),
        RunScheduledLayout());
  }

  auto ContinueDragToEmptyTabList() {
    return Steps(Log("Continue drag to empty Tab list"),
                 MoveDragMouseTo(base::BindLambdaForTesting([this]() {
                   return GetTabStripView()
                       .unpinned_tabs_scroll_view()
                       ->GetBoundsInScreen()
                       .CenterPoint();
                 })),
                 RunScheduledLayout());
  }

  auto SaveTabCenter(int tab_index) {
    return Do([this, tab_index]() {
      TabStripModel* model = browser()->GetTabStripModel();
      tabs::TabInterface* tab = model->GetTabAtIndex(tab_index);
      RootTabCollectionNode* root_node =
          GetBrowserView()
              .vertical_tab_strip_region_view_for_testing()
              ->root_node_for_testing();
      saved_drag_destination_ = root_node->GetNodeForHandle(tab->GetHandle())
                                    ->view()
                                    ->GetBoundsInScreen()
                                    .CenterPoint();
    });
  }

  auto SavePointBeforeTab(int tab_index) {
    return Do([this, tab_index]() {
      TabStripModel* model = browser()->GetTabStripModel();
      tabs::TabInterface* tab = model->GetTabAtIndex(tab_index);
      RootTabCollectionNode* root_node =
          GetBrowserView()
              .vertical_tab_strip_region_view_for_testing()
              ->root_node_for_testing();
      const gfx::Rect bounds = root_node->GetNodeForHandle(tab->GetHandle())
                                   ->view()
                                   ->GetBoundsInScreen();
      saved_drag_destination_ = bounds.CenterPoint();
      saved_drag_destination_.set_y(bounds.y() + 1);
    });
  }

  int IndexOfInitialTab(int initial_index) {
    auto* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      if (static_cast<uint64_t>(
              model->GetTabAtIndex(i)->GetHandle().raw_value()) ==
          Tab(initial_index))
        return i;
    }
    CHECK(false);
  }
  auto SavePointAfterInitialTab(int initial_index) {
    return Do([this, initial_index] {
      const auto bounds =
          GetTabViewAt(IndexOfInitialTab(initial_index))->GetBoundsInScreen();
      saved_drag_destination_ = bounds.CenterPoint();
      saved_drag_destination_.set_y(bounds.bottom() - 1);
    });
  }
  auto ContinueDragToInitialTab(int initial_index) {
    return Steps(
        MoveDragMouseTo(base::BindLambdaForTesting([this, initial_index] {
          return GetTabViewAt(IndexOfInitialTab(initial_index))
              ->GetBoundsInScreen()
              .CenterPoint();
        })),
        RunScheduledLayout());
  }

  auto SavePointBelowTab(int tab_index) {
    return Do([this, tab_index]() {
      TabStripModel* model = browser()->GetTabStripModel();
      tabs::TabInterface* tab = model->GetTabAtIndex(tab_index);
      RootTabCollectionNode* root_node =
          GetBrowserView()
              .vertical_tab_strip_region_view_for_testing()
              ->root_node_for_testing();
      const gfx::Rect bounds = root_node->GetNodeForHandle(tab->GetHandle())
                                   ->view()
                                   ->GetBoundsInScreen();
      saved_drag_destination_ = bounds.CenterPoint();
      saved_drag_destination_.set_y(bounds.bottom() +
                                    yee::kSidebarMetrics.tab_row_height / 2);
    });
  }

  auto SaveGroupHeaderCenter(int group_index) {
    const char kGroupHeader[] = "Group header drop target";
    return Steps(NameDescendantViewByType<TabGroupHeaderView>(
                     kBrowserViewElementId, kGroupHeader, group_index),
                 WithView(kGroupHeader, [this](views::View* header) {
                   saved_drag_destination_ =
                       header->GetBoundsInScreen().CenterPoint();
                 }));
  }

  auto ContinueDragToSavedTabCenter() {
    return Steps(Log("Continue drag to the saved compact tab center"),
                 MoveDragMouseTo(base::BindLambdaForTesting(
                     [this]() { return saved_drag_destination_; })),
                 RunScheduledLayout());
  }

  auto PressEscAsync() {
    return SendKeyPress(kBrowserViewElementId, ui::VKEY_ESCAPE);
  }

  auto AddTabsToNewGroup(const std::vector<int>& indices) {
    return Do([&]() {
      browser()->GetTabStripModel()->AddToNewGroup(indices);
      views::test::RunScheduledLayout(&GetBrowserView());
    });
  }

  auto AddTabsToNewSplit(int index1, int index2) {
    return Do([&, index1, index2] {
      auto* tab_strip_model = browser()->GetTabStripModel();
      tab_strip_model->ActivateTabAt(
          index1, TabStripUserGestureDetails(
                      TabStripUserGestureDetails::GestureType::kOther));
      tab_strip_model->AddToNewSplit(
          {index2}, {}, split_tabs::SplitTabCreatedSource::kTabContextMenu);
      views::test::RunScheduledLayout(&GetBrowserView());
    });
  }

  auto SelectTabAt(int tab_index) {
    const char kTabToSelect[] = "Tab to select";
    return Steps(NameTabViewAt(kTabToSelect, tab_index),
                 MoveDragMouseTo(kTabToSelect),
                 ClickMouse(ui_controls::MouseButton::LEFT, /*release=*/true,
                            TabSelectModifier()));
  }

  auto PinTabAt(int tab_index) {
    return Do([&, tab_index]() {
      browser()->GetTabStripModel()->SetTabPinned(tab_index, true);
      views::test::RunScheduledLayout(&GetBrowserView());
    });
  }

  auto NameScrollView(const char* name) {
    return NameView(name, base::BindLambdaForTesting([this]() {
                      return static_cast<views::View*>(
                          static_cast<TabStripView*>(
                              GetBrowserView()
                                  .vertical_tab_strip_region_view_for_testing()
                                  ->GetTabStripView())
                              ->unpinned_tabs_scroll_view());
                    }));
  }

  auto CollapseGroup(int group_index) {
    return Do([&, group_index]() {
      TabStripModel* model = browser()->GetTabStripModel();
      std::vector<tab_groups::TabGroupId> groups =
          model->group_model()->ListTabGroups();
      ASSERT_LT(static_cast<size_t>(group_index), groups.size());
      TabGroup* group = model->group_model()->GetTabGroup(groups[group_index]);
      vertical_tab_strip_controller()->ToggleTabGroupCollapsedState(
          group, ToggleTabGroupCollapsedStateOrigin::kMenuAction);
      views::test::RunScheduledLayout(&GetBrowserView());
    });
  }

  using Phase = DragPhaseMatches::Phase;
  using Conditions = std::function<AppliedConditions()>;

  void TearDownOnMainThread() override {
    drag_.reset();
    applied_.Stop();
    VerticalTabsInteractiveTestMixin<AppliedWaitInteractiveTestMixin<
        InteractiveBrowserTest>>::TearDownOnMainThread();
  }

  uint64_t Tab(int initial_index) const {
    return initial_handles_.at(initial_index);
  }
  // 기대 handle도 앞선 AddTab 단계가 실행된 뒤에 읽는다. 불일치하면 양쪽
  // 순서를 출력하고 Check가 시퀀스를 중단해 후속 입력을 보내지 않는다.
  auto CheckTabOrder(std::function<TabOrder()> expected_order) {
    return Check(
        [this, expected_order = std::move(expected_order)] {
          const auto expected = expected_order();
          const auto actual = ReadTabOrder(*browser()->GetTabStripModel());
          EXPECT_EQ(expected, actual);
          return expected == actual;
        },
        "실제 탭 순서와 실행 시점 기대 순서 비교");
  }
  auto AddTab(ui::ElementIdentifier id, GURL url, int index) {
    return Steps(
        AddInstrumentedTab(id, url, index), Do([this, index, url] {
          EXPECT_EQ(
              url,
              browser()->GetTabStripModel()->GetWebContentsAt(index)->GetURL());
          CHECK_EQ(static_cast<size_t>(index), initial_handles_.size());
          initial_handles_.push_back(browser()
                                         ->GetTabStripModel()
                                         ->GetTabAtIndex(index)
                                         ->GetHandle()
                                         .raw_value());
        }));
  }
  static AppliedConditions CombineConditions(
      std::initializer_list<AppliedConditions> parts) {
    AppliedConditions result;
    for (const auto& part : parts)
      result.insert(result.end(), part.begin(), part.end());
    return result;
  }
  AppliedConditions OrderCondition(const TabOrder& order, bool pinned) {
    TabModelMatches expected;
    for (const auto& item : order) {
      if (const auto* handle = std::get_if<uint64_t>(&item)) {
        expected.tabs.push_back({.handle = *handle});
      } else {
        const auto group = AppliedOwnerToken::Create();
        for (const auto member : std::get<GroupHandles>(item))
          expected.tabs.push_back({.handle = member, .group = group});
      }
    }
    return {applied_.Condition(
        applied_.source().ObserveTabModel(*browser()->GetTabStripModel()),
        expected,
        pinned ? AppliedMatch::kPinnedTabOrder : AppliedMatch::kTabOrder)};
  }
  AppliedConditions OrderCondition(const PinnedHandles& handles, bool pinned) {
    TabOrder order;
    for (const auto handle : handles)
      order.push_back(handle);
    return OrderCondition(order, pinned);
  }
  AppliedConditions AlignedPreviewCondition(int index) {
    auto* preview = GetFavoritesDragPreview();
    CHECK(preview);
    auto* model = browser()->GetTabStripModel();
    return {applied_.Condition(
        applied_.views().ObservePreview(
            *preview, *GetTabViewAt(index), source_handle_,
            model->GetTabAtIndex(index)->GetHandle().raw_value()),
        PreviewApplied{
            .source_handle = source_handle_,
            .tab_handle = static_cast<uint64_t>(
                model->GetTabAtIndex(index)->GetHandle().raw_value()),
            .visible = true},
        AppliedMatch::kPreviewAligned)};
  }
  AppliedConditions ScrollCondition(bool down) {
    return {
        applied_.Condition(applied_.views().ObserveScrollView(
                               *GetTabStripView().unpinned_tabs_scroll_view()),
                           ViewStateMatches{},
                           down ? AppliedMatch::kViewScrolledDown
                                : AppliedMatch::kViewScrollOffset)};
  }
  AppliedConditions DropZoneConditions() {
    auto* list = GetTabStripView().unpinned_tabs_scroll_view();
    gfx::Rect bounds = list->bounds();
    bounds.set_y(yee::kSidebarMetrics.favorites_drop_zone_height +
                 yee::kSidebarMetrics.section_gap);
    bounds.set_height(GetTabStripView().height() - bounds.y());
    return {
        applied_.View(GetFavoritesDropZone(), {.visible = true},
                      AppliedMatch::kViewVisible),
        applied_.View(
            GetFavoritesDropZone(),
            {.bounds = gfx::Rect(
                 0, 0, 0, yee::kSidebarMetrics.favorites_drop_zone_height)},
            AppliedMatch::kViewHeight),
        applied_.View(*list, {.bounds = bounds}, AppliedMatch::kViewBounds)};
  }
  views::View& ViewForHandle(tabs::TabHandle handle) {
    auto* node = GetBrowserView()
                     .vertical_tab_strip_region_view_for_testing()
                     ->root_node_for_testing()
                     ->GetNodeForHandle(handle);
    CHECK(node);
    return *node->view();
  }
  AppliedConditions GroupEntryConditions(tabs::TabHandle source,
                                         tabs::TabHandle settings,
                                         tabs::TabHandle version) {
    auto& settings_view = ViewForHandle(settings);
    auto* group = views::AsViewClass<TabGroupView>(settings_view.parent());
    CHECK(group);
    const auto parent = applied_.views().ObserveView(*group);
    return {applied_.View(ViewForHandle(source), {.parent = parent},
                          AppliedMatch::kViewParent),
            applied_.View(ViewForHandle(version), {.parent = parent},
                          AppliedMatch::kViewParent),
            applied_.Condition(drag_->ObserveGroupDrag(*group),
                               ViewStateMatches{.handling_drag = true,
                                                .collapsed = false,
                                                .vertical_drag_only = true},
                               AppliedMatch::kViewGroupDrag)};
  }
  AppliedConditions HiddenTabConditions(uint64_t handle) {
    auto* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      if (static_cast<uint64_t>(
              model->GetTabAtIndex(i)->GetHandle().raw_value()) == handle)
        return {applied_.View(*GetTabViewAt(i), {.drawn = false},
                              AppliedMatch::kViewDrawn)};
    }
    CHECK(false);
  }
  AppliedCondition CollectionIdle() {
    return applied_.Condition(
        applied_.source().ObserveTabCollectionAnimations(GetBrowserView(), 1),
        AnimationMatches{1, false, 1, 1});
  }
  auto AwaitConditions(Conditions conditions) {
    return ApplyAndWait(
        applied_,
        [this, conditions] {
          auto result = conditions();
          result.push_back(CollectionIdle());
          return result;
        },
        [] {}, AppliedDomain::kDrag, true);
  }
  MultiStep MoveDragMouseTo(AbsolutePositionSpecifier position) {
    auto point = std::make_shared<gfx::Point>();
    return Steps(
        Do([point, position = std::move(position)]() mutable {
          *point = std::visit(
              [](auto&& value) -> gfx::Point {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, gfx::Point>) {
                  return value;
                } else if constexpr (std::is_same_v<T, std::reference_wrapper<
                                                           gfx::Point>>) {
                  return value.get();
                } else {
                  return std::move(value).Run();
                }
              },
              std::move(position));
        }),
        MovePlanned(kBrowserViewElementId, point));
  }
  MultiStep MoveDragMouseTo(
      ElementSpecifier reference,
      RelativePositionSpecifier position = CenterPoint()) {
    auto point = std::make_shared<gfx::Point>();
    return Steps(
        WithElement(
            reference,
            [point, position = std::move(position)](
                ui::TrackedElement* element) mutable {
              *point =
                  std::holds_alternative<CenterPoint>(position)
                      ? element->GetScreenBounds().CenterPoint()
                      : std::move(std::get<RelativePositionCallback>(position))
                            .Run(element);
            }),
        MovePlanned(reference, point));
  }
  // 실제 목적지를 한 번 계산한 다음 arm한다. press가 바꾼 행 위치도 이 시점에
  // 반영한다. ACK 좌표와 preview의 제품 apply serial은 서로 다른 조건이다.
  MultiStep MovePlanned(ElementSpecifier reference,
                        std::shared_ptr<gfx::Point> point) {
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kMoveApplied);
    struct Request {
      AppliedConditions conditions;
      AppliedTicket ticket;
      DispatchDone done;
      base::TimeTicks deadline;
    };
    auto request = std::make_shared<Request>();
#if BUILDFLAG(IS_WIN)
    // Windows ui_controls는 좌표 변환이나 같은 점 이동에서 목적지가 달라질
    // 수 있다. 소유 창에 정확한 DIP를 보내고 버튼 수명은 Kombucha가 관리한다.
    auto move =
        CheckElement(reference, [this, point, request](ui::TrackedElement*) {
          return yee::test::SendMouseMoveSync(
              *point, GetBrowserView().GetWidget()->GetNativeWindow(),
              request->deadline);
        });
#else
    auto move = views::test::InteractiveMouseTestApi::MoveMouseTo(
        reference, base::BindOnce([](std::shared_ptr<gfx::Point> point,
                                     ui::TrackedElement*) { return *point; },
                                  point));
#endif
    return Steps(
        Do([this, point, request] {
          CHECK(drag_);
          auto* controller = GetBrowserView()
                                 .tab_strip_view()
                                 ->GetDragContext()
                                 ->GetDragController();
          const bool same_point =
              *point == display::Screen::Get()->GetCursorScreenPoint();
          const Phase phase = controller && controller->active()
                                  ? (same_point && !controller->started_drag()
                                         ? Phase::kCreated
                                         : Phase::kAttached)
                                  : Phase::kInactive;
          const bool preview_visible =
              phase == Phase::kAttached && !group_drag_;
          auto preview = applied_.Condition(
              drag_->ObserveVerticalPreview(GetTabStripView()),
              PreviewApplied{.source_handle = source_handle_,
                             .visible = preview_visible},
              AppliedMatch::kPreviewSettled,
              preview_visible && !same_point
                  ? AppliedFreshness::kNewApply
                  : AppliedFreshness::kCurrentAfterDispatch);
          TabModelMatches count;
          count.tabs.resize(browser()->GetTabStripModel()->count());
          request->conditions = {
              applied_.Condition(
                  drag_->ObservePhase(phase),
                  DragPhaseMatches{
                      .session = drag_session_,
                      .step = drag_step_,
                      .source_handle = source_handle_,
                      .phase = phase,
                      .attached_browser =
                          phase == Phase::kAttached
                              ? applied_.source().BrowserToken(*browser())
                              : AppliedOwnerToken{},
                      .last_input_screen = *point},
                  AppliedMatch::kDragPhase),
              preview,
              applied_.Condition(applied_.source().ObserveTabModel(
                                     *browser()->GetTabStripModel()),
                                 count, AppliedMatch::kTabCount),
              CollectionIdle()};
          request->deadline =
              base::TimeTicks::Now() + TestTimeouts::action_timeout();
          request->ticket =
              applied_.BeginUntil(request->conditions, AppliedDomain::kPreview,
                                  request->deadline, same_point);
        }),
        ArmApplied(
            kMoveApplied, applied_.session(),
            [request] { return request->ticket; },
            [request] { return request->conditions; }),
        Do([this, request] {
          ASSERT_FALSE(applied_.session().DispatchAsync(
              request->ticket,
              base::BindOnce(
                  [](std::shared_ptr<Request> request, DispatchDone done) {
                    request->done = std::move(done);
                  },
                  request)));
        }),
        std::move(move), Do([this, request] {
          views::test::RunScheduledLayout(&GetBrowserView());
          drag_->InputAckAtCursor(std::move(request->done)).Run();
        }),
        WaitForApplied(kMoveApplied));
  }

  MultiStep ApplyInput(MultiStep input,
                       Conditions expected,
                       Phase phase,
                       int source_index = -1,
                       int group_index = -1) {
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kDragApplied);
    struct Request {
      AppliedConditions conditions;
      AppliedTicket ticket;
      DispatchDone done;
    };
    auto request = std::make_shared<Request>();
    return Steps(
        Do([this, request, expected, phase, source_index, group_index] {
          if (!drag_ || source_index >= 0 || group_index >= 0) {
            ASSERT_TRUE(PrepareNativeDragInput(applied_, *browser()));
            auto* model = browser()->GetTabStripModel();
            group_drag_ = group_index >= 0;
            int index =
                source_index >= 0 ? source_index : model->active_index();
            if (group_drag_)
              index =
                  model->group_model()
                      ->GetTabGroup(
                          model->group_model()->ListTabGroups().at(group_index))
                      ->ListTabs()
                      .start();
            source_handle_ =
                model->GetTabAtIndex(index)->GetHandle().raw_value();
            drag_ = std::make_unique<DragAppliedObserver>(
                applied_.source(),
                *GetBrowserView().tab_strip_view()->GetDragContext(),
                *browser(), source_handle_, ++drag_session_, 1);
            drag_step_ = 1;
          } else {
            drag_->BeginStep(++drag_step_);
          }
          request->conditions = expected();
          TabModelMatches count;
          count.tabs.resize(browser()->GetTabStripModel()->count());
          request->conditions.push_back(applied_.Condition(
              applied_.source().ObserveTabModel(*browser()->GetTabStripModel()),
              count, AppliedMatch::kTabCount));
          request->conditions.push_back(applied_.Condition(
              drag_->ObservePhase(phase),
              DragPhaseMatches{
                  .session = drag_session_,
                  .step = drag_step_,
                  .source_handle = source_handle_,
                  .phase = phase,
                  .attached_browser =
                      phase == Phase::kAttached
                          ? applied_.source().BrowserToken(*browser())
                          : AppliedOwnerToken{}},
              AppliedMatch::kDragPhase));
          request->conditions.push_back(applied_.Condition(
              drag_->ObserveVerticalPreview(GetTabStripView()),
              PreviewApplied{
                  .source_handle = source_handle_,
                  .visible = phase == Phase::kAttached && !group_drag_},
              AppliedMatch::kPreviewSettled));
          request->conditions.push_back(CollectionIdle());
          request->ticket =
              applied_.Begin(request->conditions, AppliedDomain::kDrag);
        }),
        ArmApplied(
            kDragApplied, applied_.session(),
            [request] { return request->ticket; },
            [request] { return request->conditions; }),
        Do([this, request] {
          ASSERT_FALSE(applied_.session().DispatchAsync(
              request->ticket,
              base::BindOnce(
                  [](std::shared_ptr<Request> request, DispatchDone done) {
                    request->done = std::move(done);
                  },
                  request)));
        }),
        std::move(input), Do([this, request] {
          views::test::RunScheduledLayout(&GetBrowserView());
          drag_->InputAckAtCursor(std::move(request->done)).Run();
        }),
        WaitForApplied(kDragApplied));
  }

  AppliedTestContext<AppliedBrowserObservers> applied_;
  std::unique_ptr<DragAppliedObserver> drag_;
  std::vector<uint64_t> initial_handles_;
  uint64_t source_handle_ = 0;
  uint64_t drag_session_ = 0;
  uint64_t drag_step_ = 0;
  bool group_drag_ = false;

  gfx::AnimationTestApi::RenderModeResetter disable_animation_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  gfx::Point saved_drag_destination_;
};

void YeeVerticalTabDragTest::DragUngroupedTabIntoExpandedGroupMiddle(
    DragPosition position) {
  const bool from_below = position == DragPosition::kBelow;
  const int source_index = from_below ? 3 : 0;
  const int group_start = from_below ? 0 : 1;
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  struct GroupMiddleDragHandles {
    std::optional<tabs::TabHandle> source;
    std::optional<tabs::TabHandle> bookmarks;
    std::optional<tabs::TabHandle> settings;
    std::optional<tabs::TabHandle> version;
  };
  // sequence의 좌표·조건 callback이 같은 handle 묶음을 사용한다.
  auto handles = std::make_shared<GroupMiddleDragHandles>();
  gfx::Point middle_drop_point;
  bool middle_drop_point_valid = false;
  const auto expected_order = [&] {
    return TabOrder({GroupHandles({Tab(1), Tab(2), Tab(0), Tab(3)})});
  };
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), Do([&]() {
        if (from_below) {
          model->MoveWebContentsAt(0, source_index, false);
        }
        RecordProperty("group_middle_entry_direction",
                       from_below ? "below" : "above");
        RecordProperty("initial_ungrouped_source_index", source_index);
      }),
      AddTabsToNewGroup({group_start, group_start + 1, group_start + 2}),
      Do([&]() {
        handles->source = model->GetTabAtIndex(source_index)->GetHandle();
        handles->bookmarks = model->GetTabAtIndex(group_start)->GetHandle();
        handles->settings = model->GetTabAtIndex(group_start + 1)->GetHandle();
        handles->version = model->GetTabAtIndex(group_start + 2)->GetHandle();
      }),
      ApplyInput(
          Steps(  // 활성화 입력 뒤 Favorites 공간이 열린 실제 배치를 먼저
                  // 관찰한다.
              // 이때 그룹 멤버가 바뀔 수 있으므로 목적지는 인덱스 대신 handle로
              // 찾는다.
              StartDragTabWithinSidebar(source_index)),
          [&] { return CombineConditions({DropZoneConditions()}); },
          Phase::kAttached, source_index, -1),
      NameView("Current expanded group entry",
               base::BindLambdaForTesting([this, handles, from_below]() {
                 auto* root = GetBrowserView()
                                  .vertical_tab_strip_region_view_for_testing()
                                  ->root_node_for_testing();
                 // 위에서는 첫 행, 아래에서는 마지막 행을 통해 그룹에 진입한다.
                 auto* node = root ? root->GetNodeForHandle(
                                         from_below ? *handles->version
                                                    : *handles->bookmarks)
                                   : nullptr;
                 return node ? node->view() : nullptr;
               })),
      ApplyInput(
          Steps(MoveDragMouseTo("Current expanded group entry")),
          [&] {
            return CombineConditions({GroupEntryConditions(
                *handles->source, *handles->settings, *handles->version)});
          },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        RootTabCollectionNode* root =
            GetBrowserView()
                .vertical_tab_strip_region_view_for_testing()
                ->root_node_for_testing();
        ASSERT_NE(nullptr, root);
        auto view_of = [&](tabs::TabHandle handle) -> views::View* {
          auto* node = root->GetNodeForHandle(handle);
          return node ? node->view() : nullptr;
        };
        views::View* source = view_of(*handles->source);
        views::View* settings = view_of(*handles->settings);
        views::View* version = view_of(*handles->version);
        ASSERT_NE(nullptr, source);
        ASSERT_NE(nullptr, settings);
        ASSERT_NE(nullptr, version);
        auto* group = views::AsViewClass<TabGroupView>(settings->parent());
        ASSERT_NE(nullptr, group);
        ASSERT_EQ(group, source->parent());
        ASSERT_EQ(group, version->parent());
        ASSERT_FALSE(group->IsCollapsed());
        ASSERT_TRUE(group->IsHandlingDrag());
        ASSERT_EQ(DraggedTabsContainer::DragAxes::kVerticalOnly,
                  group->drag_axes());
        const gfx::Rect zero =
            group->GetDraggingViewsBoundsAtPoint(gfx::Point());
        ASSERT_GT(zero.height(), 0);
        const TabDragHandler& drag_handler =
            root->GetController()->GetDragHandler();
        std::optional<int> after_settings;
        std::optional<int> before_version;
        bool after_source = false;
        int dragged_count = 0;
        // 진입 때 원본이 그룹 끝으로 옮겨져도 Settings 뒤 슬롯을 지정한다.
        // native가 원본 행을 제외하는 경계와 실제 잡은 위치를 함께 반영한다.
        for (const auto& row : group->GetTargetLayoutForDrag().child_layouts) {
          if (drag_handler.IsViewDragging(*row.child_view)) {
            EXPECT_EQ(source, row.child_view);
            ++dragged_count;
            after_source = true;
            continue;
          }
          if (row.child_view != settings && row.child_view != version) {
            continue;
          }
          ASSERT_TRUE(row.visible);
          const int boundary = row.bounds.CenterPoint().y() +
                               zero.height() / 2 -
                               (after_source ? zero.height() : 0);
          (row.child_view == settings ? after_settings : before_version) =
              boundary;
        }
        ASSERT_EQ(1, dragged_count);
        ASSERT_TRUE(after_settings.has_value());
        ASSERT_TRUE(before_version.has_value());
        ASSERT_LT(*after_settings, *before_version);
        middle_drop_point = views::View::ConvertPointFromScreen(
            group, settings->GetBoundsInScreen().CenterPoint());
        middle_drop_point.set_y((*after_settings + *before_version) / 2 -
                                zero.CenterPoint().y());
        views::View::ConvertPointToScreen(group, &middle_drop_point);
        auto* unpinned = views::AsViewClass<UnpinnedTabContainerView>(
            GetTabStripView().unpinned_tabs_scroll_view()->contents());
        ASSERT_NE(nullptr, unpinned);
        ASSERT_EQ(static_cast<DraggedTabsContainer*>(group),
                  &unpinned->GetTabDragTarget(middle_drop_point));
        middle_drop_point_valid = true;
      }),
      // Do의 치명적 단언은 callback만 끝낸다. 좌표가 없으면 입력을 중단한다.
      Check([&]() { return middle_drop_point_valid; }),
      ApplyInput(
          Steps(MoveDragMouseTo(
              base::BindLambdaForTesting([&]() { return middle_drop_point; }))),
          [&] { return CombineConditions({AlignedPreviewCondition(2)}); },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_EQ(expected_order(), ReadTabOrder(*model));
        ExpectDragPreviewAlignedWithTab(2);
      }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(expected_order(), false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       CancelDragWithinUnpinnedContainer) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 1)),
          [&] {
            return CombineConditions(
                {OrderCondition(TabOrder({Tab(0), Tab(2), Tab(1)}), false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(PressEscAsync()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      Do([&]() {
        ASSERT_EQ(3, tab_strip_model->count());
        EXPECT_EQ(
            Tab(0),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(0)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(1),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(1)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(2),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(2)->GetHandle().raw_value()));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       CancelFavoriteToGroupDragRestoresSource) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  std::optional<tab_groups::TabGroupId> target_group;
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      AddTabsToNewGroup({1, 2}), Do([&]() {
        const auto groups = tab_strip_model->group_model()->ListTabGroups();
        ASSERT_EQ(1u, groups.size());
        target_group = groups.front();
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(2),
      ApplyInput(
          Steps(ContinueDragToTab(1)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({AlignedPreviewCondition(1)}); },
          Phase::kAttached, -1, -1),
      Do([&]() { ExpectDragPreviewAlignedWithTab(1); }),
      ApplyInput(
          Steps(PressEscAsync()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true),
                 OrderCondition(
                     TabOrder({Tab(0), GroupHandles({Tab(1), Tab(2)})}),
                     false)});
          },
          Phase::kInactive, -1, -1),
      // Escape가 native controller만 끝낸 뒤에도 늦은 capture loss가 올 수
      // 있다.
      Do([&]() {
        ASSERT_TRUE(target_group.has_value());
        tab_strip_model->RemoveFromGroup({1, 2});
        ASSERT_FALSE(
            tab_strip_model->group_model()->ContainsTabGroup(*target_group));
        GetBrowserView()
            .tab_strip_view()
            ->GetDragContext()
            ->OnMouseCaptureLost();
      }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true),
                 OrderCondition(TabOrder({Tab(0), Tab(1), Tab(2)}), false)});
          },
          Phase::kInactive, -1, -1),
      AddTabsToNewGroup({1, 2}), Do([&]() {
        const auto groups = tab_strip_model->group_model()->ListTabGroups();
        ASSERT_EQ(1u, groups.size());
        target_group = groups.front();
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(2),
      ApplyInput(
          Steps(ContinueDragToTab(1)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      // 활성 드래그의 landing 그룹이 제거되는 경로도 동일한 public 이벤트로
      // 검증한다.
      Do([&]() {
        auto* context = GetBrowserView().tab_strip_view()->GetDragContext();
        auto* handler = static_cast<TabDragHandlerImpl*>(context);
        ASSERT_TRUE(target_group.has_value());
        ASSERT_EQ(target_group, handler->IncomingGroupId());
        ASSERT_TRUE(handler->GroupIncomingGapBeforeIndex().has_value());
        ASSERT_TRUE(
            tab_strip_model->group_model()->ContainsTabGroup(*target_group));
        const auto range = tab_strip_model->group_model()
                               ->GetTabGroup(*target_group)
                               ->ListTabs();
        std::vector<int> members;
        for (auto index = range.start(); index < range.end(); ++index) {
          members.push_back(static_cast<int>(index));
        }
        tab_strip_model->RemoveFromGroup(members);
        ASSERT_FALSE(
            tab_strip_model->group_model()->ContainsTabGroup(*target_group));
        context->OnMouseCaptureLost();
      }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true),
                 OrderCondition(TabOrder({Tab(0), Tab(1), Tab(2)}), false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragCollapsedGroupOverExpandedGroup) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewGroup({0, 1}), AddTabsToNewGroup({2}), CollapseGroup(1),
      Do([&]() {
        std::vector<tab_groups::TabGroupId> groups =
            tab_strip_model->group_model()->ListTabGroups();
        ASSERT_EQ(2u, groups.size());
        EXPECT_TRUE(tab_strip_model->group_model()
                        ->GetTabGroup(groups[1])
                        ->visual_data()
                        ->is_collapsed());
      }),
      AwaitConditions([&] {
        return CombineConditions(
            {OrderCondition(TabOrder({GroupHandles({Tab(0), Tab(1)}),
                                      GroupHandles({Tab(2)}), Tab(3)}),
                            false)});
      }),
      ApplyInput(
          Steps(StartDragFromGroupToTab(1, 0)),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(2)}),
                          GroupHandles({Tab(0), Tab(1)}), Tab(3)}),
                false)});
          },
          Phase::kAttached, -1, 1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragCollapsedGroupStaysCollapsed) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewGroup({0, 1}), CollapseGroup(0), Do([&]() {
        std::vector<tab_groups::TabGroupId> groups =
            tab_strip_model->group_model()->ListTabGroups();
        ASSERT_EQ(1u, groups.size());
        EXPECT_TRUE(tab_strip_model->group_model()
                        ->GetTabGroup(groups[0])
                        ->visual_data()
                        ->is_collapsed());
      }),
      ApplyInput(
          Steps(StartDragFromGroupToTab(0, 2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, 0),
      // Dragging a collapsed group should keep it collapsed.
      Do([&]() {
        std::vector<tab_groups::TabGroupId> groups =
            tab_strip_model->group_model()->ListTabGroups();
        ASSERT_EQ(1u, groups.size());
        EXPECT_TRUE(tab_strip_model->group_model()
                        ->GetTabGroup(groups[0])
                        ->visual_data()
                        ->is_collapsed());
      }),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteAfterCollapsedGroupStaysTopLevel) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2}), CollapseGroup(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 3)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SaveTabCenter(3),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(1), Tab(2)}), Tab(0), Tab(3)}),
                false)});
          },
          Phase::kInactive, -1, -1),
      Check([&]() { return !model->GetTabGroupForTab(2).has_value(); }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteBeforeFirstTabCommitsFirst) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles(), true),
                 OrderCondition(TabOrder({Tab(0), Tab(1), Tab(2)}), false)});
          },
          Phase::kInactive, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(model->IsTabPinned(0));
        EXPECT_FALSE(GetTabStripView().pinned_tabs_scroll_view()->GetVisible());
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_FALSE(GetFavoritesDragPreview()->GetVisible());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteBelowLastTabCommitsLast) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBelowTab(2),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(TabOrder({Tab(1), Tab(2), Tab(0)}), false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteCanRetargetWithinExpandedGroup) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2, 3}), AwaitConditions([&] {
        return CombineConditions(
            {OrderCondition(PinnedHandles({Tab(0)}), true)});
      }),
      AwaitConditions([&] {
        return CombineConditions({OrderCondition(
            TabOrder({Tab(0), GroupHandles({Tab(1), Tab(2), Tab(3)})}),
            false)});
      }),
      ApplyInput(
          Steps(  // Enter through the first group row, then choose another
                  // internal gap.
              // This catches stale-gap hit testing that used to lock
              // Favorite-origin drags to the first slot selected on group
              // entry.
              StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(3),
      ApplyInput(
          Steps(ContinueDragToTab(1)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] {
            return CombineConditions(
                {AlignedPreviewCondition(1),
                 OrderCondition(PinnedHandles({Tab(0)}), true),
                 OrderCondition(
                     TabOrder({Tab(0), GroupHandles({Tab(1), Tab(2), Tab(3)})}),
                     false)});
          },
          Phase::kAttached, -1, -1),
      Do([&]() { ExpectDragPreviewAlignedWithTab(1); }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles(), true),
                 OrderCondition(
                     TabOrder({GroupHandles({Tab(1), Tab(2), Tab(0), Tab(3)})}),
                     false)});
          },
          Phase::kInactive, -1, -1),
      Do([&]() {
        const std::optional<tab_groups::TabGroupId> destination_group =
            tab_strip_model->GetTabGroupForTab(2);
        ASSERT_TRUE(destination_group.has_value());
        EXPECT_EQ(destination_group, tab_strip_model->GetTabGroupForTab(0));
        EXPECT_EQ(destination_group, tab_strip_model->GetTabGroupForTab(1));
        EXPECT_EQ(destination_group, tab_strip_model->GetTabGroupForTab(3));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragFavoriteIntoTabList) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      ApplyInput(
          Steps(  // Crossing out of Favorites is deferred until drop. The
                  // pinned model
              // order must therefore stay intact while the pointer chooses a
              // slot.
              StartDragBetweenTabs(0, 1)),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true),
                 OrderCondition(TabOrder({
                                    Tab(0),
                                    Tab(1),
                                    Tab(2),
                                }),
                                false)});
          },
          Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(  // Return to Favorites once so the last-Favorite zero-state
                  // has opened
              // but no unpinned gap is shifting the rows. Capture that compact
              // target, then enter through the first row and retarget to the
              // middle gap.
              ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(2),
      ApplyInput(
          Steps(ContinueDragToTab(1)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(PinnedHandles(), true),
                                      OrderCondition(TabOrder({
                                                         Tab(1),
                                                         Tab(0),
                                                         Tab(2),
                                                     }),
                                                     false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteOnGroupHeaderStaysTopLevel) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2}),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SaveGroupHeaderCenter(0),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({AlignedPreviewCondition(3)}); },
          Phase::kAttached, -1, -1),
      Do([&]() { ExpectDragPreviewAlignedWithTab(3); }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles(), true),
                 OrderCondition(
                     TabOrder({Tab(0), GroupHandles({Tab(1), Tab(2)}), Tab(3)}),
                     false)});
          },
          Phase::kInactive, -1, -1),
      Check([&]() {
        return !model->IsTabPinned(0) &&
               !model->GetTabGroupForTab(0).has_value();
      }),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetTabStripView().pinned_tabs_scroll_view()->GetVisible());
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_FALSE(GetFavoritesDragPreview()->GetVisible());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteToExpandedGroupFirstSlot) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2, 3}),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(1),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(0), Tab(1), Tab(2), Tab(3)})}),
                false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragFavoriteToExpandedGroupLastSlot) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2, 3}),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(  // Favorite 해제로 순서가 바뀌어도 마지막 그룹 탭의 정체성을
                  // 유지한다.
              ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToInitialTab(3)),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointAfterInitialTab(3),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(1), Tab(2), Tab(3), Tab(0)})}),
                false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragGroupHeader) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewGroup({1, 2}), AwaitConditions([&] {
        return CombineConditions({OrderCondition(
            TabOrder({Tab(0), GroupHandles({Tab(1), Tab(2)}), Tab(3)}),
            false)});
      }),
      ApplyInput(
          Steps(StartDragFromGroupToTab(0, 0)),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(1), Tab(2)}), Tab(0), Tab(3)}),
                false)});
          },
          Phase::kAttached, -1, 0),
      CheckResult(
          [&]() {
            TabStripModel* model = browser()->GetTabStripModel();
            std::vector<tab_groups::TabGroupId> groups =
                model->group_model()->ListTabGroups();
            if (groups.empty()) {
              return false;
            }
            return model->group_model()
                ->GetTabGroup(groups[0])
                ->visual_data()
                ->is_collapsed();
          },
          true),
      ApplyInput(
          Steps(ContinueDragToTab(3)),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({Tab(0), Tab(3), GroupHandles({Tab(1), Tab(2)})}),
                false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult(
          [&]() {
            TabStripModel* model = browser()->GetTabStripModel();
            std::vector<tab_groups::TabGroupId> groups =
                model->group_model()->ListTabGroups();
            if (groups.empty()) {
              return false;
            }
            return !model->group_model()
                        ->GetTabGroup(groups[0])
                        ->visual_data()
                        ->is_collapsed();
          },
          true));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragMultipleTabs) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      ApplyInput(
          Steps(SelectTabAt(1)), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult(
          [this]() { return browser()->GetTabStripModel()->IsTabSelected(1); },
          true),
      CheckResult(
          [this]() { return browser()->GetTabStripModel()->IsTabSelected(2); },
          true),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 0)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         Tab(1),
                                                         Tab(2),
                                                         Tab(0),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragMultipleTabsInGroup) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTabsToNewGroup({0, 1}),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     GroupHandles({
                                                         Tab(0),
                                                         Tab(1),
                                                     }),
                                                     Tab(2),
                                                     Tab(3),
                                                 }),
                                                 false)});
      }),
      ApplyInput(
          Steps(SelectTabAt(2)), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult(
          [this]() { return browser()->GetTabStripModel()->IsTabSelected(3); },
          true),
      CheckResult(
          [this]() { return browser()->GetTabStripModel()->IsTabSelected(2); },
          true),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 1)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         GroupHandles({
                                                             Tab(0),
                                                             Tab(2),
                                                             Tab(3),
                                                             Tab(1),
                                                         }),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(ContinueDragToTab(0)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         GroupHandles({
                                                             Tab(2),
                                                             Tab(3),
                                                             Tab(0),
                                                             Tab(1),
                                                         }),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragOutOfGroup) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  std::optional<tabs::TabHandle> source_handle;
  std::optional<tabs::TabHandle> target_handle;
  std::optional<tab_groups::TabGroupId> original_group;
  bool snapshot_valid = false;
  bool destination_valid = false;
  gfx::Point destination;
  const auto expected_order = [&] {
    return TabOrder({GroupHandles({Tab(0)}), Tab(2), Tab(1)});
  };
#if BUILDFLAG(IS_WIN)
  yee::test::ScopedNativeInputTrace native_input(
      GetBrowserView().GetWidget());
#endif
  const auto record_drag = [&](const std::string& phase) {
    snapshot_valid = false;
    auto* root = GetBrowserView()
                     .vertical_tab_strip_region_view_for_testing()
                     ->root_node_for_testing();
    if (!root || !source_handle || !target_handle) {
      return;
    }
    auto* source = root->GetNodeForHandle(*source_handle);
    auto* target = root->GetNodeForHandle(*target_handle);
    if (!source || !target || !source->view() || !target->view()) {
      return;
    }
    RecordProperty(phase + "_source_bounds",
                   source->view()->GetBoundsInScreen().ToString());
    RecordProperty(phase + "_target_bounds",
                   target->view()->GetBoundsInScreen().ToString());
    RecordProperty(phase + "_unpinned_y",
                   GetTabStripView().unpinned_tabs_scroll_view()->y());
    RecordProperty(phase + "_drag_active", TabDragController::IsActive());
    RecordProperty(phase + "_tab_order",
                   testing::PrintToString(ReadTabOrder(*tab_strip_model)));
#if BUILDFLAG(IS_WIN)
    native_input.Snapshot(phase,
                          target->view()->GetBoundsInScreen().CenterPoint());
    RecordProperty("drag_out_native_input", native_input.ToJson());
#endif
    snapshot_valid = true;
  };
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTabsToNewGroup({0, 1}),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     GroupHandles({
                                                         Tab(0),
                                                         Tab(1),
                                                     }),
                                                     Tab(2),
                                                 }),
                                                 false)});
      }),
      Do([&]() {
        source_handle = tab_strip_model->GetTabAtIndex(1)->GetHandle();
        target_handle = tab_strip_model->GetTabAtIndex(2)->GetHandle();
        original_group = tab_strip_model->GetTabAtIndex(0)->GetGroup();
        record_drag("before_activation");
      }),
      Check([&]() { return snapshot_valid && original_group.has_value(); }),
      ApplyInput(
          Steps(  // 첫 이동으로 열린 Favorites 공간이 목적지 행을 옮긴 뒤
                  // 좌표를 읽는다.
              StartDragTabWithinSidebar(1)),
          [&] { return CombineConditions({DropZoneConditions()}); },
          Phase::kAttached, 1, -1),
      RunScheduledLayout(), Do([&]() { record_drag("after_activation"); }),
      Check([&]() { return snapshot_valid; }), Do([&]() {
        destination_valid = false;
        auto* root = GetBrowserView()
                         .vertical_tab_strip_region_view_for_testing()
                         ->root_node_for_testing();
        if (!root || !source_handle || !target_handle) {
          return;
        }
        auto* source = root->GetNodeForHandle(*source_handle);
        auto* target = root->GetNodeForHandle(*target_handle);
        if (!source || !source->view() || !target || !target->view() ||
            !target->view()->IsDrawn() ||
            target->view()->GetBoundsInScreen().IsEmpty()) {
          return;
        }
        auto* unpinned = views::AsViewClass<UnpinnedTabContainerView>(
            target->view()->parent());
        ASSERT_NE(nullptr, unpinned);
        ASSERT_EQ(unpinned, source->view()->parent());
        ASSERT_TRUE(unpinned->IsHandlingDrag());
        ASSERT_EQ(DraggedTabsContainer::DragAxes::kVerticalOnly,
                  unpinned->drag_axes());
        const gfx::Rect zero =
            unpinned->GetDraggingViewsBoundsAtPoint(gfx::Point());
        ASSERT_GT(zero.height(), 0);
        const TabDragHandler& drag_handler =
            root->GetController()->GetDragHandler();
        std::optional<int> after_target;
        bool after_source = false;
        int dragged_count = 0;
        // 활성화 중 원본이 이미 목적지 뒤로 나와도 같은 '뒤' 슬롯을 지정한다.
        // native 삽입 경계처럼 원본 행을 제외하고 실제 잡은 위치를 반영한다.
        for (const auto& row :
             unpinned->GetTargetLayoutForDrag().child_layouts) {
          if (drag_handler.IsViewDragging(*row.child_view)) {
            EXPECT_EQ(source->view(), row.child_view);
            ++dragged_count;
            after_source = true;
            continue;
          }
          if (row.child_view != target->view()) {
            continue;
          }
          ASSERT_TRUE(row.visible);
          after_target = row.bounds.CenterPoint().y() + zero.height() / 2 -
                         (after_source ? zero.height() : 0);
        }
        ASSERT_EQ(1, dragged_count);
        ASSERT_TRUE(after_target.has_value());
        destination = views::View::ConvertPointFromScreen(
            unpinned, target->view()->GetBoundsInScreen().CenterPoint());
        destination.set_y(*after_target + zero.height() / 2 -
                          zero.CenterPoint().y());
        views::View::ConvertPointToScreen(unpinned, &destination);
        ASSERT_EQ(static_cast<DraggedTabsContainer*>(unpinned),
                  &unpinned->GetTabDragTarget(destination));
        RecordProperty("destination_after_target_boundary", *after_target);
        RecordProperty("destination_drag_bounds_at_zero", zero.ToString());
        RecordProperty("resolved_destination", destination.ToString());
#if BUILDFLAG(IS_WIN)
        native_input.Snapshot("resolved_destination", destination);
        RecordProperty("drag_out_native_input", native_input.ToJson());
#endif
        destination_valid = true;
      }),
      Check([&]() { return destination_valid; }),
      ApplyInput(
          Steps(MoveDragMouseTo(
              base::BindLambdaForTesting([&]() { return destination; }))),
          [&] {
            return CombineConditions({OrderCondition(expected_order(), false)});
          },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() { record_drag("after_target_move"); }),
      Check([&]() { return snapshot_valid; }),
      // 활성화 때 관찰한 이전 값만으로 대기가 끝나지 않도록 실제 모델도 읽는다.
      CheckTabOrder(expected_order),
      Do([&]() { record_drag("outside_group"); }),
      Check([&]() { return snapshot_valid; }),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      Do([&]() {
        ASSERT_EQ(3, tab_strip_model->count());
        EXPECT_EQ(
            Tab(0),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(0)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(2),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(1)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(1),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(2)->GetHandle().raw_value()));
        EXPECT_EQ(original_group,
                  tab_strip_model->GetTabAtIndex(0)->GetGroup());
        EXPECT_FALSE(tab_strip_model->GetTabAtIndex(2)->GetGroup().has_value());
        EXPECT_EQ(*source_handle,
                  tab_strip_model->GetTabAtIndex(2)->GetHandle());
        EXPECT_EQ(*target_handle,
                  tab_strip_model->GetTabAtIndex(1)->GetHandle());
        record_drag("released");
      }));
#if BUILDFLAG(IS_WIN)
  // 대기나 입력 단계가 실패해도 마지막 실제 이벤트 기록은 남긴다.
  RecordProperty("drag_out_native_input", native_input.ToJson());
#endif
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragOverSplit) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewSplit(2, 3), AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     Tab(0),
                                                     Tab(1),
                                                     Tab(2),
                                                     Tab(3),
                                                 }),
                                                 false)});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(1, 0)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         Tab(1),
                                                         Tab(0),
                                                         Tab(2),
                                                         Tab(3),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, 1, -1),
      ApplyInput(
          Steps(  // Dragging from index 0 to index 2 (split) should put the
                  // dragged tab to
              // index 3.
              // 빈 Favorites 초대가 첫 포인터를 덮을 수 있으므로 Tab 목록에
              // 먼저 진입한다. source 행이 복원된 뒤 split의 좌표를 다시
              // 읽는다.
              MoveDragMouseTo(base::BindLambdaForTesting([this]() {
                const gfx::Rect bounds = GetTabStripView()
                                             .unpinned_tabs_scroll_view()
                                             ->GetBoundsInScreen();
                return gfx::Point(
                    bounds.CenterPoint().x(),
                    bounds.y() + yee::kSidebarMetrics.tab_row_height / 2);
              }))),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      RunScheduledLayout(), Check([this]() {
        return !vertical_tab_strip_controller()
                    ->GetDragHandler()
                    .CollapseDraggedSourceSlot();
      }),
      CheckTabOrder(
          [this] { return TabOrder({Tab(1), Tab(0), Tab(2), Tab(3)}); }),
      ApplyInput(
          Steps(ContinueDragToTab(2)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         Tab(0),
                                                         Tab(2),
                                                         Tab(3),
                                                         Tab(1),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragOverSplitInGroup) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewGroup({0, 1, 2, 3}), AddTabsToNewSplit(2, 3),
      AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     GroupHandles({
                                                         Tab(0),
                                                         Tab(1),
                                                         Tab(2),
                                                         Tab(3),
                                                     }),
                                                 }),
                                                 false)});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(1, 0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 1, -1),
      ApplyInput(
          Steps(ContinueDragToTab(2)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         GroupHandles({
                                                             Tab(0),
                                                             Tab(2),
                                                             Tab(3),
                                                             Tab(1),
                                                         }),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(  // Dragging from index 3 to index 2 (split) should put the
                  // dragged tab to
              // index 1.
              ContinueDragToTab(2)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         GroupHandles({
                                                             Tab(0),
                                                             Tab(1),
                                                             Tab(2),
                                                             Tab(3),
                                                         }),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragSplitTabs) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3),
      AddTabsToNewSplit(2, 3), AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     Tab(0),
                                                     Tab(1),
                                                     Tab(2),
                                                     Tab(3),
                                                 }),
                                                 false)});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 0)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         Tab(2),
                                                         Tab(3),
                                                         Tab(0),
                                                         Tab(1),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(ContinueDragToTab(2)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({
                                                         Tab(0),
                                                         Tab(2),
                                                         Tab(3),
                                                         Tab(1),
                                                     }),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragTabAfterExistingFavoriteCommitsPin) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1), PinTabAt(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(1, 0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 1, -1),
      ApplyInput(
          Steps(
              ContinueDragToFavoriteSlot(/*target_item_count=*/2, /*slot=*/1)),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0), Tab(1)}), true)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       DragTabBeforeExistingFavoriteCommitsPin) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1), PinTabAt(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(1, 0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 1, -1),
      ApplyInput(
          Steps(
              ContinueDragToFavoriteSlot(/*target_item_count=*/2, /*slot=*/0)),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(1), Tab(0)}), true)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragToScroll) {
  const char kScrollViewName[] = "Scroll View";
  const char kFirstTabName[] = "First Tab";
  const int kTabCount = 50;

  RunTestSequence(
      Do([&]() {
        // 큰 화면에서도 아래로 넘칠 만큼의 작업 영역을 준비한다.
        gfx::Rect bounds = browser()->GetWindow()->GetBounds();
        bounds.set_height(std::min(bounds.height(), 600));
        browser()->GetWindow()->SetBounds(bounds);
      }),
      // Add many tabs and ensure the first one is active so we start at the
      // top.
      Do([this]() {
        for (int i = 1; i < kTabCount; ++i) {
          std::unique_ptr<content::WebContents> contents =
              content::WebContents::Create(
                  content::WebContents::CreateParams(browser()->GetProfile()));
          tab_strip_model()->InsertWebContentsAt(tab_strip_model()->count(),
                                                 std::move(contents),
                                                 ADD_INHERIT_OPENER);
        }
        tab_strip_model()->ActivateTabAt(
            0, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kOther));
      }),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        views::ScrollView* scroll = strip.unpinned_tabs_scroll_view();
        ASSERT_TRUE(scroll);
        ASSERT_TRUE(scroll->contents());
        EXPECT_EQ(strip.height(), scroll->bounds().bottom());
        EXPECT_TRUE(scroll->IsVerticalContentOverflowing());
        EXPECT_FALSE(scroll->horizontal_scroll_bar()->GetVisible());
        EXPECT_LE(scroll->contents()->GetVisibleBounds().height(),
                  scroll->height());
      }),
      NameScrollView(kScrollViewName), AwaitConditions([&] {
        return CombineConditions({ScrollCondition(false)});
      }),
      // Start dragging the first tab.
      NameTabViewAt(kFirstTabName, 0),
      ApplyInput(
          Steps(MoveDragMouseTo(kFirstTabName)),
          [&] { return CombineConditions({}); }, Phase::kInactive, -1, -1),
      ApplyInput(
          Steps(ClickMouse(ui_controls::MouseButton::LEFT, /*release=*/false)),
          [&] { return CombineConditions({}); }, Phase::kCreated, -1, -1),
      ApplyInput(
          Steps(  // Move mouse to the bottom edge of the scroll view to trigger
                  // scrolling
              // down.
              MoveDragMouseTo(kScrollViewName,
                              base::BindOnce([](ui::TrackedElement* element) {
                                const gfx::Rect bounds =
                                    element->GetScreenBounds();
                                return gfx::Point(bounds.CenterPoint().x(),
                                                  bounds.bottom() - 1);
                              }))),
          [&] { return CombineConditions({ScrollCondition(true)}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(  // Move mouse to the top edge of the scroll view to trigger
                  // scrolling up.
              MoveDragMouseTo(kScrollViewName,
                              base::BindOnce([](ui::TrackedElement* element) {
                                const gfx::Rect bounds =
                                    element->GetScreenBounds();
                                return gfx::Point(bounds.CenterPoint().x(),
                                                  bounds.y() + 1);
                              }))),
          [&] { return CombineConditions({ScrollCondition(false)}); },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragWithinGroup) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTabsToNewGroup({0, 1, 2}), AwaitConditions([&] {
        return CombineConditions({OrderCondition(TabOrder({
                                                     GroupHandles({
                                                         Tab(0),
                                                         Tab(1),
                                                         Tab(2),
                                                     }),
                                                 }),
                                                 false)});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 1)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({GroupHandles({
                                                         Tab(0),
                                                         Tab(2),
                                                         Tab(1),
                                                     })}),
                                                     false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(ContinueDragToTab(0)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({GroupHandles({
                                                         Tab(2),
                                                         Tab(0),
                                                         Tab(1),
                                                     })}),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToTab(2)),
          [&] {
            return CombineConditions({OrderCondition(TabOrder({GroupHandles({
                                                         Tab(0),
                                                         Tab(1),
                                                         Tab(2),
                                                     })}),
                                                     false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      Do([&]() {
        ASSERT_EQ(3, tab_strip_model->count());
        EXPECT_EQ(
            Tab(0),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(0)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(1),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(1)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(2),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(2)->GetHandle().raw_value()));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest, DragWithinUnpinnedContainer) {
  TabStripModel* tab_strip_model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, tab_strip_model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 1)),
          [&] {
            return CombineConditions(
                {OrderCondition(TabOrder({Tab(0), Tab(2), Tab(1)}), false)});
          },
          Phase::kAttached, 2, -1),
      ApplyInput(
          Steps(ContinueDragToTab(2)),
          [&] {
            return CombineConditions(
                {OrderCondition(TabOrder({Tab(0), Tab(1), Tab(2)}), false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ContinueDragToTab(0)),
          [&] {
            return CombineConditions(
                {OrderCondition(TabOrder({Tab(2), Tab(0), Tab(1)}), false)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(  // Release the drag and ensure tab ordering remains.
              ReleaseMouse()),
          [&] { return CombineConditions({}); }, Phase::kInactive, -1, -1),
      Do([&]() {
        ASSERT_EQ(3, tab_strip_model->count());
        EXPECT_EQ(
            Tab(2),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(0)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(0),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(1)->GetHandle().raw_value()));
        EXPECT_EQ(
            Tab(1),
            static_cast<uint64_t>(
                tab_strip_model->GetTabAtIndex(2)->GetHandle().raw_value()));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       FullFavoritesRejectsWithoutNativeReorder) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      Do([&]() {
        for (int i = 1; i < yee::kSidebarMetrics.favorites_max_count; ++i) {
          chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, false);
        }
        chrome::AddTabAt(browser(), GURL(chrome::kChromeUIVersionURL), -1,
                         false);
        chrome::AddTabAt(browser(), GURL(chrome::kChromeUISettingsURL), -1,
                         false);
        for (int i = 0; i < yee::kSidebarMetrics.favorites_max_count; ++i) {
          model->SetTabPinned(i, true);
        }
        initial_handles_.clear();
        for (int i = 0; i < model->count(); ++i)
          initial_handles_.push_back(
              model->GetTabAtIndex(i)->GetHandle().raw_value());
        EXPECT_EQ(
            GURL(chrome::kChromeUIVersionURL),
            model->GetWebContentsAt(yee::kSidebarMetrics.favorites_max_count)
                ->GetURL());
        EXPECT_EQ(
            GURL(chrome::kChromeUISettingsURL),
            model
                ->GetWebContentsAt(yee::kSidebarMetrics.favorites_max_count + 1)
                ->GetURL());
        views::test::RunScheduledLayout(&GetBrowserView());
      }),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); },
                  yee::kSidebarMetrics.favorites_max_count),
      CheckResult(
          [&]() {
            return yee::CanFavoriteContext(
                *model, yee::kSidebarMetrics.favorites_max_count + 1);
          },
          false),
      ApplyInput(
          Steps(StartDragBetweenTabs(
              yee::kSidebarMetrics.favorites_max_count + 1, 0)),
          [&] { return CombineConditions({}); }, Phase::kAttached,
          yee::kSidebarMetrics.favorites_max_count + 1, -1),
      ApplyInput(
          Steps(ContinueDragToFavoriteSlot(
              /*target_item_count=*/yee::kSidebarMetrics.favorites_max_count +
                  1,
              /*slot=*/0)),
          [&] {
            return CombineConditions({AlignedPreviewCondition(
                yee::kSidebarMetrics.favorites_max_count)});
          },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        ExpectDragPreviewAlignedWithTab(
            yee::kSidebarMetrics.favorites_max_count);
      }),
      ApplyInput(
          Steps(ReleaseMouse()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); },
                  yee::kSidebarMetrics.favorites_max_count),
      Do([&]() {
        EXPECT_EQ(
            Tab(yee::kSidebarMetrics.favorites_max_count),
            static_cast<uint64_t>(
                model->GetTabAtIndex(yee::kSidebarMetrics.favorites_max_count)
                    ->GetHandle()
                    .raw_value()));
        EXPECT_EQ(Tab(yee::kSidebarMetrics.favorites_max_count + 1),
                  static_cast<uint64_t>(
                      model
                          ->GetTabAtIndex(
                              yee::kSidebarMetrics.favorites_max_count + 1)
                          ->GetHandle()
                          .raw_value()));
        EXPECT_FALSE(
            model->IsTabPinned(yee::kSidebarMetrics.favorites_max_count));
        EXPECT_FALSE(
            model->IsTabPinned(yee::kSidebarMetrics.favorites_max_count + 1));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       LoneTabCanPinAndUnpinInsideItsOwnSidebar) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      ApplyInput(
          Steps(StartDragTabWithinSidebar(0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true)});
          },
          Phase::kInactive, -1, -1),
      ApplyInput(
          Steps(StartDragTabWithinSidebar(0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToEmptyTabList()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(PinnedHandles(), true)});
          },
          Phase::kInactive, -1, -1),
      Check([&]() { return model->count() == 1 && !model->IsTabPinned(0); }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeProjectHiddenGroupRowsKeepFavoriteDropAnchor) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL project_url = embedded_test_server()->GetURL("/title1.html");
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  auto* model = browser()->GetTabStripModel();
  tabs::TabInterface* project_tab = nullptr;
  RunTestSequence(
      AddTab(kSecondTab, project_url, 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUIBookmarksURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUISettingsURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2, 3}), Do([&] {
        auto* profile = browser()->GetProfile();
        auto* service =
            yee::projects::ProjectServiceFactory::GetForProfile(profile);
        ASSERT_TRUE(service);
        auto project = service->CreateProject(u"드래그 검수");
        ASSERT_TRUE(project);
        project_tab = model->GetTabAtIndex(1);
        ASSERT_TRUE(yee::projects::ProjectTabCoordinator::GetForProfile(profile)
                        ->Attach(*project_tab, *project, {}));
      }),
      RunScheduledLayout(), AwaitConditions([&] {
        return CombineConditions({HiddenTabConditions(Tab(1))});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(3),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder({GroupHandles({Tab(1), Tab(2), Tab(0), Tab(3)})}),
                false)});
          },
          Phase::kInactive, -1, -1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeProjectHiddenSplitGroupRowsKeepFavoriteDropAnchor) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL project_url = embedded_test_server()->GetURL("/title1.html");
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFifthTab);
  auto* model = browser()->GetTabStripModel();
  tabs::TabInterface* project_tab = nullptr;
  RunTestSequence(
      AddTab(kSecondTab, project_url, 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUIBookmarksURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUISettingsURL), 3),
      AddTab(kFifthTab, GURL(chrome::kChromeUIVersionURL), 4), PinTabAt(0),
      AddTabsToNewGroup({1, 2, 3, 4}), Do([&] {
        auto* profile = browser()->GetProfile();
        auto* service =
            yee::projects::ProjectServiceFactory::GetForProfile(profile);
        ASSERT_TRUE(service);
        auto project = service->CreateProject(u"드래그 검수");
        ASSERT_TRUE(project);
        project_tab = model->GetTabAtIndex(1);
        ASSERT_TRUE(yee::projects::ProjectTabCoordinator::GetForProfile(profile)
                        ->Attach(*project_tab, *project, {}));
        model->ActivateTabAt(2);
        model->AddToNewSplit({3}, split_tabs::SplitTabVisualData(),
                             split_tabs::SplitTabCreatedSource::kToolbarButton);
      }),
      RunScheduledLayout(), AwaitConditions([&] {
        return CombineConditions({HiddenTabConditions(Tab(1))});
      }),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(ContinueDragToFavoritesDropZone()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      SavePointBeforeTab(4), Do([&] {
        auto* const anchor =
            GetBrowserView()
                .vertical_tab_strip_region_view_for_testing()
                ->root_node_for_testing()
                ->GetNodeForHandle(model->GetTabAtIndex(4)->GetHandle());
        RecordProperty("drop_anchor_bounds",
                       anchor->view()->GetBoundsInScreen().ToString());
      }),
      ApplyInput(
          Steps(ContinueDragToSavedTabCenter()),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                TabOrder(
                    {GroupHandles({Tab(1), Tab(2), Tab(3), Tab(0), Tab(4)})}),
                false)});
          },
          Phase::kInactive, -1, -1),
      Do([&] {
        // 순서가 다를 때도 실제 모델 결과를 남기며 판정은 그대로 유지한다.
        RecordProperty("actual_order_after_drop",
                       testing::PrintToString(ReadTabOrder(*model)));
        RecordProperty("actual_pinned_after_drop",
                       testing::PrintToString(ReadPinnedHandles(*model)));
        RecordProperty("window_active_after_drop",
                       GetBrowserView().GetWidget()->IsActive());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarDraggingOneOfTwoFavoritesKeepsDock) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      PinTabAt(1),
      ApplyInput(
          Steps(StartDragBetweenTabs(1, 2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 1, -1),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_TRUE(strip.pinned_tabs_scroll_view()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  strip.pinned_tabs_scroll_view()->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height +
                      yee::kSidebarMetrics.favorites_cell_gap,
                  strip.unpinned_tabs_scroll_view()->y());
        EXPECT_EQ(2, model->IndexOfFirstNonPinnedTab());
      }),
      ApplyInput(
          Steps(ContinueDragToTab(2)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_TRUE(GetTabStripView().pinned_tabs_scroll_view()->GetVisible());
        EXPECT_EQ(2, model->IndexOfFirstNonPinnedTab());
      }),
      ApplyInput(
          Steps(PressEscAsync()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); }, 2));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarEmptyFavoritesDropZoneLifecycle) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      RunScheduledLayout(), Check([&]() {
        return !GetFavoritesDropZone().GetVisible() &&
               GetTabStripView().unpinned_tabs_scroll_view()->y() ==
                   yee::kSidebarMetrics.tab_list_top_inset;
      }),
      ApplyInput(
          Steps(StartDragTabWithinSidebar(0)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        views::View& drop_zone = GetFavoritesDropZone();
        EXPECT_TRUE(drop_zone.GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.section_horizontal_inset, drop_zone.x());
        EXPECT_EQ(0, drop_zone.y());
        EXPECT_EQ(yee::kSidebarMetrics.favorites_drop_zone_height,
                  drop_zone.height());
        EXPECT_FALSE(strip.pinned_tabs_scroll_view()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.favorites_drop_zone_height +
                      yee::kSidebarMetrics.section_gap,
                  strip.unpinned_tabs_scroll_view()->y());
        EXPECT_EQ(strip.height(),
                  strip.unpinned_tabs_scroll_view()->bounds().bottom());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_TRUE(GetFavoritesDragPreview()->GetVisible());
      }),
      ApplyInput(
          Steps(PressEscAsync()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_list_top_inset,
                  GetTabStripView().unpinned_tabs_scroll_view()->y());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_FALSE(GetFavoritesDragPreview()->GetVisible());
        EXPECT_EQ(1, model->count());
        EXPECT_FALSE(model->IsTabPinned(0));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarExpandedGroupUsesSharedRowMetrics) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTabsToNewGroup({0, 1}), RunScheduledLayout(), Do([&]() {
        auto* group =
            views::AsViewClass<TabGroupView>(GetTabViewAt(0)->parent());
        ASSERT_TRUE(group);
        ASSERT_TRUE(group->group_header());
        EXPECT_TRUE(group->group_header()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  group->group_header()->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  GetTabViewAt(0)->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  GetTabViewAt(1)->height());
        EXPECT_EQ(TabGroupView::kTabLeadingPadding, GetTabViewAt(0)->x());
        EXPECT_EQ(TabGroupView::kTabLeadingPadding, GetTabViewAt(1)->x());
        EXPECT_TRUE(group->group_header()->title_label_for_testing());
        EXPECT_TRUE(group->group_header()->editor_bubble_button());
        EXPECT_TRUE(group->group_header()->collapse_icon_for_testing());
      }),
      CollapseGroup(0), RunScheduledLayout(), Do([&]() {
        auto* group =
            views::AsViewClass<TabGroupView>(GetTabViewAt(0)->parent());
        ASSERT_TRUE(group);
        EXPECT_TRUE(group->IsCollapsed());
        EXPECT_TRUE(group->group_header()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  group->group_header()->height());
        EXPECT_FALSE(GetTabViewAt(0)->GetVisible());
        EXPECT_FALSE(GetTabViewAt(1)->GetVisible());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarExpandedSectionsFollowMetrics) {
  RunTestSequence(
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        views::View* label =
            strip.GetViewByID(yee::kSidebarFavoritesLabelViewId);
        // The current native Sidebar does not need to instantiate a Favorites
        // label at all.  A legacy host that still owns the view must keep it
        // hidden; both forms satisfy the no-heading product contract.
        EXPECT_TRUE(!label || !label->GetVisible());
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_FALSE(strip.pinned_tabs_scroll_view()->GetVisible());
        EXPECT_FALSE(strip.GetTabsSeparator()->GetVisible());

        views::ScrollView* tabs = strip.unpinned_tabs_scroll_view();
        ASSERT_TRUE(tabs);
        EXPECT_TRUE(tabs->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_list_top_inset, tabs->y());
        EXPECT_EQ(strip.height(), tabs->bounds().bottom());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  GetTabViewAt(0)->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_strip_horizontal_padding,
                  GetTabViewAt(0)->x());
      }),
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1), PinTabAt(0),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        views::ScrollView* favorites = strip.pinned_tabs_scroll_view();
        views::ScrollView* tabs = strip.unpinned_tabs_scroll_view();
        ASSERT_TRUE(favorites);
        ASSERT_TRUE(tabs);

        EXPECT_TRUE(favorites->GetVisible());
        EXPECT_EQ(0, favorites->y());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height, favorites->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height +
                      yee::kSidebarMetrics.favorites_cell_gap,
                  tabs->y());
        EXPECT_EQ(strip.height(), tabs->bounds().bottom());
        EXPECT_FALSE(strip.GetTabsSeparator()->GetVisible());
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  GetTabViewAt(0)->height());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height,
                  GetTabViewAt(1)->height());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarFavoriteCommandHonorsCapacityAndUnpin) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      Do([&]() {
        for (int i = 1; i <= yee::kSidebarMetrics.favorites_max_count; ++i) {
          chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, false);
        }
        for (int i = 0; i < yee::kSidebarMetrics.favorites_max_count; ++i) {
          model->SetTabPinned(i, true);
        }
        model->ActivateTabAt(
            yee::kSidebarMetrics.favorites_max_count,
            TabStripUserGestureDetails(
                TabStripUserGestureDetails::GestureType::kOther));
        chrome::PinTab(browser());
      }),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); },
                  yee::kSidebarMetrics.favorites_max_count),
      Check([&]() {
        return !model->IsTabPinned(yee::kSidebarMetrics.favorites_max_count);
      }),
      RunScheduledLayout(), Do([&]() {
        // ACCESSIBLE_ONLY 탭을 native 키보드 탐색 상태에서 포커스한다.
        GetBrowserView().GetFocusManager()->SetKeyboardAccessible(true);
        GetTabViewAt(yee::kSidebarMetrics.favorites_max_count)->RequestFocus();
      }),
      Check([&]() { return chrome::HasKeyboardFocusedTab(browser()); }),
      Do([&]() { chrome::PinKeyboardFocusedTab(browser()); }),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); },
                  yee::kSidebarMetrics.favorites_max_count),
      Check([&]() {
        return !model->IsTabPinned(yee::kSidebarMetrics.favorites_max_count);
      }),
      Do([&]() { GetTabViewAt(0)->RequestFocus(); }),
      Check([&]() { return chrome::HasKeyboardFocusedTab(browser()); }),
      Do([&]() { chrome::PinKeyboardFocusedTab(browser()); }),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); },
                  yee::kSidebarMetrics.favorites_max_count - 1));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarGroupHeaderDragHasNoFavoriteAffordance) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTabsToNewGroup({0, 1}),
      ApplyInput(
          Steps(StartDragFromGroupToTab(0, 2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, 0),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_TRUE(!GetFavoritesDragPreview() ||
                    !GetFavoritesDragPreview()->GetVisible());
        EXPECT_EQ(0, model->IndexOfFirstNonPinnedTab());
      }),
      ApplyInput(
          Steps(PressEscAsync()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      CheckResult([&]() { return model->IndexOfFirstNonPinnedTab(); }, 0));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarGroupedTabCanBecomeFavorite) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kFourthTab);
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2),
      AddTab(kFourthTab, GURL(chrome::kChromeUIVersionURL), 3), PinTabAt(0),
      AddTabsToNewGroup({1, 2}),
      ApplyInput(
          Steps(StartDragBetweenTabs(2, 3)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 2, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_TRUE(GetTabStripView().pinned_tabs_scroll_view()->GetVisible());
      }),
      ApplyInput(
          Steps(
              ContinueDragToFavoriteSlot(/*target_item_count=*/2, /*slot=*/1)),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0)}), true)});
          },
          Phase::kAttached, -1, -1),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions(
                {OrderCondition(PinnedHandles({Tab(0), Tab(2)}), true)});
          },
          Phase::kInactive, -1, -1),
      Do([&]() {
        EXPECT_FALSE(model->GetTabGroupForTab(1).has_value());
        EXPECT_EQ(Tab(2),
                  static_cast<uint64_t>(
                      model->GetTabAtIndex(1)->GetHandle().raw_value()));
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarLastFavoriteDropZoneSurvivesUntilCancel) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1), PinTabAt(0),
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        EXPECT_TRUE(GetFavoritesDropZone().GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.favorites_drop_zone_height,
                  GetFavoritesDropZone().height());
        EXPECT_FALSE(strip.pinned_tabs_scroll_view()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.favorites_drop_zone_height +
                      yee::kSidebarMetrics.section_gap,
                  strip.unpinned_tabs_scroll_view()->y());
        EXPECT_TRUE(model->IsTabPinned(0));
      }),
      ApplyInput(
          Steps(ContinueDragToTab(1)), [&] { return CombineConditions({}); },
          Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_TRUE(GetFavoritesDropZone().GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.favorites_drop_zone_height +
                      yee::kSidebarMetrics.section_gap,
                  GetTabStripView().unpinned_tabs_scroll_view()->y());
        EXPECT_TRUE(model->IsTabPinned(0));
      }),
      ApplyInput(
          Steps(PressEscAsync()), [&] { return CombineConditions({}); },
          Phase::kInactive, -1, -1),
      RunScheduledLayout(), Do([&]() {
        TabStripView& strip = GetTabStripView();
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        EXPECT_TRUE(strip.pinned_tabs_scroll_view()->GetVisible());
        EXPECT_EQ(yee::kSidebarMetrics.tab_row_height +
                      yee::kSidebarMetrics.favorites_cell_gap,
                  strip.unpinned_tabs_scroll_view()->y());
        EXPECT_TRUE(model->IsTabPinned(0));
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_FALSE(GetFavoritesDragPreview()->GetVisible());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarSingleFavoriteDragReordersWithinDock) {
  TabStripModel* model = browser()->GetTabStripModel();
  ASSERT_NE(nullptr, model);
  RunTestSequence(
      AddTab(kSecondTab, GURL(chrome::kChromeUIBookmarksURL), 1),
      AddTab(kThirdTab, GURL(chrome::kChromeUISettingsURL), 2), PinTabAt(0),
      PinTabAt(1), PinTabAt(2), AwaitConditions([&] {
        return CombineConditions(
            {OrderCondition(PinnedHandles({Tab(0), Tab(1), Tab(2)}), true)});
      }),
#if BUILDFLAG(IS_WIN)
      Do([&] { model->ActivateTabAt(2); }), RunScheduledLayout(),
      ApplyInput(
          Steps(Do([&] {
            auto* const tab = GetTabViewAt(0);
            ASSERT_TRUE(tab && tab->IsDrawn());
            const auto window = tab->GetWidget()->GetNativeWindow();
            ASSERT_TRUE(yee::test::SendMouseMoveSync(
                tab->GetBoundsInScreen().CenterPoint(), window));
            ASSERT_TRUE(yee::test::SendLeftMouseClickSync(window));
          })),
          [&] {
            return AppliedConditions{applied_.Condition(
                applied_.source().ObserveTabModel(*model),
                TabModelMatches{
                    .active_tab = static_cast<uint64_t>(
                        model->GetTabAtIndex(0)->GetHandle().raw_value())},
                AppliedMatch::kTabActive)};
          },
          Phase::kInactive, -1, -1),
      Check([&] { return model->active_index() == 0; }),
#endif
      ApplyInput(
          Steps(StartDragBetweenTabs(0, 1)),
          [&] { return CombineConditions({}); }, Phase::kAttached, 0, -1),
      ApplyInput(
          Steps(
              ContinueDragToFavoriteSlot(/*target_item_count=*/3, /*slot=*/2)),
          [&] { return CombineConditions({}); }, Phase::kAttached, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_FALSE(GetFavoritesDropZone().GetVisible());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_TRUE(GetFavoritesDragPreview()->GetVisible());
      }),
      ApplyInput(
          Steps(ReleaseMouse()),
          [&] {
            return CombineConditions({OrderCondition(
                PinnedHandles({Tab(1), Tab(2), Tab(0)}), true)});
          },
          Phase::kInactive, -1, -1),
      RunScheduledLayout(), Do([&]() {
        EXPECT_EQ(3, model->IndexOfFirstNonPinnedTab());
        ASSERT_TRUE(GetFavoritesDragPreview());
        EXPECT_FALSE(GetFavoritesDragPreview()->GetVisible());
      }));
}

IN_PROC_BROWSER_TEST_F(YeeVerticalTabDragTest,
                       YeeSidebarTopLevelTabCanEnterExpandedGroupMiddle) {
  DragUngroupedTabIntoExpandedGroupMiddle(DragPosition::kAbove);
}

IN_PROC_BROWSER_TEST_F(
    YeeVerticalTabDragTest,
    YeeSidebarTopLevelTabCanEnterExpandedGroupMiddleFromBelow) {
  DragUngroupedTabIntoExpandedGroupMiddle(DragPosition::kBelow);
}

}  // namespace
