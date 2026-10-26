// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/site_controls.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "chrome/browser/ui/views/yee/brand.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

constexpr int kPanelWidth = 360;
constexpr int kActionSize = 28;
constexpr int kShieldIconSize = 18;
constexpr int kBadgeHeight = 18;
constexpr int kBadgeGap = 3;
constexpr int kButtonInset = 4;

class SiteControlsBubble;
SiteControlsBubble* g_site_controls_bubble = nullptr;

std::unique_ptr<views::Label> CreateBodyLabel(std::u16string text) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetTextStyle(views::style::STYLE_SECONDARY);
  label->SetMultiLine(true);
  return label;
}

std::unique_ptr<views::View> CreateSection() {
  auto section = std::make_unique<views::View>();
  auto* layout = section->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(12, 16), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  return section;
}

class SiteControlsBubble : public LocationBarBubbleDelegateView {
 public:
  SiteControlsBubble(views::View* anchor,
                     Browser* browser,
                     content::WebContents* contents)
      : LocationBarBubbleDelegateView(anchor, contents),
        browser_(browser),
        contents_(contents),
        url_(contents->GetVisibleURL()) {
    CHECK(!g_site_controls_bubble);
    g_site_controls_bubble = this;

    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowCloseButton(true);
    SetTitle(base::UTF8ToUTF16(url_.host()));
    set_fixed_width(kPanelWidth);
    set_close_on_deactivate(true);
    SetLayoutManager(std::make_unique<views::FillLayout>());

    yee::content_blocking::ContentBlockingTabHelper::CreateForWebContents(
        contents_);
    AddChildView(CreateProtectionSection());
    auto* helper =
        yee::content_blocking::ContentBlockingTabHelper::FromWebContents(
            contents_);
    helper_subscription_ = helper->AddChangedCallback(base::BindRepeating(
        &SiteControlsBubble::RefreshBlockedCount, base::Unretained(this)));
    if (auto* service =
            yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
                browser_->GetProfile())) {
      service_subscription_ = service->AddChangedCallback(base::BindRepeating(
          &SiteControlsBubble::RefreshProtectionState, base::Unretained(this)));
    }
  }

  SiteControlsBubble(const SiteControlsBubble&) = delete;
  SiteControlsBubble& operator=(const SiteControlsBubble&) = delete;

  ~SiteControlsBubble() override {
    if (g_site_controls_bubble == this) {
      g_site_controls_bubble = nullptr;
    }
  }

 private:
  std::unique_ptr<views::View> CreateProtectionSection() {
    auto section = CreateSection();
    auto* service =
        yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
            browser_->GetProfile());
    const bool enabled = service && service->EnabledForSite(url_);

    auto heading = std::make_unique<views::Label>(
        enabled ? u"Protection is on for this site"
                : u"Protection is off for this site");
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetTextStyle(views::style::STYLE_HEADLINE_5);
    protection_heading_ = section->AddChildView(std::move(heading));

    auto toggle_row = std::make_unique<views::View>();
    auto* row_layout =
        toggle_row->SetLayoutManager(std::make_unique<views::FlexLayout>());
    row_layout->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
    auto* toggle_label = toggle_row->AddChildView(
        std::make_unique<views::Label>(u"Block ads and trackers"));
    toggle_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    toggle_label->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                 views::MinimumFlexSizeRule::kScaleToZero,
                                 views::MaximumFlexSizeRule::kUnbounded));
    protection_toggle_ = toggle_row->AddChildView(
        std::make_unique<views::ToggleButton>(base::BindRepeating(
            &SiteControlsBubble::OnProtectionToggled, base::Unretained(this))));
    protection_toggle_->SetIsOn(enabled);
    protection_toggle_->GetViewAccessibility().SetName(
        u"Block ads and trackers on this site");
    protection_toggle_->SetEnabled(service && url_.SchemeIsHTTPOrHTTPS());
    section->AddChildView(std::move(toggle_row));

    auto* helper =
        yee::content_blocking::ContentBlockingTabHelper::FromWebContents(
            contents_);
    const size_t blocked = helper ? helper->blocked_count() : 0;
    blocked_label_ = section->AddChildView(CreateBodyLabel(
        base::NumberToString16(blocked) + u" network requests blocked"));
    return section;
  }

  void OnProtectionToggled() {
    auto* service =
        yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
            browser_->GetProfile());
    if (!service) {
      return;
    }
    service->SetEnabledForSite(url_, protection_toggle_->GetIsOn());
    protection_heading_->SetText(protection_toggle_->GetIsOn()
                                     ? u"Protection is on for this site"
                                     : u"Protection is off for this site");
    GetWidget()->Close();
    contents_->GetController().Reload(content::ReloadType::NORMAL, true);
  }

  void RefreshBlockedCount() {
    if (!blocked_label_ || !contents_) {
      return;
    }
    auto* helper =
        yee::content_blocking::ContentBlockingTabHelper::FromWebContents(
            contents_);
    const size_t blocked = helper ? helper->blocked_count() : 0;
    blocked_label_->SetText(base::NumberToString16(blocked) +
                            u" network requests blocked");
  }

  void RefreshProtectionState() {
    if (!contents_) {
      return;
    }
    auto* service =
        yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
            browser_->GetProfile());
    const bool enabled = service && service->EnabledForSite(url_);
    protection_toggle_->SetIsOn(enabled);
    protection_heading_->SetText(enabled ? u"Protection is on for this site"
                                         : u"Protection is off for this site");
  }

  void PrimaryPageChanged(content::Page& page) override {
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  void WebContentsDestroyed() override {
    contents_ = nullptr;
    LocationBarBubbleDelegateView::WebContentsDestroyed();
  }

  const raw_ptr<Browser> browser_;
  raw_ptr<content::WebContents> contents_;
  const GURL url_;
  raw_ptr<views::Label> protection_heading_ = nullptr;
  raw_ptr<views::ToggleButton> protection_toggle_ = nullptr;
  raw_ptr<views::Label> blocked_label_ = nullptr;
  base::CallbackListSubscription helper_subscription_;
  base::CallbackListSubscription service_subscription_;
};

}  // namespace

SiteControlsButton::SiteControlsButton(Browser* browser)
    : views::ImageButton(base::BindRepeating(&SiteControlsButton::OnPressed,
                                             base::Unretained(this))),
      browser_(browser) {
  SetPreferredSize(gfx::Size(kActionSize, kActionSize));
  SetMinimumImageSize(gfx::Size(kShieldIconSize, kShieldIconSize));
  SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, kButtonInset)));
  views::InstallRoundRectHighlightPathGenerator(
      this, gfx::Insets(), kSidebarMetrics.shell_control_corner_radius);

  auto badge = std::make_unique<views::Label>();
  badge->SetFontList(
      badge->font_list().DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  // 글자 대비는 실제 chip 배경과 짝을 이룬다. Label의 자동 가독성 보정이
  // 이 색 조합을 덮어쓰지 않게 한다.
  badge->SetAutoColorReadabilityEnabled(false);
  badge->SetSubpixelRenderingEnabled(false);
  badge->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 4)));
  badge->SetCanProcessEventsWithinSubtree(false);
  badge->GetViewAccessibility().SetIsIgnored(true);
  badge->SetVisible(false);
  badge_ = AddChildView(std::move(badge));
  if (browser_) {
    if (auto* service =
            yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
                browser_->GetProfile())) {
      service_subscription_ = service->AddChangedCallback(base::BindRepeating(
          &SiteControlsButton::RefreshVisualState, base::Unretained(this)));
    }
  }
  RefreshVisualState();
}

SiteControlsButton::~SiteControlsButton() = default;

void SiteControlsButton::Update(content::WebContents* contents,
                                bool hide_for_omnibox_input) {
  if (web_contents() != contents) {
    helper_subscription_ = base::CallbackListSubscription();
    Observe(contents);
    if (contents) {
      yee::content_blocking::ContentBlockingTabHelper::CreateForWebContents(
          contents);
      auto* helper =
          yee::content_blocking::ContentBlockingTabHelper::FromWebContents(
              contents);
      helper_subscription_ = helper->AddChangedCallback(base::BindRepeating(
          &SiteControlsButton::OnBlockingStateChanged, base::Unretained(this)));
    }
  }
  const bool can_show = contents &&
                        contents->GetVisibleURL().SchemeIsHTTPOrHTTPS() &&
                        !hide_for_omnibox_input;
  SetVisible(can_show);
  OnBlockingStateChanged();
}

void SiteControlsButton::SetSurfacePresentation(
    std::optional<BrowserSurfacePresentation> presentation) {
  if (presentation_ == presentation) {
    return;
  }
  presentation_ = std::move(presentation);
  RefreshVisualState();
}

bool SiteControlsButton::OnMousePressed(const ui::MouseEvent& event) {
  // 마우스를 떼기 전에 포커스를 잃어 팝업이 닫힐 수 있다.
  // 누를 때의 의도를 유지해 손을 뗄 때 다시 열리지 않게 한다.
  bubble_was_open_on_mouse_press_ =
      g_site_controls_bubble &&
      g_site_controls_bubble->GetAnchorView() == this;
  return views::ImageButton::OnMousePressed(event);
}

void SiteControlsButton::OnPressed(const ui::Event& event) {
  const bool was_open_on_press =
      std::exchange(bubble_was_open_on_mouse_press_, false);
  if (g_site_controls_bubble &&
      g_site_controls_bubble->GetAnchorView() == this) {
    CloseSiteControlsBubble();
    return;
  }
  if (event.IsMouseEvent() && was_open_on_press) {
    return;
  }
  if (web_contents() && browser_) {
    ShowSiteControlsBubble(this, browser_, web_contents());
  }
}

void SiteControlsButton::OnBlockingStateChanged() {
  auto* helper =
      web_contents()
          ? yee::content_blocking::ContentBlockingTabHelper::FromWebContents(
                web_contents())
          : nullptr;
  blocked_count_ = helper ? helper->blocked_count() : 0;
  RefreshVisualState();
}

void SiteControlsButton::RefreshVisualState() {
  bool enabled = false;
  if (web_contents() && browser_) {
    if (auto* service =
            yee::content_blocking::ContentBlockingServiceFactory::GetForProfile(
                browser_->GetProfile())) {
      enabled = service->EnabledForSite(web_contents()->GetVisibleURL());
    }
  }
  const bool custom_surface =
      presentation_ && GetNativeTheme() &&
      !ShouldUseNativeBrowserSurfaceColors(*GetNativeTheme()) &&
      presentation_->palette_mode ==
          BrowserSurfacePresentation::PaletteMode::kCustomSurface;
  SkColor color =
      custom_surface
          ? presentation_->primary
          : (GetColorProvider() ? GetColorProvider()->GetColor(ui::kColorIcon)
                                : gfx::kPlaceholderColor);
  if (GetColorProvider()) {
    SkColor background =
        GetColorProvider()->GetColor(ui::kColorSysSurfaceVariant);
    if (custom_surface) {
      background =
          GetState() == STATE_PRESSED
              ? presentation_->popup_selected
              : (GetState() == STATE_HOVERED ? presentation_->popup_hover
                                             : presentation_->location_hover);
    }
    const SkColor foreground = color_utils::BlendForMinContrast(
                                   color, background, std::nullopt,
                                   color_utils::kMinimumReadableContrastRatio)
                                   .color;
    badge_->SetBackgroundColor(background);
    badge_->SetEnabledColor(foreground);
    badge_->SetBackground(views::CreateRoundedRectBackground(background, 4));
    if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
      SetBackground(views::CreateRoundedRectBackground(
          background, kSidebarMetrics.shell_control_corner_radius));
    } else {
      SetBackground(nullptr);
    }
    views::FocusRing::Get(this)->SetColorId(
        custom_surface ? std::make_optional(ResolveBrowserSurfaceFocusColorId(
                             *GetColorProvider(), presentation_->surface,
                             ui::kColorFocusableBorderFocused))
                       : std::nullopt);
  }
  if (!enabled) {
    color =
        custom_surface
            ? presentation_->secondary
            : (GetColorProvider()
                   ? GetColorProvider()->GetColor(ui::kColorSysOnSurfaceVariant)
                   : color);
  }
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(vector_icons::kShieldIcon, color,
                                               kShieldIconSize));
  const bool show_badge = enabled && blocked_count_ > 0;
  badge_->SetVisible(show_badge);
  std::u16string accessible_name = branding::ProductName() + u" site controls";
  if (show_badge) {
    badge_->SetText(
        blocked_count_ > 99 ? u"99+" : base::NumberToString16(blocked_count_));
    accessible_name += u", " + base::NumberToString16(blocked_count_) +
                       u" network requests blocked";
  }
  SetImageHorizontalAlignment(show_badge ? views::ImageButton::ALIGN_LEFT
                                         : views::ImageButton::ALIGN_CENTER);
  SetPreferredSize(gfx::Size(show_badge ? kButtonInset * 2 + kShieldIconSize +
                                              kBadgeGap +
                                              badge_->GetPreferredSize().width()
                                        : kActionSize,
                             kActionSize));
  SetTooltipText(accessible_name);
  GetViewAccessibility().SetName(accessible_name);
  InvalidateLayout();
  SchedulePaint();
}

void SiteControlsButton::StateChanged(ButtonState old_state) {
  views::ImageButton::StateChanged(old_state);
  RefreshVisualState();
}

void SiteControlsButton::Layout(PassKey) {
  LayoutSuperclass<views::ImageButton>(this);
  if (!badge_->GetVisible()) {
    return;
  }
  gfx::Size badge_size = badge_->GetPreferredSize();
  badge_size.set_height(kBadgeHeight);
  badge_->SetBounds(std::max(0, width() - kButtonInset - badge_size.width()),
                    (height() - badge_size.height()) / 2, badge_size.width(),
                    badge_size.height());
}

void SiteControlsButton::OnThemeChanged() {
  views::ImageButton::OnThemeChanged();
  RefreshVisualState();
}

void SiteControlsButton::WebContentsDestroyed() {
  // The toolbar outlives its last tab during window teardown. Color updates
  // can still arrive while BrowserView is being removed from its widget.
  helper_subscription_ = base::CallbackListSubscription();
  Observe(nullptr);
  blocked_count_ = 0;
  SetVisible(false);
  RefreshVisualState();
}

bool ShowSiteControlsBubble(views::View* anchor,
                            Browser* browser,
                            content::WebContents* contents) {
  if (!anchor || !browser || !contents) {
    return false;
  }
  if (!contents->GetVisibleURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  CloseSiteControlsBubble();
  auto* bubble = new SiteControlsBubble(anchor, browser, contents);
  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
  return true;
}

void CloseSiteControlsBubble() {
  if (g_site_controls_bubble) {
    g_site_controls_bubble->GetWidget()->CloseNow();
  }
}

bool IsSiteControlsBubbleShowing() {
  return g_site_controls_bubble != nullptr;
}

BEGIN_METADATA(SiteControlsButton)
END_METADATA

}  // namespace yee
