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
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/views/location_bar/location_bar_bubble_delegate_view.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "components/security_state/content/security_state_tab_helper.h"
#include "components/security_state/core/security_state.h"
#include "components/url_formatter/elide_url.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_id.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
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
constexpr int kBadgeHeight = 14;

class SiteControlsBubble;
SiteControlsBubble* g_site_controls_bubble = nullptr;

std::u16string SecuritySummary(content::WebContents* contents) {
  auto* helper = SecurityStateTabHelper::FromWebContents(contents);
  if (!helper) {
    return u"Connection information unavailable";
  }
  switch (helper->GetSecurityLevel()) {
    case security_state::SECURE:
      return u"Connection is secure";
    case security_state::DANGEROUS:
      return u"Connection is not secure";
    case security_state::WARNING:
      return u"Connection needs attention";
    case security_state::NONE:
    case security_state::SECURITY_LEVEL_COUNT:
      return u"Connection information unavailable";
  }
}

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
                     content::WebContents* contents,
                     SiteControlsSection initial_section)
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

    auto tabbed_pane = std::make_unique<views::TabbedPane>(
        views::TabbedPane::Orientation::kHorizontal,
        views::TabbedPane::TabStripStyle::kWithIcon);
    tabs_ = tabbed_pane.get();
    tabs_->SetDrawTabDivider(true);
    tabs_->AddTab(u"Protection", CreateProtectionSection(),
                  &vector_icons::kShieldIcon);
    tabs_->AddTab(u"Page info", CreatePageInfoSection(),
                  &vector_icons::kInfoIcon);
    tabs_->SelectTabAt(
        initial_section == SiteControlsSection::kProtection ? 0 : 1, false);
    AddChildView(std::move(tabbed_pane));

    yee::content_blocking::ContentBlockingTabHelper::CreateForWebContents(
        contents_);
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

  std::unique_ptr<views::View> CreatePageInfoSection() {
    auto section = CreateSection();

    auto security = std::make_unique<views::Label>(SecuritySummary(contents_));
    security->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    security->SetTextStyle(views::style::STYLE_HEADLINE_5);
    section->AddChildView(std::move(security));

    section->AddChildView(
        CreateBodyLabel(url_formatter::FormatUrlForSecurityDisplay(url_)));

    auto settings_button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(&SiteControlsBubble::OpenSiteSettings,
                            base::Unretained(this)),
        u"Site settings");
    settings_button->SetStyle(ui::ButtonStyle::kDefault);
    settings_button->SetProperty(views::kCrossAxisAlignmentKey,
                                 views::LayoutAlignment::kStart);
    section->AddChildView(std::move(settings_button));
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

  void OpenSiteSettings() {
    GetWidget()->Close();
    chrome::ShowSiteSettings(browser_, url_);
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
  raw_ptr<views::TabbedPane> tabs_ = nullptr;
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
  SetTooltipText(u"Yee site controls");
  GetViewAccessibility().SetName(u"Yee site controls");
  SetPreferredSize(gfx::Size(kActionSize, kActionSize));
  SetMinimumImageSize(gfx::Size(kShieldIconSize, kShieldIconSize));
  SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                kActionSize / 2);

  auto badge = std::make_unique<views::Label>();
  badge->SetTextStyle(views::style::STYLE_BODY_5_EMPHASIS);
  badge->SetEnabledColor(SK_ColorWHITE);
  badge->SetBackground(views::CreateRoundedRectBackground(ui::kColorSysPrimary,
                                                          kBadgeHeight / 2));
  badge->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 4)));
  badge->SetCanProcessEventsWithinSubtree(false);
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

void SiteControlsButton::SetIconColor(std::optional<SkColor> color) {
  icon_color_ = color;
  RefreshVisualState();
}

void SiteControlsButton::OnPressed() {
  if (web_contents() && browser_) {
    ShowSiteControlsBubble(this, browser_, web_contents(),
                           SiteControlsSection::kProtection);
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
  SkColor color = icon_color_.value_or(
      GetColorProvider() ? GetColorProvider()->GetColor(ui::kColorIcon)
                         : gfx::kPlaceholderColor);
  if (!enabled) {
    color = SkColorSetA(color, 0x66);
  }
  SetImageModel(views::Button::STATE_NORMAL,
                ui::ImageModel::FromVectorIcon(vector_icons::kShieldIcon, color,
                                               kShieldIconSize));
  const bool show_badge = enabled && blocked_count_ > 0;
  badge_->SetVisible(show_badge);
  std::u16string accessible_name = u"Yee site controls";
  if (show_badge) {
    badge_->SetText(
        blocked_count_ > 99 ? u"99+" : base::NumberToString16(blocked_count_));
    accessible_name += u", " + base::NumberToString16(blocked_count_) +
                       u" network requests blocked";
  }
  SetTooltipText(accessible_name);
  GetViewAccessibility().SetName(accessible_name);
  InvalidateLayout();
  SchedulePaint();
}

void SiteControlsButton::Layout(PassKey) {
  LayoutSuperclass<views::ImageButton>(this);
  if (!badge_->GetVisible()) {
    return;
  }
  gfx::Size badge_size = badge_->GetPreferredSize();
  badge_size.set_height(kBadgeHeight);
  badge_->SetBounds(std::max(0, width() - badge_size.width()), 0,
                    badge_size.width(), badge_size.height());
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
                            content::WebContents* contents,
                            SiteControlsSection initial_section) {
  if (!anchor || !browser || !contents) {
    return false;
  }
  if (!contents->GetVisibleURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  if (g_site_controls_bubble) {
    g_site_controls_bubble->GetWidget()->CloseNow();
  }
  auto* bubble =
      new SiteControlsBubble(anchor, browser, contents, initial_section);
  views::BubbleDialogDelegateView::CreateBubble(bubble);
  bubble->ShowForReason(LocationBarBubbleDelegateView::USER_GESTURE);
  return true;
}

bool IsSiteControlsBubbleShowing() {
  return g_site_controls_bubble != nullptr;
}

BEGIN_METADATA(SiteControlsButton)
END_METADATA

}  // namespace yee
