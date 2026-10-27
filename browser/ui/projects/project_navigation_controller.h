// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_CONTROLLER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/base/unowned_user_data/scoped_unowned_user_data.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view_tracker.h"

class BrowserView;
class BrowserWindowInterface;
class TabStripModel;

namespace views {
class View;
class MenuRunner;
}  // namespace views

namespace yee::projects {
class ProjectNavigationView;
class ProjectPicker;
class ProjectService;
class ProjectTabCoordinator;

// 창의 페이지/허브 전환만 소유한다. TabStripModel과 URL 탐색은 Chromium에
// 맡긴다.
class ProjectNavigationController : public TabStripModelObserver {
 public:
  static constexpr int kHubViewId = 93020;
  static constexpr int kHubHeaderViewId = 93021;
  static constexpr int kSidebarViewId = 93022;
  DECLARE_USER_DATA(ProjectNavigationController);
  explicit ProjectNavigationController(BrowserWindowInterface* browser);
  ~ProjectNavigationController() override;
  static ProjectNavigationController* From(BrowserWindowInterface* browser);
  static bool IsHubShowing(BrowserWindowInterface* browser);
  static bool IsHubShowingForView(BrowserView* view);
  // 세로 프로젝트 참조가 실제로 제공되는 동안만 일반 native 행을 생략한다.
  static bool ShouldShowNativeTab(const tabs::TabInterface* tab);

  std::unique_ptr<views::View> CreateNavigationView();
  std::unique_ptr<views::View> CreateSidebarView();
  void SetSidebarExpandedPresentation(bool expanded);
  // 주소줄이 결정한 색을 페이지 선택기에만 전달한다. 허브 색은 독립적이다.
  void SetToolbarSurfacePresentation(
      const std::optional<BrowserSurfacePresentation>& presentation);
  bool can_create_project() const;
  void AttachBrowserView(BrowserView* view);
  void AfterBrowserLayout();
  void CreateProject();
  void ShowHub(std::string project_id);
  void ExitHub(bool focus_page = true);
  bool showing_hub() const { return showing_hub_; }
  bool IsHubHeaderControlAt(const gfx::Point& browser_view_point);
  // 처리한 명령은 true를 반환한다. 주소줄·창 닫기 등은 페이지로 돌아간 뒤
  // 위임한다.
  bool HandleCommand(int command);
  std::optional<bool> CommandEnabledOverride(int command) const;

 private:
  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnModalChanged(tabs::TabInterface* tab);
  bool IsInCurrentPageSurface(const tabs::TabInterface* tab) const;
  void ObserveTabDialogs();
  void ScheduleRefresh();
  void Refresh();
  void InvalidateNativeTabLayouts();
  void ShowPicker();
  void OpenSelectedHub();
  void RenameProject(std::string project_id);
  void DeleteProject(std::string project_id);
  void AttachCurrentPage();
  void OpenPage(std::string page_id);
  void RemovePage(std::string page_id);
  void ClosePage(std::string page_id);
  void ShowPageMenu(std::string page_id,
                    views::View* source,
                    const gfx::Point& point,
                    ui::mojom::MenuSourceType type);
  BrowserView* browser_view();
  void RestoreNativeViews();

  struct NativeViewState {
    std::unique_ptr<views::ViewTracker> tracker;
    bool visible;
    bool enabled;
    bool ignored;
    bool leaf;
    bool visibility_changed;
  };

  const raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<ProjectService> service_;
  raw_ptr<ProjectTabCoordinator> tabs_;
  ui::ScopedUnownedUserData<ProjectNavigationController> scoped_data_;
  base::CallbackListSubscription service_subscription_;
  base::CallbackListSubscription tabs_subscription_;
  base::CallbackListSubscription session_subscription_;
  std::vector<base::CallbackListSubscription> modal_subscriptions_;
  views::ViewTracker browser_view_tracker_;
  views::ViewTracker navigation_tracker_;
  views::ViewTracker sidebar_tracker_;
  views::ViewTracker hub_tracker_;
  views::ViewTracker hub_toolbar_tracker_;
  views::ViewTracker hub_selector_tracker_;
  std::vector<NativeViewState> native_states_;
  std::unique_ptr<ProjectPicker> picker_model_;
  std::unique_ptr<views::MenuRunner> picker_runner_;
  std::string selected_project_id_;
  std::string rendered_hub_project_id_;
  std::string revealed_page_id_;
  std::string revealed_project_id_;
  std::optional<BrowserSurfacePresentation> toolbar_presentation_;
  bool showing_hub_ = false;
  bool sidebar_expanded_ = true;
  bool refresh_pending_ = false;
  base::WeakPtrFactory<ProjectNavigationController> weak_factory_{this};
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_CONTROLLER_H_
