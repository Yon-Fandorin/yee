// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_VIEW_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/projects/project_page_button.h"
#include "ui/views/view.h"

class Profile;

namespace views {
class MdTextButton;
class ScrollView;
}  // namespace views

namespace yee::projects {

using NavigationPage = ProjectPageItem;
class ProjectActionMenu;

struct NavigationProject {
  std::string id;
  std::u16string name;
  std::vector<NavigationPage> pages;
};

// 프로젝트 참조를 표시한다. 실제 탭 수명과 명령은 native 소유자가 유지한다.
// 진입 콜백의 bool은 포인터 여부이며 키보드 진입은 허브 초기 초점을 유지한다.
class ProjectSidebarView : public views::View {
  METADATA_HEADER(ProjectSidebarView, views::View)

 public:
  ProjectSidebarView(
      Profile* profile,
      base::RepeatingCallback<void(std::string, bool)> open_project,
      base::RepeatingCallback<void(std::string)> open_page,
      base::RepeatingCallback<void(std::string)> close_page,
      base::RepeatingCallback<void(std::string)> rename_project,
      base::RepeatingCallback<void(std::string)> delete_project,
      base::RepeatingCallback<void(std::string,
                                   views::View*,
                                   const gfx::Point&,
                                   ui::mojom::MenuSourceType)> show_menu);
  void SetGroupMemberInset(int inset);
  void RevealPage(const std::string& project_id);
  ~ProjectSidebarView() override;
  void SetProjects(const std::vector<NavigationProject>& projects,
                   const std::string& selected,
                   bool writable);
  void Layout(PassKey) override;

 private:
  void UpdatePreferredSize();
  void Toggle(std::string id,
              views::View* pages,
              views::MdTextButton* arrow,
              views::MdTextButton* name);
  void ShowMoreMenu(std::string id,
                    views::View* anchor,
                    const ui::Event& event);
  const raw_ptr<Profile> profile_;
  int group_member_inset_ = 0;
  bool repaint_after_layout_ = false;
  std::optional<int> pending_scroll_position_;
  raw_ptr<views::ScrollView> scroll_;
  std::set<std::string> expanded_;
  std::map<std::string, raw_ptr<views::View>> focus_targets_;
  base::RepeatingCallback<void(std::string, bool)> open_project_;
  base::RepeatingCallback<void(std::string)> open_page_;
  base::RepeatingCallback<void(std::string)> close_page_;
  base::RepeatingCallback<void(std::string)> rename_project_;
  base::RepeatingCallback<void(std::string)> delete_project_;
  std::unique_ptr<ProjectActionMenu> project_menu_;
  base::RepeatingCallback<void(std::string,
                               views::View*,
                               const gfx::Point&,
                               ui::mojom::MenuSourceType)>
      show_menu_;
};

// 이름과 목록 버튼은 서로 다른 동작이며 하나의 큰 메뉴 버튼으로 합치지 않는다.
class ProjectNavigationView : public views::View {
  METADATA_HEADER(ProjectNavigationView, views::View)

 public:
  ProjectNavigationView(base::RepeatingCallback<void(bool)> open_hub,
                        base::RepeatingClosure show_picker);
  ~ProjectNavigationView() override;
  void SetProject(const std::u16string& name, bool available);
  void FocusName();
  void SetSurfacePresentation(
      const std::optional<BrowserSurfacePresentation>& presentation);
  void OnThemeChanged() override;
  views::View* picker_anchor() const;

 private:
  void UpdateSurfaceColors();
  raw_ptr<views::MdTextButton> name_;
  raw_ptr<views::MdTextButton> picker_;
  bool follows_page_surface_ = false;
  std::optional<BrowserSurfacePresentation> presentation_;
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_VIEW_H_
