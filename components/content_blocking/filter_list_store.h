// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_FILTER_LIST_STORE_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_FILTER_LIST_STORE_H_

#include "components/yee_content_blocking/baseline_list_store.h"

namespace yee::content_blocking {
// Original text stays on the blocking store sequence; UI state contains
// metadata.
struct SubscribedFilterList {
  FilterSubscription info;
  std::string original;
};
struct FilterListSet {
  std::array<std::string, 2> baseline;
  bool baseline_downloaded = false;
  base::Time checked_at;
  std::vector<SubscribedFilterList> subscriptions;
};
struct FilterListDownload {
  std::string url;
  std::string body;
};
struct FilterListUpdateResult {
  bool succeeded = false;
  std::vector<std::string> failed_urls;
};

// All operations use one sequenced blocking worker. Legacy two-list generations
// remain readable. Every publication atomically selects the complete rule set.
FilterListSet ReadFilterListSet(const base::FilePath& directory);
FilterListUpdateResult UpdateFilterLists(
    const base::FilePath& directory,
    std::array<std::string, 2> baseline,
    std::vector<FilterListDownload> downloads,
    base::Time checked_at,
    std::string_view running_generation);
std::string AddFilterSubscription(const base::FilePath& directory,
                                  std::string_view url,
                                  std::string body,
                                  std::string_view running_generation);
std::string ChangeFilterSubscription(const base::FilePath& directory,
                                     std::string_view url,
                                     std::optional<bool> enabled,
                                     std::string_view running_generation);
}  // namespace yee::content_blocking
#endif
