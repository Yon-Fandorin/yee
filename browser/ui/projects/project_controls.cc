// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_controls.h"

#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace yee::projects {

std::unique_ptr<views::MdTextButton> CreateProjectTextButton(
    views::Button::PressedCallback action,
    const std::u16string& title) {
  auto button = std::make_unique<views::MdTextButton>(std::move(action), title);
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  button->SetEnabledTextColors(ui::kColorSysOnSurfaceSubtle);
  button->SetCornerRadius(kSidebarMetrics.shell_control_corner_radius);
  button->SetCustomPadding(gfx::Insets::VH(4, 8));
  button->SetMinSize(gfx::Size(0, kSidebarMetrics.shell_control_size));
  views::InstallRoundRectHighlightPathGenerator(
      button.get(), gfx::Insets(), kSidebarMetrics.shell_control_corner_radius);
  return button;
}

std::unique_ptr<views::MdTextButton> CreateProjectIconButton(
    views::Button::PressedCallback action,
    const gfx::VectorIcon& icon,
    const std::u16string& label) {
  auto button = CreateProjectTextButton(std::move(action), u"");
  const gfx::Size size(kSidebarMetrics.shell_control_size,
                       kSidebarMetrics.shell_control_size);
  button->SetMinSize(size);
  button->SetMaxSize(size);
  button->SetCustomPadding(gfx::Insets());
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(icon, ui::kColorSysOnSurfaceSubtle, 16));
  button->SetTooltipText(label);
  button->GetViewAccessibility().SetName(label);
  return button;
}

ProjectActionMenu::ProjectActionMenu(base::RepeatingClosure rename,
                                     base::RepeatingClosure remove)
    : SimpleMenuModel(this),
      rename_(std::move(rename)),
      remove_(std::move(remove)) {
  AddItem(1, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_RENAME));
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(2, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_DELETE));
}

ProjectActionMenu::~ProjectActionMenu() = default;

void ProjectActionMenu::Run(views::View* anchor, const ui::Event& event) {
  if (runner_ && runner_->IsRunning())
    return;
  runner_ = std::make_unique<views::MenuRunner>(this, 0);
  runner_->RunMenuAt(anchor->GetWidget(), nullptr, anchor->GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::GetMenuSourceTypeForEvent(event));
}

bool ProjectActionMenu::IsCommandIdChecked(int id) const {
  return false;
}

bool ProjectActionMenu::IsCommandIdEnabled(int id) const {
  return true;
}

void ProjectActionMenu::ExecuteCommand(int id, int flags) {
  // 메뉴를 닫은 뒤 대화상자를 열어 anchor 재생성과 수명이 겹치지 않게 한다.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, id == 1 ? rename_ : remove_);
}

}  // namespace yee::projects
