// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_content_blocking/content_blocking_service.h"

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/yee_content_blocking/settings.h"

namespace yee::content_blocking {
namespace {

std::string NormalizeHost(const GURL& site) {
  if (!site.SchemeIsHTTPOrHTTPS() || site.host().empty()) {
    return {};
  }
  return base::ToLowerASCII(site.host());
}

ContentSettingsPattern ExactHostPattern(std::string_view host) {
  return ContentSettingsPattern::CreateBuilder()
      ->WithSchemeWildcard()
      ->WithHost(std::string(host))
      ->WithPortWildcard()
      ->WithPathWildcard()
      ->Build();
}

}  // namespace

ContentBlockingSettingsSnapshot::ContentBlockingSettingsSnapshot() = default;
ContentBlockingSettingsSnapshot::~ContentBlockingSettingsSnapshot() = default;

bool ContentBlockingSettingsSnapshot::EnabledForSite(const GURL& site) const {
  if (!yee::content_blocking::EnabledForSite(site)) {
    return false;
  }
  const std::string host = NormalizeHost(site);
  if (host.empty()) {
    return false;
  }
  base::AutoLock lock(lock_);
  return !std::ranges::binary_search(disabled_hosts_, host);
}

void ContentBlockingSettingsSnapshot::ReplaceDisabledHosts(
    std::vector<std::string> hosts) {
  std::ranges::sort(hosts);
  hosts.erase(std::unique(hosts.begin(), hosts.end()), hosts.end());
  base::AutoLock lock(lock_);
  disabled_hosts_ = std::move(hosts);
}

bool ContentBlockingSettingsSnapshot::IsBlockedDomain(const GURL& url) const {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  std::string_view host = url.host();
  if (host.ends_with('.')) {
    host.remove_suffix(1);
  }
  base::AutoLock lock(lock_);
  bool subdomain = false;
  while (!host.empty()) {
    const auto rule = std::ranges::lower_bound(blocked_domains_, host, {},
                                               &BlockedDomain::domain);
    if (rule != blocked_domains_.end() && rule->domain == host &&
        (!subdomain || rule->include_subdomains)) {
      return true;
    }
    const auto dot = host.find('.');
    if (dot == std::string_view::npos) {
      break;
    }
    host.remove_prefix(dot + 1);
    subdomain = true;
  }
  return false;
}

void ContentBlockingSettingsSnapshot::ReplaceBlockedDomains(
    std::vector<BlockedDomain> domains) {
  std::ranges::sort(domains, {}, &BlockedDomain::domain);
  base::AutoLock lock(lock_);
  blocked_domains_ = std::move(domains);
}

ContentBlockingService::ContentBlockingService(PrefService* prefs,
                                               bool off_the_record)
    : prefs_(prefs),
      off_the_record_(off_the_record),
      settings_snapshot_(
          base::MakeRefCounted<ContentBlockingSettingsSnapshot>()) {
  CHECK(prefs_);
  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(
      kDisabledSitesPref,
      base::BindRepeating(&ContentBlockingService::OnDisabledSitesChanged,
                          base::Unretained(this)));
  pref_change_registrar_.Add(
      kBlockedDomainsPref,
      base::BindRepeating(&ContentBlockingService::OnBlockedDomainsChanged,
                          base::Unretained(this)));
  OnDisabledSitesChanged();
  OnBlockedDomainsChanged();
}

ContentBlockingService::~ContentBlockingService() = default;
void ContentBlockingService::StartBaselineListUpdates() {
  if (!off_the_record_ && !list_updater_) {
    list_updater_ = BaselineListUpdater::MaybeCreate();
  }
}

// static
void ContentBlockingService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kDisabledSitesPref);
  registry->RegisterListPref(kBlockedDomainsPref);
}

bool ContentBlockingService::EnabledForSite(const GURL& site) const {
  return settings_snapshot_->EnabledForSite(site);
}

std::vector<std::string> ContentBlockingService::DisabledHosts() const {
  return ReadDisabledHosts();
}

std::vector<BlockedDomain> ContentBlockingService::BlockedDomains() const {
  std::map<std::string, bool> normalized;
  for (const auto& value : prefs_->GetList(kBlockedDomainsPref)) {
    if (!value.is_dict()) {
      continue;
    }
    const auto* input = value.GetDict().FindString("domain");
    const auto scope = value.GetDict().FindBool("includeSubdomains");
    if (!input || !scope.has_value()) {
      continue;
    }
    if (const auto domain = CanonicalBlockedDomain(*input)) {
      normalized.try_emplace(*domain, *scope);
      if (normalized.size() == kMaxBlockedDomains) {
        break;
      }
    }
  }
  std::vector<BlockedDomain> domains;
  for (const auto& [domain, scope] : normalized) {
    domains.push_back({domain, scope});
  }
  return domains;
}

bool ContentBlockingService::SetBlockedDomain(std::string_view input,
                                              bool include_subdomains) {
  const auto domain = CanonicalBlockedDomain(input);
  if (!domain) {
    return false;
  }
  auto domains = BlockedDomains();
  auto existing = std::ranges::find(domains, *domain, &BlockedDomain::domain);
  if (existing != domains.end()) {
    existing->include_subdomains = include_subdomains;
  } else {
    if (domains.size() == kMaxBlockedDomains) {
      return false;
    }
    domains.push_back({*domain, include_subdomains});
  }
  SaveBlockedDomains(domains);
  return true;
}

void ContentBlockingService::RemoveBlockedDomain(std::string_view input) {
  const auto domain = CanonicalBlockedDomain(input);
  if (!domain) {
    return;
  }
  auto domains = BlockedDomains();
  std::erase_if(domains,
                [&](const auto& rule) { return rule.domain == *domain; });
  SaveBlockedDomains(domains);
}

std::optional<size_t> ContentBlockingService::ImportBlockedDomains(
    const std::vector<BlockedDomain>& additions) {
  auto domains = BlockedDomains();
  std::map<std::string, bool> merged;
  for (const auto& rule : domains) {
    merged.emplace(rule.domain, rule.include_subdomains);
  }
  for (const auto& rule : additions) {
    const auto domain = CanonicalBlockedDomain(rule.domain);
    if (!domain) {
      return std::nullopt;
    }
    merged.try_emplace(*domain, rule.include_subdomains);
    if (merged.size() > kMaxBlockedDomains) {
      return std::nullopt;
    }
  }
  const size_t count = merged.size() - domains.size();
  if (count) {
    domains.clear();
    for (const auto& [domain, scope] : merged) {
      domains.push_back({domain, scope});
    }
    SaveBlockedDomains(domains);
  }
  return count;
}

void ContentBlockingService::SaveBlockedDomains(
    const std::vector<BlockedDomain>& domains) {
  base::ListValue values;
  for (const auto& rule : domains) {
    values.Append(base::DictValue()
                      .Set("domain", rule.domain)
                      .Set("includeSubdomains", rule.include_subdomains));
  }
  prefs_->SetList(kBlockedDomainsPref, std::move(values));
}

void ContentBlockingService::OnBlockedDomainsChanged() {
  settings_snapshot_->ReplaceBlockedDomains(BlockedDomains());
  changed_callbacks_.Notify();
}

base::CallbackListSubscription ContentBlockingService::AddChangedCallback(
    base::RepeatingClosure callback) {
  return changed_callbacks_.Add(std::move(callback));
}

void ContentBlockingService::SetEnabledForSite(const GURL& site, bool enabled) {
  const std::string host = NormalizeHost(site);
  if (host.empty()) {
    return;
  }

  ScopedListPrefUpdate update(prefs_, kDisabledSitesPref);
  base::ListValue& sites = update.Get();
  sites.EraseIf([&](const base::Value& value) {
    return value.is_string() &&
           base::EqualsCaseInsensitiveASCII(value.GetString(), host);
  });
  if (!enabled) {
    sites.Append(host);
  }
}

ContentSettingsForOneType ContentBlockingService::GetRendererRules() const {
  ContentSettingsForOneType rules;
  for (const std::string& host : ReadDisabledHosts()) {
    ContentSettingsPattern pattern = ExactHostPattern(host);
    if (!pattern.IsValid()) {
      continue;
    }
    rules.emplace_back(
        pattern, ContentSettingsPattern::Wildcard(),
        content_settings::ContentSettingToValue(CONTENT_SETTING_BLOCK),
        content_settings::mojom::ProviderType::kPrefProvider, off_the_record_);
  }
  rules.emplace_back(
      ContentSettingsPattern::Wildcard(), ContentSettingsPattern::Wildcard(),
      content_settings::ContentSettingToValue(CONTENT_SETTING_ALLOW),
      content_settings::mojom::ProviderType::kDefaultProvider, off_the_record_);
  return rules;
}

std::vector<std::string> ContentBlockingService::ReadDisabledHosts() const {
  std::vector<std::string> hosts;
  for (const base::Value& value : prefs_->GetList(kDisabledSitesPref)) {
    if (!value.is_string()) {
      continue;
    }
    std::string host = base::ToLowerASCII(value.GetString());
    if (!host.empty()) {
      hosts.push_back(std::move(host));
    }
  }
  std::ranges::sort(hosts);
  hosts.erase(std::unique(hosts.begin(), hosts.end()), hosts.end());
  return hosts;
}

void ContentBlockingService::OnDisabledSitesChanged() {
  settings_snapshot_->ReplaceDisabledHosts(ReadDisabledHosts());
  changed_callbacks_.Notify();
}

}  // namespace yee::content_blocking
