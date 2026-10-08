// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/webui/webui_resources.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/webui/frontend_resources.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/template_expressions.h"
#include "ui/base/webui/web_ui_util.h"

namespace yee {
namespace {
#include "chrome/browser/ui/views/yee/webui/webui_string_map.inc"

std::optional<std::string_view> ResourceForPath(
    std::string_view path,
    const std::vector<std::string>& document_paths) {
  path = path.substr(0, path.find('?'));
  if (path.empty() || std::find(document_paths.begin(), document_paths.end(),
                                path) != document_paths.end()) {
    path = "index.html";
  }
  for (const auto& resource : webui_resources::kResources) {
    if (resource.path == path) {
      return resource.response;
    }
  }
  return std::nullopt;
}

}  // namespace

content::WebUIDataSource* CreatePageDataSource(
    content::WebUI* web_ui,
    std::string_view host,
    int title_id,
    std::vector<std::string> document_paths) {
  auto* source = content::WebUIDataSource::CreateAndAdd(
      Profile::FromWebUI(web_ui), std::string(host));
  // Svelte uses this policy for compiler-generated static HTML templates.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types svelte-trusted-html;");
  const auto& locale = g_browser_process->GetApplicationLocale();
  ui::TemplateReplacements replacements;
  webui::SetLoadTimeDataDefaults(locale, &replacements);
  replacements["documentTitle"] = l10n_util::GetStringUTF8(title_id);
  const auto html = base::MakeRefCounted<base::RefCountedString>(
      ui::ReplaceTemplateExpressions(
          ResourceForPath("", document_paths).value(), replacements));
  for (const auto& resource : webui_resources::kResources) {
    source->SetResourcePathToResponse(resource.path,
                                      resource.path == "index.html"
                                          ? std::string_view(html->as_string())
                                          : resource.response);
  }
  source->SetResourcePathToResponse("", std::string_view(html->as_string()));
  for (const auto& path : document_paths) {
    source->SetResourcePathToResponse(path,
                                      std::string_view(html->as_string()));
  }
  // Main-document navigation and configurations without the renderer's local
  // resource loader also use the normal data-source request path.
  source->SetRequestFilter(
      base::BindRepeating(
          [](const std::vector<std::string>& document_paths,
             const std::string& path) {
            return ResourceForPath(path, document_paths).has_value();
          },
          document_paths),
      base::BindRepeating(
          [](scoped_refptr<base::RefCountedString> html,
             const std::vector<std::string>& document_paths,
             const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            const auto resource = ResourceForPath(path, document_paths).value();
            if (resource.data() ==
                ResourceForPath("", document_paths).value().data()) {
              std::move(callback).Run(html);
            } else {
              std::move(callback).Run(
                  base::MakeRefCounted<base::RefCountedString>(
                      std::string(resource)));
            }
          },
          html, document_paths));
  source->AddLocalizedStrings(kWebUIStrings);
  source->AddString("productName", version_info::GetProductName());
  source->AddString("nativeSettingsUrl", "chrome://settings/");
  source->AddString("applicationLocale", locale);
  source->UseStringsJs();
  return source;
}
}  // namespace yee
