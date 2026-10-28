// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_hub_view.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/functional/bind.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/yee/projects/project_controls.h"
#include "chrome/browser/ui/views/yee/projects/project_row_view.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"

namespace yee::projects {
namespace {

views::Label* AddLabel(views::View* parent,
                       const std::u16string& text,
                       int context,
                       ui::ColorId color) {
  auto* label =
      parent->AddChildView(std::make_unique<views::Label>(text, context));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(color);
  return label;
}

}  // namespace

ProjectHubView::ProjectHubView(Profile* profile,
                               const std::u16string& name,
                               const std::u16string& description,
                               const std::vector<HubPage>& pages,
                               bool writable,
                               bool can_add,
                               bool restoring,
                               HubActions actions) {
  // 웹 콘텐츠 호스트와 버튼의 합성 레이어 위에 본문 전체를 그린다.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetBackground(views::CreateSolidBackground(ui::kColorSysBase));
  GetViewAccessibility().SetRole(ax::mojom::Role::kRegion);
  GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_YEE_PROJECT_OVERVIEW));
  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->ClipHeightTo(0, std::numeric_limits<int>::max());
  scroll_->SetBackgroundColor(ui::kColorSysBase);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto column = std::make_unique<views::View>();
  column->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(32, 4, 24, 4),
      8));
  auto* body = scroll_->SetContents(std::move(column));

  auto* title = AddLabel(body, name, views::style::CONTEXT_DIALOG_TITLE,
                         ui::kColorSysOnSurface);
  title->SetFontList(title->font_list().DeriveWithSizeDelta(4));
  title->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
  title->GetViewAccessibility().SetHierarchicalLevel(1);
  if (!description.empty()) {
    auto* detail = AddLabel(body, description, views::style::CONTEXT_LABEL,
                            ui::kColorSysOnSurfaceSubtle);
    detail->SetMultiLine(true);
  }

  actions_row_ = body->AddChildView(std::make_unique<views::View>());
  actions_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 0), 2));
  auto add = CreateProjectTextButton(
      actions.add_current,
      l10n_util::GetStringUTF16(IDS_YEE_PROJECT_ADD_CURRENT));
  add->SetEnabledTextColors(ui::kColorSysPrimary);
  add->SetEnabled(writable && can_add);
  add->SetImageModel(views::Button::STATE_NORMAL,
                     ui::ImageModel::FromVectorIcon(vector_icons::kAdd2Icon,
                                                    ui::kColorSysPrimary, 16));
  add_ = actions_row_->AddChildView(std::move(add));
  auto more = CreateProjectIconButton(
      base::BindRepeating(&ProjectHubView::ShowMoreMenu,
                          base::Unretained(this)),
      kMoreVertIcon, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_MORE));
  more->SetEnabled(writable);
  more_ = actions_row_->AddChildView(std::move(more));
  menu_ = std::make_unique<ProjectActionMenu>(actions.rename,
                                              actions.delete_project);

  if (!writable) {
    auto* warning =
        AddLabel(body, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_READ_ONLY),
                 views::style::CONTEXT_LABEL, ui::kColorSysOnSurfaceSubtle);
    warning->SetMultiLine(true);
  }
  AddLabel(body, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_PAGES),
           views::style::CONTEXT_LABEL, ui::kColorSysOnSurfaceSubtle);
  body->AddChildView(std::make_unique<views::Separator>());
  if (pages.empty()) {
    AddLabel(body, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_EMPTY),
             views::style::CONTEXT_LABEL, ui::kColorSysOnSurface);
    auto* empty_description = AddLabel(
        body, l10n_util::GetStringUTF16(IDS_YEE_PROJECT_EMPTY_DESCRIPTION),
        views::style::CONTEXT_LABEL, ui::kColorSysOnSurfaceSubtle);
    empty_description->SetMultiLine(true);
  }
  for (const auto& page : pages) {
    auto open_page = base::BindRepeating(actions.open_page, page.id);
    auto* row = body->AddChildView(
        std::make_unique<ProjectRowView>(false, false, open_page, 8));
    auto open = std::make_unique<ProjectPageButton>(
        profile, page, open_page,
        page.live ? base::BindRepeating(actions.close_page, page.id)
                  : base::RepeatingClosure(),
        page.live ? base::BindRepeating(actions.show_page_menu, page.id)
                  : PageMenuCallback());
    open->SetCustomPadding(gfx::Insets::VH(6, 8));
    open->SetEnabled(!restoring);
    auto* open_button = row->AddChildView(std::move(open));
    open_buttons_[page.id] = open_button;
    row->SetPrimary(open_button);
    auto* host = AddLabel(row, page.host, views::style::CONTEXT_LABEL,
                          ui::kColorSysOnSurfaceSubtle);
    host->SetMaximumWidth(140);
    hosts_.push_back(host);
    states_.push_back(
        AddLabel(row,
                 l10n_util::GetStringUTF16(page.live ? IDS_YEE_PROJECT_LIVE
                                                     : IDS_YEE_PROJECT_SAVED),
                 views::style::CONTEXT_LABEL, ui::kColorSysOnSurfaceSubtle));
    auto unlink = CreateProjectIconButton(
        base::BindRepeating(actions.remove_page, page.id), kRemoveIcon,
        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_REMOVE));
    unlink->SetEnabled(writable);
    const std::u16string remove_label =
        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_REMOVE_TOOLTIP) + u": " +
        page.title;
    unlink->SetTooltipText(remove_label);
    unlink->GetViewAccessibility().SetName(remove_label);
    auto* remove = unlink.get();
    row->AddTrailing(std::move(unlink), true);
    remove_buttons_[page.id] = remove;
  }
}

ProjectHubView::~ProjectHubView() = default;

void ProjectHubView::FocusInitial() {
  if (add_->GetEnabled()) {
    add_->RequestFocus();
  } else if (more_->GetEnabled()) {
    more_->RequestFocus();
  } else {
    for (const auto& [id, button] : open_buttons_) {
      if (button->GetEnabled()) {
        button->RequestFocus();
        return;
      }
    }
  }
}

HubFocus ProjectHubView::GetFocusedAction() const {
  const auto* focused =
      GetFocusManager() ? GetFocusManager()->GetFocusedView() : nullptr;
  if (focused == add_)
    return {HubFocus::Action::kAdd, {}};
  if (focused == more_)
    return {HubFocus::Action::kMore, {}};
  for (const auto& [id, button] : open_buttons_) {
    if (focused == button)
      return {HubFocus::Action::kOpen, id};
  }
  for (const auto& [id, button] : remove_buttons_) {
    if (focused == button)
      return {HubFocus::Action::kRemove, id};
  }
  return {};
}

void ProjectHubView::RestoreFocus(const HubFocus& focus) {
  views::View* target = nullptr;
  switch (focus.action) {
    case HubFocus::Action::kNone:
      return;
    case HubFocus::Action::kAdd:
      target = add_;
      break;
    case HubFocus::Action::kMore:
      target = more_;
      break;
    case HubFocus::Action::kOpen:
      if (auto it = open_buttons_.find(focus.page_id);
          it != open_buttons_.end())
        target = it->second;
      break;
    case HubFocus::Action::kRemove:
      if (auto it = remove_buttons_.find(focus.page_id);
          it != remove_buttons_.end())
        target = it->second;
      break;
  }
  if (target && target->GetEnabled()) {
    if (ProjectRowView::RestoreFocusTo(target))
      return;
    if (target->IsDrawn()) {
      target->RequestFocus();
      return;
    }
  }
  FocusInitial();
}

int ProjectHubView::scroll_position() const {
  return pending_scroll_position_.value_or(scroll_->CurrentOffset().y());
}

void ProjectHubView::RestoreScrollPosition(int position) {
  pending_scroll_position_ = position;
  InvalidateLayout();
}

void ProjectHubView::Layout(PassKey) {
  const int column_width = std::min(720, std::max(0, width() - 48));
  for (const auto& host : hosts_)
    host->SetVisible(column_width >= 640);
  for (const auto& state : states_)
    state->SetVisible(column_width >= 380);
  auto* body_layout =
      static_cast<views::BoxLayout*>(scroll_->contents()->GetLayoutManager());
  auto* actions_layout =
      static_cast<views::BoxLayout*>(actions_row_->GetLayoutManager());
  int actions_width = actions_layout->inside_border_insets().width();
  for (const auto& child : actions_row_->children()) {
    actions_width += child->GetPreferredSize({}).width();
  }
  actions_width += actions_layout->between_child_spacing() *
                   (actions_row_->children().size() - 1);
  const int available_width = column_width -
                              body_layout->inside_border_insets().width() -
                              scroll_->GetScrollBarLayoutWidth();
  // 각 동작의 문구가 들어갈 수 없으면 세로로 배치해 가려지는 버튼을 없앤다.
  actions_layout->SetOrientation(
      actions_width <= available_width
          ? views::BoxLayout::Orientation::kHorizontal
          : views::BoxLayout::Orientation::kVertical);
  actions_layout->set_cross_axis_alignment(
      actions_width <= available_width
          ? views::BoxLayout::CrossAxisAlignment::kCenter
          : views::BoxLayout::CrossAxisAlignment::kStart);
  scroll_->SetBounds((width() - column_width) / 2, 0, column_width, height());
  LayoutSuperclass<views::View>(this);
  if (pending_scroll_position_) {
    // 새 viewport와 contents 높이가 정해진 뒤 유효한 범위 안에 복원한다.
    const int position = *pending_scroll_position_;
    pending_scroll_position_.reset();
    scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), position);
  }
}

void ProjectHubView::ShowMoreMenu(const ui::Event& event) {
  menu_->Run(more_, event);
}

void ProjectHubView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SetBackground(views::CreateSolidBackground(ui::kColorSysBase));
}

BEGIN_METADATA(ProjectHubView)
END_METADATA

}  // namespace yee::projects
