// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/extensions/extensions_ui.h"

#include <memory>

#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"
#include "chrome/browser/ui/webui/page_not_available_for_guest/page_not_available_for_guest_ui.h"
#include "chrome/grit/generated_resources.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "extensions/common/extension_features.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/l10n/l10n_util.h"

namespace yee {
ExtensionsUIConfig::ExtensionsUIConfig()
    : WebUIConfig(content::kChromeUIScheme, branding::kProductExtensionsHost) {}

std::unique_ptr<content::WebUIController>
ExtensionsUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                         const GURL& url) {
  auto* profile = Profile::FromWebUI(web_ui);
  if (profile->IsGuestSession()) {
    auto controller = std::make_unique<PageNotAvailableForGuestUI>(
        web_ui, branding::kProductExtensionsHost);
    // 공통 guest controller는 Chromium의 원래 host만 알므로 탐색 전에 기존
    // data source에 제품 host를 반영한다.
    const auto title = l10n_util::GetStringUTF16(IDS_YEE_WEBUI_EXTENSIONS_TITLE);
    base::DictValue strings;
    strings.Set("pageTitle", base::UTF16ToUTF8(title));
    strings.Set("pageHeading", base::UTF16ToUTF8(l10n_util::GetStringFUTF16(
                                   IDS_PAGE_NOT_AVAILABLE_FOR_GUEST_HEADING,
                                   title)));
    content::WebUIDataSource::Update(profile, branding::kProductExtensionsHost,
                                     strings);
    return controller;
  }
  return std::make_unique<ExtensionsUI>(web_ui);
}

ExtensionsUI::ExtensionsUI(content::WebUI* web_ui)
    : extensions::ExtensionsUI(web_ui) {
  auto* source = CreatePageDataSource(web_ui, branding::kProductExtensionsHost,
                                      IDS_YEE_WEBUI_EXTENSIONS_TITLE);
  source->AddBoolean(
      "enableEnhancedSiteControls",
      base::FeatureList::IsEnabled(
          extensions_features::kExtensionsMenuAccessControl));
  source->AddBoolean(
      "enableUserPermittedSites",
      base::FeatureList::IsEnabled(
          extensions_features::kExtensionsMenuAccessControlWithPermittedSites));
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' chrome://resources chrome://theme chrome://favicon2 "
      "data:;");
}

ExtensionsUI::~ExtensionsUI() = default;
}  // namespace yee
