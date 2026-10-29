// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include <algorithm>
#include <set>
#include <string_view>

#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/session_service_test_helper.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/base_tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/browser_root_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/split_tab_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/new_tab_button.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
#include "chrome/browser/ui/views/tabs/tab_style_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/projects/project_hub_view.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_controller.h"
#include "chrome/browser/ui/views/yee/projects/project_row_view.h"
#include "chrome/browser/ui/views/yee/vertical_group_color_mark.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "chrome/browser/yee_projects/project_service_factory.h"
#include "chrome/browser/yee_projects/project_tab_coordinator.h"
#include "chrome/browser/yee_projects/project_tab_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/sessions/core/session_types.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/test/draw_waiter_for_test.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/compositor/transform_recorder.h"
#include "ui/events/event.h"
#include "ui/events/gesture_event_details.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_painted_layer_delegates.h"
#include "ui/views/animation/test/ink_drop_impl_test_api.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/scroll_bar.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/paint_info.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/views/yee/projects/project_navigation_test_util.h"
#endif

namespace yee::projects {
namespace {

ui::MenuModel* FindSubmenu(ui::MenuModel& model, std::u16string_view title) {
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (model.GetLabelAt(i) == title) {
      return model.GetSubmenuModelAt(i);
    }
  }
  return nullptr;
}

void ExpectUniqueMenuCommands(const ui::MenuModel& model) {
  std::set<int> commands;
  const auto inspect = [&](auto&& self, const ui::MenuModel& current) -> void {
    for (size_t i = 0; i < current.GetItemCount(); ++i) {
      if (current.GetTypeAt(i) != ui::MenuModel::TYPE_SEPARATOR) {
        EXPECT_TRUE(commands.insert(current.GetCommandIdAt(i)).second)
            << current.GetCommandIdAt(i);
      }
      if (auto* child = current.GetSubmenuModelAt(i)) {
        self(self, *child);
      }
    }
  };
  inspect(inspect, model);
}

class ProjectMenuNativeDelegate : public TabContextMenuController::Delegate {
 public:
  bool IsContextMenuCommandChecked(TabStripModel::ContextMenuCommand) override {
    ++requests;
    return false;
  }
  bool IsContextMenuCommandEnabled(tabs::TabInterface*,
                                   TabStripModel::ContextMenuCommand) override {
    ++requests;
    return false;
  }
  bool IsContextMenuCommandAlerted(TabStripModel::ContextMenuCommand) override {
    ++requests;
    return false;
  }
  void ExecuteContextMenuCommand(tabs::TabInterface*,
                                 TabStripModel::ContextMenuCommand,
                                 int) override {
    ++requests;
  }
  bool GetContextMenuAccelerator(int, ui::Accelerator*) override {
    ++requests;
    return false;
  }
  int requests = 0;
};

views::LabelButton* FindButton(views::View* root, std::u16string_view title) {
  if (auto* button = views::AsViewClass<views::LabelButton>(root);
      button && (button->GetText() == title ||
                 button->GetViewAccessibility().GetCachedName() == title))
    return button;
  for (const auto& child : root->children()) {
    if (auto* found = FindButton(child, title))
      return found;
  }
  return nullptr;
}

TabView* FindNativeTab(views::View* root, const tabs::TabInterface* target) {
  if (auto* tab = views::AsViewClass<TabView>(root);
      tab && tab->GetTabInterface() == target)
    return tab;
  for (const auto& child : root->children()) {
    if (auto* found = FindNativeTab(child, target))
      return found;
  }
  return nullptr;
}

views::View* FindProjectSidebar(views::View* root) {
  if (std::string_view(root->GetClassName()).ends_with("ProjectSidebarView"))
    return root;
  for (const auto& child : root->children()) {
    if (auto* found = FindProjectSidebar(child))
      return found;
  }
  return nullptr;
}

views::ScrollView* FindScrollView(views::View* root) {
  if (auto* scroll = views::AsViewClass<views::ScrollView>(root)) {
    return scroll;
  }
  for (const auto& child : root->children()) {
    if (auto* found = FindScrollView(child)) {
      return found;
    }
  }
  return nullptr;
}

void ExpectButtonsInside(views::View* root, const gfx::Rect& bounds) {
  if (!root->IsDrawn())
    return;
  if (auto* button = views::AsViewClass<views::Button>(root)) {
    EXPECT_FALSE(button->bounds().IsEmpty());
    EXPECT_TRUE(bounds.Contains(button->GetBoundsInScreen()))
        << button->GetClassName() << ": "
        << button->GetBoundsInScreen().ToString() << " / " << bounds.ToString();
  }
  for (const auto& child : root->children())
    ExpectButtonsInside(child, bounds);
}

views::Label* FindLabel(views::View* root, const std::u16string& text) {
  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == text)
    return label;
  for (const auto& child : root->children()) {
    if (auto* found = FindLabel(child, text))
      return found;
  }
  return nullptr;
}

bool HasLabel(views::View* root, const std::u16string& text) {
  return FindLabel(root, text) != nullptr;
}

class ProjectNavigationBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/empty.html")));
  }
  ProjectService* service() {
    return ProjectServiceFactory::GetForProfile(browser()->GetProfile());
  }
  ProjectTabCoordinator* coordinator() {
    return ProjectTabCoordinator::GetForProfile(browser()->GetProfile());
  }
  // 요청한 실행에서만 실제 창을 저장해 상태별 시각 검수를 지원한다.
  void CaptureFrame(std::string_view name) {
    const auto directory =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "yee-project-paint-capture-dir");
    if (directory.empty())
      return;
    ASSERT_TRUE(base::CreateDirectory(directory));
    auto* widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    widget->LayoutRootViewIfNecessary();
    auto* compositor = widget->GetCompositor();
    compositor->ScheduleFullRedraw();
    ASSERT_TRUE(
        ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
    base::test::TestFuture<gfx::Image> snapshot;
    ui::GrabWindowSnapshot(widget->GetNativeWindow(),
                           gfx::Rect(widget->GetWindowBoundsInScreen().size()),
                           snapshot.GetCallback());
    auto image = snapshot.Take();
    ASSERT_FALSE(image.IsEmpty());
    auto png = image.As1xPNGBytes();
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(
        base::WriteFile(directory.AppendASCII(std::string(name) + ".png"),
                        static_cast<base::span<const uint8_t>>(*png)));
  }
};

// 프로젝트 일반 행을 생략해도 실제 탭은 남고, 접힌 Sidebar·수평·참조 해제에서
// 복귀한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectTabsAppearOnceAndReturnToNativeNavigation) {
  auto project = service()->CreateProject(u"단일 탐색");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  auto* contents = tab->GetContents();
  ASSERT_TRUE(content::ExecJs(contents, "document.title='프로젝트 페이지';"));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  orientation->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !BrowserAnimationController::From(browser())->IsAnimating(
          TabStripAnimations::kVerticalTabStrip);
    }));
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
  };
  layout();
  auto* native = FindNativeTab(view, tab);
  ASSERT_TRUE(native && native->IsDrawn());
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const auto id = coordinator()->PageId(tab);
  layout();
  EXPECT_FALSE(native->IsDrawn());
  EXPECT_EQ(contents, model->GetActiveWebContents());
  EXPECT_EQ(native, FindNativeTab(view, tab));
  EXPECT_EQ(1, model->count());
  auto expect_empty_native_region = [&] {
    auto* strip = views::AsViewClass<TabStripView>(
        view->tab_strip_view()->GetTabStripView());
    ASSERT_TRUE(strip);
    auto* scroll = strip->unpinned_tabs_scroll_view();
    ASSERT_TRUE(scroll && scroll->contents());
    EXPECT_FALSE(scroll->IsVerticalContentOverflowing());
    EXPECT_FALSE(scroll->vertical_scroll_bar()->GetVisible());
    EXPECT_EQ(0, scroll->vertical_scroll_bar()->GetMaxPosition());
    EXPECT_EQ(0, scroll->CurrentOffset().y());
    EXPECT_EQ(scroll->GetVisibleRect().height(), scroll->contents()->height());
    EXPECT_GT(scroll->contents()->height(), 0);
  };
  expect_empty_native_region();
  const auto original_bounds = browser()->GetWindow()->GetBounds();
  for (int height : {500, 800, original_bounds.height()}) {
    browser()->GetWindow()->SetBounds(gfx::Rect(
        original_bounds.origin(), gfx::Size(original_bounds.width(), height)));
    layout();
    expect_empty_native_region();
  }
  CaptureFrame("project-empty-native-region");
  auto* sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(sidebar);
  auto* page = FindButton(sidebar, u"프로젝트 페이지");
  ASSERT_TRUE(page && page->IsDrawn());
  EXPECT_FALSE(page->GetImage(views::Button::STATE_NORMAL).isNull());
  ui::AXNodeData data;
  page->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_TRUE(data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  orientation->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return orientation->IsCollapsed(); }));
  layout();
  EXPECT_TRUE(native->IsDrawn());
  orientation->RequestCollapse(false);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !orientation->IsCollapsed(); }));
  layout();
  EXPECT_FALSE(native->IsDrawn());
  orientation->SetVerticalTabsEnabled(false);
  layout();
  EXPECT_TRUE(ProjectNavigationController::ShouldShowNativeTab(tab));
  orientation->SetVerticalTabsEnabled(true);
  layout();
  EXPECT_FALSE(FindNativeTab(view, tab)->IsDrawn());
  sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(sidebar);
  ASSERT_TRUE(FindButton(sidebar, u"프로젝트 페이지"));
  EXPECT_TRUE(FindButton(sidebar, u"프로젝트 페이지")->IsDrawn());
  auto destination = service()->CreateProject(u"새 소속");
  ASSERT_TRUE(destination);
  ASSERT_TRUE(coordinator()->Attach(*tab, *destination, id, *project, true));
  layout();
  sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(FindButton(sidebar, u"프로젝트 페이지"));
  EXPECT_TRUE(FindButton(sidebar, u"프로젝트 페이지")->IsDrawn());
  model->SetTabPinned(model->GetIndexOfTab(tab), true);
  layout();
  EXPECT_TRUE(ProjectNavigationController::ShouldShowNativeTab(tab));
  model->SetTabPinned(model->GetIndexOfTab(tab), false);
  layout();
  ASSERT_TRUE(service()->RemovePage(id));
  layout();
  EXPECT_TRUE(FindNativeTab(view, tab)->IsDrawn());
  EXPECT_EQ(contents, model->GetActiveWebContents());
  // 일반 탭이 돌아오고 실제로 넘치면 native 스크롤도 다시 동작한다.
  browser()->GetWindow()->SetBounds(gfx::Rect(
      original_bounds.origin(), gfx::Size(original_bounds.width(), 500)));
  for (int i = 0; i < 20; ++i) {
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  }
  auto* strip = views::AsViewClass<TabStripView>(
      view->tab_strip_view()->GetTabStripView());
  ASSERT_TRUE(strip);
  auto* scroll = strip->unpinned_tabs_scroll_view();
  // Mac의 idle observer 안에서 RunUntil을 중첩하지 않는다.
  layout();
  ASSERT_TRUE(base::test::RunUntil([&] {
    view->GetWidget()->LayoutRootViewIfNecessary();
    return scroll->vertical_scroll_bar()->GetVisible();
  }));
  EXPECT_GT(scroll->contents()->height(), scroll->GetVisibleRect().height());
  CaptureFrame("project-returned-native-overflow");
}

// 일반 그룹의 실제 소유권은 유지하며 숨긴 멤버의 빈 행과 빈 그룹을 남기지
// 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectPresentationKeepsNativeGroupMembership) {
  // 이 사례는 최종 배치의 drop 대상을 검사한다. 전환 모션은 별도 사례가 맡는다.
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto project = service()->CreateProject(u"그룹 참조");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   -1, true);
  auto* first = model->GetTabAtIndex(0);
  auto* second = model->GetTabAtIndex(1);
  ASSERT_TRUE(content::WaitForLoadStop(second->GetContents()));
  const auto group = model->AddToNewGroup({0, 1});
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
  };
  layout();
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  layout();
  EXPECT_FALSE(FindNativeTab(view, first)->IsDrawn());
  EXPECT_TRUE(FindNativeTab(view, second)->IsDrawn());
  EXPECT_EQ(group, first->GetGroup());
  EXPECT_EQ(group, second->GetGroup());
  auto* native_second = FindNativeTab(view, second);
  auto* group_view = views::AsViewClass<TabGroupView>(native_second->parent());
  ASSERT_TRUE(group_view);
  const auto point = views::View::ConvertPointToTarget(
      native_second, group_view, native_second->GetLocalBounds().CenterPoint());
  const auto drop = group_view->GetLinkDropIndex(point);
  ASSERT_TRUE(drop);
  EXPECT_EQ(model->GetIndexOfTab(second), drop->index);
  EXPECT_EQ(BrowserRootView::DropIndex::RelativeToIndex::kReplaceIndex,
            drop->relative_to_index);
  ASSERT_TRUE(coordinator()->Attach(*second, *project, {}));
  layout();
  EXPECT_FALSE(FindNativeTab(view, second)->IsDrawn());
  EXPECT_EQ(2, model->count());
  auto* container =
      views::AsViewClass<UnpinnedTabContainerView>(group_view->parent());
  ASSERT_TRUE(container);
  const auto trailing_drop =
      container->GetLinkDropIndex(group_view->bounds().CenterPoint());
  ASSERT_TRUE(trailing_drop);
  EXPECT_EQ(model->count(), trailing_drop->index);
  EXPECT_EQ(BrowserRootView::DropIndex::RelativeToIndex::kInsertBeforeIndex,
            trailing_drop->relative_to_index);
  ASSERT_TRUE(service()->DeleteProject(*project));
  layout();
  EXPECT_TRUE(FindNativeTab(view, first)->IsDrawn());
  EXPECT_TRUE(FindNativeTab(view, second)->IsDrawn());
  EXPECT_EQ(group, first->GetGroup());
}

// 분할 탭 한쪽만 프로젝트에 넣어도 남은 native 행을 표시하고 실제 분할은
// 보존한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectPresentationKeepsNativeSplitMembership) {
  auto project = service()->CreateProject(u"분할 참조");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   -1, true);
  model->ActivateTabAt(0);
  const auto split =
      model->AddToNewSplit({1}, split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
  auto* first = model->GetTabAtIndex(0);
  auto* second = model->GetTabAtIndex(1);
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    base::RunLoop().RunUntilIdle();
    // 기존 Unpinned manager는 브라우저 생성 때 animation duration을 저장한다.
    // 실제 부모·그룹의 배치 애니메이션이 끝난 뒤 링크 대상을 검사한다.
    ASSERT_TRUE(base::test::RunUntil([&] {
      if (BrowserAnimationController::From(browser())->IsAnimating(
              TabStripAnimations::kVerticalTabStrip)) {
        return false;
      }
      auto* tab_view = FindNativeTab(view, first);
      if (!tab_view) {
        return false;
      }
      for (auto* parent = tab_view->parent(); parent;
           parent = parent->parent()) {
        if ((views::IsViewClass<TabGroupView>(parent) ||
             views::IsViewClass<UnpinnedTabContainerView>(parent)) &&
            static_cast<TabCollectionAnimatingLayoutManager*>(
                parent->GetLayoutManager())
                ->is_animating()) {
          return false;
        }
      }
      return true;
    }));
    view->DeprecatedLayoutImmediately();
  };
  auto expect_link_target = [&](tabs::TabInterface* visible) {
    auto* split_view =
        views::AsViewClass<SplitTabView>(FindNativeTab(view, first)->parent());
    ASSERT_TRUE(split_view);
    if (visible) {
      ASSERT_TRUE(split_view->IsDrawn());
      ASSERT_FALSE(split_view->bounds().IsEmpty());
    }
    for (int x : {split_view->width() / 4, split_view->width() * 3 / 4}) {
      const gfx::Point point(x, split_view->height() / 2);
      auto drop = split_view->GetLinkDropIndex(point);
      if (!visible) {
        EXPECT_FALSE(drop);
        continue;
      }
      ASSERT_TRUE(drop);
      EXPECT_EQ(model->GetIndexOfTab(visible), drop->index);
      EXPECT_EQ(BrowserRootView::DropIndex::RelativeToIndex::kReplaceIndex,
                drop->relative_to_index);
      // 실제 컨테이너의 재귀 탐색도 같은 native 대상을 반환해야 한다.
      for (auto* parent = split_view->parent(); parent;
           parent = parent->parent()) {
        const auto in_parent =
            views::View::ConvertPointToTarget(split_view, parent, point);
        SCOPED_TRACE(testing::Message()
                     << parent->GetClassName()
                     << " point=" << in_parent.ToString()
                     << " split=" << split_view->bounds().ToString()
                     << " parent=" << parent->bounds().ToString());
        std::optional<BrowserRootView::DropIndex> outer;
        std::string candidates;
        if (auto* group = views::AsViewClass<TabGroupView>(parent)) {
          outer = group->GetLinkDropIndex(in_parent);
        } else if (auto* container =
                       views::AsViewClass<UnpinnedTabContainerView>(parent)) {
          for (const auto& child : container->children()) {
            auto* candidate = child.get();
            candidates += std::string(candidate->GetClassName()) + " " +
                          candidate->bounds().ToString() +
                          " visible=" + std::to_string(candidate->GetVisible());
            if (auto* candidate_group =
                    views::AsViewClass<TabGroupView>(candidate)) {
              candidates +=
                  " collapsed=" +
                  std::to_string(candidate_group->IsCollapsed()) + " header=" +
                  candidate_group->group_header()->bounds().ToString();
            }
            candidates += "; ";
          }
          outer = container->GetLinkDropIndex(in_parent);
        } else {
          continue;
        }
        SCOPED_TRACE(candidates);
        ASSERT_TRUE(outer);
        EXPECT_EQ(model->GetIndexOfTab(visible), outer->index);
        EXPECT_EQ(BrowserRootView::DropIndex::RelativeToIndex::kReplaceIndex,
                  outer->relative_to_index);
      }
    }
  };
  layout();
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  layout();
  EXPECT_FALSE(FindNativeTab(view, first)->IsDrawn());
  EXPECT_TRUE(FindNativeTab(view, second)->IsDrawn());
  EXPECT_EQ(split, first->GetSplit());
  EXPECT_EQ(split, second->GetSplit());
  expect_link_target(second);
  model->AddToNewGroup({0, 1});
  layout();
  expect_link_target(second);
  ASSERT_TRUE(coordinator()->Attach(*second, *project, {}));
  layout();
  EXPECT_FALSE(FindNativeTab(view, second)->IsDrawn());
  expect_link_target(nullptr);
  ASSERT_TRUE(service()->RemovePage(coordinator()->PageId(first)));
  layout();
  EXPECT_TRUE(FindNativeTab(view, first)->IsDrawn());
  EXPECT_FALSE(FindNativeTab(view, second)->IsDrawn());
  EXPECT_EQ(split, first->GetSplit());
  expect_link_target(first);
}

// 다른 창의 자료 정보가 바뀌어도 읽던 위치를 유지하고 프로젝트 전환은
// 새 허브의 처음부터 보여준다. 목록이 짧아지면 유효한 범위로 제한한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       MetadataRefreshPreservesHubAndSidebarScroll) {
  auto project = service()->CreateProject(u"스크롤 자료");
  auto other = service()->CreateProject(u"다른 프로젝트");
  ASSERT_TRUE(project && other);
  std::vector<std::string> pages;
  for (int i = 0; i < 36; ++i) {
    auto page =
        service()->AttachPage(*project,
                              embedded_test_server()->GetURL(
                                  "/empty.html?entry=" + std::to_string(i)),
                              u"자료 " + base::UTF8ToUTF16(std::to_string(i)));
    ASSERT_TRUE(page);
    pages.push_back(*page);
  }
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
  };
  layout();
  auto* heading = FindButton(FindProjectSidebar(view), u"스크롤 자료");
  ASSERT_TRUE(heading);
  views::test::ButtonTestApi(heading).NotifyDefaultMouseClick();
  controller->ShowHub(*project);
  layout();
  auto hub = [&] {
    return views::AsViewClass<ProjectHubView>(
        view->GetViewByID(ProjectNavigationController::kHubViewId));
  };
  auto sidebar_scroll = [&] {
    return FindScrollView(FindProjectSidebar(view));
  };
  auto hub_scroll = [&] { return FindScrollView(hub()); };
  ASSERT_TRUE(hub() && sidebar_scroll());
  view->GetFocusManager()->ClearFocus();
  sidebar_scroll()->ScrollToPosition(sidebar_scroll()->vertical_scroll_bar(),
                                     140);
  ASSERT_TRUE(hub_scroll());
  hub_scroll()->ScrollToPosition(hub_scroll()->vertical_scroll_bar(), 260);
  // 플랫폼별 행 높이 때문에 요청한 위치가 스크롤 끝에 제한될 수 있다.
  const int sidebar_offset = sidebar_scroll()->CurrentOffset().y();
  const int hub_offset = hub_scroll()->CurrentOffset().y();
  ASSERT_GT(sidebar_offset, 0);
  ASSERT_GT(hub_offset, 0);
  ASSERT_LE(sidebar_offset, 140);
  ASSERT_LE(hub_offset, 260);
  const auto url = service()->FindPage(pages.front())->url;
  for (const auto& title : {u"갱신 1", u"갱신 2"}) {
    ASSERT_TRUE(service()->UpdatePage(pages.front(), url, title));
    // 레이아웃 전에 연속 갱신해도 보존 중인 offset을 0으로 덮지 않는다.
    controller->ShowHub(*project);
    controller->ShowHub(*project);
    layout();
    EXPECT_TRUE(FindButton(hub(), title));
    EXPECT_EQ(sidebar_offset, sidebar_scroll()->CurrentOffset().y());
    EXPECT_EQ(hub_offset, hub_scroll()->CurrentOffset().y());
  }
  controller->ShowHub(*other);
  layout();
  EXPECT_EQ(0, hub_scroll()->CurrentOffset().y());
  controller->ShowHub(*project);
  layout();
  EXPECT_EQ(0, hub_scroll()->CurrentOffset().y());
  for (const auto& id : pages) {
    ASSERT_TRUE(service()->RemovePage(id));
  }
  layout();
  EXPECT_EQ(0, sidebar_scroll()->CurrentOffset().y());
  EXPECT_EQ(0, hub_scroll()->CurrentOffset().y());
}

// 행 전체에 native 상태색을 적용하고 hover/focus 보조 조작의 칸을 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectRowsKeepNativeHoverActiveAndFocusStates) {
  auto project = service()->CreateProject(u"상태 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  ASSERT_TRUE(
      content::ExecJs(tab->GetContents(), "document.title='상태 페이지'"));
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* sidebar = FindProjectSidebar(view);
  auto* page = FindButton(sidebar, u"상태 페이지");
  ASSERT_TRUE(page);
  auto* row = views::AsViewClass<ProjectRowView>(page->parent());
  auto* close =
      FindButton(row, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) +
                          u": 상태 페이지");
  ASSERT_TRUE(row && close);
  ASSERT_TRUE(views::AsViewClass<TabCloseButton>(close));
  EXPECT_EQ(views::InkDropHost::InkDropMode::ON,
            views::InkDrop::Get(close)->GetMode());
  const auto primary_bounds = page->bounds();
  const auto trailing_bounds = close->parent()->bounds();
  const auto expected = ResolveSidebarItemColors(
      *row->GetColorProvider(), SidebarItemVisualState::kActive, 1,
      view->GetWidget()->ShouldPaintAsActive(), false);
  for (auto state : {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
                     views::Button::STATE_PRESSED}) {
    page->SetState(state);
    view->DeprecatedLayoutImmediately();
    ASSERT_TRUE(row->background());
    EXPECT_EQ(expected.fill, row->background()->color().ResolveToSkColor(
                                 row->GetColorProvider()));
    EXPECT_EQ(primary_bounds, page->bounds());
    EXPECT_EQ(trailing_bounds, close->parent()->bounds());
    EXPECT_TRUE(close->GetVisible());
    EXPECT_EQ(SK_ColorTRANSPARENT,
              *views::AsViewClass<views::MdTextButton>(page)
                   ->GetBgColorOverrideDeprecated());
    EXPECT_FALSE(page->background());
    EXPECT_FALSE(close->background());
    CaptureFrame("project-page-state-" + std::to_string(state));
  }
  page->SetEnabled(false);
  EXPECT_EQ(SK_ColorTRANSPARENT, row->background()->color().ResolveToSkColor(
                                     row->GetColorProvider()));
  page->SetEnabled(true);
  // native 탭 닫기는 접근성 키보드 탐색에서만 초점을 받는다.
  view->GetFocusManager()->SetKeyboardAccessible(true);
  page->RequestFocus();
  EXPECT_TRUE(close->GetVisible());
  ASSERT_TRUE(views::FocusRing::Get(row));
  EXPECT_TRUE(views::FocusRing::Get(row)->GetVisible());
  CaptureFrame("project-page-keyboard-focus");
  close->RequestFocus();
  EXPECT_EQ(close, view->GetFocusManager()->GetFocusedView());
  EXPECT_TRUE(close->GetVisible());
  EXPECT_FALSE(views::FocusRing::Get(row)->GetVisible());
  ASSERT_TRUE(views::FocusRing::Get(close));
  EXPECT_TRUE(views::FocusRing::Get(close)->GetVisible());
  CaptureFrame("project-close-keyboard-focus");
  view->GetFocusManager()->ClearFocus();
  EXPECT_FALSE(views::FocusRing::Get(row)->GetVisible());
  EXPECT_FALSE(views::FocusRing::Get(close)->GetVisible());
}

// 프로젝트 행은 그룹처럼 폴딩하며, 허브 보조 조작은 폴딩 상태를 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       LeadingDisclosureAndHubKeepIndependentState) {
  auto project = service()->CreateProject(u"동작 분리 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  ASSERT_TRUE(content::ExecJs(model->GetActiveWebContents(),
                              "document.title='분리 페이지'"));
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* sidebar = FindProjectSidebar(view);
  auto* name = FindButton(sidebar, u"동작 분리 검수");
  auto* toggle = FindButton(
      sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_TOGGLE_PAGES) +
                   u": 동작 분리 검수");
  ASSERT_TRUE(name && toggle);
  auto* row = views::AsViewClass<ProjectRowView>(name->parent());
  ASSERT_TRUE(row);
  toggle->RequestFocus();
  EXPECT_EQ(toggle, view->GetFocusManager()->GetFocusedView());
  ASSERT_TRUE(views::FocusRing::Get(toggle));
  EXPECT_TRUE(views::FocusRing::Get(toggle)->GetVisible());
  EXPECT_FALSE(views::FocusRing::Get(row)->GetVisible());
  name->RequestFocus();
  EXPECT_TRUE(views::FocusRing::Get(row)->GetVisible());
  EXPECT_FALSE(views::FocusRing::Get(toggle)->GetVisible());
  view->GetFocusManager()->ClearFocus();
  EXPECT_LT(toggle->bounds().CenterPoint().x(), name->bounds().x());
  ui::MouseEvent exit(ui::EventType::kMouseExited, gfx::Point(), gfx::Point(),
                      base::TimeTicks::Now(), 0, 0);
  static_cast<views::View*>(row)->OnMouseExited(exit);
  const auto resting_fill =
      row->background()->color().ResolveToSkColor(row->GetColorProvider());
  const auto normal_icon =
      gfx::Image(toggle->GetImage(views::Button::STATE_NORMAL));
  const auto title_bounds = name->bounds();
  CaptureFrame("project-heading-resting");
  ui::MouseEvent enter(ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
                       base::TimeTicks::Now(), 0, 0);
  static_cast<views::View*>(row)->OnMouseEntered(enter);
  EXPECT_FALSE(gfx::test::AreImagesEqual(
      normal_icon, gfx::Image(toggle->GetImage(views::Button::STATE_NORMAL))));
  EXPECT_EQ(title_bounds, name->bounds());
  EXPECT_NE(resting_fill, row->background()->color().ResolveToSkColor(
                              row->GetColorProvider()));
  CaptureFrame("project-heading-hover");
  static_cast<views::View*>(row)->OnMouseExited(exit);
  EXPECT_EQ(resting_fill, row->background()->color().ResolveToSkColor(
                              row->GetColorProvider()));
  EXPECT_TRUE(gfx::test::AreImagesEqual(
      normal_icon, gfx::Image(toggle->GetImage(views::Button::STATE_NORMAL))));
  static_cast<views::View*>(row)->OnMouseEntered(enter);
  row->SetVisible(false);
  row->SetVisible(true);
  EXPECT_EQ(resting_fill, row->background()->color().ResolveToSkColor(
                              row->GetColorProvider()));
  EXPECT_TRUE(gfx::test::AreImagesEqual(
      normal_icon, gfx::Image(toggle->GetImage(views::Button::STATE_NORMAL))));
  views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
  EXPECT_FALSE(controller->showing_hub());
  EXPECT_FALSE(FindButton(sidebar, u"분리 페이지")->IsDrawn());
  CaptureFrame("project-heading-collapsed");
  views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
  EXPECT_FALSE(controller->showing_hub());
  EXPECT_TRUE(FindButton(sidebar, u"분리 페이지")->IsDrawn());
  views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
  EXPECT_FALSE(FindButton(sidebar, u"분리 페이지")->IsDrawn());
  auto* hub_button =
      FindButton(sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_OVERVIEW) +
                              u": 동작 분리 검수");
  ASSERT_TRUE(hub_button);
  views::test::ButtonTestApi(hub_button).NotifyDefaultMouseClick();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(controller->showing_hub());
  CaptureFrame("project-hub-sidebar");
  EXPECT_EQ(nullptr, view->GetFocusManager()->GetFocusedView());
  EXPECT_EQ(nullptr, view->GetFocusManager()->GetStoredFocusView());
  EXPECT_FALSE(view->GetFocusManager()->RestoreFocusedView());
  EXPECT_EQ(nullptr, view->GetFocusManager()->GetFocusedView());
  sidebar = FindProjectSidebar(view);
  EXPECT_FALSE(FindButton(sidebar, u"분리 페이지")->IsDrawn());
  controller->ExitHub();
  base::RunLoop().RunUntilIdle();
  sidebar = FindProjectSidebar(view);
  EXPECT_FALSE(FindButton(sidebar, u"분리 페이지")->IsDrawn());
  hub_button =
      FindButton(sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_OVERVIEW) +
                              u": 동작 분리 검수");
  ASSERT_TRUE(hub_button);
  ASSERT_TRUE(ProjectRowView::RestoreFocusTo(hub_button));
  ui::KeyEvent key(ui::EventType::kKeyPressed, ui::VKEY_RETURN, 0);
  views::test::ButtonTestApi(hub_button).NotifyClick(key);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(controller->showing_hub());
  auto* header =
      view->GetViewByID(ProjectNavigationController::kHubHeaderViewId);
  ASSERT_TRUE(header);
  EXPECT_TRUE(header->Contains(view->GetFocusManager()->GetFocusedView()));
  view->GetFocusManager()->ClearFocus();
  EXPECT_TRUE(view->GetFocusManager()->RestoreFocusedView());
  EXPECT_TRUE(header->Contains(view->GetFocusManager()->GetFocusedView()));
  EXPECT_FALSE(FindButton(FindProjectSidebar(view), u"분리 페이지")->IsDrawn());
}

// RTL에서도 보조 버튼의 아이콘과 상태 면은 같은 버튼 안에 남아야 한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       SidebarHeadingActionsKeepStateLayersInsideTheirButtons) {
  auto project = service()->CreateProject(u"새 소속");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = ProjectNavigationController::From(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !BrowserAnimationController::From(browser())->IsAnimating(
        TabStripAnimations::kVerticalTabStrip);
  }));
  view->GetFocusManager()->SetKeyboardAccessible(true);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  const auto check_heading = [&](int revision) {
    view->GetWidget()->LayoutRootViewIfNecessary();
    auto* sidebar = FindProjectSidebar(view);
    const auto& title = service()->FindProject(*project)->name;
    auto* name = FindButton(sidebar, title);
    auto* toggle = FindButton(
        sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_TOGGLE_PAGES) +
                     u": " + title);
    ASSERT_TRUE(name && toggle);
    auto* row = views::AsViewClass<ProjectRowView>(name->parent());
    ASSERT_TRUE(row);
    const auto title_bounds = name->GetBoundsInScreen();
    if (base::i18n::IsRTL()) {
      EXPECT_GE(toggle->GetBoundsInScreen().x(), title_bounds.right());
    } else {
      EXPECT_LE(toggle->GetBoundsInScreen().right(), title_bounds.x());
    }
    ui::MouseEvent enter(ui::EventType::kMouseEntered, gfx::Point(),
                         gfx::Point(), base::TimeTicks::Now(), 0, 0);
    ui::MouseEvent leave(ui::EventType::kMouseExited, gfx::Point(),
                         gfx::Point(), base::TimeTicks::Now(), 0, 0);
    static_cast<views::View*>(row)->OnMouseEntered(enter);
    view->GetWidget()->LayoutRootViewIfNecessary();
    int action = 0;
    for (int label : {IDS_YEE_PROJECT_OVERVIEW, IDS_YEE_PROJECT_MORE}) {
      auto* button =
          FindButton(sidebar, l10n_util::GetStringUTF16(label) + u": " + title);
      ASSERT_TRUE(button && button->IsDrawn());
      auto* ink = views::InkDrop::Get(button)->GetInkDrop();
      for (auto state :
           {views::Button::STATE_HOVERED, views::Button::STATE_PRESSED}) {
        SCOPED_TRACE(testing::Message()
                     << "revision=" << revision << ", action=" << action
                     << ", state=" << state);
        button->SetState(state);
        ink->SetHovered(true);
        view->GetWidget()->LayoutRootViewIfNecessary();
        views::InkDropContainerView* container = nullptr;
        for (views::View* child : button->children()) {
          if (auto* found =
                  views::AsViewClass<views::InkDropContainerView>(child)) {
            container = found;
          }
        }
        ASSERT_TRUE(container);
        EXPECT_EQ(button->GetLocalBounds(), container->bounds());
        views::test::InkDropImplTestApi api(
            static_cast<views::InkDropImpl*>(ink));
        auto* root = api.GetRootLayer();
        ASSERT_TRUE(root && button->layer());
        EXPECT_EQ(button->layer(), root->parent());
        EXPECT_EQ(button->GetLocalBounds(), root->bounds());
        EXPECT_EQ(button->GetLocalBounds(), root->GetTargetBounds());
        EXPECT_TRUE(root->GetTargetTransform().IsIdentity());
        EXPECT_EQ(title_bounds, name->GetBoundsInScreen());
        CaptureFrame("project-heading-action-" + std::to_string(revision) +
                     "-" + std::to_string(action) + "-" +
                     std::to_string(state));
      }
      ink->SetHovered(false);
      button->SetState(views::Button::STATE_NORMAL);
      button->RequestFocus();
      ASSERT_TRUE(views::FocusRing::Get(button));
      EXPECT_TRUE(views::FocusRing::Get(button)->GetVisible());
      CaptureFrame("project-heading-action-focus-" + std::to_string(revision) +
                   "-" + std::to_string(action));
      view->GetFocusManager()->ClearFocus();
      button->SetEnabled(false);
      CaptureFrame("project-heading-action-disabled-" +
                   std::to_string(revision) + "-" + std::to_string(action));
      button->SetEnabled(true);
      ++action;
    }
    static_cast<views::View*>(row)->OnMouseExited(leave);
    CaptureFrame("project-heading-actions-leave-" + std::to_string(revision));
  };
  ASSERT_NO_FATAL_FAILURE(check_heading(0));
  ASSERT_TRUE(service()->RenameProject(*project, u"새 소속 갱신"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* sidebar = FindProjectSidebar(view);
    return sidebar && FindButton(sidebar, u"새 소속 갱신");
  }));
  ASSERT_NO_FATAL_FAILURE(check_heading(1));
  controller->ShowHub(*project);
  base::RunLoop().RunUntilIdle();
  ASSERT_NO_FATAL_FAILURE(check_heading(2));
}

// 펼침·접힘 뒤 허브가 행을 갱신해도 실제 일반 탭의 시작 위치가 움직이지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       FoldingAndHubKeepSidebarSectionGapStable) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto project = service()->CreateProject(u"간격 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  ASSERT_TRUE(content::ExecJs(model->GetActiveWebContents(),
                              "document.title='간격 페이지'"));
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
  };
  layout();
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   -1, true);
  auto* ordinary = model->GetActiveTab();
  ASSERT_TRUE(content::WaitForLoadStop(ordinary->GetContents()));
  layout();
  // 활성 일반 탭으로 이동해도 앞서 펼친 프로젝트 상태는 유지된다.
  for (bool expanded : {false, true, false}) {
    auto* sidebar = FindProjectSidebar(view);
    ASSERT_TRUE(sidebar);
    auto* toggle = FindButton(
        sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_TOGGLE_PAGES) +
                     u": 간격 검수");
    ASSERT_TRUE(toggle);
    views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
    layout();
    ASSERT_EQ(expanded, FindButton(sidebar, u"간격 페이지")->IsDrawn());
    const auto preferred = sidebar->GetPreferredSize();
    const auto sidebar_bounds = sidebar->GetBoundsInScreen();
    auto* tab = FindNativeTab(view, ordinary);
    ASSERT_TRUE(tab);
    const auto tab_bounds = tab->GetBoundsInScreen();
    views::test::ButtonTestApi(
        FindButton(sidebar,
                   l10n_util::GetStringUTF16(IDS_YEE_PROJECT_OVERVIEW) +
                       u": 간격 검수"))
        .NotifyDefaultMouseClick();
    layout();
    ASSERT_TRUE(controller->showing_hub());
    sidebar = FindProjectSidebar(view);
    EXPECT_EQ(preferred, sidebar->GetPreferredSize()) << expanded;
    EXPECT_EQ(sidebar_bounds, sidebar->GetBoundsInScreen()) << expanded;
    EXPECT_EQ(tab_bounds.origin(),
              FindNativeTab(view, ordinary)->GetBoundsInScreen().origin());
    controller->ExitHub();
    layout();
    EXPECT_EQ(preferred, FindProjectSidebar(view)->GetPreferredSize());
    EXPECT_EQ(tab_bounds.origin(),
              FindNativeTab(view, ordinary)->GetBoundsInScreen().origin());
  }
}

#if BUILDFLAG(IS_MAC)
// 동작 줄이기에서도 펼친 글자는 포인터 이동 없이 실제 합성 화면에 나타난다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       FoldingRepaintsVisibleTextWithoutPointerOrAnimation) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto project = service()->CreateProject(u"폴딩 그리기");
  ASSERT_TRUE(project);
  const std::vector<std::u16string> titles = {
      u"첫 번째 참고 페이지", u"두 번째 참고 페이지", u"세 번째 참고 페이지"};
  for (size_t i = 0; i < titles.size(); ++i) {
    ASSERT_TRUE(service()->AttachPage(
        *project,
        embedded_test_server()->GetURL("/empty.html?" + std::to_string(i)),
        titles[i]));
  }
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* widget = view->GetWidget();
  auto* compositor = widget->GetCompositor();
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ProjectNavigationController::From(browser())->ShowHub(*project);
  base::RunLoop().RunUntilIdle();
  widget->LayoutRootViewIfNecessary();
  auto* sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(sidebar);
  auto* name = FindButton(sidebar, u"폴딩 그리기");
  ASSERT_TRUE(name);
  views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
  widget->LayoutRootViewIfNecessary();
  compositor->ScheduleFullRedraw();
  ASSERT_TRUE(ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
  auto capture_label = [&](const std::u16string& title) {
    auto* button = FindButton(sidebar, title);
    CHECK(button);
    auto* label = FindLabel(button, title);
    CHECK(label);
    gfx::Rect bounds = label->GetBoundsInScreen();
    bounds.Offset(-widget->GetWindowBoundsInScreen().OffsetFromOrigin());
    base::test::TestFuture<gfx::Image> snapshot;
    ui::GrabWindowSnapshot(widget->GetNativeWindow(), bounds,
                           snapshot.GetCallback());
    return snapshot.Take();
  };
  const auto capture_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-project-paint-capture-dir");
  if (!capture_dir.empty())
    ASSERT_TRUE(base::CreateDirectory(capture_dir));
  auto save_capture = [&](const gfx::Image& image, const std::string& name) {
    if (capture_dir.empty())
      return;
    auto png = image.As1xPNGBytes();
    EXPECT_TRUE(png && png->size());
    if (png && png->size())
      EXPECT_TRUE(
          base::WriteFile(capture_dir.AppendASCII(name + ".png"),
                          static_cast<base::span<const uint8_t>>(*png)));
  };
  std::vector<gfx::Image> expected;
  for (const auto& title : titles) {
    auto image = capture_label(title);
    ASSERT_FALSE(image.IsEmpty());
    const auto& bitmap = image.AsBitmap();
    size_t varied = 0;
    const auto background = bitmap.getColor(0, 0);
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x)
        varied += bitmap.getColor(x, y) != background;
    }
    ASSERT_GT(varied, 20u) << "비어 있는 초기 캡처를 기준으로 사용하지 않는다.";
    save_capture(image, "initial-" + std::to_string(expected.size()));
    expected.push_back(std::move(image));
  }
  for (int pass = 0; pass < 3; ++pass) {
    views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
    widget->LayoutRootViewIfNecessary();
    EXPECT_FALSE(FindButton(sidebar, titles.front())->IsDrawn());
    compositor->ScheduleDraw();
    ASSERT_TRUE(
        ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
    views::test::ButtonTestApi(name).NotifyDefaultMouseClick();
    widget->LayoutRootViewIfNecessary();
    // 강제 full redraw는 하지 않는다. 폴딩 자체가 갱신한 합성 출력을 읽는다.
    compositor->ScheduleDraw();
    ASSERT_TRUE(
        ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
    for (size_t i = 0; i < titles.size(); ++i) {
      auto actual = capture_label(titles[i]);
      ASSERT_FALSE(actual.IsEmpty());
      save_capture(actual,
                   "fold-" + std::to_string(pass) + "-" + std::to_string(i));
      EXPECT_TRUE(gfx::test::AreImagesEqual(expected[i], actual))
          << pass << ": " << i;
    }
  }
}
#endif

// 허브는 선택 목적지다. 뒤의 native 탭은 모델 선택을 보존하되 paint와 AX의
// 선택·상시 닫기 표시는 복귀할 때까지 숨긴다. Favorite 분할의 부모 바탕도 본다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubMasksNativeTabSelectionWithoutChangingModel) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto project = service()->CreateProject(u"선택 목적지");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* active = model->GetActiveTab();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = ProjectNavigationController::From(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto layout = [&] {
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
  };
  const auto raster = [](TabView* tab, views::View* surface, bool persistent) {
    if (!persistent) {
      // 일반·그룹 행의 바탕 소유자는 native TabStyleViews다.
      gfx::Canvas canvas(tab->size(), 1, false);
      tab->tab_styling()->PaintTab(&canvas);
      return canvas.GetBitmap();
    }
    SkBitmap bitmap;
    {
      ui::CanvasPainter painter(&bitmap, surface->size(), 1.f,
                                SK_ColorTRANSPARENT, false);
      ui::TransformRecorder position(painter.context());
      if (!surface->layer()) {
        // 부착된 행의 부모 내 원점만 보정하고 실제 배치·클립은 바꾸지 않는다.
        gfx::Transform offset;
        offset.Translate(-surface->GetMirroredX(), -surface->y());
        position.Transform(offset);
      }
      surface->Paint(views::PaintInfo::CreateRootPaintInfo(
          painter.context(), surface->parent()->size()));
    }
    return bitmap;
  };
  const auto expect_fill = [&](TabView* tab, views::View* surface,
                               SidebarItemVisualState state, bool persistent) {
    const auto bitmap = raster(tab, surface, persistent);
    ASSERT_GT(bitmap.width(), 4);
    ASSERT_GT(bitmap.height(), 4);
    const auto expected =
        ResolveSidebarItemColors(*surface->GetColorProvider(), state, 0,
                                 surface->GetWidget()->ShouldPaintAsActive(),
                                 persistent)
            .fill;
    // 글자·favicon·닫기 상태를 제외한 행 바탕의 실제 픽셀을 검증한다.
    const auto actual = bitmap.getColor(2, bitmap.height() / 2);
    EXPECT_EQ(SkPreMultiplyColor(expected), SkPreMultiplyColor(actual));
  };
  for (int presentation : {0, 1, 2, 3}) {
    SCOPED_TRACE(presentation);
    if (presentation == 1)
      model->SetTabPinned(model->GetIndexOfTab(active), true);
    if (presentation == 2) {
      model->SetTabPinned(model->GetIndexOfTab(active), false);
      model->AddToNewGroup({model->GetIndexOfTab(active)});
    }
    if (presentation == 3) {
      model->RemoveFromGroup({model->GetIndexOfTab(active)});
      model->SetTabPinned(model->GetIndexOfTab(active), true);
      chrome::AddTabAt(
          browser(), embedded_test_server()->GetURL("/title1.html"), -1, true);
      auto* peer = model->GetActiveTab();
      ASSERT_TRUE(content::WaitForLoadStop(peer->GetContents()));
      model->SetTabPinned(model->GetIndexOfTab(peer), true);
      model->ActivateTabAt(model->GetIndexOfTab(active));
      model->AddToNewSplit({model->GetIndexOfTab(peer)},
                           split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
      ASSERT_TRUE(active->IsSplit());
      ASSERT_EQ(active->GetSplit(), peer->GetSplit());
    }
    layout();
    auto* tab = FindNativeTab(view, active);
    ASSERT_TRUE(tab);
    ASSERT_TRUE(tab->IsDrawn());
    ASSERT_FALSE(tab->IsClosing());
    ASSERT_EQ(TabStripOrientation::kVertical,
              tab->collection_node()->orientation());
    tab->UpdateHovered(false);
    views::View* surface = tab;
    if (presentation == 3) {
      auto* split = views::AsViewClass<SplitTabView>(tab->parent());
      ASSERT_TRUE(split);
      ui::MouseEvent exit(ui::EventType::kMouseExited, gfx::Point(),
                          gfx::Point(), base::TimeTicks::Now(), 0, 0);
      split->OnMouseExited(exit);
      surface = split;
    }
    EXPECT_TRUE(tab->tab_styling()->IsApparentlyActive());
    expect_fill(tab, surface, SidebarItemVisualState::kActive,
                presentation == 1 || presentation == 3);
    controller->ShowHub(*project);
    layout();
    tab = FindNativeTab(view, active);
    ASSERT_TRUE(tab);
    EXPECT_TRUE(tab->IsActive());
    EXPECT_EQ(active, model->GetActiveTab());
    EXPECT_FALSE(tab->tab_styling()->IsApparentlyActive());
    ui::AXNodeData data;
    tab->GetViewAccessibility().GetAccessibleNodeData(&data);
    EXPECT_FALSE(data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
    EXPECT_FALSE(tab->close_button_for_testing()->GetVisible());
    expect_fill(tab, surface, SidebarItemVisualState::kResting,
                presentation == 1 || presentation == 3);
    controller->ExitHub();
    layout();
    EXPECT_TRUE(tab->tab_styling()->IsApparentlyActive());
    tab->GetViewAccessibility().GetAccessibleNodeData(&data);
    EXPECT_TRUE(data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
    EXPECT_EQ(presentation == 0 || presentation == 2,
              tab->close_button_for_testing()->GetVisible());
    expect_fill(tab, surface, SidebarItemVisualState::kActive,
                presentation == 1 || presentation == 3);
    EXPECT_EQ(active, model->GetActiveTab());
    EXPECT_EQ(presentation == 3 ? 2 : 1, model->count());
  }
}

// Native 그룹도 고정 leading 칸에서 mark↔화살표를 바꾸고 trailing 화살표는
// 숨긴다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       NativeGroupDisclosureReplacesLeadingMarkOnHover) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  browser()->tab_strip_model()->AddToNewGroup({0});
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto find_header = [](auto&& self, views::View* root) -> TabGroupHeaderView* {
    if (auto* header = views::AsViewClass<TabGroupHeaderView>(root))
      return header;
    for (const auto& child : root->children()) {
      if (auto* result = self(self, child))
        return result;
    }
    return nullptr;
  };
  auto* header = find_header(find_header, view);
  ASSERT_TRUE(header);
  VerticalGroupColorMark* mark = nullptr;
  for (const auto& child : header->children()) {
    if (auto* item = views::AsViewClass<VerticalGroupColorMark>(child))
      mark = item;
  }
  ASSERT_TRUE(mark);
  ASSERT_FALSE(mark->bounds().IsEmpty());
  const auto bounds = mark->bounds();
  auto raster = [&]() {
    gfx::Canvas canvas(mark->size(), 1, false);
    mark->OnPaint(&canvas);
    return canvas.GetBitmap();
  };
  const auto resting = raster();
  ui::MouseEvent enter(ui::EventType::kMouseEntered, gfx::Point(), gfx::Point(),
                       base::TimeTicks::Now(), 0, 0);
  header->OnMouseEntered(enter);
  EXPECT_FALSE(gfx::test::AreBitmapsEqual(resting, raster()));
  EXPECT_FALSE(header->collapse_icon_for_testing()->GetVisible());
  EXPECT_EQ(bounds, mark->bounds());
  ui::MouseEvent exit(ui::EventType::kMouseExited, gfx::Point(), gfx::Point(),
                      base::TimeTicks::Now(), 0, 0);
  header->OnMouseExited(exit);
  EXPECT_TRUE(gfx::test::AreBitmapsEqual(resting, raster()));
  view->GetFocusManager()->SetKeyboardAccessible(true);
  header->RequestFocus();
  ASSERT_EQ(header, view->GetFocusManager()->GetFocusedView());
  EXPECT_FALSE(gfx::test::AreBitmapsEqual(resting, raster()));
}

// 허브의 합성 면은 native MCV의 곡률과 공통 Header 표면/outline 순서를 따른다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubSharesNativeClipPaletteAndOutlineOrder) {
  auto project = service()->CreateProject(u"표면 검수");
  ASSERT_TRUE(project);
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  for (bool vertical : {true, false}) {
    tabs::VerticalTabStripStateController::From(browser())
        ->SetVerticalTabsEnabled(vertical);
    controller->ShowHub(*project);
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
    auto* header =
        view->GetViewByID(ProjectNavigationController::kHubHeaderViewId);
    auto* outline = view->GetViewByID(kCombinedSurfaceOutlineViewId);
    ASSERT_TRUE(hub && header && outline);
    ASSERT_TRUE(hub->layer());
    EXPECT_EQ(view->multi_contents_view()->GetBackgroundRadii(),
              hub->layer()->rounded_corner_radii());
    EXPECT_FALSE(hub->layer()->fills_bounds_opaquely());
    EXPECT_FALSE(header->background());
    ASSERT_TRUE(view->GetYeeHeaderPresentation());
    EXPECT_EQ(view->GetColorProvider()->GetColor(ui::kColorSysBase),
              view->GetYeeHeaderPresentation()->surface);
    ASSERT_TRUE(view->GetIndexOf(hub));
    ASSERT_TRUE(view->GetIndexOf(outline));
    EXPECT_LT(*view->GetIndexOf(hub), *view->GetIndexOf(outline));
    EXPECT_TRUE(view->toolbar()->GetVisible());
    controller->ExitHub();
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    EXPECT_FALSE(controller->showing_hub());
  }
}

// 실제 favicon driver의 아이콘과 변경 알림을 따르고 닫힌 참조는 로컬 캐시를
// 읽는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       PageFaviconsFollowLiveChangesAndSavedCache) {
  const auto url =
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  auto* driver =
      favicon::ContentFaviconDriver::FromWebContents(tab->GetContents());
  ASSERT_TRUE(driver);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return driver->FaviconIsValid() && !driver->GetFavicon().IsEmpty();
  }));
  ASSERT_TRUE(
      content::ExecJs(tab->GetContents(), "document.title='아이콘 페이지';"));
  const std::u16string title = u"아이콘 페이지";
  auto project = service()->CreateProject(u"아이콘 검수");
  ASSERT_TRUE(project);
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* sidebar = FindProjectSidebar(view);
  auto* page = FindButton(sidebar, title);
  ASSERT_TRUE(page);
  EXPECT_TRUE(gfx::test::AreImagesEqual(
      gfx::Image(page->GetImage(views::Button::STATE_NORMAL)),
      driver->GetFavicon()));
  const auto previous = driver->GetFavicon();
  ASSERT_TRUE(content::ExecJs(
      tab->GetContents(),
      "document.querySelector('link[rel=icon]').href='/favicon/icon.svg';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !gfx::test::AreImagesEqual(driver->GetFavicon(), previous);
  }));
  EXPECT_TRUE(gfx::test::AreImagesEqual(
      gfx::Image(page->GetImage(views::Button::STATE_NORMAL)),
      driver->GetFavicon()));
  const auto id = coordinator()->PageId(tab);
  const auto image = driver->GetFavicon();
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  model->CloseWebContentsAt(model->GetIndexOfTab(tab),
                            TabCloseTypes::CLOSE_USER_GESTURE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !coordinator()->FindLiveTab(id, browser()); }));
  auto* controller = ProjectNavigationController::From(browser());
  controller->ShowHub(*project);
  auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  ASSERT_TRUE(hub);
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* current = view->GetViewByID(ProjectNavigationController::kHubViewId);
    auto* saved = current ? FindButton(current, title) : nullptr;
    return saved &&
           gfx::test::AreImagesEqual(
               gfx::Image(saved->GetImage(views::Button::STATE_NORMAL)), image);
  }));
}

// 프로젝트가 펼쳐져도 전역 Favorites dock은 프로젝트와 일반 탭보다 위에
// 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       FavoritesStayAboveProjectAndOrdinaryRows) {
  auto project = service()->CreateProject(u"순서 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  model->SetTabPinned(0, true);
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(model->GetActiveWebContents()));
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title2.html"),
                   -1, true);
  auto* ordinary = model->GetActiveTab();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* dock = view->GetViewByID(yee::kSidebarFavoritesDockViewId);
  auto* sidebar = FindProjectSidebar(view);
  auto* native = FindNativeTab(view, ordinary);
  ASSERT_TRUE(dock && sidebar && native);
  EXPECT_TRUE(dock->IsDrawn());
  EXPECT_TRUE(sidebar->IsDrawn());
  EXPECT_TRUE(native->IsDrawn());
  EXPECT_LE(dock->GetBoundsInScreen().bottom(),
            sidebar->GetBoundsInScreen().y());
  EXPECT_LE(sidebar->GetBoundsInScreen().bottom(),
            native->GetBoundsInScreen().y());
}

// 프로젝트 행의 닫기와 가운데 클릭도 native 닫기 정책과 닫은 탭 복원을 따른다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectCloseAndMiddleClickKeepNativeTabRestore) {
  auto project = service()->CreateProject(u"닫기 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  for (bool middle : {false, true}) {
    chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                     -1, true);
    auto* tab = model->GetActiveTab();
    ASSERT_TRUE(content::WaitForLoadStop(tab->GetContents()));
    const auto title = tab->GetContents()->GetTitle();
    ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
    const auto id = coordinator()->PageId(tab);
    const int count = model->count();
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    auto* sidebar = FindProjectSidebar(view);
    ASSERT_TRUE(sidebar);
    if (middle) {
      views::View* page = FindButton(sidebar, title);
      ASSERT_TRUE(page);
      const auto point = page->GetLocalBounds().CenterPoint();
      const ui::MouseEvent press(
          ui::EventType::kMousePressed, point, point, base::TimeTicks::Now(),
          ui::EF_MIDDLE_MOUSE_BUTTON, ui::EF_MIDDLE_MOUSE_BUTTON);
      const ui::MouseEvent release(
          ui::EventType::kMouseReleased, point, point, base::TimeTicks::Now(),
          ui::EF_MIDDLE_MOUSE_BUTTON, ui::EF_MIDDLE_MOUSE_BUTTON);
      ASSERT_TRUE(page->OnMousePressed(press));
      page->OnMouseReleased(release);
    } else {
      auto* close = FindButton(
          sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) +
                       u": " + title);
      ASSERT_TRUE(close);
      views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
    }
    ASSERT_TRUE(
        base::test::RunUntil([&] { return model->count() == count - 1; }));
    EXPECT_TRUE(service()->FindPage(id));
    chrome::RestoreTab(browser());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return coordinator()->FindLiveTab(id, browser()); }));
    EXPECT_EQ(count, model->count());
  }
}

// 실제 수평 legacy/통합·세로 탭의 컨텍스트 메뉴를 프로젝트 행에서도 연다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectPageMenuUsesNativeTabSourceInBothLayouts) {
  auto project = service()->CreateProject(u"메뉴 검수");
  ASSERT_TRUE(project);
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(
      content::ExecJs(tab->GetContents(), "document.title='메뉴 페이지';"));
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    controller->ShowHub(*project);
    view->DeprecatedLayoutImmediately();
    auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
    ASSERT_TRUE(hub);
    auto* page = FindButton(hub, u"메뉴 페이지");
    ASSERT_TRUE(page && page->context_menu_controller());
    auto show_menu = base::BindOnce(
        [](views::View* page) {
          page->context_menu_controller()->ShowContextMenuForView(
              page, page->GetBoundsInScreen().CenterPoint(),
              ui::mojom::MenuSourceType::kMouse);
        },
        base::Unretained(page));
#if BUILDFLAG(IS_MAC)
    // Cocoa의 동기 tracking loop 안에서 메뉴가 열린 뒤 닫는다.
    EXPECT_TRUE(
        RunWithNativeContextMenuAutoCloseForTesting(std::move(show_menu)));
#else
    std::move(show_menu).Run();
#endif
    // macOS는 Cocoa native 메뉴를 사용하므로 Views controller가 없을 수 있다.
    if (auto* menu = views::MenuController::GetActiveInstance())
      menu->Cancel(views::MenuController::ExitType::kAll);
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(controller->showing_hub());
    EXPECT_EQ(tab, browser()->tab_strip_model()->GetActiveTab());
    controller->ExitHub();
    if (!vertical &&
        base::FeatureList::IsEnabled(features::kNewTabButtonContextMenu)) {
      view->DeprecatedLayoutImmediately();
      auto* button =
          views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
              kNewTabButtonElementId,
              views::ElementTrackerViews::GetContextForView(view), true);
      ASSERT_TRUE(button && button->context_menu_controller());
      auto show_new_tab_menu = base::BindOnce(
          [](views::View* button) {
            button->context_menu_controller()->ShowContextMenuForView(
                button, button->GetBoundsInScreen().CenterPoint(),
                ui::mojom::MenuSourceType::kMouse);
          },
          base::Unretained(button));
#if BUILDFLAG(IS_MAC)
      EXPECT_TRUE(RunWithNativeContextMenuAutoCloseForTesting(
          std::move(show_new_tab_menu)));
#else
      std::move(show_new_tab_menu).Run();
#endif
      if (auto* menu = views::MenuController::GetActiveInstance()) {
        menu->Cancel(views::MenuController::ExitType::kAll);
      }
      EXPECT_EQ(tab, browser()->tab_strip_model()->GetActiveTab());
    }
  }
}

// 실제 탭을 바꾸지 않고 허브에서 돌아와도 프로젝트 제목 대신 현재 페이지를
// 선택한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubExitRefreshesSidebarSelectionWithoutChangingTab) {
  auto project = service()->CreateProject(u"선택 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  ASSERT_TRUE(
      content::ExecJs(tab->GetContents(), "document.title='선택 페이지';"));
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto selected = [](views::View* button) {
    ui::AXNodeData data;
    button->GetViewAccessibility().GetAccessibleNodeData(&data);
    return data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected);
  };
  for (int command : {IDC_BACK, IDC_CLOSE_TAB}) {
    controller->ShowHub(*project);
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    auto* sidebar = FindProjectSidebar(view);
    ASSERT_TRUE(sidebar);
    EXPECT_TRUE(selected(FindButton(sidebar, u"선택 검수")));
    EXPECT_FALSE(selected(FindButton(sidebar, u"선택 페이지")));
    ASSERT_TRUE(controller->HandleCommand(command));
    base::RunLoop().RunUntilIdle();
    view->DeprecatedLayoutImmediately();
    sidebar = FindProjectSidebar(view);
    EXPECT_FALSE(selected(FindButton(sidebar, u"선택 검수")));
    EXPECT_TRUE(selected(FindButton(sidebar, u"선택 페이지")));
    EXPECT_EQ(tab, model->GetActiveTab());
    EXPECT_EQ(1, model->count());
  }
}

// 펼치기 애니메이션 도중 방향을 왕복해도 예전 tick이 폐기된 motion을 읽지
// 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       RapidSidebarDirectionChangeStopsPreviousAnimation) {
  auto project = service()->CreateProject(u"빠른 전환");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  auto* animations = BrowserAnimationController::From(browser());
  orientation->SetVerticalTabsEnabled(true);
  orientation->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !animations->IsAnimating(TabStripAnimations::kVerticalTabStrip);
  }));
  orientation->RequestCollapse(false);
  if (gfx::Animation::ShouldRenderRichAnimation())
    ASSERT_TRUE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  orientation->SetVerticalTabsEnabled(false);
  orientation->SetVerticalTabsEnabled(true);
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* compositor = view->GetWidget()->GetCompositor();
  for (int frame = 0; frame < 2; ++frame) {
    compositor->ScheduleFullRedraw();
    ui::DrawWaiterForTest::WaitForCompositingEnded(compositor);
  }
  EXPECT_FALSE(animations->IsAnimating(TabStripAnimations::kVerticalTabStrip));
  EXPECT_FALSE(orientation->IsCollapsed());
  EXPECT_EQ(1, model->count());
  auto* sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(sidebar);
  EXPECT_TRUE(sidebar->IsDrawn());
}

// 방향 전환 중 native strip 할당이 없는 프레임에서도 스크롤 여부를 계산할 수
// 있다.
IN_PROC_BROWSER_TEST_F(
    ProjectNavigationBrowserTest,
    UnifiedHorizontalLayoutToleratesMissingNativeAllocation) {
  if (!base::FeatureList::IsEnabled(tabs::kTabStripUnification))
    GTEST_SKIP() << "통합 탭바에서 실행하는 임시 배치 상태 검사다.";
  auto* model = browser()->tab_strip_model();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  orientation->SetVerticalTabsEnabled(false);
  base::RunLoop().RunUntilIdle();
  auto* region = view->tab_strip_view();
  auto* strip = region->GetTabStripView();
  ASSERT_TRUE(strip);
  const bool ignored = strip->GetProperty(views::kViewIgnoredByLayoutKey);
  strip->SetProperty(views::kViewIgnoredByLayoutKey, true);
  const auto proposed =
      static_cast<views::LayoutManagerBase*>(region->GetLayoutManager())
          ->GetProposedLayout(region->size());
  EXPECT_FALSE(proposed.GetLayoutFor(strip));
  region->DeprecatedLayoutImmediately();
  strip->SetProperty(views::kViewIgnoredByLayoutKey, ignored);
  view->DeprecatedLayoutImmediately();
  EXPECT_FALSE(orientation->ShouldDisplayVerticalTabs());
  EXPECT_EQ(1, model->count());
}

// 실제 문서와 입력값을 유지하면서 두 탭 배치에서 네이티브 허브를 왕복한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubPreservesPageAndMasksNativeChrome) {
  auto project = service()->CreateProject(u"조사");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* contents = model->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(
      contents,
      "document.body.innerHTML='<input "
      "id=note>';document.querySelector('#note').value='작성 중';"));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(controller);
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(orientation);
  // Mac 기본 키보드 모드에서도 허브 자체의 조작과 복귀 초점을 제공한다.
  view->GetFocusManager()->SetKeyboardAccessible(false);
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    view->DeprecatedLayoutImmediately();
    const GURL url = contents->GetLastCommittedURL();
    auto* page = view->multi_contents_view()
                     ->GetActiveContentsContainerView()
                     ->contents_view();
    const bool page_ignored = page->GetViewAccessibility().GetIsIgnored();
    const bool page_leaf = page->GetViewAccessibility().IsLeaf();
    controller->ShowHub(*project);
    view->DeprecatedLayoutImmediately();
    EXPECT_TRUE(controller->showing_hub());
    EXPECT_EQ(1, model->count());
    EXPECT_EQ(contents, model->GetActiveWebContents());
    EXPECT_EQ(url, contents->GetLastCommittedURL());
    EXPECT_TRUE(view->multi_contents_view()->GetVisible());
    EXPECT_FALSE(view->multi_contents_view()->GetEnabled());
    EXPECT_FALSE(view->toolbar()->GetEnabled());
    EXPECT_TRUE(view->toolbar()->GetViewAccessibility().IsLeaf());
    EXPECT_TRUE(view->toolbar()->GetViewAccessibility().GetIsIgnored());
    EXPECT_FALSE(view->multi_contents_view()
                     ->GetActiveContentsContainerView()
                     ->contents_view()
                     ->GetVisible());
    auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
    ASSERT_TRUE(hub);
    // native 콘텐츠·주소줄의 합성 레이어가 허브 위로 비치지 않아야 한다.
    ASSERT_TRUE(hub->layer());
    EXPECT_FALSE(hub->layer()->fills_bounds_opaquely());
    EXPECT_EQ(view->multi_contents_view()->bounds(), hub->bounds());
    auto* add =
        FindButton(hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_ADD_CURRENT));
    ASSERT_TRUE(add);
    EXPECT_FALSE(add->bounds().IsEmpty());
    auto* header =
        view->GetViewByID(ProjectNavigationController::kHubHeaderViewId);
    if (header && header->IsDrawn()) {
      ASSERT_TRUE(header->layer());
      EXPECT_FALSE(header->layer()->fills_bounds_opaquely());
      EXPECT_EQ(
          nullptr,
          FindButton(header, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_BACK)));
      EXPECT_EQ(nullptr,
                FindButton(header, l10n_util::GetStringUTF16(
                                       IDS_YEE_PROJECT_TOGGLE_SIDEBAR)));
      EXPECT_EQ(nullptr, FindButton(header, l10n_util::GetStringUTF16(
                                                IDS_YEE_PROJECT_CUSTOMIZE)));
      EXPECT_EQ(
          nullptr,
          FindButton(hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_BACK)));
      auto* name = FindButton(header, service()->FindProject(*project)->name);
      ASSERT_TRUE(name);
      EXPECT_EQ(name, view->GetFocusManager()->GetFocusedView());
      view->GetFocusManager()->AdvanceFocus(false);
      EXPECT_NE(name, view->GetFocusManager()->GetFocusedView());
      EXPECT_TRUE(header->Contains(view->GetFocusManager()->GetFocusedView()));
      const auto local = name->GetLocalBounds().CenterPoint();
      const auto browser_point =
          views::View::ConvertPointToTarget(name, view, local);
      EXPECT_TRUE(controller->IsHubHeaderControlAt(browser_point));
      EXPECT_EQ(HTCLIENT,
                view->NonClientHitTest(views::View::ConvertPointToTarget(
                    view, view->parent(), browser_point)));
    }
    auto* focused = view->GetFocusManager()->GetFocusedView();
    EXPECT_FALSE(focused && view->toolbar()->Contains(focused));
    EXPECT_FALSE(focused && view->multi_contents_view()->Contains(focused));
    EXPECT_TRUE(controller->HandleCommand(IDC_BACK));
    EXPECT_FALSE(controller->showing_hub());
    EXPECT_TRUE(view->toolbar()->GetEnabled());
    EXPECT_TRUE(view->multi_contents_view()->GetEnabled());
    EXPECT_TRUE(view->multi_contents_view()
                    ->GetActiveContentsContainerView()
                    ->contents_view()
                    ->GetVisible());
    EXPECT_EQ(page_ignored, page->GetViewAccessibility().GetIsIgnored());
    EXPECT_EQ(page_leaf, page->GetViewAccessibility().IsLeaf());
    EXPECT_FALSE(page->GetViewAccessibility().IsChildOfLeaf());
    EXPECT_EQ(
        "작성 중",
        content::EvalJs(contents, "document.querySelector('#note').value"));
  }
}

// 데이터 갱신 없이 배치를 바꿔도 메뉴 탐색과 세로 목록이 맞고 주소줄은 비운다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       NavigationFollowsTabLayoutWithoutMetadataChanges) {
  auto project = service()->CreateProject(u"방향 전환");
  ASSERT_TRUE(project);
  base::RunLoop().RunUntilIdle();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = ProjectNavigationController::From(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_FALSE(orientation->IsCollapsed());
  EXPECT_FALSE(FindButton(view->toolbar(), u"방향 전환"));
  auto* sidebar = FindProjectSidebar(view);
  ASSERT_TRUE(sidebar);
  for (bool hub : {false, true}) {
    if (hub)
      controller->ShowHub(*project);
    for (bool vertical : {true, false, true, false}) {
      orientation->SetVerticalTabsEnabled(vertical);
      view->DeprecatedLayoutImmediately();
      base::RunLoop().RunUntilIdle();
      EXPECT_FALSE(FindButton(view->toolbar(), u"방향 전환"));
      NewTabButtonMenuModel new_tab_menu(browser());
      const auto label = l10n_util::GetStringUTF16(IDS_YEE_PROJECTS);
      auto* projects = FindSubmenu(new_tab_menu, label);
      EXPECT_EQ(!vertical, projects != nullptr);
      ExpectUniqueMenuCommands(new_tab_menu);
      if (!vertical) {
        ASSERT_TRUE(projects);
        ProjectMenuNativeDelegate delegate;
        ui::SimpleMenuModel nested(nullptr);
        nested.AddItem(2, u"기존 하위 명령");
        TabContextMenuController context(
            browser()->tab_strip_model()->GetActiveTab()->GetHandle(),
            &delegate);
        auto root = std::make_unique<ui::SimpleMenuModel>(&context);
        root->AddItem(1, u"기존 native 명령");
        root->AddSubMenu(3, u"기존 하위 메뉴", &nested);
        context.LoadModel(std::move(root));
        auto* menu = context.GetMenuModel();
        ASSERT_TRUE(FindSubmenu(*menu, label));
        ExpectUniqueMenuCommands(*menu);
        const size_t index = menu->GetItemCount() - 1;
        const int command = menu->GetCommandIdAt(index);
        const int requests = delegate.requests;
        ui::Accelerator accelerator;
        EXPECT_TRUE(context.IsCommandIdEnabled(command));
        EXPECT_FALSE(context.IsCommandIdChecked(command));
        EXPECT_FALSE(context.IsCommandIdAlerted(command));
        EXPECT_FALSE(context.GetAcceleratorForCommandId(command, &accelerator));
        context.ExecuteCommand(command, 0);
        EXPECT_EQ(requests, delegate.requests);
      }
      sidebar = FindProjectSidebar(view);
      if (vertical) {
        ASSERT_TRUE(sidebar);
        EXPECT_TRUE(sidebar->GetVisible());
      } else {
        EXPECT_FALSE(sidebar);
      }
      EXPECT_EQ(hub, controller->showing_hub());
    }
    controller->ExitHub();
  }
}

// 넓은 창을 줄였다가 다시 넓혀도 허브 동작과 상단 선택기가 가려지지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubKeepsActionsVisibleAtMinimumWindowWidth) {
  auto project =
      service()->CreateProject(u"아주 긴 프로젝트 이름으로 너비 검수");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  base::RunLoop().RunUntilIdle();
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_FALSE(orientation->IsCollapsed());
  const gfx::Point origin = browser()->GetWindow()->GetBounds().origin();
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    controller->ShowHub(*project);
    for (int width : {1100, 500, 1100}) {
      SCOPED_TRACE(testing::Message()
                   << "vertical=" << vertical << ", width=" << width);
      browser()->GetWindow()->SetBounds(
          gfx::Rect(origin, gfx::Size(width, 650)));
      view->DeprecatedLayoutImmediately();
      base::RunLoop().RunUntilIdle();
      view->DeprecatedLayoutImmediately();
      auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
      ASSERT_TRUE(hub);
      auto* add = FindButton(
          hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_ADD_CURRENT));
      auto* more =
          FindButton(hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_MORE));
      ASSERT_TRUE(add && more);
      EXPECT_GE(add->width(), add->GetPreferredSize({}).width());
      EXPECT_GE(more->width(), more->GetPreferredSize({}).width());
      EXPECT_EQ(kSidebarMetrics.shell_control_size, more->height());
      const auto add_bounds = add->GetBoundsInScreen();
      const auto more_bounds = more->GetBoundsInScreen();
      if (add_bounds.x() != more_bounds.x())
        EXPECT_EQ(add_bounds.CenterPoint().y(), more_bounds.CenterPoint().y());
      ExpectButtonsInside(hub, hub->GetBoundsInScreen());
      auto* header =
          view->GetViewByID(ProjectNavigationController::kHubHeaderViewId);
      ASSERT_TRUE(header && header->IsDrawn());
      ExpectButtonsInside(header, header->GetBoundsInScreen());
      const gfx::Rect window_bounds = browser()->GetWindow()->GetBounds();
      EXPECT_TRUE(window_bounds.Contains(hub->GetBoundsInScreen()));
      EXPECT_TRUE(window_bounds.Contains(header->GetBoundsInScreen()));
      auto* name = FindButton(header, u"아주 긴 프로젝트 이름으로 너비 검수");
      ASSERT_TRUE(name);
      // 상위 Flex의 남는 폭이 이름과 화살표 사이의 큰 간격이 되지 않는다.
      EXPECT_LE(name->parent()->width(),
                name->parent()->GetPreferredSize({}).width());
      CaptureFrame(std::string("project-hub-") +
                   (vertical ? "sidebar-" : "horizontal-") +
                   std::to_string(width));
    }
    controller->ExitHub();
  }
}

// 동적 제목의 &를 그대로 표시하고 허브 선택기의 독립 색을 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       NavigationMenusKeepHubIndependentFromPageSurface) {
  const std::u16string project_name = u"색상 & 검수 && 유지";
  const std::u16string page_title = u"페이지 & 비교 && 유지";
  auto project = service()->CreateProject(project_name);
  ASSERT_TRUE(project);
  auto* const tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(content::ExecJs(tab->GetContents(),
                              "document.title='페이지 & 비교 && 유지';"));
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  base::RunLoop().RunUntilIdle();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = ProjectNavigationController::From(browser());
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(false);
  view->DeprecatedLayoutImmediately();
  uint64_t revision = 0;
  for (SkColor surface : {SK_ColorWHITE, SkColorSetRGB(0x18, 0x1A, 0x1F)}) {
    ++revision;
    const auto presentation =
        ResolveBrowserSurfacePresentation(surface, 1, revision, revision);
    view->toolbar()->SetCompactShellSurfacePresentation(presentation);
    EXPECT_FALSE(FindButton(view->toolbar(), project_name));
    NewTabButtonMenuModel menu(browser());
    auto* projects =
        FindSubmenu(menu, l10n_util::GetStringUTF16(IDS_YEE_PROJECTS));
    ASSERT_TRUE(projects);
    ExpectUniqueMenuCommands(menu);
    bool found_page = false;
    std::optional<size_t> project_index;
    for (size_t i = 0; i < projects->GetItemCount(); ++i) {
      if (projects->GetLabelAt(i) == page_title) {
        EXPECT_FALSE(projects->MayHaveMnemonicsAt(i));
        found_page = true;
      }
      if (projects->GetLabelAt(i) == project_name) {
        EXPECT_FALSE(projects->MayHaveMnemonicsAt(i));
        project_index = i;
      }
    }
    ASSERT_TRUE(found_page);
    ASSERT_TRUE(project_index);
    projects->ActivatedAt(*project_index, 0);
    ASSERT_TRUE(
        base::test::RunUntil([&] { return controller->showing_hub(); }));
    auto* header =
        view->GetViewByID(ProjectNavigationController::kHubHeaderViewId);
    ASSERT_TRUE(header);
    auto* hub_name = FindButton(header, project_name);
    ASSERT_TRUE(hub_name);
    view->toolbar()->SetCompactShellSurfacePresentation(presentation);
    EXPECT_EQ(hub_name->GetColorProvider()->GetColor(ui::kColorSysOnSurface),
              hub_name->GetCurrentTextColor());
    controller->ExitHub();
  }
}

// Mac의 별도 Toolbar Widget을 포함해 전체화면 왕복에서 허브와 실제 탭을
// 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubSurvivesFullscreenToolbarReparenting) {
  auto project = service()->CreateProject(u"전체화면");
  ASSERT_TRUE(project);
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* controller = ProjectNavigationController::From(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(
      contents, "document.body.innerHTML='<input id=note value=preserved>';"));
  browser()->GetWindow()->Activate();
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    view->DeprecatedLayoutImmediately();
    controller->ShowHub(*project);
    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    view->DeprecatedLayoutImmediately();
    EXPECT_TRUE(controller->showing_hub());
    EXPECT_EQ(contents, browser()->tab_strip_model()->GetActiveWebContents());
    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    view->DeprecatedLayoutImmediately();
    EXPECT_TRUE(controller->showing_hub());
    controller->ExitHub();
    EXPECT_EQ(
        "preserved",
        content::EvalJs(contents, "document.querySelector('#note').value"));
  }
}

// 닫은 페이지 참조는 남고 기본 탭 복제로 생긴 탭은 프로젝트를 상속하지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       DuplicateAndCloseKeepIndependentIdentities) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* first = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  const std::string id = coordinator()->PageId(first);
  ASSERT_FALSE(id.empty());
  auto* duplicate = chrome::DuplicateTabAt(browser(), model->active_index());
  ASSERT_TRUE(duplicate);
  auto* second = tabs::TabInterface::MaybeGetFromContents(duplicate);
  ASSERT_TRUE(second);
  EXPECT_TRUE(coordinator()->PageId(second).empty());
  ASSERT_TRUE(coordinator()->Attach(*second, *project, {}));
  EXPECT_NE(id, coordinator()->PageId(second));
  const int index = model->GetIndexOfTab(first);
  model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return coordinator()->FindLiveTab(id, browser()) == nullptr; }));
  EXPECT_TRUE(service()->FindPage(id));
  EXPECT_EQ(2u, service()->FindProject(*project)->page_ids.size());
  chrome::RestoreTab(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return coordinator()->FindLiveTab(id, browser()) != nullptr; }));
  EXPECT_EQ(2u, service()->FindProject(*project)->page_ids.size());
}

// 내용 교체 후 UUID는 새 WebContents로 이어지고 저장된 항목이 중복되지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       DiscardTransfersIdentityToReplacementContents) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const std::string id = coordinator()->PageId(tab);
  auto* old = tab->GetContents();
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  replacement->GetController().CopyStateFrom(&old->GetController(), false);
  auto* new_contents = replacement.get();
  auto deleted = model->DiscardWebContentsAt(model->GetIndexOfTab(tab),
                                             std::move(replacement));
  EXPECT_EQ(tab, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(new_contents, tab->GetContents());
  EXPECT_EQ(id, ProjectTabHelper::PageId(new_contents));
  EXPECT_TRUE(ProjectTabHelper::PageId(deleted.get()).empty());
  deleted.reset();
  EXPECT_EQ(1u, service()->FindProject(*project)->page_ids.size());
}

// 창 사이 이동에서 같은 UUID를 유지하고 프로젝트 삭제는 실제 탭을 닫지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       MovingBetweenWindowsKeepsIdentity) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const std::string id = coordinator()->PageId(tab);
  BrowserWindowInterface* target = CreateBrowser(browser()->GetProfile());
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  auto detached = model->DetachTabAtForInsertion(model->GetIndexOfTab(tab));
  target->tab_strip_model()->InsertDetachedTabAt(0, std::move(detached),
                                                 ADD_ACTIVE);
  EXPECT_EQ(nullptr, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(tab, coordinator()->FindLiveTab(id, target));
  EXPECT_EQ(target, tab->GetBrowserWindowInterface());
  service()->DeleteProject(*project);
  EXPECT_TRUE(coordinator()->PageId(tab).empty());
  EXPECT_EQ(tab, target->tab_strip_model()->GetTabAtIndex(0));
}

// 비공개 창은 일반 프로필의 프로젝트 저장소나 탭 연결을 공유하지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       OffTheRecordCannotAccessProjects) {
  auto* private_profile = browser()->GetProfile()->GetPrimaryOTRProfile(true);
  EXPECT_EQ(nullptr, ProjectServiceFactory::GetForProfile(private_profile));
  EXPECT_EQ(nullptr, ProjectTabCoordinator::GetForProfile(private_profile));
  auto* private_browser = CreateBrowser(private_profile);
  tabs::VerticalTabStripStateController::From(private_browser)
      ->SetVerticalTabsEnabled(false);
  NewTabButtonMenuModel menu(private_browser);
  EXPECT_FALSE(FindSubmenu(menu, l10n_util::GetStringUTF16(IDS_YEE_PROJECTS)));
}

// 닫기 경고를 취소한 탭은 계속 열림 상태이며 UUID와 내용 교체 관찰을 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       CancelBeforeUnloadKeepsLiveBinding) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  auto* contents = tab->GetContents();
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const std::string id = coordinator()->PageId(tab);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  model->ActivateTab(tab);
  ASSERT_TRUE(
      content::ExecJs(contents, "window.onbeforeunload=()=> '작성 중';"));
  content::PrepContentsForBeforeUnloadTest(contents);
  model->CloseWebContents(contents, TabCloseTypes::CLOSE_USER_GESTURE);
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  dialog->view()->CancelAppModalDialog();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(tab, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(id, coordinator()->PageId(tab));
  EXPECT_EQ(2, model->count());
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload=null;"));
  model->ActivateTabAt(model->GetIndexOfTab(tab) == 0 ? 1 : 0);
  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  replacement->GetController().CopyStateFrom(&contents->GetController(), false);
  auto deleted = model->DiscardWebContentsAt(model->GetIndexOfTab(tab),
                                             std::move(replacement));
  EXPECT_EQ(id, coordinator()->PageId(tab));
  EXPECT_EQ(tab, coordinator()->FindLiveTab(id, browser()));
}

// 이미 선택된 탭을 다시 눌러도 허브를 닫으며 native 뒤로 명령은 허브에
// 적용한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       NativeReselectionAndBackReturnToPage) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* model = browser()->tab_strip_model();
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  browser()->GetWindow()->Activate();
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    view->DeprecatedLayoutImmediately();
    for (bool space : {false, true}) {
      controller->ShowHub(*project);
      ASSERT_TRUE(controller->showing_hub());
      views::View* native = nullptr;
      if (!vertical && view->horizontal_tab_strip_for_testing()) {
        native = view->horizontal_tab_strip_for_testing()->tab_at(
            model->active_index());
      } else {
        native = FindNativeTab(view->tab_strip_view(), model->GetActiveTab());
      }
      ASSERT_TRUE(native);
      native->RequestFocus();
      ui::KeyEvent event(
          space ? ui::EventType::kKeyReleased : ui::EventType::kKeyPressed,
          space ? ui::VKEY_SPACE : ui::VKEY_RETURN, ui::EF_NONE);
      EXPECT_TRUE(space ? native->OnKeyReleased(event)
                        : native->OnKeyPressed(event));
      EXPECT_FALSE(controller->showing_hub());
      EXPECT_TRUE(base::test::RunUntil([&] {
        return content::EvalJs(model->GetActiveWebContents(),
                               "document.hasFocus()")
            .ExtractBool();
      }));
    }
    controller->ShowHub(*project);
    EXPECT_TRUE(chrome::IsCommandEnabled(browser(), IDC_BACK));
    chrome::ExecuteCommand(browser(), IDC_BACK);
    EXPECT_FALSE(controller->showing_hub());
    EXPECT_EQ(1, model->count());
  }
}

// 별도 blocked 알림을 사용하는 웹 모달도 허브 아래에 숨기지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       BlockedTabRevealsNativePage) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* controller = ProjectNavigationController::From(browser());
  controller->ShowHub(*project);
  ASSERT_TRUE(controller->showing_hub());
  model->SetTabBlocked(model->active_index(), true);
  EXPECT_FALSE(controller->showing_hub());
  controller->ShowHub(*project);
  EXPECT_FALSE(controller->showing_hub());
  model->SetTabBlocked(model->active_index(), false);
}

// 제목 갱신으로 행을 다시 그려도 같은 페이지의 키보드 초점을 복원한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       MetadataRefreshKeepsLogicalFocus) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* contents = model->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(contents, "document.title='첫 페이지';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetTitle() == u"첫 페이지"; }));
  ASSERT_TRUE(coordinator()->Attach(*model->GetActiveTab(), *project, {}));
  const std::string id = coordinator()->PageId(model->GetActiveTab());
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  controller->ShowHub(*project);
  auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  auto* button = FindButton(hub, u"첫 페이지");
  ASSERT_TRUE(button);
  button->RequestFocus();
  ASSERT_TRUE(content::ExecJs(contents, "document.title='갱신한 페이지';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service()->FindPage(id)->title == u"갱신한 페이지"; }));
  base::RunLoop().RunUntilIdle();
  hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  auto* updated = FindButton(hub, u"갱신한 페이지");
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated, view->GetFocusManager()->GetFocusedView());
  auto* remove = FindButton(
      hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_REMOVE_TOOLTIP) +
               u": 갱신한 페이지");
  ASSERT_TRUE(remove && remove->IsDrawn());
  remove->RequestFocus();
  ASSERT_EQ(remove, view->GetFocusManager()->GetFocusedView());
  ASSERT_TRUE(content::ExecJs(contents, "document.title='제거 초점 페이지';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service()->FindPage(id)->title == u"제거 초점 페이지"; }));
  base::RunLoop().RunUntilIdle();
  hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  remove = FindButton(
      hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_REMOVE_TOOLTIP) +
               u": 제거 초점 페이지");
  ASSERT_TRUE(remove);
  EXPECT_EQ(remove, view->GetFocusManager()->GetFocusedView());
  EXPECT_TRUE(remove->IsDrawn());
  controller->ExitHub();
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title2.html"),
                   -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(model->GetActiveWebContents()));
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* sidebar = FindProjectSidebar(view);
  auto* open = FindButton(sidebar, u"제거 초점 페이지");
  ASSERT_TRUE(open);
  open->RequestFocus();
  auto* close = FindButton(
      sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) +
                   u": 제거 초점 페이지");
  ASSERT_TRUE(close && close->IsDrawn());
  view->GetFocusManager()->SetKeyboardAccessible(true);
  close->RequestFocus();
  ASSERT_EQ(close, view->GetFocusManager()->GetFocusedView());
  ASSERT_TRUE(content::ExecJs(contents, "document.title='닫기 초점 페이지';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service()->FindPage(id)->title == u"닫기 초점 페이지"; }));
  base::RunLoop().RunUntilIdle();
  sidebar = FindProjectSidebar(view);
  close = FindButton(sidebar,
                     l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) +
                         u": 닫기 초점 페이지");
  ASSERT_TRUE(close);
  EXPECT_EQ(close, view->GetFocusManager()->GetFocusedView());
  EXPECT_TRUE(close->IsDrawn());
}

// 같은 UUID의 추가 복원 탭과 알 수 없는 UUID는 실제 탭을 보존한 채 분리한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ConflictingAndOrphanSessionIdentitiesStayUnassociated) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* first = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  const std::string id = coordinator()->PageId(first);
  for (const std::string& claim :
       {id, base::Uuid::GenerateRandomV4().AsLowercaseString()}) {
    auto contents = content::WebContents::Create(
        content::WebContents::CreateParams(browser()->GetProfile()));
    auto* raw = contents.get();
    ProjectTabHelper::Restore(raw, {{ProjectTabHelper::kSessionKey, claim}});
    model->AppendWebContents(std::move(contents), true);
    EXPECT_TRUE(coordinator()
                    ->PageId(tabs::TabInterface::MaybeGetFromContents(raw))
                    .empty());
    EXPECT_EQ(first, coordinator()->FindLiveTab(id, browser()));
  }
  EXPECT_EQ(3, model->count());
  EXPECT_EQ(1u, service()->FindProject(*project)->page_ids.size());
}

// 세션 파일 전체를 재구축해도 UUID 추가 데이터가 실제 탭 레코드에 남는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       SessionRebuildPersistsPageUUID) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* tab = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const std::string id = coordinator()->PageId(tab);
  auto* session = SessionServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(session);
  session->ResetFromCurrentBrowsers();
  SessionServiceTestHelper helper(session);
  helper.SaveNow();
  session->MoveCurrentSessionToLastSession();
  base::test::TestFuture<std::vector<std::unique_ptr<sessions::SessionWindow>>,
                         SessionID, bool>
      result;
  session->GetLastSession(result.GetCallback());
  ASSERT_TRUE(result.Wait());
  ASSERT_FALSE(result.Get<2>());
  const auto& windows = result.Get<0>();
  ASSERT_FALSE(windows.empty());
  bool found = false;
  for (const auto& window : windows) {
    for (const auto& saved : window->tabs) {
      auto it = saved->extra_data.find(ProjectTabHelper::kSessionKey);
      found |= it != saved->extra_data.end() && it->second == id;
    }
  }
  EXPECT_TRUE(found);
}

// 분할 비율과 활성 문서를 보존하고 보조 패널을 열어도 허브를 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       HubKeepsSplitAndAuxiliaryPanelState) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* first = model->GetActiveWebContents();
  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   -1, true);
  auto* second = model->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(second));
  model->ActivateTabAt(0);
  split_tabs::SplitTabVisualData visual(split_tabs::SplitTabLayout::kSideBySide,
                                        0.37);
  const auto split = model->AddToNewSplit(
      {1}, visual, split_tabs::SplitTabCreatedSource::kToolbarButton);
  auto* active = model->GetActiveTab();
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  controller->ShowHub(*project);
  view->DeprecatedLayoutImmediately();
  ASSERT_TRUE(controller->showing_hub());
  for (const auto& container :
       view->multi_contents_view()->contents_container_views()) {
    EXPECT_FALSE(container->contents_view()->GetVisible());
  }
  auto* panel = SidePanelUI::From(browser());
  ASSERT_TRUE(panel);
  panel->Show(SidePanelEntryId::kBookmarks, std::nullopt, true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return panel->IsSidePanelShowing(); }));
  EXPECT_TRUE(controller->showing_hub());
  EXPECT_EQ(active, model->GetActiveTab());
  EXPECT_EQ(split, model->GetSplitForTab(0));
  EXPECT_EQ(split, model->GetSplitForTab(1));
  EXPECT_EQ(visual, *model->GetSplitData(split)->visual_data());
  controller->ExitHub();
  view->DeprecatedLayoutImmediately();
  EXPECT_TRUE(panel->IsSidePanelShowing());
  EXPECT_EQ(first, model->GetWebContentsAt(0));
  EXPECT_EQ(second, model->GetWebContentsAt(1));
  EXPECT_EQ(active, model->GetActiveTab());
  EXPECT_EQ(visual, *model->GetSplitData(split)->visual_data());
  model->SetTabBlocked(1, true);
  controller->ShowHub(*project);
  EXPECT_FALSE(controller->showing_hub());
  model->SetTabBlocked(1, false);
  panel->Close();
}

// 같은 창에서 여러 번 열면 native 탭과 재열기 예약을 재사용한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ReopenUsesOneNativeTabPerItem) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto page = service()->AttachPage(
      *project, embedded_test_server()->GetURL("/empty.html"), u"보관 페이지");
  ASSERT_TRUE(page);
  auto release = coordinator()->BeginReopenPage(*page, browser());
  ASSERT_FALSE(release.is_null());
  EXPECT_TRUE(coordinator()->BeginReopenPage(*page, browser()).is_null());
  std::move(release).Run();
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  controller->ShowHub(*project);
  auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  auto* open = FindButton(hub, u"보관 페이지");
  ASSERT_TRUE(open);
  views::test::ButtonTestApi(open).NotifyDefaultMouseClick();
  auto* created = coordinator()->FindLiveTab(*page, browser());
  ASSERT_TRUE(created);
  ASSERT_TRUE(content::WaitForLoadStop(created->GetContents()));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
  controller->ShowHub(*project);
  hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  open = FindButton(hub, created->GetContents()->GetTitle());
  ASSERT_TRUE(open);
  views::test::ButtonTestApi(open).NotifyDefaultMouseClick();
  EXPECT_EQ(created, coordinator()->FindLiveTab(*page, browser()));
  EXPECT_EQ(2, browser()->tab_strip_model()->count());
}

// 다른 창에 열린 항목은 이 창에서는 보관 상태이며, 닫아도 이 창 허브를
// 유지한다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ClosingTabRefreshesOtherWindowHub) {
  auto project = service()->CreateProject(u"자료");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* tab = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*tab, *project, {}));
  const std::string id = coordinator()->PageId(tab);
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  auto* other_controller = ProjectNavigationController::From(other);
  auto* other_view = BrowserView::GetBrowserViewForBrowser(other);
  other_controller->ShowHub(*project);
  auto* hub = other_view->GetViewByID(ProjectNavigationController::kHubViewId);
  ASSERT_TRUE(hub);
  ASSERT_TRUE(HasLabel(hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_SAVED)));
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  model->CloseWebContentsAt(model->GetIndexOfTab(tab),
                            TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !coordinator()->FindLiveTab(id, browser()); }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    auto* current_hub =
        other_view->GetViewByID(ProjectNavigationController::kHubViewId);
    return current_hub && HasLabel(current_hub, l10n_util::GetStringUTF16(
                                                    IDS_YEE_PROJECT_SAVED));
  }));
  EXPECT_TRUE(other_controller->showing_hub());
}

// 같은 자료를 각 창에서 열어도 native 탭·폼·탐색·닫기는 다른 창에 전파하지
// 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ProjectPageInstancesStayInTheirOwnWindow) {
  auto project = service()->CreateProject(u"창별 자료");
  ASSERT_TRUE(project);
  auto* first_model = browser()->tab_strip_model();
  auto* first = first_model->GetActiveTab();
  ASSERT_TRUE(content::ExecJs(
      first->GetContents(),
      "document.title='첫 창 제목';document.body.innerHTML='<input id=note>';"
      "document.querySelector('#note').value='첫 창 작성 중';"));
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  const auto id = coordinator()->PageId(first);
  const auto first_url = first->GetContents()->GetLastCommittedURL();
  const int first_count = first_model->count();
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      other, embedded_test_server()->GetURL("/empty.html")));
  auto* other_model = other->tab_strip_model();
  const int other_count = other_model->count();
  auto* controller = ProjectNavigationController::From(other);
  auto* view = BrowserView::GetBrowserViewForBrowser(other);
  tabs::VerticalTabStripStateController::From(other)->SetVerticalTabsEnabled(
      true);
  controller->ShowHub(*project);
  view->DeprecatedLayoutImmediately();
  auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  ASSERT_TRUE(HasLabel(hub, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_SAVED)));
  auto* open = FindButton(hub, u"첫 창 제목");
  ASSERT_TRUE(open);
  views::test::ButtonTestApi(open).NotifyDefaultMouseClick();
  auto* second = coordinator()->FindLiveTab(id, other);
  ASSERT_TRUE(second && second != first);
  ASSERT_TRUE(content::WaitForLoadStop(second->GetContents()));
  EXPECT_EQ(first, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(second, other_model->GetActiveTab());
  EXPECT_EQ(other_count + 1, other_model->count());
  EXPECT_EQ(first_count, first_model->count());
  EXPECT_EQ(first, first_model->GetActiveTab());
  EXPECT_EQ(first_url, first->GetContents()->GetLastCommittedURL());
  EXPECT_EQ("첫 창 작성 중",
            content::EvalJs(first->GetContents(),
                            "document.querySelector('#note').value"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      other, embedded_test_server()->GetURL("/title2.html")));
  ASSERT_TRUE(
      content::ExecJs(second->GetContents(), "document.title='둘째 창 제목';"));
  base::RunLoop().RunUntilIdle();
  auto* first_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* first_controller = ProjectNavigationController::From(browser());
  first_controller->ShowHub(*project);
  EXPECT_TRUE(FindButton(
      first_view->GetViewByID(ProjectNavigationController::kHubViewId),
      u"첫 창 제목"));
  controller->ShowHub(*project);
  hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
  open = FindButton(hub, u"둘째 창 제목");
  ASSERT_TRUE(open);
  views::test::ButtonTestApi(open).NotifyDefaultMouseClick();
  EXPECT_EQ(second, coordinator()->FindLiveTab(id, other));
  EXPECT_EQ(other_count + 1, other_model->count());
  EXPECT_TRUE(first_controller->showing_hub());
  first_controller->ExitHub();
  base::RunLoop().RunUntilIdle();
  view->DeprecatedLayoutImmediately();
  auto* sidebar = FindProjectSidebar(view);
  auto* close = FindButton(
      sidebar, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) +
                   u": 둘째 창 제목");
  ASSERT_TRUE(views::AsViewClass<TabCloseButton>(close));
  views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !coordinator()->FindLiveTab(id, other); }));
  EXPECT_EQ(first, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(first_count, first_model->count());
  EXPECT_TRUE(service()->FindPage(id));
  EXPECT_EQ(1u, service()->FindProject(*project)->page_ids.size());
  EXPECT_EQ("첫 창 작성 중",
            content::EvalJs(first->GetContents(),
                            "document.querySelector('#note').value"));
}

// 공유 캐시와 같은 제목으로 바뀌어도 현재 창의 행은 갱신된다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       LocalTitleRefreshesWhenSharedMetadataIsUnchanged) {
  auto project = service()->CreateProject(u"창별 제목");
  ASSERT_TRUE(project);
  auto* first = browser()->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(
      content::ExecJs(first->GetContents(), "document.title='제목 A';"));
  ASSERT_TRUE(coordinator()->Attach(*first, *project, {}));
  const auto id = coordinator()->PageId(first);
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      other, first->GetContents()->GetLastCommittedURL()));
  auto* second = other->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(coordinator()->BindRestoredPage(*second, id));
  ASSERT_TRUE(
      content::ExecJs(second->GetContents(), "document.title='제목 B';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service()->FindPage(id)->title == u"제목 B"; }));
  auto* controller = ProjectNavigationController::From(browser());
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  controller->ShowHub(*project);
  ASSERT_TRUE(FindButton(
      view->GetViewByID(ProjectNavigationController::kHubViewId), u"제목 A"));
  view->DeprecatedLayoutImmediately();
  auto* picker = FindButton(
      view->GetViewByID(ProjectNavigationController::kHubHeaderViewId),
      l10n_util::GetStringUTF16(IDS_YEE_PROJECT_PICKER));
  ASSERT_TRUE(picker);
  views::test::ButtonTestApi(picker).NotifyDefaultMouseClick();
  auto* menu = views::MenuController::GetActiveInstance();
  ASSERT_TRUE(menu && menu->GetSelectedMenuItem());
  std::vector<std::u16string> labels;
  for (auto* item : menu->GetSelectedMenuItem()
                        ->GetRootMenuItem()
                        ->GetSubmenu()
                        ->GetMenuItems())
    labels.push_back(item->title());
  menu->Cancel(views::MenuController::ExitType::kAll);
  EXPECT_NE(labels.end(), std::ranges::find(labels, u"제목 A"));
  EXPECT_EQ(labels.end(), std::ranges::find(labels, u"제목 B"));
  ASSERT_TRUE(
      content::ExecJs(first->GetContents(), "document.title='제목 B';"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
    return hub && FindButton(hub, u"제목 B") && !FindButton(hub, u"제목 A");
  }));
  EXPECT_TRUE(controller->showing_hub());
  EXPECT_EQ(first, browser()->tab_strip_model()->GetActiveTab());
  EXPECT_EQ(second, other->tab_strip_model()->GetActiveTab());
  ASSERT_TRUE(content::ExecJs(first->GetContents(), "document.title='';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return first->GetContents()->GetTitle() != u"제목 B"; }));
  const auto native_title = first->GetContents()->GetTitle();
  ASSERT_FALSE(native_title.empty());
  ASSERT_TRUE(
      content::ExecJs(second->GetContents(), "document.title='제목 C';"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return service()->FindPage(id)->title == u"제목 C"; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* hub = view->GetViewByID(ProjectNavigationController::kHubViewId);
    return hub && FindButton(hub, native_title) && !FindButton(hub, u"제목 C");
  }));
}

// 재열기 예약과 복원 UUID는 창 안에서만 중복을 막는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       ReopenAndRestoreIdentityAreScopedToWindow) {
  auto project = service()->CreateProject(u"창별 복원");
  ASSERT_TRUE(project);
  auto page = service()->AttachPage(
      *project, embedded_test_server()->GetURL("/empty.html"), u"보관 자료");
  ASSERT_TRUE(page);
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      other, embedded_test_server()->GetURL("/empty.html")));
  auto first_release = coordinator()->BeginReopenPage(*page, browser());
  auto second_release = coordinator()->BeginReopenPage(*page, other);
  ASSERT_TRUE(first_release && second_release);
  EXPECT_TRUE(coordinator()->BeginReopenPage(*page, browser()).is_null());
  EXPECT_TRUE(coordinator()->BeginReopenPage(*page, other).is_null());
  std::move(first_release).Run();
  auto first_again = coordinator()->BeginReopenPage(*page, browser());
  EXPECT_TRUE(first_again);
  EXPECT_TRUE(coordinator()->BeginReopenPage(*page, other).is_null());
  std::move(first_again).Run();
  std::move(second_release).Run();
  auto* first = browser()->tab_strip_model()->GetActiveTab();
  auto* second = other->tab_strip_model()->GetActiveTab();
  ASSERT_TRUE(coordinator()->BindRestoredPage(*first, *page));
  ASSERT_TRUE(coordinator()->BindRestoredPage(*second, *page));
  EXPECT_EQ(first, coordinator()->FindLiveTab(*page, browser()));
  EXPECT_EQ(second, coordinator()->FindLiveTab(*page, other));
  EXPECT_TRUE(coordinator()->BindRestoredPage(*second, *page));
  chrome::NewTab(other, NewTabTypes::kNewTabCommand);
  auto* duplicate = other->tab_strip_model()->GetActiveTab();
  EXPECT_FALSE(coordinator()->BindRestoredPage(*duplicate, *page));
  EXPECT_TRUE(coordinator()->PageId(duplicate).empty());
  EXPECT_EQ(second, coordinator()->FindLiveTab(*page, other));
}

// 동일 자료가 이미 있는 창으로 이동해도 native 탭을 병합하거나 닫지 않는다.
IN_PROC_BROWSER_TEST_F(ProjectNavigationBrowserTest,
                       MovingToWindowWithSamePageKeepsBothNativeTabs) {
  auto project = service()->CreateProject(u"창 이동 충돌");
  ASSERT_TRUE(project);
  auto* model = browser()->tab_strip_model();
  auto* moved = model->GetActiveTab();
  ASSERT_TRUE(coordinator()->Attach(*moved, *project, {}));
  const auto id = coordinator()->PageId(moved);
  BrowserWindowInterface* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      other, embedded_test_server()->GetURL("/title1.html")));
  auto* other_model = other->tab_strip_model();
  auto* existing = other_model->GetActiveTab();
  ASSERT_TRUE(coordinator()->BindRestoredPage(*existing, id));
  const int count = other_model->count();
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  auto detached = model->DetachTabAtForInsertion(model->GetIndexOfTab(moved));
  other_model->InsertDetachedTabAt(0, std::move(detached), ADD_ACTIVE);
  EXPECT_EQ(count + 1, other_model->count());
  EXPECT_EQ(moved, other_model->GetTabAtIndex(0));
  EXPECT_TRUE(coordinator()->PageId(moved).empty());
  EXPECT_EQ(id, coordinator()->PageId(existing));
  EXPECT_EQ(existing, coordinator()->FindLiveTab(id, other));
  EXPECT_EQ(nullptr, coordinator()->FindLiveTab(id, browser()));
  EXPECT_EQ(1u, service()->FindProject(*project)->page_ids.size());
}

}  // namespace
}  // namespace yee::projects
