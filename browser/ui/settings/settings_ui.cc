// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/settings/settings_ui.h"
#include "chrome/browser/ui/views/yee/settings/content_blocking_settings_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/settings/grit/settings_strings.h"
#include "chrome/browser/ui/views/yee/settings/settings_resources.h"
#include "chrome/browser/yee_content_blocking/blocked_domains.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/url_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/template_expressions.h"
#include "ui/base/webui/web_ui_util.h"

namespace yee {
namespace {
#include "chrome/browser/ui/views/yee/settings/settings_string_map.inc"

std::optional<std::string_view> ResourceForPath(std::string_view path) {
  path = path.substr(0, path.find('?'));
  if (path.empty() || path == "content-blocking" ||
      path == "content-blocking/") {
    path = "index.html";
  }
  for (const auto& resource : settings_resources::kResources) {
    if (resource.path == path) {
      return resource.response;
    }
  }
  return std::nullopt;
}

}  // namespace

SettingsUIConfig::SettingsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         branding::kProductSettingsHost) {}

SettingsUI::SettingsUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  auto* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), branding::kProductSettingsHost);
  const auto& locale = g_browser_process->GetApplicationLocale();
  ui::TemplateReplacements replacements;
  webui::SetLoadTimeDataDefaults(locale, &replacements);
  replacements["settingsTitle"] =
      l10n_util::GetStringUTF8(IDS_YEE_SETTINGS_SETTINGS_TITLE);
  const auto html = base::MakeRefCounted<base::RefCountedString>(
      ui::ReplaceTemplateExpressions(ResourceForPath("").value(),
                                     replacements));
  for (const auto& resource : settings_resources::kResources) {
    source->SetResourcePathToResponse(resource.path,
                                      resource.path == "index.html"
                                          ? std::string_view(html->as_string())
                                          : resource.response);
  }
  source->SetResourcePathToResponse("", std::string_view(html->as_string()));
  source->SetResourcePathToResponse("content-blocking",
                                    std::string_view(html->as_string()));
  source->SetResourcePathToResponse("content-blocking/",
                                    std::string_view(html->as_string()));
  // Main-document navigation and configurations without the renderer's local
  // resource loader also use the normal data-source request path.
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) {
        return ResourceForPath(path).has_value();
      }),
      base::BindRepeating(
          [](scoped_refptr<base::RefCountedString> html,
             const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            const auto resource = ResourceForPath(path).value();
            if (resource.data() == ResourceForPath("").value().data()) {
              std::move(callback).Run(html);
            } else {
              std::move(callback).Run(
                  base::MakeRefCounted<base::RefCountedString>(
                      std::string(resource)));
            }
          },
          html));
  source->AddLocalizedStrings(kSettingsStrings);
  source->AddInteger("domainImportMaxBytes",
                     content_blocking::kMaxDomainImportBytes);
  source->AddString("productName", version_info::GetProductName());
  source->AddString("nativeSettingsUrl", "chrome://settings/");
  source->AddString("applicationLocale", locale);
  source->UseStringsJs();
  web_ui->AddMessageHandler(CreateContentBlockingSettingsHandler());
}
SettingsUI::~SettingsUI() = default;
}  // namespace yee
