// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_CONTROLS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_CONTROLS_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/md_text_button.h"

namespace gfx {
struct VectorIcon;
}
namespace views {
class MenuRunner;
}

namespace yee::projects {

// 허브와 Sidebar의 보조 조작은 같은 native 크기·상태 경로를 사용한다.
std::unique_ptr<views::MdTextButton> CreateProjectTextButton(
    views::Button::PressedCallback action,
    const std::u16string& title);
std::unique_ptr<views::MdTextButton> CreateProjectIconButton(
    views::Button::PressedCallback action,
    const gfx::VectorIcon& icon,
    const std::u16string& label);

class ProjectActionMenu : public ui::SimpleMenuModel,
                          public ui::SimpleMenuModel::Delegate {
 public:
  ProjectActionMenu(base::RepeatingClosure rename,
                    base::RepeatingClosure remove);
  ~ProjectActionMenu() override;
  void Run(views::View* anchor, const ui::Event& event);

 private:
  bool IsCommandIdChecked(int id) const override;
  bool IsCommandIdEnabled(int id) const override;
  void ExecuteCommand(int id, int flags) override;

  base::RepeatingClosure rename_;
  base::RepeatingClosure remove_;
  std::unique_ptr<views::MenuRunner> runner_;
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_CONTROLS_H_
