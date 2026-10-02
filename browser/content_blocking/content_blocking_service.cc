// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/values.h"
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
  OnDisabledSitesChanged();
}

ContentBlockingService::~ContentBlockingService() = default;
void ContentBlockingService::StartBaselineListUpdates() {
  if (!off_the_record_ && !list_updater_)
    list_updater_ = BaselineListUpdater::MaybeCreate();
}

// static
void ContentBlockingService::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kDisabledSitesPref);
}

bool ContentBlockingService::EnabledForSite(const GURL& site) const {
  return settings_snapshot_->EnabledForSite(site);
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
