// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/downloads/downloads_ui.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"

namespace yee {
DownloadsUIConfig::DownloadsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         branding::kProductDownloadsHost) {}

DownloadsUI::DownloadsUI(content::WebUI* web_ui) : ::DownloadsUI(web_ui) {
  auto* source = CreatePageDataSource(web_ui, branding::kProductDownloadsHost,
                                      IDS_YEE_WEBUI_DOWNLOADS_TITLE);
  const auto* profile = Profile::FromWebUI(web_ui);
  source->AddBoolean(
      "allowDeletingHistory",
      profile->GetPrefs()->GetBoolean(prefs::kAllowDeletingBrowserHistory) &&
          !profile->IsChild());
}
DownloadsUI::~DownloadsUI() = default;
}  // namespace yee
