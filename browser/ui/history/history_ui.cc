// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/history/history_ui.h"

#include <memory>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"
#include "chrome/browser/ui/webui/page_not_available_for_guest/page_not_available_for_guest_ui.h"
#include "chrome/common/url_constants.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace yee {
HistoryUIConfig::HistoryUIConfig()
    : WebUIConfig(content::kChromeUIScheme, branding::kProductHistoryHost) {}

std::unique_ptr<content::WebUIController>
HistoryUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                       const GURL& url) {
  if (Profile::FromWebUI(web_ui)->IsGuestSession()) {
    return std::make_unique<PageNotAvailableForGuestUI>(
        web_ui, branding::kProductHistoryHost);
  }
  return std::make_unique<HistoryUI>(web_ui);
}

HistoryUI::HistoryUI(content::WebUI* web_ui) : ::HistoryUI(web_ui) {
  auto* source = CreatePageDataSource(web_ui, branding::kProductHistoryHost,
                                      IDS_YEE_WEBUI_HISTORY_TITLE);
  source->AddBoolean("allowDeletingHistory",
                     Profile::FromWebUI(web_ui)->GetPrefs()->GetBoolean(
                         prefs::kAllowDeletingBrowserHistory));
  source->AddString("historyActivityUrl", chrome::kMyActivityUrlInHistory);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src 'self' chrome://resources chrome://theme chrome://favicon2 "
      "data:;");
}
HistoryUI::~HistoryUI() = default;
}  // namespace yee
