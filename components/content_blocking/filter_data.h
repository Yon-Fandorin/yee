// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_FILTER_DATA_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_FILTER_DATA_H_

#include <string>

#include "base/files/file_path.h"

namespace yee::content_blocking {

inline constexpr char kCommunityFilterDirectorySwitch[] =
    "yee-community-filter-dir";
enum class FilterDataStatus { kMissing, kInvalid, kLoaded };
struct FilterDataSnapshot {
  FilterDataStatus status = FilterDataStatus::kMissing;
  std::string filters;
  std::string resources;
  std::string generation;
  // Optional binary cache, bound to this pack and the current bundled rules.
  std::string compiled_filters;
};

// Fixed-path, external filters and JavaScript resources. The manifest checksum
// checks consistency, not authenticity. Only trusted startup configuration can
// choose a replacement package; a web page cannot supply one.
FilterDataSnapshot ReadCommunityFilterData(const base::FilePath& directory);

// Called on the startup thread before sandboxing and filter engine consumers.
// Linux zygote children inherit the same immutable snapshot. No later file IO.
void InitializeCommunityFilterDataBeforeSandbox(
    const base::FilePath& directory);
const FilterDataSnapshot& CommunityFilterData();

}  // namespace yee::content_blocking
#endif
