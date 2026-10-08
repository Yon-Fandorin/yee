// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/bookmarks/bookmarks_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"
#include "chrome/browser/ui/webui/page_not_available_for_guest/page_not_available_for_guest_ui.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace yee {
BookmarksUIConfig::BookmarksUIConfig()
    : WebUIConfig(content::kChromeUIScheme, branding::kProductBookmarksHost) {}

std::unique_ptr<content::WebUIController>
BookmarksUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                         const GURL& url) {
  if (Profile::FromWebUI(web_ui)->IsGuestSession()) {
    return std::make_unique<PageNotAvailableForGuestUI>(
        web_ui, branding::kProductBookmarksHost);
  }
  return std::make_unique<BookmarksUI>(web_ui);
}

BookmarksUI::BookmarksUI(content::WebUI* web_ui) : ::BookmarksUI(web_ui) {
  auto* source = CreatePageDataSource(web_ui, branding::kProductBookmarksHost,
                                      IDS_YEE_WEBUI_BOOKMARKS_TITLE);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' chrome://resources chrome://theme chrome://favicon2 "
      "data:;");
}

BookmarksUI::~BookmarksUI() = default;
}  // namespace yee
