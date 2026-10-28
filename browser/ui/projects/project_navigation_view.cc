// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_navigation_view.h"

#include <limits>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/browser/ui/views/yee/projects/project_controls.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_presentation.h"
#include "chrome/browser/ui/views/yee/projects/project_row_view.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_utils.h"

namespace yee::projects {

namespace {
std::unique_ptr<views::MdTextButton> SidebarButton(
    const std::u16string& title,
    views::Button::PressedCallback action) {
  auto button = std::make_unique<ProjectRowButton>(std::move(action), title);
  button->SetStyle(ui::ButtonStyle::kText);
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  button->SetEnabledTextColors(ui::kColorSysOnSurfaceSubtle);
  button->SetCornerRadius(kSidebarMetrics.tab_row_corner_radius);
  button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  button->SetCustomPadding(gfx::Insets::VH(4, 8));
  button->SetMinSize(gfx::Size(0, kSidebarMetrics.tab_row_height));
  return button;
}
}  // namespace

ProjectSidebarView::ProjectSidebarView(
    Profile* profile,
    base::RepeatingCallback<void(std::string, bool)> open_project,
    base::RepeatingCallback<void(std::string)> open_page,
    base::RepeatingCallback<void(std::string)> close_page,
    base::RepeatingCallback<void(std::string)> rename_project,
    base::RepeatingCallback<void(std::string)> delete_project,
    base::RepeatingCallback<void(std::string,
                                 views::View*,
                                 const gfx::Point&,
                                 ui::mojom::MenuSourceType)> show_menu)
    : profile_(profile),
      open_project_(std::move(open_project)),
      open_page_(std::move(open_page)),
      close_page_(std::move(close_page)),
      rename_project_(std::move(rename_project)),
      delete_project_(std::move(delete_project)),
      show_menu_(std::move(show_menu)) {
  SetLayoutManager(std::make_unique<views::FillLayout>());
  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  scroll_->ClipHeightTo(0, std::numeric_limits<int>::max());
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
}

ProjectSidebarView::~ProjectSidebarView() = default;

void ProjectSidebarView::SetGroupMemberInset(int inset) {
  group_member_inset_ = inset;
}

void ConfigureProjectSidebarPresentation(views::View& sidebar,
                                         int group_member_inset) {
  auto* project_sidebar = views::AsViewClass<ProjectSidebarView>(&sidebar);
  CHECK(project_sidebar);
  project_sidebar->SetGroupMemberInset(group_member_inset);
}

void ProjectSidebarView::SetProjects(
    const std::vector<NavigationProject>& projects,
    const std::string& selected,
    bool writable) {
  std::string focus_key;
  auto* focused =
      GetFocusManager() ? GetFocusManager()->GetFocusedView() : nullptr;
  for (const auto& [key, view] : focus_targets_) {
    if (view == focused) {
      focus_key = key;
    }
  }
  focus_targets_.clear();
  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(4, kSidebarMetrics.section_horizontal_inset),
      kSidebarMetrics.tab_row_margin));
  for (const auto& project : projects) {
    auto open_project = base::BindRepeating(open_project_, project.id);
    auto* heading = body->AddChildView(std::make_unique<ProjectRowView>(
        true, project.id == selected, base::RepeatingClosure()));
    auto leading = SidebarButton(u"", {});
    auto* arrow_ptr = heading->AddChildView(std::move(leading));
    const auto folder = ui::ImageModel::FromVectorIcon(
        vector_icons::kFolderFlippableIcon, ui::kColorSysOnSurfaceSubtle,
        kSidebarMetrics.group_mark_slot_size);
    auto* name = heading->AddChildView(SidebarButton(project.name, {}));
    name->SetCustomPadding(
        gfx::Insets::VH(4, kSidebarMetrics.group_mark_trailing_margin));
    heading->SetPrimary(name);
    name->GetViewAccessibility().SetIsSelected(project.id == selected);
    focus_targets_["project:" + project.id] = name;
    auto hub = CreateProjectIconButton(
        base::BindRepeating(
            [](base::RepeatingCallback<void(bool)> open,
               const ui::Event& event) {
              open.Run(event.IsMouseEvent() || event.IsGestureEvent());
            },
            open_project),
        kGridViewIcon,
        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_OVERVIEW) + u": " +
            project.name);
    auto* hub_button = hub.get();
    heading->AddTrailing(std::move(hub), true);
    focus_targets_["hub:" + project.id] = hub_button;
    auto more = CreateProjectIconButton(
        {}, kMoreVertIcon,
        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_MORE) + u": " + project.name);
    auto* more_button = more.get();
    more->SetEnabled(writable);
    more->SetCallback(base::BindRepeating(&ProjectSidebarView::ShowMoreMenu,
                                          base::Unretained(this), project.id,
                                          more_button));
    heading->AddTrailing(std::move(more), true);
    focus_targets_["more:" + project.id] = more_button;
    auto* page_list = body->AddChildView(std::make_unique<views::View>());
    page_list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(0, group_member_inset_, 0, 0),
        kSidebarMetrics.tab_row_margin));
    const bool expanded = expanded_.contains(project.id);
    page_list->SetVisible(expanded);
    for (const auto& page : project.pages) {
      auto open_page = base::BindRepeating(open_page_, page.id);
      auto* row = page_list->AddChildView(
          std::make_unique<ProjectRowView>(true, page.active, open_page));
      auto* button = row->AddChildView(std::make_unique<ProjectPageButton>(
          profile_, page, open_page,
          page.live ? base::BindRepeating(close_page_, page.id)
                    : base::RepeatingClosure(),
          page.live ? base::BindRepeating(show_menu_, page.id)
                    : PageMenuCallback()));
      button->SetMinSize(gfx::Size(0, kSidebarMetrics.tab_row_height));
      row->SetPrimary(button);
      if (page.live)
        row->SetMiddleClickAction(base::BindRepeating(close_page_, page.id));
      focus_targets_["page:" + page.id] = button;
      if (page.live) {
        auto close = std::make_unique<TabCloseButton>(
            base::BindRepeating(close_page_, page.id),
            base::DoNothingAs<void(views::View*, const ui::MouseEvent&)>());
        const auto label =
            l10n_util::GetStringUTF16(IDS_YEE_PROJECT_CLOSE_PAGE) + u": " +
            page.title;
        close->SetTooltipText(label);
        close->GetViewAccessibility().SetName(label);
        auto* close_button = close.get();
        row->AddTrailing(std::move(close), true);
        focus_targets_["close:" + page.id] = close_button;
      }
    }
    auto toggle =
        base::BindRepeating(&ProjectSidebarView::Toggle, base::Unretained(this),
                            project.id, page_list, arrow_ptr, name);
    arrow_ptr->SetCallback(toggle);
    name->SetCallback(toggle);
    heading->SetActivationAction(toggle);
    const auto toggle_label =
        l10n_util::GetStringUTF16(IDS_YEE_PROJECT_TOGGLE_PAGES) + u": " +
        project.name;
    arrow_ptr->GetViewAccessibility().SetName(toggle_label);
    arrow_ptr->SetTooltipText(toggle_label);
    if (expanded)
      arrow_ptr->GetViewAccessibility().SetIsExpanded();
    else
      arrow_ptr->GetViewAccessibility().SetIsCollapsed();
    if (expanded)
      name->GetViewAccessibility().SetIsExpanded();
    else
      name->GetViewAccessibility().SetIsCollapsed();
    focus_targets_["toggle:" + project.id] = arrow_ptr;
    heading->SetLeadingDisclosure(arrow_ptr, folder, expanded);
  }
  // 같은 목록의 메타데이터 갱신은 읽던 위치를 유지한다. 레이아웃 전 여러 번
  // 갱신되면 이미 보존한 위치를 새 contents의 초기값으로 덮어쓰지 않는다.
  if (!pending_scroll_position_ && scroll_->contents()) {
    pending_scroll_position_ = scroll_->CurrentOffset().y();
  }
  scroll_->SetContents(std::move(body));
  UpdatePreferredSize();
  auto found = focus_targets_.find(focus_key);
  if (found != focus_targets_.end() &&
      !ProjectRowView::RestoreFocusTo(found->second)) {
    found->second->RequestFocus();
  }
  repaint_after_layout_ = true;
  InvalidateLayout();
}

void ProjectSidebarView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  if (pending_scroll_position_) {
    const int position = *pending_scroll_position_;
    pending_scroll_position_.reset();
    scroll_->ScrollToPosition(scroll_->vertical_scroll_bar(), position);
  }
  if (repaint_after_layout_ && parent()) {
    repaint_after_layout_ = false;
    // 폴딩 뒤에는 native 형제 행도 이동한다. 새 bounds로 Sidebar 전체를
    // 갱신해 포인터 이동이나 화살표 애니메이션 없이도 글자·아이콘을 그린다.
    if (auto* body = scroll_->contents())
      body->SchedulePaint();
    parent()->SchedulePaint();
  }
}

void ProjectSidebarView::RevealPage(const std::string& project_id) {
  expanded_.insert(project_id);
}

void ProjectSidebarView::UpdatePreferredSize() {
  const auto* body = scroll_->contents();
  // 폴딩과 데이터 갱신이 실제 자식 배치의 같은 높이를 사용해야 한다.
  SetPreferredSize(
      gfx::Size(180, body->children().empty()
                         ? 0
                         : std::min(12 * (kSidebarMetrics.tab_row_height +
                                          kSidebarMetrics.tab_row_margin) +
                                        8,
                                    body->GetPreferredSize().height())));
}

void ProjectSidebarView::Toggle(std::string id,
                                views::View* pages,
                                views::MdTextButton* arrow,
                                views::MdTextButton* name) {
  const bool expanded = !expanded_.contains(id);
  if (expanded) {
    expanded_.insert(id);
  } else {
    expanded_.erase(id);
  }
  pages->SetVisible(expanded);
  static_cast<ProjectRowView*>(arrow->parent())
      ->SetDisclosureExpanded(expanded);
  if (expanded)
    arrow->GetViewAccessibility().SetIsExpanded();
  else
    arrow->GetViewAccessibility().SetIsCollapsed();
  if (expanded)
    name->GetViewAccessibility().SetIsExpanded();
  else
    name->GetViewAccessibility().SetIsCollapsed();
  // 펼치기만으로 중앙 허브/페이지나 native 탭 선택을 바꾸지 않는다.
  UpdatePreferredSize();
  repaint_after_layout_ = true;
  InvalidateLayout();
}

void ProjectSidebarView::ShowMoreMenu(std::string id,
                                      views::View* anchor,
                                      const ui::Event& event) {
  project_menu_ = std::make_unique<ProjectActionMenu>(
      base::BindRepeating(rename_project_, id),
      base::BindRepeating(delete_project_, id));
  project_menu_->Run(anchor, event);
}

BEGIN_METADATA(ProjectSidebarView)
END_METADATA

ProjectNavigationView::ProjectNavigationView(
    base::RepeatingCallback<void(bool)> open_hub,
    base::RepeatingClosure show_picker) {
  // 선호 너비는 두 버튼의 내용으로 정하고 실제 축소는 내부 flex가 처리한다.
  SetLayoutManagerUseConstrainedSpace(false);
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  name_ = AddChildView(CreateProjectTextButton(
      base::BindRepeating(
          [](base::RepeatingCallback<void(bool)> open, const ui::Event& event) {
            open.Run(event.IsMouseEvent() || event.IsGestureEvent());
          },
          std::move(open_hub)),
      l10n_util::GetStringUTF16(IDS_YEE_PROJECTS)));
  name_->SetStyle(ui::ButtonStyle::kText);
  name_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  name_->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  name_->SetEnabledTextColors(ui::kColorSysOnSurface);
  name_->SetCornerRadius(kSidebarMetrics.shell_control_corner_radius);
  name_->SetCustomPadding(gfx::Insets::VH(4, 8));
  name_->SetMaxSize(gfx::Size(144, kSidebarMetrics.shell_control_size));
  layout->SetFlexForView(name_, 1, true);
  picker_ = AddChildView(CreateProjectIconButton(
      std::move(show_picker), vector_icons::kExpandMoreOldIcon,
      l10n_util::GetStringUTF16(IDS_YEE_PROJECT_PICKER)));
  picker_->SetStyle(ui::ButtonStyle::kText);
  picker_->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  picker_->SetMinSize(gfx::Size(kSidebarMetrics.shell_control_size,
                                kSidebarMetrics.shell_control_size));
  picker_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  picker_->SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  picker_->SetCornerRadius(kSidebarMetrics.shell_control_corner_radius);
  picker_->SetCustomPadding(gfx::Insets());
  picker_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kExpandMoreOldIcon,
                                     ui::kColorSysOnSurfaceSubtle, 16));
  picker_->SetTooltipText(l10n_util::GetStringUTF16(IDS_YEE_PROJECT_PICKER));
  picker_->GetViewAccessibility().SetName(
      l10n_util::GetStringUTF16(IDS_YEE_PROJECT_PICKER));
}

ProjectNavigationView::~ProjectNavigationView() = default;

void ProjectNavigationView::FocusName() {
  name_->RequestFocus();
}

void ProjectNavigationView::SetProject(const std::u16string& name,
                                       bool available) {
  name_->SetText(name.empty() ? l10n_util::GetStringUTF16(IDS_YEE_PROJECTS)
                              : name);
  name_->SetTooltipText(name);
  name_->SetEnabled(available);
}

views::View* ProjectNavigationView::picker_anchor() const {
  return picker_;
}

void ProjectNavigationView::SetSurfacePresentation(
    const std::optional<BrowserSurfacePresentation>& presentation) {
  follows_page_surface_ = true;
  presentation_ = presentation;
  UpdateSurfaceColors();
}

void ProjectNavigationView::OnThemeChanged() {
  views::View::OnThemeChanged();
  if (follows_page_surface_)
    UpdateSurfaceColors();
}

void ProjectNavigationView::UpdateSurfaceColors() {
  const bool custom =
      presentation_ &&
      presentation_->palette_mode ==
          BrowserSurfacePresentation::PaletteMode::kCustomSurface;
  const ui::ColorVariant foreground =
      custom ? ui::ColorVariant(presentation_->primary)
             : ui::ColorVariant(kColorToolbarButtonText);
  const ui::ColorVariant disabled =
      custom ? ui::ColorVariant(presentation_->disabled)
             : ui::ColorVariant(kColorToolbarTextDisabled);
  name_->SetEnabledTextColors(foreground);
  name_->SetTextColor(views::Button::STATE_DISABLED, disabled);
  // MdTextButton은 상태 변화 때 문자색으로 아이콘을 다시 칠한다.
  picker_->SetEnabledTextColors(
      custom ? foreground : ui::ColorVariant(kColorToolbarButtonIcon));
  picker_->SetTextColor(
      views::Button::STATE_DISABLED,
      custom ? disabled : ui::ColorVariant(kColorToolbarButtonIconDisabled));
  picker_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kExpandMoreOldIcon,
          custom ? foreground : ui::ColorVariant(kColorToolbarButtonIcon), 16));
  picker_->SetImageModel(
      views::Button::STATE_DISABLED,
      ui::ImageModel::FromVectorIcon(
          vector_icons::kExpandMoreOldIcon,
          custom ? disabled : ui::ColorVariant(kColorToolbarButtonIconDisabled),
          16));
  for (auto* button : {name_.get(), picker_.get()}) {
    auto* ink_drop = views::InkDrop::Get(button);
    ink_drop->SetBaseColor(
        custom ? ui::ColorVariant(presentation_->location_hover)
               : ui::ColorVariant(ui::kColorSysStateHoverOnSubtle));
    // 이미 표시 중인 hover/ripple도 새 snapshot의 색으로 다시 만든다.
    if (ink_drop->HasInkDrop())
      ink_drop->GetInkDrop()->HostViewThemeChanged();
    SetBrowserSurfaceControlBackground(
        *button,
        custom ? std::make_optional(presentation_->surface) : std::nullopt);
    if (GetColorProvider()) {
      views::FocusRing::Get(button)->SetColorId(
          custom ? std::make_optional(ResolveBrowserSurfaceFocusColorId(
                       *GetColorProvider(), presentation_->surface,
                       ui::kColorFocusableBorderFocused))
                 : std::nullopt);
    }
  }
}

BEGIN_METADATA(ProjectNavigationView)
END_METADATA

}  // namespace yee::projects
