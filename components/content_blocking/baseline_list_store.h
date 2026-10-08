// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_BASELINE_LIST_STORE_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_BASELINE_LIST_STORE_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/time/time.h"

namespace yee::content_blocking {
inline constexpr char kBaselineListDirectorySwitch[] = "yee-baseline-list-dir";
inline constexpr char kBaselineListGenerationSwitch[] =
    "yee-baseline-list-generation";
inline constexpr char kBaselineListHandleSwitch[] = "yee-baseline-list-handle";
inline constexpr size_t kMaxBaselineListBytes = 16 * 1024 * 1024;
inline constexpr size_t kMaxFilterSubscriptions = 10;
inline constexpr size_t kMaxSubscribedListBytes = 4 * 1024 * 1024;
inline constexpr size_t kMaxSubscriptionTotalBytes = 16 * 1024 * 1024;
// Official and subscribed lists, compiled data, and bounded metadata.
inline constexpr size_t kMaxBaselineListSnapshotBytes =
    2 * kMaxBaselineListBytes + kMaxSubscriptionTotalBytes + 64 * 1024 * 1024 +
    4096;
inline constexpr std::array<std::string_view, 2> kBaselineListFiles = {
    "easylist.txt", "easyprivacy.txt"};
inline constexpr std::array<std::string_view, 2> kBaselineListURLs = {
    "https://easylist-downloads.adblockplus.org/easylist.txt",
    "https://easylist-downloads.adblockplus.org/easyprivacy.txt"};

struct FilterSubscription {
  std::string url;
  std::string title;
  bool enabled = true;
  base::Time checked_at;
};

struct BaselineListSnapshot {
  std::string generation;
  std::string filters;
  std::string compiled_filters;
  base::Time checked_at;
  bool recovered = false;
  bool baseline_downloaded = false;
  std::vector<FilterSubscription> subscriptions;
};

// Preserves original text in immutable generation directories. A single atomic
// state file selects a complete pair; previous good data is never overwritten.
// All IO and validation run before sandboxing or on a blocking worker.
std::optional<std::string> PreprocessBaselineList(std::string_view text);
std::optional<std::string> PreprocessSubscribedList(std::string_view text);
std::optional<std::string> CanonicalFilterSubscriptionURL(std::string_view url);
BaselineListSnapshot ReadBaselineListStore(const base::FilePath& directory);
BaselineListSnapshot ReadBaselineListGeneration(const base::FilePath& directory,
                                                std::string_view generation);
bool InstallBaselineLists(const base::FilePath& directory,
                          const std::array<std::string, 2>& originals,
                          base::Time checked_at,
                          std::string_view running_generation);

// Only the browser reads the profile store. Sandboxed renderers receive its
// immutable startup selection through an inherited read-only memory handle.
base::ReadOnlySharedMemoryRegion CreateBaselineListRegion(
    const BaselineListSnapshot& snapshot);
std::optional<BaselineListSnapshot> ReadBaselineListRegion(
    const base::ReadOnlySharedMemoryRegion& region);
const base::ReadOnlySharedMemoryRegion& BaselineListRegion();
void InitializeBaselineListsBeforeSandbox(const base::FilePath& user_data_dir,
                                          bool browser_process);
const BaselineListSnapshot& BaselineLists();
}  // namespace yee::content_blocking
#endif
