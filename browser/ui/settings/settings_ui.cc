// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/settings/settings_ui.h"

#include "chrome/browser/ui/views/yee/settings/content_blocking_settings_handler.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"
#include "chrome/browser/yee_content_blocking/blocked_domains.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/url_constants.h"

namespace yee {
SettingsUIConfig::SettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         branding::kProductSettingsHost) {}

SettingsUI::SettingsUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  auto* source = CreatePageDataSource(
      web_ui, branding::kProductSettingsHost, IDS_YEE_WEBUI_SETTINGS_TITLE,
      {"content-blocking", "content-blocking/"});
  source->AddInteger("domainImportMaxBytes",
                     content_blocking::kMaxDomainImportBytes);
  web_ui->AddMessageHandler(CreateContentBlockingSettingsHandler());
}
SettingsUI::~SettingsUI() = default;
}  // namespace yee
