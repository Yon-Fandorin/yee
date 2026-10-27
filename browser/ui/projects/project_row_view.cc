// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_row_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/browser/ui/tabs/tab_style.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/event.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee::projects {

ProjectRowButton::ProjectRowButton(PressedCallback callback,
                                   const std::u16string& title)
    : MdTextButton(std::move(callback), title, views::style::CONTEXT_LABEL) {
  SetBackground(nullptr);
}

ProjectRowButton::~ProjectRowButton() = default;

void ProjectRowButton::UpdateBackgroundColor() {
  SetBackground(nullptr);
}

ProjectRowView::ProjectRowView(bool sidebar,
                               bool selected,
                               base::RepeatingClosure open,
                               int spacing)
    : sidebar_(sidebar),
      selected_(selected),
      open_(std::move(open)),
      disclosure_arrow_(base::BindRepeating(&ProjectRowView::UpdateRowState,
                                            base::Unretained(this))) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), spacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetNotifyEnterExitOnChild(true);
  views::InstallRoundRectHighlightPathGenerator(
      this, gfx::Insets(), kSidebarMetrics.tab_row_corner_radius);
  views::FocusRing::Install(this);
  views::FocusRing::Get(this)->SetHasFocusPredicate(
      base::BindRepeating([](const views::View* view) {
        const auto* row = static_cast<const ProjectRowView*>(view);
        return row->primary_ && row->primary_->HasFocus();
      }));
}

ProjectRowView::~ProjectRowView() {
  primary_state_ = {};
  trailing_actions_.clear();
  leading_state_ = {};
  paint_active_subscription_ = {};
  if (focus_manager_)
    focus_manager_->RemoveFocusChangeListener(this);
}

void ProjectRowView::SetActivationAction(base::RepeatingClosure action) {
  open_ = std::move(action);
}

void ProjectRowView::SetMiddleClickAction(base::RepeatingClosure action) {
  middle_click_ = std::move(action);
}

void ProjectRowView::SetPrimary(views::MdTextButton* button) {
  primary_ = button;
  button->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
  views::FocusRing::Remove(button);
  static_cast<views::BoxLayout*>(GetLayoutManager())->SetFlexForView(button, 1);
  primary_state_ = button->AddStateChangedCallback(base::BindRepeating(
      &ProjectRowView::UpdateRowState, base::Unretained(this)));
  UpdateRowState();
}

void ProjectRowView::AddTrailing(views::LabelButton* button,
                                 bool reveal_on_hover) {
  // 실제 조작만 숨기고 슬롯은 남겨 hover 진입 때 제목이 움직이지 않게 한다.
  auto owned = RemoveChildViewT(button);
  auto* slot = AddChildView(std::make_unique<views::View>());
  slot->SetPreferredSize(gfx::Size(kSidebarMetrics.tab_row_height,
                                   kSidebarMetrics.tab_row_height));
  auto* layout = slot->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  slot->AddChildView(std::move(owned));
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  trailing_actions_.push_back(
      {button, reveal_on_hover,
       button->AddStateChangedCallback(base::BindRepeating(
           &ProjectRowView::UpdateRowState, base::Unretained(this)))});
  UpdateRowState();
}

void ProjectRowView::SetLeadingDisclosure(views::MdTextButton* button,
                                          ui::ImageModel resting_icon,
                                          bool expanded) {
  leading_ = button;
  resting_icon_ = std::move(resting_icon);
  disclosure_arrow_.SetExpanded(expanded, false);
  const int width = kSidebarMetrics.tab_content_horizontal_padding +
                    kSidebarMetrics.group_mark_slot_size;
  button->SetMinSize(gfx::Size(width, kSidebarMetrics.tab_row_height));
  button->SetMaxSize(gfx::Size(width, kSidebarMetrics.tab_row_height));
  button->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  views::InkDrop::Get(button)->SetMode(views::InkDropHost::InkDropMode::OFF);
  leading_state_ = button->AddStateChangedCallback(base::BindRepeating(
      &ProjectRowView::UpdateRowState, base::Unretained(this)));
  UpdateRowState();
}

void ProjectRowView::SetDisclosureExpanded(bool expanded) {
  const bool focused =
      GetFocusManager() && Contains(GetFocusManager()->GetFocusedView());
  disclosure_arrow_.SetExpanded(expanded, IsDrawn() && (hovered_ || focused));
  UpdateRowState();
}

bool ProjectRowView::RestoreFocusTo(views::View* target) {
  if (!target || !target->GetEnabled())
    return false;
  for (auto* parent = target->parent(); parent; parent = parent->parent()) {
    auto* row = views::AsViewClass<ProjectRowView>(parent);
    if (!row)
      continue;
    if (!row->IsDrawn())
      return false;
    for (const auto& action : row->trailing_actions_) {
      if (target == action.button)
        target->SetVisible(true);
    }
    target->RequestFocus();
    return target->HasFocus();
  }
  return false;
}

void ProjectRowView::UpdateRowState() {
  if (!primary_ || !GetColorProvider())
    return;
  const bool enabled = primary_->GetEnabled();
  const bool focused =
      GetFocusManager() && Contains(GetFocusManager()->GetFocusedView());
  // Exit 통지 중에는 RootView의 hover 대상이 아직 이전 행일 수 있다.
  const bool hovered = hovered_;
  const bool pressed =
      pressed_ || primary_->GetState() == views::Button::STATE_PRESSED ||
      (leading_ && leading_->GetState() == views::Button::STATE_PRESSED) ||
      std::ranges::any_of(trailing_actions_, [](const auto& action) {
        return action.button->GetState() == views::Button::STATE_PRESSED;
      });
  auto state = SidebarItemVisualState::kResting;
  if (enabled && (selected_ || pressed))
    state = SidebarItemVisualState::kActive;
  else if (enabled && hovered)
    state = SidebarItemVisualState::kHovered;
  const auto& colors = *GetColorProvider();
  const auto native = ResolveSidebarItemColors(
      colors, state, 1, !GetWidget() || GetWidget()->ShouldPaintAsActive(),
      false);
  SkColor fill = native.fill;
  SkColor foreground = native.foreground;
  if (!sidebar_) {
    const auto surface = ResolveBrowserSurfacePresentation(
        colors.GetColor(ui::kColorSysBase), 0, 0, 0);
    fill = state == SidebarItemVisualState::kActive    ? surface.popup_selected
           : state == SidebarItemVisualState::kHovered ? surface.popup_hover
                                                       : SK_ColorTRANSPARENT;
    foreground = surface.primary;
  }
  if (!enabled)
    fill = SK_ColorTRANSPARENT;
  SetBackground(views::CreateRoundedRectBackground(
      fill, kSidebarMetrics.tab_row_corner_radius));
  primary_->SetEnabledTextColors(foreground);
  if (leading_) {
    leading_->SetEnabledTextColors(foreground);
    leading_->SetImageModel(views::Button::STATE_NORMAL,
                            hovered || focused
                                ? ui::ImageModel::FromImageSkia(
                                      disclosure_arrow_.GetImage(foreground))
                                : resting_icon_);
  }
  for (const auto& action : trailing_actions_) {
    if (auto* close = views::AsViewClass<TabCloseButton>(action.button)) {
      auto tab_colors = TabStyle::Get()->CalculateTargetColors(
          selected_ ? TabStyle::TabSelectionState::kActive
                    : TabStyle::TabSelectionState::kInactive,
          selected_, hovered,
          !GetWidget() || GetWidget()->ShouldPaintAsActive(),
          GetColorProvider());
      tab_colors.foreground_color = foreground;
      close->SetColors(tab_colors);
    } else {
      action.button->SetEnabledTextColors(foreground);
    }
    action.button->SetVisible(!action.reveal_on_hover ||
                              (enabled && (hovered || selected_ || focused)));
  }
  views::FocusRing::Get(this)->Refresh();
  SchedulePaint();
}

void ProjectRowView::AddedToWidget() {
  views::View::AddedToWidget();
  focus_manager_ = GetFocusManager();
  focus_manager_->AddFocusChangeListener(this);
  widget_observation_.Observe(GetWidget());
  paint_active_subscription_ =
      GetWidget()->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
          &ProjectRowView::UpdateRowState, base::Unretained(this)));
  UpdateRowState();
}

void ProjectRowView::RemovedFromWidget() {
  hovered_ = false;
  pressed_ = false;
  middle_pressed_ = false;
  if (focus_manager_)
    focus_manager_->RemoveFocusChangeListener(this);
  focus_manager_ = nullptr;
  paint_active_subscription_ = {};
  widget_observation_.Reset();
  views::View::RemovedFromWidget();
}

void ProjectRowView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateRowState();
}

void ProjectRowView::VisibilityChanged(views::View* starting_from,
                                       bool visible) {
  views::View::VisibilityChanged(starting_from, visible);
  if (!visible) {
    // Widget 숨김은 exit 없이 mouse handler를 폐기할 수 있다.
    hovered_ = false;
    pressed_ = false;
    middle_pressed_ = false;
    UpdateRowState();
  }
}

void ProjectRowView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  UpdateRowState();
}

void ProjectRowView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  UpdateRowState();
}

bool ProjectRowView::OnMousePressed(const ui::MouseEvent& event) {
  pressed_ = primary_ && primary_->GetEnabled() &&
             event.IsOnlyLeftMouseButton() && !open_.is_null();
  middle_pressed_ = primary_ && primary_->GetEnabled() &&
                    event.IsOnlyMiddleMouseButton() && !middle_click_.is_null();
  UpdateRowState();
  return pressed_ || middle_pressed_;
}

void ProjectRowView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool activate = pressed_ && primary_ && primary_->GetEnabled() &&
                        event.IsLeftMouseButton() &&
                        HitTestPoint(event.location());
  const bool close = middle_pressed_ && primary_ && primary_->GetEnabled() &&
                     event.IsMiddleMouseButton() &&
                     HitTestPoint(event.location());
  pressed_ = false;
  middle_pressed_ = false;
  UpdateRowState();
  // 콜백이 이 행을 포함한 허브를 동기적으로 없앨 수 있다.
  auto open = open_;
  auto middle = middle_click_;
  if (activate)
    open.Run();
  else if (close)
    middle.Run();
}

void ProjectRowView::OnMouseCaptureLost() {
  pressed_ = false;
  middle_pressed_ = false;
  UpdateRowState();
}

void ProjectRowView::OnWillChangeFocus(views::View*, views::View*) {}

void ProjectRowView::OnDidChangeFocus(views::View*, views::View*) {
  UpdateRowState();
}

void ProjectRowView::OnWidgetDestroying(views::Widget*) {
  if (focus_manager_)
    focus_manager_->RemoveFocusChangeListener(this);
  focus_manager_ = nullptr;
  paint_active_subscription_ = {};
  widget_observation_.Reset();
}

BEGIN_METADATA(ProjectRowButton)
END_METADATA

BEGIN_METADATA(ProjectRowView)
END_METADATA

}  // namespace yee::projects
