// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_page_button.h"

#include "base/functional/bind.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/gfx/font.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/style/typography.h"
#include "ui/views/style/typography_provider.h"

namespace yee::projects {

ProjectPageButton::ProjectPageButton(Profile* profile,
                                     const ProjectPageItem& page,
                                     base::RepeatingClosure open,
                                     base::RepeatingClosure close,
                                     PageMenuCallback menu)
    : ProjectRowButton(std::move(open), page.title),
      close_(std::move(close)),
      menu_(std::move(menu)) {
  SetStyle(ui::ButtonStyle::kText);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  SetBgColorOverrideDeprecated(SK_ColorTRANSPARENT);
  SetEnabledTextColors(ui::kColorSysOnSurface);
  SetCornerRadius(kSidebarMetrics.tab_row_corner_radius);
  SetCustomPadding(gfx::Insets::VH(4, 8));
  SetMinSize(gfx::Size());
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetImageLabelSpacing(8);
  SetTooltipText(page.title + u"\n" + page.host);
  GetViewAccessibility().SetIsSelected(page.active);
  label()->SetFontList(
      views::TypographyProvider::Get()
          .GetFont(views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY)
          .Derive(kSidebarMetrics.tab_title_font_delta, gfx::Font::NORMAL,
                  gfx::Font::Weight::MEDIUM));
  label()->SetLineHeight(kSidebarMetrics.tab_title_line_height);
  SetFavicon({});
  if (page.tab) {
    Observe(page.tab->GetContents());
    if (auto* driver =
            favicon::ContentFaviconDriver::FromWebContents(web_contents())) {
      favicon_observation_.Observe(driver);
      const auto image = driver->GetFavicon();
      live_icon_received_ = !image.IsEmpty();
      SetFavicon(image);
    }
  }
  if (menu_)
    set_context_menu_controller(this);
  if (auto* service = FaviconServiceFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS)) {
    service->GetFaviconImageForPageURL(
        page.url,
        base::BindOnce(
            [](base::WeakPtr<ProjectPageButton> button,
               const favicon_base::FaviconImageResult& result) {
              if (button && !button->live_icon_received_ &&
                  !result.image.IsEmpty())
                button->SetFavicon(result.image);
            },
            weak_factory_.GetWeakPtr()),
        &favicon_tasks_);
  }
}

ProjectPageButton::~ProjectPageButton() = default;

void ProjectPageButton::SetFavicon(const gfx::Image& image) {
  SetImageModel(STATE_NORMAL, image.IsEmpty()
                                  ? favicon::GetDefaultFaviconModel()
                                  : ui::ImageModel::FromImage(image));
}

void ProjectPageButton::OnFaviconUpdated(favicon::FaviconDriver* driver,
                                         NotificationIconType type,
                                         const GURL& icon_url,
                                         bool icon_url_changed,
                                         const gfx::Image& image) {
  if (type == NON_TOUCH_16_DIP) {
    live_icon_received_ = true;
    SetFavicon(image);
  }
}

void ProjectPageButton::WebContentsDestroyed() {
  favicon_observation_.Reset();
  Observe(nullptr);
}

bool ProjectPageButton::OnMousePressed(const ui::MouseEvent& event) {
  if (event.IsMiddleMouseButton() && close_)
    return true;
  return MdTextButton::OnMousePressed(event);
}

void ProjectPageButton::OnMouseReleased(const ui::MouseEvent& event) {
  if (event.IsMiddleMouseButton() && HitTestPoint(event.location()) && close_) {
    auto close = close_;
    close.Run();
    return;
  }
  MdTextButton::OnMouseReleased(event);
}

void ProjectPageButton::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType type) {
  if (menu_)
    menu_.Run(source, point, type);
}

BEGIN_METADATA(ProjectPageButton)
END_METADATA

}  // namespace yee::projects
