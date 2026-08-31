// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/sidebar_footer.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/callback_list.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/char_iterator.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_reopen_suppressor.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace yee {
namespace {

constexpr int kMenuInsets = 7;
constexpr int kMenuIconGlyphSize = 16;
constexpr int kMenuCornerRadius = 8;
constexpr int kMenuRowHorizontalInset = 6;
constexpr int kMenuRowVerticalInset = 3;

gfx::Insets FooterSurfaceInsets() {
  return gfx::Insets::VH(
      kSidebarMetrics.sidebar_footer_surface_vertical_inset(),
      kSidebarMetrics.sidebar_footer_surface_horizontal_inset());
}

std::unique_ptr<views::Border> CreateFooterSurfaceBorder(bool paint_outline,
                                                         SkColor outline) {
  if (!paint_outline) {
    return views::CreateEmptyBorder(FooterSurfaceInsets());
  }
  return views::CreatePaddedBorder(
      views::CreateRoundedRectBorder(
          kSidebarMetrics.sidebar_footer_surface_outline_width,
          kSidebarMetrics.sidebar_footer_corner_radius, outline),
      gfx::Insets::VH(
          kSidebarMetrics.sidebar_footer_surface_padding_vertical,
          kSidebarMetrics.sidebar_footer_surface_padding_horizontal));
}

bool HasKeyboardTraversalFocus(const views::View& view) {
  const views::FocusManager* const focus_manager = view.GetFocusManager();
  return view.HasFocus() && focus_manager &&
         focus_manager->focus_change_reason() ==
             views::FocusManager::FocusChangeReason::kFocusTraversal;
}

enum class MenuIconTone {
  kNeutral,
  kAccent,
};

SidebarFooterModel NormalizeModel(SidebarFooterModel model) {
  if (model.contexts.empty()) {
    model = CreateLocalSidebarFooterModel({});
  }
  model.selected_context_index =
      std::min(model.selected_context_index, model.contexts.size() - 1);
  return model;
}

const SidebarContextItem& SelectedContext(const SidebarFooterModel& model) {
  CHECK(!model.contexts.empty());
  CHECK_LT(model.selected_context_index, model.contexts.size());
  return model.contexts[model.selected_context_index];
}

std::u16string FirstCodePointMark(std::u16string_view value) {
  if (value.empty()) {
    return u"•";
  }
  base::i18n::UTF16CharIterator iterator(value);
  iterator.Advance();
  std::u16string result(value.substr(0, iterator.array_pos()));
  result[0] = base::ToUpperASCII(result[0]);
  return result;
}

std::u16string ContextSubtitle(const SidebarContextItem& context) {
  return context.tenant_name + u" · " + context.account_name;
}

std::unique_ptr<views::Label> CreateMarkLabel(std::u16string text, int size) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetID(kSidebarFooterWorkspaceMarkViewId);
  label->SetPreferredSize(gfx::Size(size, size));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label->SetTextStyle(views::style::STYLE_BODY_5_BOLD);
  label->SetEnabledColor(ui::kColorSysOnTonalContainer);
  label->SetSkipSubpixelRenderingOpacityCheck(true);
  label->SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysTonalContainer, std::min(kMenuCornerRadius, size / 2)));
  label->SetCanProcessEventsWithinSubtree(false);
  return label;
}

std::unique_ptr<views::ImageView> CreateMenuIcon(
    const gfx::VectorIcon& icon,
    MenuIconTone tone = MenuIconTone::kNeutral) {
  auto icon_view = std::make_unique<views::ImageView>();
  icon_view->SetID(kSidebarFooterMenuActionIconViewId);
  icon_view->SetPreferredSize(
      gfx::Size(kSidebarMetrics.sidebar_footer_icon_size,
                kSidebarMetrics.sidebar_footer_icon_size));
  const ui::ColorId icon_color = tone == MenuIconTone::kAccent
                                     ? ui::kColorSysOnTonalContainer
                                     : ui::kColorIconSecondary;
  icon_view->SetImage(
      ui::ImageModel::FromVectorIcon(icon, icon_color, kMenuIconGlyphSize));
  if (tone == MenuIconTone::kAccent) {
    icon_view->SetBackground(views::CreateRoundedRectBackground(
        ui::kColorSysTonalContainer, kMenuCornerRadius));
  }
  icon_view->SetCanProcessEventsWithinSubtree(false);
  return icon_view;
}

std::unique_ptr<views::Label> CreateTrailingLabel(std::u16string text,
                                                  bool emphasized = false) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetEnabledColor(emphasized ? ui::kColorSysOnTonalContainer
                                    : ui::kColorLabelForegroundSecondary);
  label->SetTextStyle(views::style::STYLE_BODY_5);
  label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label->SetSkipSubpixelRenderingOpacityCheck(true);
  label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(1, 5)));
  label->SetBackground(views::CreateRoundedRectBackground(
      emphasized ? ui::kColorSysTonalContainer : ui::kColorSysNeutralContainer,
      8));
  label->SetCanProcessEventsWithinSubtree(false);
  return label;
}

std::unique_ptr<views::ImageView> CreateTrailingIcon(
    const gfx::VectorIcon& icon,
    ui::ColorId color = ui::kColorIconSecondary) {
  auto icon_view = std::make_unique<views::ImageView>();
  icon_view->SetPreferredSize(gfx::Size(16, 16));
  icon_view->SetImage(ui::ImageModel::FromVectorIcon(icon, color, 16));
  icon_view->SetCanProcessEventsWithinSubtree(false);
  return icon_view;
}

std::unique_ptr<views::View> CreateTrailingStack(
    std::unique_ptr<views::View> status,
    bool show_disclosure) {
  auto trailing = std::make_unique<views::View>();
  auto* layout = trailing->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  if (status) {
    trailing->AddChildView(std::move(status));
  }
  if (show_disclosure) {
    trailing->AddChildView(
        CreateTrailingIcon(vector_icons::kKeyboardArrowRightFlippableIcon));
  }
  trailing->SetCanProcessEventsWithinSubtree(false);
  return trailing;
}

std::unique_ptr<views::View> CreateDisclosure() {
  return CreateTrailingStack(nullptr, /*show_disclosure=*/true);
}

std::unique_ptr<views::ImageView> CreateFooterChevron() {
  auto chevron = std::make_unique<views::ImageView>();
  const int size = kSidebarMetrics.sidebar_footer_disclosure_size;
  chevron->SetPreferredSize(gfx::Size(size, size));
  chevron->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kKeyboardArrowUpIcon, ui::kColorIconSecondary, size));
  chevron->SetID(kSidebarFooterChevronViewId);
  chevron->SetCanProcessEventsWithinSubtree(false);
  return chevron;
}

class SidebarFooterMenuRow : public HoverButton {
 public:
  SidebarFooterMenuRow(PressedCallback callback, Params params, bool selected)
      : HoverButton(std::move(callback), std::move(params)),
        selected_(selected) {
    SetFocusRingCornerRadius(kMenuCornerRadius);
    title()->SetSkipSubpixelRenderingOpacityCheck(true);
    title()->SetTextStyle(views::style::STYLE_BODY_4_EMPHASIS);
    if (subtitle()) {
      subtitle()->SetSkipSubpixelRenderingOpacityCheck(true);
      subtitle()->SetTextStyle(views::style::STYLE_BODY_5);
    }
    GetViewAccessibility().SetIsSelected(selected_);
    UpdateSurface();
  }
  SidebarFooterMenuRow(const SidebarFooterMenuRow&) = delete;
  SidebarFooterMenuRow& operator=(const SidebarFooterMenuRow&) = delete;
  ~SidebarFooterMenuRow() override = default;

 protected:
  void OnThemeChanged() override {
    HoverButton::OnThemeChanged();
    UpdateSurface();
  }

 private:
  void UpdateSurface() {
    if (selected_) {
      SetBackground(views::CreateRoundedRectBackground(
          ui::kColorSysNeutralContainer, kMenuCornerRadius));
    } else {
      SetBackground(nullptr);
    }
  }

  const bool selected_;
};

class FooterBubbleDelegate final : public views::BubbleDialogDelegate {
 public:
  explicit FooterBubbleDelegate(views::View* anchor_view)
      : BubbleDialogDelegate(views::BubbleAnchor(anchor_view),
                             views::BubbleBorder::BOTTOM_LEFT) {}

  gfx::Rect GetAnchorRect() const override {
    gfx::Rect anchor_rect = BubbleDialogDelegate::GetAnchorRect();
    anchor_rect.Offset(0, -kSidebarMetrics.sidebar_footer_menu_gap);
    return anchor_rect;
  }
};

// Owns both halves of a BubbleDialogDelegate lifetime. The Widget must be
// destroyed before its delegate; keeping that invariant in one type avoids the
// detached-delegate state where an anchor theme notification can observe a
// null bubble Widget.
class FooterBubble final : public views::WidgetObserver {
 public:
  FooterBubble(std::unique_ptr<FooterBubbleDelegate> delegate,
               views::Widget::ClosedCallback on_close,
               base::RepeatingClosure on_native_destroying)
      : delegate_(std::move(delegate)),
        widget_(views::BubbleDialogDelegate::CreateBubble(delegate_.get(),
                                                          std::move(on_close))),
        on_native_destroying_(std::move(on_native_destroying)) {
    CHECK(widget_);
    widget_observation_.Observe(widget_.get());
  }

  FooterBubble(const FooterBubble&) = delete;
  FooterBubble& operator=(const FooterBubble&) = delete;

  ~FooterBubble() override {
    // Stop copying theme changes from the browser before Widget destruction
    // clears WidgetDelegate::GetWidget().
    delegate_->SetAnchorView(nullptr);
    widget_observation_.Reset();
    widget_.reset();
    delegate_.reset();
  }

  views::Widget* widget() { return widget_.get(); }

  void OnWidgetDestroying(views::Widget* widget) override {
    // Native teardown can bypass Widget::Close() (for example CloseNow() or
    // parent-window destruction). Detach immediately while the delegate still
    // has a Widget so a later browser ThemeChanged cannot reach a zombie
    // bubble.
    delegate_->SetAnchorView(nullptr);
    widget_observation_.Reset();
    if (on_native_destroying_) {
      on_native_destroying_.Run();
    }
  }

 private:
  // Declaration order is intentional: normal member destruction also deletes
  // the Widget before the delegate.
  std::unique_ptr<FooterBubbleDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::RepeatingClosure on_native_destroying_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

HoverButton::Params CreateFooterTriggerParams(const SidebarFooterModel& model) {
  const SidebarContextItem& context = SelectedContext(model);
  HoverButton::Params params;
  params.icon_view = CreateMarkLabel(FirstCodePointMark(context.workspace_name),
                                     kSidebarMetrics.sidebar_footer_icon_size);
  params.title = context.workspace_name;
  params.subtitle = ContextSubtitle(context);
  params.add_vertical_label_spacing = false;
  params.icon_label_spacing = kSidebarMetrics.sidebar_footer_icon_label_spacing;
  params.secondary_view = CreateFooterChevron();
  return params;
}

class YeeSidebarFooterMenuView : public views::View,
                                 public gfx::AnimationDelegate {
 public:
  YeeSidebarFooterMenuView(
      views::BubbleDialogDelegate* bubble_delegate,
      int anchor_width,
      SidebarFooterModel model,
      SidebarContextSelectedCallback select_context_callback,
      SidebarFooterBrowserActionCallback browser_action_callback,
      SidebarMemoryChangedCallback memory_changed_callback)
      : bubble_delegate_(bubble_delegate),
        anchor_width_(anchor_width),
        model_(NormalizeModel(std::move(model))),
        select_context_callback_(std::move(select_context_callback)),
        browser_action_callback_(std::move(browser_action_callback)),
        memory_changed_callback_(std::move(memory_changed_callback)),
        screen_animation_(this) {
    CHECK(bubble_delegate_);
    SetID(kSidebarFooterMenuViewId);
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(kMenuInsets), 2));
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetScreenContents(SidebarFooterScreen::kRoot, /*move_focus=*/false);
    bubble_delegate_->SetInitiallyFocusedView(first_actionable_);
  }

  YeeSidebarFooterMenuView(const YeeSidebarFooterMenuView&) = delete;
  YeeSidebarFooterMenuView& operator=(const YeeSidebarFooterMenuView&) = delete;
  ~YeeSidebarFooterMenuView() override { screen_animation_.Stop(); }

  void AddedToWidget() override {
    views::View::AddedToWidget();
    if (!gfx::Animation::ShouldRenderRichAnimation() ||
        gfx::Animation::PrefersReducedMotion()) {
      layer()->SetOpacity(1.0f);
      return;
    }
    transitioning_out_ = false;
    screen_animation_.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    screen_animation_.SetSlideDuration(base::Milliseconds(
        kSidebarMetrics.sidebar_footer_screen_fade_in_duration_ms));
    screen_animation_.Reset(0.0);
    layer()->SetOpacity(0.0f);
    screen_animation_.Show();
  }

  void SetAnchorWidth(int anchor_width) {
    anchor_width_ = std::max(0, anchor_width);
    for (views::View* child : children()) {
      gfx::Size preferred_size = child->GetPreferredSize();
      preferred_size.set_width(ContentWidth());
      child->SetPreferredSize(preferred_size);
    }
    InvalidateLayout();
    PreferredSizeChanged();
    if (GetWidget()) {
      bubble_delegate_->SizeToContents();
    }
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_ESCAPE &&
        screen_ != SidebarFooterScreen::kRoot) {
      ShowScreen(ParentSidebarFooterScreen(screen_), /*move_focus=*/true);
      return true;
    }
    return views::View::OnKeyPressed(event);
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    CHECK_EQ(animation, &screen_animation_);
    layer()->SetOpacity(
        static_cast<float>(screen_animation_.GetCurrentValue()));
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    CHECK_EQ(animation, &screen_animation_);
    AnimationProgressed(animation);
    if (!transitioning_out_) {
      return;
    }
    transitioning_out_ = false;
    SetScreenContents(pending_screen_, pending_move_focus_);
    layer()->SetOpacity(0.0f);
    screen_animation_.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    screen_animation_.SetSlideDuration(base::Milliseconds(
        kSidebarMetrics.sidebar_footer_screen_fade_in_duration_ms));
    screen_animation_.Reset(0.0);
    screen_animation_.Show();
  }

 private:
  int ContentWidth() const {
    return std::max(0, anchor_width_ - 2 * kMenuInsets);
  }

  HoverButton* AddRow(
      std::u16string title,
      std::u16string subtitle,
      std::unique_ptr<views::View> icon,
      std::unique_ptr<views::View> trailing,
      views::Button::PressedCallback callback,
      int view_id = 0,
      int preferred_height = kSidebarMetrics.sidebar_footer_menu_row_height,
      bool enabled = true,
      bool selected = false) {
    HoverButton::Params params;
    params.icon_view = std::move(icon);
    params.title = std::move(title);
    params.subtitle = std::move(subtitle);
    params.add_vertical_label_spacing = false;
    params.icon_label_spacing = 8;
    params.secondary_view = std::move(trailing);

    std::unique_ptr<HoverButton> row = std::make_unique<SidebarFooterMenuRow>(
        std::move(callback), std::move(params), selected);
    row->SetPreferredSize(gfx::Size(ContentWidth(), preferred_height));
    row->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(kMenuRowVerticalInset, kMenuRowHorizontalInset)));
    if (view_id) {
      row->SetID(view_id);
    }
    row->SetEnabled(enabled);
    HoverButton* const result = AddChildView(std::move(row));
    if (!first_actionable_ && enabled) {
      first_actionable_ = result;
    }
    return result;
  }

  void AddSectionLabel(std::u16string text) {
    auto label = std::make_unique<views::Label>(std::move(text));
    label->SetPreferredSize(gfx::Size(ContentWidth(), 22));
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 7, 2, 7)));
    label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    label->SetTextStyle(views::style::STYLE_BODY_5_BOLD);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetSkipSubpixelRenderingOpacityCheck(true);
    AddChildView(std::move(label));
  }

  void AddHeader(std::u16string eyebrow,
                 std::u16string title,
                 SidebarFooterScreen destination) {
    AddRow(std::move(title), std::move(eyebrow),
           CreateMenuIcon(vector_icons::kArrowBackIcon), nullptr,
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this), destination,
                               /*move_focus=*/true),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_root_row_height);
  }

  void AddRoot() {
    const SidebarContextItem& context = SelectedContext(model_);
    AddRow(context.workspace_name, ContextSubtitle(context),
           CreateMarkLabel(FirstCodePointMark(context.workspace_name),
                           kSidebarMetrics.sidebar_footer_icon_size),
           CreateDisclosure(),
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this),
                               SidebarFooterScreen::kContext,
                               /*move_focus=*/true),
           kSidebarFooterRootContextViewId,
           kSidebarMetrics.sidebar_footer_root_row_height);
    AddRow(u"Browser tools", u"Recent tabs, downloads, and more",
           CreateMenuIcon(vector_icons::kHistoryIcon), CreateDisclosure(),
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this),
                               SidebarFooterScreen::kBrowserTools,
                               /*move_focus=*/true),
           kSidebarFooterRootBrowserToolsViewId,
           kSidebarMetrics.sidebar_footer_root_row_height);
    AddRow(u"Yee & memory", u"Workspace-specific memory",
           CreateMenuIcon(vector_icons::kLightbulbIcon),
           CreateTrailingStack(
               CreateTrailingLabel(
                   model_.memory_available
                       ? (model_.memory_enabled ? u"On" : u"Paused")
                       : u"Not connected",
                   /*emphasized=*/model_.memory_available &&
                       model_.memory_enabled),
               /*show_disclosure=*/true),
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this),
                               SidebarFooterScreen::kMemory,
                               /*move_focus=*/true),
           kSidebarFooterRootMemoryViewId,
           kSidebarMetrics.sidebar_footer_root_row_height);
  }

  void AddContext() {
    AddHeader(u"Account & contexts", u"Switch context",
              SidebarFooterScreen::kRoot);
    AddSectionLabel(u"Available contexts");
    for (size_t index = 0; index < model_.contexts.size(); ++index) {
      const SidebarContextItem& choice = model_.contexts[index];
      AddRow(choice.workspace_name,
             choice.tenant_name + u" · " + choice.account_name,
             CreateMarkLabel(FirstCodePointMark(choice.workspace_name),
                             kSidebarMetrics.sidebar_footer_icon_size),
             index == model_.selected_context_index
                 ? CreateTrailingIcon(vector_icons::kCheckCircleFilledIcon,
                                      ui::kColorIconSecondary)
                 : nullptr,
             base::BindRepeating(&YeeSidebarFooterMenuView::SelectContext,
                                 base::Unretained(this), index),
             /*view_id=*/0, kSidebarMetrics.sidebar_footer_root_row_height,
             /*enabled=*/true,
             /*selected=*/index == model_.selected_context_index);
    }
  }

  void AddBrowserTools() {
    AddHeader(u"Browser", u"Browser tools", SidebarFooterScreen::kRoot);
    AddRow(u"Reopen closed tab", u"Restore the most recently closed tab",
           CreateMenuIcon(vector_icons::kUndoIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kReopenClosedTab),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(u"Downloads", u"Open downloaded files and activity",
           CreateMenuIcon(vector_icons::kDownloadIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kDownloads),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(u"History", u"Review browsing history",
           CreateMenuIcon(vector_icons::kHistoryIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kHistory),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(u"Tabs from other devices", u"Continue from a synced device",
           CreateMenuIcon(vector_icons::kDevicesIcon), nullptr,
           BrowserActionCallback(
               SidebarFooterBrowserAction::kTabsFromOtherDevices),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(u"Manage extensions", u"Review installed browser extensions",
           CreateMenuIcon(vector_icons::kChromeExtensionIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kManageExtensions),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
  }

  void AddMemory() {
    AddHeader(u"Yee", u"Memory", SidebarFooterScreen::kRoot);
    const SidebarContextItem& context = SelectedContext(model_);
    AddSectionLabel(context.workspace_name + u" workspace");
    if (model_.memory_available) {
      AddRow(u"Workspace memory", u"Memory stays inside the selected context",
             nullptr,
             CreateTrailingLabel(model_.memory_enabled ? u"On" : u"Paused",
                                 /*emphasized=*/model_.memory_enabled),
             base::BindRepeating(&YeeSidebarFooterMenuView::ToggleMemory,
                                 base::Unretained(this)),
             /*view_id=*/0, kSidebarMetrics.sidebar_footer_root_row_height);
    } else {
      AddRow(u"Workspace memory", u"Connect a workspace provider to use memory",
             nullptr, CreateTrailingLabel(u"Unavailable"),
             views::Button::PressedCallback(),
             /*view_id=*/0, kSidebarMetrics.sidebar_footer_root_row_height,
             /*enabled=*/false);
    }
  }

  void ShowScreen(SidebarFooterScreen screen, bool move_focus) {
    const bool rich_animation = GetWidget() && move_focus &&
                                gfx::Animation::ShouldRenderRichAnimation() &&
                                !gfx::Animation::PrefersReducedMotion();
    if (!rich_animation) {
      SetScreenContents(screen, move_focus);
      return;
    }

    if (screen_animation_.is_animating()) {
      screen_animation_.Stop();
      transitioning_out_ = false;
      layer()->SetOpacity(1.0f);
    }
    pending_screen_ = screen;
    pending_move_focus_ = move_focus;
    transitioning_out_ = true;
    screen_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
    screen_animation_.SetSlideDuration(base::Milliseconds(
        kSidebarMetrics.sidebar_footer_screen_fade_out_duration_ms));
    screen_animation_.Reset(1.0);
    screen_animation_.Hide();
  }

  void SetScreenContents(SidebarFooterScreen screen, bool move_focus) {
    screen_ = screen;
    first_actionable_ = nullptr;
    RemoveAllChildViews();
    switch (screen_) {
      case SidebarFooterScreen::kRoot:
        AddRoot();
        break;
      case SidebarFooterScreen::kContext:
        AddContext();
        break;
      case SidebarFooterScreen::kBrowserTools:
        AddBrowserTools();
        break;
      case SidebarFooterScreen::kMemory:
        AddMemory();
        break;
    }
    InvalidateLayout();
    PreferredSizeChanged();
    if (GetWidget()) {
      bubble_delegate_->SizeToContents();
      if (move_focus && first_actionable_) {
        first_actionable_->RequestFocus();
      }
    }
  }

  void SelectContext(size_t context_index) {
    CHECK_LT(context_index, model_.contexts.size());
    model_.selected_context_index = context_index;
    if (select_context_callback_) {
      select_context_callback_.Run(context_index);
    }
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  views::Button::PressedCallback BrowserActionCallback(
      SidebarFooterBrowserAction action) {
    return base::BindRepeating(&YeeSidebarFooterMenuView::InvokeBrowserAction,
                               base::Unretained(this), action);
  }

  void InvokeBrowserAction(SidebarFooterBrowserAction action) {
    if (browser_action_callback_) {
      browser_action_callback_.Run(action);
    }
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  void ToggleMemory() {
    model_.memory_enabled = !model_.memory_enabled;
    if (memory_changed_callback_) {
      memory_changed_callback_.Run(model_.memory_enabled);
    }
    ShowScreen(SidebarFooterScreen::kMemory, /*move_focus=*/true);
  }

  const raw_ptr<views::BubbleDialogDelegate> bubble_delegate_;
  int anchor_width_ = 0;
  SidebarFooterModel model_;
  SidebarContextSelectedCallback select_context_callback_;
  SidebarFooterBrowserActionCallback browser_action_callback_;
  SidebarMemoryChangedCallback memory_changed_callback_;
  SidebarFooterScreen screen_ = SidebarFooterScreen::kRoot;
  SidebarFooterScreen pending_screen_ = SidebarFooterScreen::kRoot;
  bool pending_move_focus_ = false;
  bool transitioning_out_ = false;
  raw_ptr<HoverButton> first_actionable_ = nullptr;
  gfx::SlideAnimation screen_animation_;
};

class YeeSidebarFooterView : public HoverButton {
 private:
  class HoverAnimationDelegate final : public gfx::AnimationDelegate {
   public:
    explicit HoverAnimationDelegate(YeeSidebarFooterView* owner)
        : owner_(owner) {}

    void AnimationProgressed(const gfx::Animation* animation) override {
      owner_->UpdateSurface();
    }

    void AnimationEnded(const gfx::Animation* animation) override {
      AnimationProgressed(animation);
    }

   private:
    const raw_ptr<YeeSidebarFooterView> owner_;
  };

 public:
  YeeSidebarFooterView(
      SidebarFooterModel model,
      SidebarContextSelectedCallback context_callback,
      SidebarFooterBrowserActionCallback browser_action_callback,
      SidebarMemoryChangedCallback memory_changed_callback)
      : HoverButton(PressedCallback(), CreateFooterTriggerParams(model)),
        model_(NormalizeModel(std::move(model))),
        context_callback_(std::move(context_callback)),
        browser_action_callback_(std::move(browser_action_callback)),
        memory_changed_callback_(std::move(memory_changed_callback)),
        hover_animation_delegate_(this),
        hover_animation_(&hover_animation_delegate_) {
    SetID(kSidebarFooterViewId);
    SetCallback(base::BindRepeating(&YeeSidebarFooterView::ToggleBubble,
                                    base::Unretained(this)));
    SetPreferredSize(gfx::Size(0, kSidebarMetrics.sidebar_footer_row_height));
    // HoverButton installs a rectangular highlight path by default. Keep its
    // focus and ink-drop surfaces on the same rounded geometry as the footer
    // background so state highlights never reveal square corners.
    SetFocusRingCornerRadius(kSidebarMetrics.sidebar_footer_corner_radius);
    views::FocusRing::Install(this);
    views::FocusRing* const focus_ring = views::FocusRing::Get(this);
    focus_ring->SetColorId(ui::kColorSysStateFocusRing);
    focus_ring->SetHasFocusPredicate(
        base::BindRepeating([](const views::View* view) {
          return HasKeyboardTraversalFocus(*view);
        }));
    // Yee owns the row's state ladder. Retain the native click ripple without
    // layering HoverButton's automatic focus highlight over the same surface.
    views::InkDrop::UseInkDropWithoutAutoHighlight(
        views::InkDrop::Get(this), /*highlight_on_hover=*/false,
        /*highlight_on_focus=*/false);
    SetProperty(views::kMarginsKey,
                gfx::Insets::TLBR(kSidebarMetrics.sidebar_footer_top_spacing,
                                  kSidebarMetrics.section_horizontal_inset, 0,
                                  kSidebarMetrics.section_horizontal_inset));
    mark_ = static_cast<views::Label*>(icon_view());
    chevron_ = static_cast<views::ImageView*>(
        secondary_view()->GetViewByID(kSidebarFooterChevronViewId));
    CHECK(mark_);
    CHECK(chevron_);
    title()->SetTextStyle(views::style::STYLE_BODY_4_EMPHASIS);
    subtitle()->SetTextStyle(views::style::STYLE_BODY_5);
    subtitle()->SetEnabledColor(ui::kColorLabelForegroundSecondary);
    title()->SetSkipSubpixelRenderingOpacityCheck(true);
    subtitle()->SetSkipSubpixelRenderingOpacityCheck(true);
    GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kMenu);
    GetViewAccessibility().SetIsCollapsed();
    UpdateIdentity();
  }

  YeeSidebarFooterView(const YeeSidebarFooterView&) = delete;
  YeeSidebarFooterView& operator=(const YeeSidebarFooterView&) = delete;
  ~YeeSidebarFooterView() override = default;

  void SetFooterVisible(bool visible) {
    if (!visible) {
      CloseBubble();
    }
    SetVisible(visible);
  }

  bool IsPositionInWindowCaption(const gfx::Point& point) const {
    return !GetVisible() || !GetLocalBounds().Contains(point);
  }

  views::View* GetMenuForTesting() { return bubble_tracker_.view(); }

 protected:
  void AddedToWidget() override {
    HoverButton::AddedToWidget();
    if (views::Widget* const widget = GetWidget()) {
      paint_as_active_subscription_ =
          widget->RegisterPaintAsActiveChangedCallback(base::BindRepeating(
              &YeeSidebarFooterView::UpdateSurface, base::Unretained(this)));
    }
    UpdateSurface();
  }

  void RemovedFromWidget() override {
    paint_as_active_subscription_ = {};
    HoverButton::RemovedFromWidget();
  }

  void OnBoundsChanged(const gfx::Rect& previous_bounds) override {
    HoverButton::OnBoundsChanged(previous_bounds);
    if (previous_bounds.width() == width()) {
      return;
    }
    if (auto* const menu =
            static_cast<YeeSidebarFooterMenuView*>(bubble_tracker_.view())) {
      menu->SetAnchorWidth(width());
    }
  }

  bool OnMousePressed(const ui::MouseEvent& event) override {
    bubble_reopen_suppressor_.OnMousePressed();
    return HoverButton::OnMousePressed(event);
  }

  void OnGestureEvent(ui::GestureEvent* event) override {
    if (event->type() == ui::EventType::kGestureTapDown) {
      bubble_reopen_suppressor_.OnMousePressed();
    }
    HoverButton::OnGestureEvent(event);
  }

  void OnThemeChanged() override {
    HoverButton::OnThemeChanged();
    UpdateSurface();
  }

  void OnFocus() override {
    HoverButton::OnFocus();
    UpdateSurface();
  }

  void OnBlur() override {
    HoverButton::OnBlur();
    UpdateSurface();
  }

  void StateChanged(ButtonState old_state) override {
    HoverButton::StateChanged(old_state);
    const bool was_hovered = old_state == STATE_HOVERED;
    const bool is_hovered = GetState() == STATE_HOVERED;
    if (was_hovered != is_hovered) {
      if (gfx::Animation::ShouldRenderRichAnimation()) {
        hover_animation_.SetSlideDuration(base::Milliseconds(
            kSidebarMetrics.sidebar_row_hover_animation_duration_ms));
        hover_animation_.SetTweenType(is_hovered ? gfx::Tween::EASE_OUT
                                                 : gfx::Tween::EASE_IN);
        if (is_hovered) {
          hover_animation_.Show();
        } else {
          hover_animation_.Hide();
        }
      } else {
        hover_animation_.Reset(is_hovered ? 1.0 : 0.0);
      }
    }
    UpdateSurface();
  }

 private:
  void ToggleBubble(const ui::Event& event) {
    const bool is_pointer_interaction =
        event.IsMouseEvent() || event.IsGestureEvent();
    if (bubble_reopen_suppressor_.ShouldSuppressBubbleShow(
            is_pointer_interaction)) {
      CloseBubble();
      return;
    }
    if (bubble_tracker_.view()) {
      CloseBubble();
      return;
    }
    auto bubble_delegate = std::make_unique<FooterBubbleDelegate>(this);
    bubble_delegate->SetButtons(
        static_cast<int>(ui::mojom::DialogButton::kNone));
    bubble_delegate->set_margins(gfx::Insets());
    bubble_delegate->set_close_on_deactivate(true);
    bubble_delegate->set_corner_radius(12);
    bubble_delegate->set_shadow(views::BubbleBorder::STANDARD_SHADOW);
    bubble_delegate->SetBackgroundColor(ui::kColorSysBaseContainerElevated);
    bubble_delegate->SetAccessibleTitle(u"Workspace and account menu");
    auto menu = std::make_unique<YeeSidebarFooterMenuView>(
        bubble_delegate.get(), width(), model_,
        base::BindRepeating(&YeeSidebarFooterView::SelectContext,
                            weak_factory_.GetWeakPtr()),
        browser_action_callback_, memory_changed_callback_);
    bubble_tracker_.SetView(menu.get());
    bubble_delegate->SetContentsView(std::move(menu));
    bubble_ = std::make_unique<FooterBubble>(
        std::move(bubble_delegate),
        base::BindOnce(&YeeSidebarFooterView::OnBubbleClosed,
                       weak_factory_.GetWeakPtr()),
        base::BindRepeating(&YeeSidebarFooterView::OnBubbleNativeDestroying,
                            weak_factory_.GetWeakPtr()));
    bubble_reopen_suppressor_.Observe(bubble_->widget());
    bubble_->widget()->Show();
    UpdateSurface();
  }

  void CloseBubble() {
    if (bubble_) {
      bubble_->widget()->Close();
    }
  }

  void OnBubbleClosed(views::Widget::ClosedReason reason) {
    if (!bubble_) {
      return;
    }
    // Pointer dismissal and window deactivation must leave focus at the newly
    // selected destination. Escape is the keyboard cancellation path, so only
    // that close reason returns traversal focus to the trigger.
    ReleaseBubble(/*restore_keyboard_focus=*/
                  reason == views::Widget::ClosedReason::kEscKeyPressed);
  }

  void OnBubbleNativeDestroying() {
    bubble_tracker_.SetView(nullptr);
    UpdateSurface();
    // Native teardown can skip or defer the normal close callback. Finish the
    // client-owned cleanup on the next task so Widget observer dispatch has
    // unwound, without trying to return focus while the browser itself may be
    // closing.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&YeeSidebarFooterView::ReleaseNativeClosedBubble,
                       weak_factory_.GetWeakPtr()));
  }

  void ReleaseNativeClosedBubble() {
    if (bubble_) {
      ReleaseBubble(/*restore_keyboard_focus=*/false);
    }
  }

  void ReleaseBubble(bool restore_keyboard_focus) {
    bubble_tracker_.SetView(nullptr);
    // A menu action can close the bubble from inside a Button callback. Defer
    // destruction until that event stack unwinds while retaining the Widget ->
    // delegate destruction order encoded by FooterBubble.
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(bubble_));
    UpdateSurface();
    if (restore_keyboard_focus && GetVisible()) {
      RequestFocusWithReason(
          views::FocusManager::FocusChangeReason::kFocusTraversal);
    }
  }

  void SelectContext(size_t context_index) {
    CHECK_LT(context_index, model_.contexts.size());
    model_.selected_context_index = context_index;
    UpdateIdentity();
    if (context_callback_) {
      context_callback_.Run(context_index);
    }
  }

  void UpdateIdentity() {
    const SidebarContextItem& context = SelectedContext(model_);
    title()->SetText(context.workspace_name);
    subtitle()->SetText(ContextSubtitle(context));
    mark_->SetText(FirstCodePointMark(context.workspace_name));
    GetViewAccessibility().SetName(context.workspace_name + u", " +
                                   context.tenant_name + u", " +
                                   context.account_name + u", context menu");
  }

  void UpdateSurface() {
    if (bubble_tracker_.view()) {
      GetViewAccessibility().SetIsExpanded();
    } else {
      GetViewAccessibility().SetIsCollapsed();
    }
    const ui::ColorProvider* const color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }
    const double hover_progress = hover_animation_.GetCurrentValue();
    SidebarItemVisualState visual_state = SidebarItemVisualState::kResting;
    if (bubble_tracker_.view() || GetState() == STATE_PRESSED) {
      visual_state = SidebarItemVisualState::kActive;
    } else if (GetState() == STATE_HOVERED || hover_progress > 0.0) {
      visual_state = SidebarItemVisualState::kHovered;
    } else if (HasKeyboardTraversalFocus(*this)) {
      // Pointer focus is intentionally neutral. Only keyboard traversal uses
      // the stronger outlined treatment shared with the visible focus ring.
      visual_state = SidebarItemVisualState::kActive;
    }
    const views::Widget* const widget = GetWidget();
    const SidebarItemColors colors =
        ResolveSidebarItemColors(*color_provider, visual_state, hover_progress,
                                 !widget || widget->ShouldPaintAsActive(),
                                 /*persistent_surface=*/false);
    const bool paint_background =
        visual_state != SidebarItemVisualState::kResting;
    const bool paint_outline = visual_state == SidebarItemVisualState::kActive;
    // A transparent painter is still a surface in the View hierarchy. Remove
    // the resting background and use a non-painting border for rest and hover.
    // Both border variants reserve the same metrics-owned insets, so text does
    // not move when the active outline appears.
    SetBackground(
        paint_background
            ? views::CreateRoundedRectBackground(
                  colors.fill, kSidebarMetrics.sidebar_footer_corner_radius)
            : nullptr);
    SetBorder(CreateFooterSurfaceBorder(paint_outline, colors.stroke));
    // Identity hierarchy remains stable across interaction states: the
    // workspace name is always primary, while the tenant/account line stays
    // secondary. Hover, focus, and open state are expressed by the shared
    // surface instead of making both labels jump in contrast.
    title()->SetEnabledColor(ui::kColorLabelForeground);
    subtitle()->SetEnabledColor(ui::kColorLabelForegroundSecondary);
    const int chevron_size = kSidebarMetrics.sidebar_footer_disclosure_size;
    chevron_->SetImage(ui::ImageModel::FromVectorIcon(
        bubble_tracker_.view() ? vector_icons::kKeyboardArrowDownIcon
                               : vector_icons::kKeyboardArrowUpIcon,
        ui::kColorIconSecondary, chevron_size));
  }

  SidebarFooterModel model_;
  SidebarContextSelectedCallback context_callback_;
  SidebarFooterBrowserActionCallback browser_action_callback_;
  SidebarMemoryChangedCallback memory_changed_callback_;
  raw_ptr<views::Label> mark_ = nullptr;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  views::ViewTracker bubble_tracker_;
  WebUIBubbleReopenSuppressor bubble_reopen_suppressor_;
  base::CallbackListSubscription paint_as_active_subscription_;
  HoverAnimationDelegate hover_animation_delegate_;
  gfx::SlideAnimation hover_animation_;
  std::unique_ptr<FooterBubble> bubble_;
  base::WeakPtrFactory<YeeSidebarFooterView> weak_factory_{this};
};

}  // namespace

SidebarFooterModel CreateLocalSidebarFooterModel(std::u16string account_name) {
  const bool account_name_is_placeholder = account_name.empty();
  if (account_name_is_placeholder) {
    account_name = u"Local profile";
  }
  SidebarFooterModel model;
  model.contexts.push_back(
      {.tenant_name = u"Local",
       .workspace_name = u"Personal",
       .account_name = std::move(account_name),
       .account_name_is_placeholder = account_name_is_placeholder});
  return model;
}

std::unique_ptr<views::View> CreateSidebarFooterView(
    SidebarFooterModel model,
    SidebarContextSelectedCallback context_selected_callback,
    SidebarFooterBrowserActionCallback browser_action_callback,
    SidebarMemoryChangedCallback memory_changed_callback) {
  return std::make_unique<YeeSidebarFooterView>(
      NormalizeModel(std::move(model)), std::move(context_selected_callback),
      std::move(browser_action_callback), std::move(memory_changed_callback));
}

void SetSidebarFooterVisible(views::View& view, bool visible) {
  CHECK_EQ(view.GetID(), kSidebarFooterViewId);
  static_cast<YeeSidebarFooterView&>(view).SetFooterVisible(visible);
}

bool IsSidebarFooterPositionInWindowCaption(const views::View& view,
                                            const gfx::Point& point) {
  CHECK_EQ(view.GetID(), kSidebarFooterViewId);
  return static_cast<const YeeSidebarFooterView&>(view)
      .IsPositionInWindowCaption(point);
}

views::View* GetSidebarFooterMenuForTesting(views::View& view) {
  CHECK_EQ(view.GetID(), kSidebarFooterViewId);
  return static_cast<YeeSidebarFooterView&>(view).GetMenuForTesting();
}

}  // namespace yee
