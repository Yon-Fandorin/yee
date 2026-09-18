// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/filter_data.h"

#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "crypto/hash.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/rust/src/lib.rs.h"

namespace yee::content_blocking {
namespace {
constexpr char kManifest[] = "YeeCommunityFilterManifest.json";
constexpr char kRules[] = "YeeCommunityFilters.txt";
constexpr char kResources[] = "YeeCommunityResources.json";
constexpr size_t kMaxManifestBytes = 128 * 1024;
constexpr size_t kMaxRulesBytes = 16 * 1024 * 1024;
constexpr size_t kMaxResourcesBytes = 16 * 1024 * 1024;
struct StartupData {
  bool initialized = false;
  FilterDataSnapshot snapshot;
};
StartupData& Data() {
  static base::NoDestructor<StartupData> data;
  return *data;
}
}  // namespace

FilterDataSnapshot ReadCommunityFilterData(const base::FilePath& directory) {
  FilterDataSnapshot snapshot;
  if (directory.empty())
    return snapshot;
  if (!directory.IsAbsolute()) {
    snapshot.status = FilterDataStatus::kInvalid;
    return snapshot;
  }
  const auto manifest_path = directory.AppendASCII(kManifest);
  if (!base::PathExists(manifest_path))
    return snapshot;
  snapshot.status = FilterDataStatus::kInvalid;
  std::string manifest;
  if (!base::ReadFileToStringWithMaxSize(manifest_path, &manifest,
                                       kMaxManifestBytes)) {
    return snapshot;
  }
  const auto dict = base::JSONReader::ReadDict(manifest, base::JSON_PARSE_RFC);
  if (!dict || dict->FindInt("schema_version") != 2)
    return snapshot;
  const auto* file = dict->FindString("rules_file");
  const auto* checksum = dict->FindString("rules_sha256");
  if (!file || *file != kRules || !checksum || checksum->size() != 64)
    return snapshot;
  const auto* resource_file = dict->FindString("resources_file");
  const auto* resource_checksum = dict->FindString("resources_sha256");
  if (!resource_file || *resource_file != kResources || !resource_checksum ||
      resource_checksum->size() != 64) {
    return snapshot;
  }
  std::string rules;
  if (!base::ReadFileToStringWithMaxSize(directory.AppendASCII(kRules),
                                       &rules, kMaxRulesBytes) ||
      rules.empty() || !base::IsStringUTF8(rules) ||
      base::HexEncodeLower(crypto::hash::Sha256(rules)) != *checksum) {
    return snapshot;
  }
  std::string resources;
  if (!base::ReadFileToStringWithMaxSize(directory.AppendASCII(kResources),
                                       &resources, kMaxResourcesBytes) ||
      resources.empty() || !base::IsStringUTF8(resources) ||
      base::HexEncodeLower(crypto::hash::Sha256(resources)) !=
          *resource_checksum ||
      !base::JSONReader::ReadList(resources, base::JSON_PARSE_RFC) ||
      !community_resources_valid(rust::Str(resources.data(), resources.size()),
                                 rust::Str(kBundledResources.data(), kBundledResources.size()))) {
    return snapshot;
  }
  snapshot.status = FilterDataStatus::kLoaded;
  snapshot.generation = base::HexEncodeLower(
      crypto::hash::Sha256(*checksum + "\n" + *resource_checksum));
  snapshot.filters = std::move(rules);
  snapshot.resources = std::move(resources);
  return snapshot;
}

void InitializeCommunityFilterDataBeforeSandbox(
    const base::FilePath& directory) {
  auto& data = Data();
  CHECK(!data.initialized);
  data.snapshot = ReadCommunityFilterData(directory);
  data.initialized = true;
  if (data.snapshot.status == FilterDataStatus::kInvalid) {
    LOG(ERROR) << "Invalid Yee community filter data; using bundled baseline";
  } else if (data.snapshot.status == FilterDataStatus::kMissing) {
    LOG(WARNING) << "Yee community filter data missing; using bundled baseline";
  }
}

const FilterDataSnapshot& CommunityFilterData() {
  return Data().snapshot;
}
}  // namespace yee::content_blocking
