// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_HUB_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_HUB_VIEW_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/yee/projects/project_page_button.h"
#include "ui/views/view.h"

namespace views {
class ScrollView;
class MenuRunner;
class Label;
class MdTextButton;
}  // namespace views

namespace yee::projects {

using HubPage = ProjectPageItem;

class ProjectActionMenu;

struct HubActions {
  base::RepeatingClosure add_current;
  base::RepeatingClosure rename;
  base::RepeatingClosure delete_project;
  base::RepeatingCallback<void(std::string)> open_page;
  base::RepeatingCallback<void(std::string)> remove_page;
  base::RepeatingCallback<void(std::string)> close_page;
  base::RepeatingCallback<void(std::string,
                               views::View*,
                               const gfx::Point&,
                               ui::mojom::MenuSourceType)>
      show_page_menu;
};

struct HubFocus {
  enum class Action { kNone, kAdd, kMore, kOpen, kRemove };
  Action action = Action::kNone;
  std::string page_id;
};

class ProjectHubView : public views::View {
  METADATA_HEADER(ProjectHubView, views::View)

 public:
  ProjectHubView(Profile* profile,
                 const std::u16string& name,
                 const std::u16string& description,
                 const std::vector<HubPage>& pages,
                 bool writable,
                 bool can_add,
                 bool restoring,
                 HubActions actions);
  ~ProjectHubView() override;
  HubFocus GetFocusedAction() const;
  void RestoreFocus(const HubFocus& focus);
  int scroll_position() const;
  void RestoreScrollPosition(int position);
  void FocusInitial();
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  raw_ptr<views::ScrollView> scroll_;
  std::optional<int> pending_scroll_position_;
  raw_ptr<views::View> actions_row_;
  raw_ptr<views::MdTextButton> add_;
  raw_ptr<views::MdTextButton> more_;
  std::vector<raw_ptr<views::Label>> hosts_;
  std::vector<raw_ptr<views::Label>> states_;
  std::unique_ptr<ProjectActionMenu> menu_;
  void ShowMoreMenu(const ui::Event& event);
  std::map<std::string, raw_ptr<views::MdTextButton>> open_buttons_;
  std::map<std::string, raw_ptr<views::MdTextButton>> remove_buttons_;
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_HUB_VIEW_H_
