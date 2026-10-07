// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/settings/settings_ui.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/callback_list.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/yee/settings/grit/settings_strings.h"
#include "chrome/browser/ui/views/yee/settings/settings_resources.h"
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "components/yee_branding/internal_urls.h"
#include "components/yee_content_blocking/settings.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/common/url_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/template_expressions.h"
#include "ui/base/webui/web_ui_util.h"

namespace yee {
namespace {
using content_blocking::BaselineListUpdater;
using content_blocking::BaselineListUpdateStatus;
using content_blocking::DomainImport;
using content_blocking::DomainImportFormat;
using content_blocking::DomainImportRowError;

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

class SettingsHandler : public content::WebUIMessageHandler {
 public:
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "getContentBlockingState",
        base::BindRepeating(&SettingsHandler::GetState,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "setContentBlockingException",
        base::BindRepeating(&SettingsHandler::SetException,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "checkContentBlockingLists",
        base::BindRepeating(&SettingsHandler::CheckLists,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "addBlockedDomain", base::BindRepeating(&SettingsHandler::SetDomain,
                                                base::Unretained(this), false));
    web_ui()->RegisterMessageCallback(
        "setBlockedDomain", base::BindRepeating(&SettingsHandler::SetDomain,
                                                base::Unretained(this), true));
    web_ui()->RegisterMessageCallback(
        "removeBlockedDomain",
        base::BindRepeating(&SettingsHandler::RemoveDomain,
                            base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "previewBlockedDomains",
        base::BindRepeating(&SettingsHandler::ReadDomainFile,
                            base::Unretained(this), false));
    web_ui()->RegisterMessageCallback(
        "importBlockedDomains",
        base::BindRepeating(&SettingsHandler::ReadDomainFile,
                            base::Unretained(this), true));
  }

 private:
  Profile* profile() { return Profile::FromWebUI(web_ui()); }
  content_blocking::ContentBlockingService* service() {
    return content_blocking::ContentBlockingServiceFactory::GetForProfile(
        profile());
  }

  void OnJavascriptAllowed() override {
    update_subscription_ = BaselineListUpdater::AddChangedCallback(
        base::BindRepeating(&SettingsHandler::Changed, base::Unretained(this)));
    if (auto* blocking = service()) {
      subscription_ = blocking->AddChangedCallback(base::BindRepeating(
          &SettingsHandler::Changed, base::Unretained(this)));
    }
  }
  void OnJavascriptDisallowed() override {
    subscription_ = {};
    update_subscription_ = {};
    weak_factory_.InvalidateWeakPtrs();
  }
  void Changed() {
    if (IsJavascriptAllowed()) {
      FireWebUIListener("content-blocking-settings-changed");
    }
  }

  bool CallbackValid(const base::ListValue& args, size_t expected) {
    return args.size() == expected && args[0].is_string();
  }
  void GetState(const base::ListValue& args) {
    if (!CallbackValid(args, 1)) {
      return;
    }
    AllowJavascript();
    SendState(args[0].GetString());
  }
  void SendState(std::string callback_id,
                 std::optional<bool> updated = std::nullopt) {
    BaselineListUpdater::GetStatus(base::BindOnce(
        &SettingsHandler::ResolveState, weak_factory_.GetWeakPtr(),
        std::move(callback_id), updated));
  }
  void ResolveState(std::string callback_id,
                    std::optional<bool> updated,
                    BaselineListUpdateStatus status) {
    if (!IsJavascriptAllowed()) {
      return;
    }
    base::ListValue sites;
    base::ListValue domains;
    if (auto* blocking = service()) {
      for (const auto& host : blocking->DisabledHosts()) {
        sites.Append(host);
      }
      for (const auto& rule : blocking->BlockedDomains()) {
        domains.Append(base::DictValue()
                           .Set("domain", rule.domain)
                           .Set("includeSubdomains", rule.include_subdomains));
      }
    }
    const bool private_profile =
        profile()->IsOffTheRecord() || profile()->IsGuestSession();
    auto state =
        base::DictValue()
            .Set("enabled", base::FeatureList::IsEnabled(
                                content_blocking::kYeeContentBlocking))
            .Set("privateProfile", private_profile)
            .Set("exceptions", std::move(sites))
            .Set("blockedDomains", std::move(domains))
            .Set("updatesAvailable", status.available && !private_profile)
            .Set("updating", status.in_flight)
            .Set("downloaded", status.downloaded)
            .Set("runningDownloaded", status.running_downloaded)
            .Set("pendingRestart", status.pending_restart)
            .Set("recovered", status.recovered)
            .Set("checkedAt",
                 status.checked_at.is_null()
                     ? 0.0
                     : status.checked_at.InMillisecondsFSinceUnixEpoch());
    if (updated.has_value()) {
      state.Set("updateSucceeded", *updated);
    }
    ResolveJavascriptCallback(base::Value(callback_id), state);
  }
  void SetException(const base::ListValue& args) {
    if (!CallbackValid(args, 3) || !args[1].is_string() || !args[2].is_bool()) {
      return;
    }
    AllowJavascript();
    const auto& input = args[1].GetString();
    const GURL site(input.find("://") == std::string::npos ? "https://" + input
                                                           : input);
    if (input.size() > 2048 || !site.is_valid() ||
        !site.SchemeIsHTTPOrHTTPS() || site.host().empty() ||
        !site.username().empty() || !site.password().empty()) {
      RejectJavascriptCallback(args[0], base::Value("invalid-site"));
      return;
    }
    auto* blocking = service();
    if (!blocking) {
      RejectJavascriptCallback(args[0], base::Value("service-unavailable"));
      return;
    }
    blocking->SetEnabledForSite(site, args[2].GetBool());
    SendState(args[0].GetString());
  }
  void CheckLists(const base::ListValue& args) {
    if (!CallbackValid(args, 1)) {
      return;
    }
    AllowJavascript();
    if (profile()->IsOffTheRecord() || profile()->IsGuestSession() ||
        !service() ||
        !BaselineListUpdater::RequestUpdate(
            base::BindOnce(&SettingsHandler::ListsChecked,
                           weak_factory_.GetWeakPtr(), args[0].GetString()))) {
      RejectJavascriptCallback(args[0], base::Value("updates-unavailable"));
    }
  }
  bool CanChangeDomains(const base::Value& callback_id) {
    if (!service() ||
        !base::FeatureList::IsEnabled(content_blocking::kYeeContentBlocking)) {
      RejectJavascriptCallback(callback_id, base::Value("service-unavailable"));
      return false;
    }
    return true;
  }
  void SetDomain(bool replace, const base::ListValue& args) {
    if (!CallbackValid(args, 3) || !args[1].is_string() || !args[2].is_bool()) {
      return;
    }
    AllowJavascript();
    if (!CanChangeDomains(args[0])) {
      return;
    }
    const auto domain =
        content_blocking::CanonicalBlockedDomain(args[1].GetString());
    if (!domain) {
      RejectJavascriptCallback(args[0], base::Value("invalid-domain"));
      return;
    }
    const bool exists = std::ranges::any_of(
        service()->BlockedDomains(),
        [&](const auto& rule) { return rule.domain == *domain; });
    if (exists != replace) {
      RejectJavascriptCallback(
          args[0], base::Value(exists ? "duplicate-domain" : "domain-missing"));
    } else if (!service()->SetBlockedDomain(*domain, args[2].GetBool())) {
      RejectJavascriptCallback(args[0], base::Value("too-many-domains"));
    } else {
      SendState(args[0].GetString());
    }
  }
  void RemoveDomain(const base::ListValue& args) {
    if (!CallbackValid(args, 2) || !args[1].is_string()) {
      return;
    }
    AllowJavascript();
    if (!CanChangeDomains(args[0])) {
      return;
    }
    service()->RemoveBlockedDomain(args[1].GetString());
    SendState(args[0].GetString());
  }
  void ReadDomainFile(bool apply, const base::ListValue& args) {
    if (!CallbackValid(args, 3) || !args[1].is_string() ||
        !args[2].is_string()) {
      return;
    }
    AllowJavascript();
    if (!CanChangeDomains(args[0])) {
      return;
    }
    const auto& format = args[1].GetString();
    if (format != "csv" && format != "txt") {
      RejectJavascriptCallback(args[0], base::Value("invalid-file"));
      return;
    }
    if (args[2].GetString().size() > content_blocking::kMaxDomainImportBytes) {
      RejectJavascriptCallback(args[0], base::Value("file-too-large"));
      return;
    }
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](std::string text, DomainImportFormat format) {
              return content_blocking::ParseDomainImport(text, format);
            },
            args[2].GetString(),
            format == "csv" ? DomainImportFormat::kCsv
                            : DomainImportFormat::kText),
        base::BindOnce(&SettingsHandler::DomainFileParsed,
                       weak_factory_.GetWeakPtr(), args[0].GetString(), apply));
  }
  void DomainFileParsed(std::string callback_id,
                        bool apply,
                        DomainImport parsed) {
    if (!IsJavascriptAllowed() || !CanChangeDomains(base::Value(callback_id))) {
      return;
    }
    if (!parsed.error.empty()) {
      RejectJavascriptCallback(base::Value(callback_id),
                               base::Value(parsed.error));
      return;
    }
    if (apply) {
      std::vector<content_blocking::BlockedDomain> rules;
      for (const auto& row : parsed.rows) {
        if (row.error == DomainImportRowError::kNone && row.rule) {
          rules.push_back(*row.rule);
        }
      }
      const auto added = service()->ImportBlockedDomains(rules);
      if (!added) {
        RejectJavascriptCallback(base::Value(callback_id),
                                 base::Value("too-many-domains"));
      } else {
        ResolveJavascriptCallback(base::Value(callback_id),
                                  base::Value(static_cast<int>(*added)));
      }
      return;
    }
    std::set<std::string> existing;
    for (const auto& rule : service()->BlockedDomains()) {
      existing.insert(rule.domain);
    }
    base::ListValue rows;
    int additions = 0, duplicates = 0, invalid = 0;
    for (const auto& row : parsed.rows) {
      std::string status;
      switch (row.error) {
        case DomainImportRowError::kNone:
          status = row.rule && existing.contains(row.rule->domain) ? "duplicate"
                                                                   : "add";
          break;
        case DomainImportRowError::kDuplicate:
          status = "duplicate";
          break;
        case DomainImportRowError::kInvalidDomain:
          status = "invalid-domain";
          break;
        case DomainImportRowError::kInvalidColumns:
          status = "invalid-columns";
          break;
        case DomainImportRowError::kInvalidScope:
          status = "invalid-scope";
          break;
      }
      if (status == "add") {
        ++additions;
      } else if (status == "duplicate") {
        ++duplicates;
      } else {
        ++invalid;
      }
      rows.Append(base::DictValue()
                      .Set("line", static_cast<int>(row.line))
                      .Set("domain", row.rule ? row.rule->domain : row.input)
                      .Set("includeSubdomains",
                           row.rule && row.rule->include_subdomains)
                      .Set("status", status));
    }
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::DictValue()
                                  .Set("rows", std::move(rows))
                                  .Set("additions", additions)
                                  .Set("duplicates", duplicates)
                                  .Set("invalid", invalid));
  }
  void ListsChecked(std::string callback_id, bool succeeded) {
    if (IsJavascriptAllowed()) {
      SendState(std::move(callback_id), succeeded);
    }
  }

  base::CallbackListSubscription subscription_;
  base::CallbackListSubscription update_subscription_;
  base::WeakPtrFactory<SettingsHandler> weak_factory_{this};
};
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
  web_ui->AddMessageHandler(std::make_unique<SettingsHandler>());
}
SettingsUI::~SettingsUI() = default;
}  // namespace yee
