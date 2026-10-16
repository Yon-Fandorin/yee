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
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/text_utils.h"
#include "ui/gfx/vector_icon_types.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/style/typography.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace yee {
namespace {

constexpr int kMenuInsets = kSidebarMetrics.sidebar_footer_menu_inset;
constexpr int kMenuIconGlyphSize =
    kSidebarMetrics.sidebar_footer_menu_icon_size;
constexpr int kMenuCornerRadius =
    kSidebarMetrics.sidebar_footer_menu_corner_radius;
constexpr int kMenuRowHorizontalInset =
    kSidebarMetrics.sidebar_footer_menu_horizontal_inset;
constexpr int kMenuRowVerticalInset = 3;

gfx::Insets FooterSurfaceInsets() {
  return gfx::Insets::VH(
      kSidebarMetrics.sidebar_footer_surface_vertical_inset(),
      kSidebarMetrics.sidebar_footer_surface_horizontal_inset());
}

bool HasKeyboardTraversalFocus(const views::View &view) {
  const views::FocusManager *const focus_manager = view.GetFocusManager();
  return view.HasFocus() && focus_manager &&
         focus_manager->focus_change_reason() ==
             views::FocusManager::FocusChangeReason::kFocusTraversal;
}

ui::ColorId MenuOutlineColorId(const views::View &view) {
  const bool light_mode =
      view.GetWidget() && view.GetWidget()->GetColorMode() ==
                              ui::ColorProviderKey::ColorMode::kLight;
  return light_mode ? ui::kColorSysNeutralOutline : ui::kColorMenuSeparator;
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
  if (context.is_local_context || context.tenant_name.empty()) {
    return context.account_name;
  }
  return context.tenant_name + u" · " + context.account_name;
}

int PreferredMenuWidth(const SidebarFooterModel& model) {
  const auto& typography = views::TypographyProvider::Get();
  const gfx::FontList& title_font = typography.GetFont(
      views::style::CONTEXT_MENU, views::style::STYLE_BODY_4);
  const gfx::FontList& subtitle_font = typography.GetFont(
      views::style::CONTEXT_MENU, views::style::STYLE_BODY_5);
  int text_width = 0;
  for (int id :
       {IDS_YEE_WEBUI_FOOTER_BROWSER_TOOLS, IDS_YEE_WEBUI_FOOTER_MEMORY,
        IDS_YEE_WEBUI_FOOTER_SWITCH_CONTEXT, IDS_YEE_WEBUI_FOOTER_REOPEN_TAB,
        IDS_YEE_WEBUI_FOOTER_DOWNLOADS, IDS_YEE_WEBUI_FOOTER_HISTORY,
        IDS_YEE_WEBUI_FOOTER_OTHER_DEVICES, IDS_YEE_WEBUI_FOOTER_EXTENSIONS,
        IDS_YEE_WEBUI_FOOTER_NOT_CONNECTED}) {
    text_width = std::max(
        text_width,
        gfx::GetStringWidth(l10n_util::GetStringUTF16(id), title_font));
  }
  for (const auto& context : model.contexts) {
    text_width = std::max(
        text_width, gfx::GetStringWidth(context.workspace_name, title_font));
    text_width =
        std::max(text_width,
                 gfx::GetStringWidth(ContextSubtitle(context), subtitle_font));
  }
  const int columns = 2 * (kMenuInsets + kMenuRowHorizontalInset) +
                      kSidebarMetrics.sidebar_footer_menu_icon_slot_size +
                      2 * kSidebarMetrics.sidebar_footer_icon_label_spacing +
                      kSidebarMetrics.sidebar_footer_menu_disclosure_size;
  return std::clamp(text_width + columns,
                    kSidebarMetrics.sidebar_footer_menu_min_width,
                    kSidebarMetrics.sidebar_footer_menu_max_width);
}

std::unique_ptr<views::Label> CreateMarkLabel(std::u16string text, int size) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetID(kSidebarFooterWorkspaceMarkViewId);
  label->SetPreferredSize(gfx::Size(size, size));
  label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  label->SetTextStyle(views::style::STYLE_BODY_5_EMPHASIS);
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
      gfx::Size(kSidebarMetrics.sidebar_footer_menu_icon_slot_size,
                kSidebarMetrics.sidebar_footer_menu_icon_slot_size));
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
  label->SetEnabledColor(emphasized ? ui::kColorSysPrimary
                                    : ui::kColorLabelForegroundSecondary);
  label->SetTextStyle(views::style::STYLE_BODY_5);
  label->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  label->SetSkipSubpixelRenderingOpacityCheck(true);
  label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 2)));
  label->SetCanProcessEventsWithinSubtree(false);
  return label;
}

std::unique_ptr<views::ImageView> CreateTrailingIcon(
    const gfx::VectorIcon& icon,
    ui::ColorId color = ui::kColorIconSecondary) {
  auto icon_view = std::make_unique<views::ImageView>();
  const int size = kSidebarMetrics.sidebar_footer_menu_disclosure_size;
  icon_view->SetPreferredSize(gfx::Size(size, size));
  icon_view->SetImage(ui::ImageModel::FromVectorIcon(icon, color, size));
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
  chevron->SetPreferredSize(
      gfx::Size(kSidebarMetrics.sidebar_footer_icon_size,
                kSidebarMetrics.sidebar_footer_icon_size));
  chevron->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kKeyboardArrowUpIcon, ui::kColorIconSecondary, size));
  chevron->SetID(kSidebarFooterChevronViewId);
  chevron->SetCanProcessEventsWithinSubtree(false);
  return chevron;
}

void SetCompactContentMargins(views::View *icon, views::View *secondary) {
    // 두 열의 세로 간격은 기본 control-list 여백 대신 이 행의 border가 정한다.
  if (icon) {
    icon->parent()->SetProperty(views::kMarginsKey, gfx::Insets());
  }
  if (secondary) {
    secondary->SetProperty(
        views::kMarginsKey,
        gfx::Insets().set_left(
            kSidebarMetrics.sidebar_footer_icon_label_spacing));
  }
}

class SidebarFooterMenuRow : public HoverButton {
 public:
  SidebarFooterMenuRow(PressedCallback callback,
                       Params params,
                       bool selected,
                       bool keyboard_focus_visible)
      : HoverButton(std::move(callback), std::move(params)),
        keyboard_focus_visible_(keyboard_focus_visible) {
    SetCompactContentMargins(icon_view(), secondary_view());
    SetFocusRingCornerRadius(kMenuCornerRadius);
    // 자동 focus 채움을 끄고 hover·누름에만 상태 면을 표시한다.
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    views::FocusRing::Install(this);
    views::FocusRing::Get(this)->SetColorId(ui::kColorSysStateFocusRing);
    views::FocusRing::Get(this)->SetHasFocusPredicate(
        base::BindRepeating([](const views::View* view) {
          const auto* row = static_cast<const SidebarFooterMenuRow*>(view);
          return row->HasFocus() && (row->keyboard_focus_visible_ ||
                                     HasKeyboardTraversalFocus(*row));
        }));
    title()->SetSkipSubpixelRenderingOpacityCheck(true);
    title()->SetTextStyle(subtitle() ? views::style::STYLE_BODY_4_EMPHASIS
                                     : views::style::STYLE_BODY_4);
    if (subtitle()) {
      subtitle()->SetSkipSubpixelRenderingOpacityCheck(true);
      subtitle()->SetTextStyle(views::style::STYLE_BODY_5);
    }
    GetViewAccessibility().SetIsSelected(selected);
    UpdateSurface();
  }
  SidebarFooterMenuRow(const SidebarFooterMenuRow&) = delete;
  SidebarFooterMenuRow& operator=(const SidebarFooterMenuRow&) = delete;
  ~SidebarFooterMenuRow() override = default;

  void ShowKeyboardFocus() {
    keyboard_focus_visible_ = true;
    views::FocusRing::Get(this)->SchedulePaint();
  }

 protected:
  void OnThemeChanged() override {
    HoverButton::OnThemeChanged();
    UpdateSurface();
  }

  void StateChanged(ButtonState old_state) override {
    HoverButton::StateChanged(old_state);
    UpdateSurface();
  }

 private:
  void UpdateSurface() {
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      SetBackground(views::CreateRoundedRectBackground(
          GetState() == STATE_PRESSED ? ui::kColorSysStateRippleNeutralOnSubtle
                                      : ui::kColorSysStateHoverOnSubtle,
          kMenuCornerRadius));
    } else {
      SetBackground(nullptr);
    }
  }

  bool keyboard_focus_visible_ = false;
};

class SidebarFooterMenuSeparator : public views::Separator {
 protected:
  void OnThemeChanged() override {
    views::Separator::OnThemeChanged();
    SetColorId(MenuOutlineColorId(*this));
  }
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
               base::RepeatingClosure on_native_destroying,
               base::ScopedClosureRunner popup_lock)
      : popup_lock_(std::move(popup_lock)),
        delegate_(std::move(delegate)),
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
    popup_lock_.RunAndReset();
  }

 private:
  // 정상 닫기는 지연 삭제되므로 Widget이 사라질 때까지 잠금을 유지한다.
  base::ScopedClosureRunner popup_lock_;
  // Declaration order is intentional: normal member destruction also deletes
  // the Widget before the delegate.
  std::unique_ptr<FooterBubbleDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  base::RepeatingClosure on_native_destroying_;
  base::ScopedObservation<views::Widget, views::WidgetObserver>
      widget_observation_{this};
};

HoverButton::Params CreateFooterTriggerParams(const SidebarFooterModel &model) {
  const SidebarContextItem &context = SelectedContext(model);
  HoverButton::Params params;
  params.icon_view = CreateMarkLabel(FirstCodePointMark(context.workspace_name),
                                     kSidebarMetrics.sidebar_footer_icon_size);
  params.title = context.workspace_name;
  params.subtitle = ContextSubtitle(context);
  params.add_vertical_label_spacing = false;
  params.icon_label_spacing =
      2 * kSidebarMetrics.tab_content_horizontal_padding +
      kSidebarMetrics.tab_icon_design_width -
      kSidebarMetrics.sidebar_footer_surface_horizontal_inset() -
      kSidebarMetrics.sidebar_footer_icon_size;
  params.secondary_view = CreateFooterChevron();
  return params;
}

class YeeSidebarFooterMenuView : public views::View,
                                 public gfx::AnimationDelegate {
public:
  YeeSidebarFooterMenuView(
      views::BubbleDialogDelegate *bubble_delegate, SidebarFooterModel model,
      bool keyboard_navigation,
      SidebarContextSelectedCallback select_context_callback,
      SidebarFooterBrowserActionCallback browser_action_callback,
      SidebarMemoryChangedCallback memory_changed_callback)
      : bubble_delegate_(bubble_delegate),
        model_(NormalizeModel(std::move(model))),
        menu_width_(PreferredMenuWidth(model_)),
        keyboard_navigation_(keyboard_navigation),
        select_context_callback_(std::move(select_context_callback)),
        browser_action_callback_(std::move(browser_action_callback)),
        memory_changed_callback_(std::move(memory_changed_callback)),
        screen_animation_(this) {
    CHECK(bubble_delegate_);
    SetID(kSidebarFooterMenuViewId);
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::VH(kMenuInsets, 0), 2));
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

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if ((event.key_code() == ui::VKEY_ESCAPE ||
         event.key_code() == ui::VKEY_LEFT) &&
        screen_ != SidebarFooterScreen::kRoot) {
      keyboard_navigation_ = true;
      ShowScreen(ParentSidebarFooterScreen(screen_), /*move_focus=*/true);
      return true;
    }
    if (event.key_code() == ui::VKEY_UP || event.key_code() == ui::VKEY_DOWN ||
        event.key_code() == ui::VKEY_HOME || event.key_code() == ui::VKEY_END) {
      keyboard_navigation_ = true;
      std::vector<SidebarFooterMenuRow*> enabled_rows;
      for (const auto& row : rows_) {
        row->ShowKeyboardFocus();
        if (row->GetEnabled()) {
          enabled_rows.push_back(row);
        }
      }
      if (enabled_rows.empty()) {
        return true;
      }
      auto current =
          std::ranges::find(enabled_rows, GetFocusManager()->GetFocusedView());
      int index = current == enabled_rows.end()
                      ? (event.key_code() == ui::VKEY_DOWN ? -1 : 0)
                      : static_cast<int>(current - enabled_rows.begin());
      const int count = static_cast<int>(enabled_rows.size());
      if (event.key_code() == ui::VKEY_HOME) {
        index = 0;
      } else if (event.key_code() == ui::VKEY_END) {
        index = count - 1;
      } else {
        index =
            (index + (event.key_code() == ui::VKEY_UP ? count - 1 : 1)) % count;
      }
      enabled_rows[index]->RequestFocusWithReason(
          views::FocusManager::FocusChangeReason::kFocusTraversal);
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
  int ContentWidth() const { return menu_width_ - 2 * kMenuInsets; }

  void RegisterRow(SidebarFooterMenuRow* row) {
    rows_.push_back(row);
    if (!first_actionable_ && row->GetEnabled()) {
      first_actionable_ = row;
    }
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

    auto row = std::make_unique<SidebarFooterMenuRow>(
        std::move(callback), std::move(params), selected, keyboard_navigation_);
    row->SetPreferredSize(gfx::Size(ContentWidth(), preferred_height));
    row->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(kMenuRowVerticalInset, kMenuRowHorizontalInset)));
    row->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kMenuInsets));
    if (view_id) {
      row->SetID(view_id);
    }
    row->SetEnabled(enabled);
    SidebarFooterMenuRow* const result = row.get();
    AddChildView<HoverButton>(std::move(row));
    RegisterRow(result);
    return result;
  }

  void AddSectionLabel(std::u16string text) {
    auto label = std::make_unique<views::Label>(std::move(text));
    label->SetPreferredSize(gfx::Size(ContentWidth(), 20));
    label->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kMenuInsets));
    label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(
        4, kMenuRowHorizontalInset, 0, kMenuRowHorizontalInset)));
    label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    label->SetTextStyle(views::style::STYLE_BODY_5);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetSkipSubpixelRenderingOpacityCheck(true);
    AddChildView(std::move(label));
  }

  void AddSeparator() {
    auto separator = std::make_unique<SidebarFooterMenuSeparator>();
    separator->SetOrientation(views::Separator::Orientation::kHorizontal);
    separator->SetColorId(ui::kColorMenuSeparator);
    separator->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 0)));
    separator->SetPreferredSize(gfx::Size(menu_width_, 9));
    AddChildView<views::Separator>(std::move(separator));
  }

  void AddDescription(std::u16string text) {
    auto label = std::make_unique<views::Label>(std::move(text));
    label->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, kMenuInsets));
    label->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::VH(6, kMenuRowHorizontalInset)));
    label->SetEnabledColor(ui::kColorSysOnSurfaceSubtle);
    label->SetTextStyle(views::style::STYLE_BODY_5);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMultiLine(true);
    label->SetSkipSubpixelRenderingOpacityCheck(true);
    label->SizeToFit(ContentWidth());
    AddChildView(std::move(label));
  }

  void AddHeader(std::u16string title, SidebarFooterScreen destination) {
    HoverButton* const back =
        AddRow(std::move(title), {},
               CreateMenuIcon(vector_icons::kArrowBackIcon), nullptr,
               base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                                   base::Unretained(this), destination,
                                   /*move_focus=*/true),
               /*view_id=*/0);
    back->GetViewAccessibility().SetDescription(
        l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_BACK));
    back->title()->SetTextStyle(views::style::STYLE_BODY_4_EMPHASIS);
    AddSeparator();
  }

  void AddRoot() {
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_BROWSER_TOOLS), {},
           CreateMenuIcon(vector_icons::kHistoryIcon), CreateDisclosure(),
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this),
                               SidebarFooterScreen::kBrowserTools,
                               /*move_focus=*/true),
           kSidebarFooterRootBrowserToolsViewId);
    HoverButton* const memory =
        AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MEMORY), {},
               CreateMenuIcon(vector_icons::kLightbulbIcon), CreateDisclosure(),
               base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                                   base::Unretained(this),
                                   SidebarFooterScreen::kMemory,
                                   /*move_focus=*/true),
               kSidebarFooterRootMemoryViewId);
    memory->GetViewAccessibility().SetDescription(l10n_util::GetStringUTF16(
        model_.memory_available ? IDS_YEE_WEBUI_FOOTER_MEMORY_DESCRIPTION
                                : IDS_YEE_WEBUI_FOOTER_MEMORY_UNAVAILABLE));
    AddSeparator();
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_SWITCH_CONTEXT), {},
           CreateMenuIcon(vector_icons::kDesktopWindowsIcon),
           CreateDisclosure(),
           base::BindRepeating(&YeeSidebarFooterMenuView::ShowScreen,
                               base::Unretained(this),
                               SidebarFooterScreen::kContext,
                               /*move_focus=*/true),
           kSidebarFooterRootContextViewId);
  }

  void AddContext() {
    AddHeader(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_SWITCH_CONTEXT),
              SidebarFooterScreen::kRoot);
    for (size_t index = 0; index < model_.contexts.size(); ++index) {
      const SidebarContextItem& choice = model_.contexts[index];
      AddRow(
          choice.workspace_name, ContextSubtitle(choice),
          /*icon=*/nullptr,
          index == model_.selected_context_index
              ? CreateTrailingIcon(views::kCheckIcon, ui::kColorIconSecondary)
              : nullptr,
          base::BindRepeating(&YeeSidebarFooterMenuView::SelectContext,
                              base::Unretained(this), index),
          /*view_id=*/0, kSidebarMetrics.sidebar_footer_root_row_height,
          /*enabled=*/true,
          /*selected=*/index == model_.selected_context_index);
    }
  }

  void AddBrowserTools() {
    AddHeader(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_BROWSER_TOOLS),
              SidebarFooterScreen::kRoot);
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_REOPEN_TAB), {},
           CreateMenuIcon(vector_icons::kUndoIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kReopenClosedTab),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_DOWNLOADS), {},
           CreateMenuIcon(vector_icons::kDownloadIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kDownloads),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_HISTORY), {},
           CreateMenuIcon(vector_icons::kHistoryIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kHistory),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_OTHER_DEVICES), {},
           CreateMenuIcon(vector_icons::kDevicesIcon), nullptr,
           BrowserActionCallback(
               SidebarFooterBrowserAction::kTabsFromOtherDevices),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
    AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_EXTENSIONS), {},
           CreateMenuIcon(vector_icons::kChromeExtensionIcon), nullptr,
           BrowserActionCallback(SidebarFooterBrowserAction::kManageExtensions),
           /*view_id=*/0, kSidebarMetrics.sidebar_footer_menu_row_height);
  }

  void AddMemory() {
    AddHeader(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MEMORY),
              SidebarFooterScreen::kRoot);
    const SidebarContextItem& context = SelectedContext(model_);
    AddSectionLabel(context.workspace_name);
    if (model_.memory_available) {
      AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MEMORY), {},
             CreateMenuIcon(vector_icons::kLightbulbIcon),
             CreateTrailingLabel(
                 l10n_util::GetStringUTF16(
                     model_.memory_enabled ? IDS_YEE_WEBUI_FOOTER_MEMORY_ON
                                           : IDS_YEE_WEBUI_FOOTER_MEMORY_OFF),
                 /*emphasized=*/model_.memory_enabled),
             base::BindRepeating(&YeeSidebarFooterMenuView::ToggleMemory,
                                 base::Unretained(this)),
             kSidebarFooterMemoryToggleViewId);
    } else {
      AddRow(l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_NOT_CONNECTED), {},
             CreateMenuIcon(vector_icons::kLightbulbIcon), nullptr,
             views::Button::PressedCallback(), kSidebarFooterMemoryToggleViewId,
             kSidebarMetrics.sidebar_footer_menu_row_height,
             /*enabled=*/false);
    }
    AddDescription(l10n_util::GetStringUTF16(
        model_.memory_available ? IDS_YEE_WEBUI_FOOTER_MEMORY_DESCRIPTION
                                : IDS_YEE_WEBUI_FOOTER_MEMORY_UNAVAILABLE));
  }

  void ShowScreen(SidebarFooterScreen screen, bool move_focus) {
    if (GetFocusManager() && GetFocusManager()->GetFocusedView() &&
        HasKeyboardTraversalFocus(*GetFocusManager()->GetFocusedView())) {
      keyboard_navigation_ = true;
    }
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
    rows_.clear();
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
  SidebarFooterModel model_;
  const int menu_width_;
  bool keyboard_navigation_ = false;
  SidebarContextSelectedCallback select_context_callback_;
  SidebarFooterBrowserActionCallback browser_action_callback_;
  SidebarMemoryChangedCallback memory_changed_callback_;
  SidebarFooterScreen screen_ = SidebarFooterScreen::kRoot;
  SidebarFooterScreen pending_screen_ = SidebarFooterScreen::kRoot;
  bool pending_move_focus_ = false;
  bool transitioning_out_ = false;
  raw_ptr<HoverButton> first_actionable_ = nullptr;
  std::vector<raw_ptr<SidebarFooterMenuRow>> rows_;
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
      SidebarMemoryChangedCallback memory_changed_callback,
      SidebarPopupLockCallback acquire_popup_lock)
      : HoverButton(PressedCallback(), CreateFooterTriggerParams(model)),
        model_(NormalizeModel(std::move(model))),
        context_callback_(std::move(context_callback)),
        browser_action_callback_(std::move(browser_action_callback)),
        memory_changed_callback_(std::move(memory_changed_callback)),
        acquire_popup_lock_(std::move(acquire_popup_lock)),
        hover_animation_delegate_(this),
        hover_animation_(&hover_animation_delegate_) {
    SetID(kSidebarFooterViewId);
    SetCompactContentMargins(icon_view(), secondary_view());
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
    AddExtraAccessibleText(
        l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MENU_TITLE));
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
    // 지속 활성 면은 Yee가 그린다. 기본 anchor 강조가 ink drop을 계속
    // ACTIVATED로 두어 배경을 한 번 더 채우지 않게 한다.
    bubble_delegate->set_highlight_button_when_shown(false);
    bubble_delegate->set_corner_radius(
        kSidebarMetrics.sidebar_footer_menu_surface_corner_radius);
    bubble_delegate->set_shadow(views::BubbleBorder::DIALOG_SHADOW);
    bubble_delegate->SetBackgroundColor(ui::kColorSysBaseContainerElevated);
    bubble_delegate->SetAccessibleTitle(
        l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MENU_TITLE));
    auto menu = std::make_unique<YeeSidebarFooterMenuView>(
        bubble_delegate.get(), model_, /*keyboard_navigation=*/
        !is_pointer_interaction,
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
                            weak_factory_.GetWeakPtr()),
        acquire_popup_lock_ ? acquire_popup_lock_.Run()
                            : base::ScopedClosureRunner());
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
    // 닫힌 인스턴스를 즉시 분리하고 삭제만 미룬다. 그 사이 새 팝업을 열어도
    // 이전 인스턴스의 정리가 새 팝업에 영향을 주지 않는다.
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
    if (!restore_keyboard_focus) {
      // 부모 창이 다시 활성화돼도 닫힌 메뉴의 Footer 포커스를 복원하지 않는다.
      // 클릭한 다른 목적지의 현재·저장 포커스는 유지한다.
      if (views::FocusManager *const focus_manager = GetFocusManager()) {
        if (HasFocus()) {
          focus_manager->ClearFocus();
        }
        if (focus_manager->GetStoredFocusView() == this) {
          focus_manager->SetStoredFocusView(nullptr);
        }
      }
    }
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
    GetViewAccessibility().SetName(l10n_util::GetStringFUTF16(
        IDS_YEE_WEBUI_FOOTER_MENU_ACCESSIBLE_NAME, context.workspace_name,
        ContextSubtitle(context)));
  }

  void UpdateSurface() {
    if (bubble_tracker_.view()) {
      GetViewAccessibility().SetIsExpanded();
    } else {
      GetViewAccessibility().SetIsCollapsed();
    }
    const ui::ColorProvider *const color_provider = GetColorProvider();
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
      // 키보드 포커스는 활성 면과 system focus ring 한 겹으로 구분한다.
      visual_state = SidebarItemVisualState::kActive;
    }
    const views::Widget *const widget = GetWidget();
    const SidebarItemColors colors =
        ResolveSidebarItemColors(*color_provider, visual_state, hover_progress,
                                 !widget || widget->ShouldPaintAsActive(),
                                 /*persistent_surface=*/false);
    const bool paint_background =
        visual_state != SidebarItemVisualState::kResting;
    // 모든 상태가 같은 여백을 예약한다. 포커스에는 system ring만 그려
    // 내부 stroke와 겹친 이중 윤곽이 생기지 않게 한다.
    SetBackground(
        paint_background
            ? views::CreateRoundedRectBackground(
                  colors.fill, kSidebarMetrics.sidebar_footer_corner_radius)
            : nullptr);
    SetBorder(views::CreateEmptyBorder(FooterSurfaceInsets()));
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
  SidebarPopupLockCallback acquire_popup_lock_;
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
    account_name =
        l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_LOCAL_PROFILE);
  }
  SidebarFooterModel model;
  model.contexts.push_back(
      {.tenant_name = l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_LOCAL),
       .workspace_name =
           l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_PERSONAL),
       .account_name = std::move(account_name),
       .account_name_is_placeholder = account_name_is_placeholder,
       .is_local_context = true});
  return model;
}

std::unique_ptr<views::View> CreateSidebarFooterView(
    SidebarFooterModel model,
    SidebarContextSelectedCallback context_selected_callback,
    SidebarFooterBrowserActionCallback browser_action_callback,
    SidebarMemoryChangedCallback memory_changed_callback,
    SidebarPopupLockCallback acquire_popup_lock) {
  return std::make_unique<YeeSidebarFooterView>(
      NormalizeModel(std::move(model)), std::move(context_selected_callback),
      std::move(browser_action_callback), std::move(memory_changed_callback),
      std::move(acquire_popup_lock));
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
