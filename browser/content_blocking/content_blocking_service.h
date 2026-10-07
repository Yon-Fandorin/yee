// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "chrome/browser/yee_content_blocking/blocked_domains.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"

class PrefService;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace yee::content_blocking {
class BaselineListUpdater;

inline constexpr char kDisabledSitesPref[] =
    "yee.content_blocking.disabled_sites";
inline constexpr char kBlockedDomainsPref[] =
    "yee.content_blocking.blocked_domains";

// Thread-safe policy snapshot used by URLLoaderFactory proxies after they move
// to their matching sequence. Profile and PrefService pointers never cross the
// UI-thread boundary.
class ContentBlockingSettingsSnapshot
    : public base::RefCountedThreadSafe<ContentBlockingSettingsSnapshot> {
 public:
  ContentBlockingSettingsSnapshot();

  bool EnabledForSite(const GURL& site) const;
  void ReplaceDisabledHosts(std::vector<std::string> hosts);
  bool IsBlockedDomain(const GURL& url) const;
  void ReplaceBlockedDomains(std::vector<BlockedDomain> domains);

 private:
  friend class base::RefCountedThreadSafe<ContentBlockingSettingsSnapshot>;
  ~ContentBlockingSettingsSnapshot();

  mutable base::Lock lock_;
  std::vector<std::string> disabled_hosts_ GUARDED_BY(lock_);
  std::vector<BlockedDomain> blocked_domains_ GUARDED_BY(lock_);
};

// Profile-owned persistence for exceptions and user-defined domain rules.
// The command-line switches remain a development override; product UI writes
// only this profile preference.
class ContentBlockingService : public KeyedService {
 public:
  ContentBlockingService(PrefService* prefs, bool off_the_record);
  ContentBlockingService(const ContentBlockingService&) = delete;
  ContentBlockingService& operator=(const ContentBlockingService&) = delete;
  ~ContentBlockingService() override;

  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  bool EnabledForSite(const GURL& site) const;
  void SetEnabledForSite(const GURL& site, bool enabled);
  std::vector<std::string> DisabledHosts() const;
  std::vector<BlockedDomain> BlockedDomains() const;
  bool SetBlockedDomain(std::string_view input, bool include_subdomains);
  void RemoveBlockedDomain(std::string_view input);
  // Import merges valid rules without replacing existing scope choices. A
  // rejected batch does not modify preferences; success returns additions.
  std::optional<size_t> ImportBlockedDomains(
      const std::vector<BlockedDomain>& domains);
  base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure callback);

  scoped_refptr<ContentBlockingSettingsSnapshot> settings_snapshot() const {
    return settings_snapshot_;
  }

  // Sent with Chromium's per-navigation renderer settings. Host-specific rules
  // precede the default rule so the renderer can use the first matching value.
  ContentSettingsForOneType GetRendererRules() const;
  void StartBaselineListUpdates();

 private:
  std::vector<std::string> ReadDisabledHosts() const;
  void OnDisabledSitesChanged();
  void SaveBlockedDomains(const std::vector<BlockedDomain>& domains);
  void OnBlockedDomainsChanged();

  const raw_ptr<PrefService> prefs_;
  const bool off_the_record_;
  PrefChangeRegistrar pref_change_registrar_;
  const scoped_refptr<ContentBlockingSettingsSnapshot> settings_snapshot_;
  base::RepeatingClosureList changed_callbacks_;
  std::unique_ptr<BaselineListUpdater> list_updater_;
};

}  // namespace yee::content_blocking

#endif  // CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_H_
