// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_BASELINE_LIST_STORE_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_BASELINE_LIST_STORE_H_

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/time/time.h"

namespace yee::content_blocking {
inline constexpr char kBaselineListDirectorySwitch[] = "yee-baseline-list-dir";
inline constexpr char kBaselineListGenerationSwitch[] =
    "yee-baseline-list-generation";
inline constexpr char kBaselineListHandleSwitch[] = "yee-baseline-list-handle";
inline constexpr size_t kMaxBaselineListBytes = 16 * 1024 * 1024;
// Two selected lists, up to 64 MiB of compiled data and bounded metadata.
inline constexpr size_t kMaxBaselineListSnapshotBytes =
    2 * kMaxBaselineListBytes + 64 * 1024 * 1024 + 1024;
inline constexpr std::array<std::string_view, 2> kBaselineListFiles = {
    "easylist.txt", "easyprivacy.txt"};
inline constexpr std::array<std::string_view, 2> kBaselineListURLs = {
    "https://easylist-downloads.adblockplus.org/easylist.txt",
    "https://easylist-downloads.adblockplus.org/easyprivacy.txt"};

struct BaselineListSnapshot {
  std::string generation;
  std::string filters;
  std::string compiled_filters;
  base::Time checked_at;
  bool recovered = false;
};

// Preserves original text in immutable generation directories. A single atomic
// state file selects a complete pair; previous good data is never overwritten.
// All IO and validation run before sandboxing or on a blocking worker.
std::optional<std::string> PreprocessBaselineList(std::string_view text);
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
