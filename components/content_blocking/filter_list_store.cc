// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/filter_list_store.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/filter_data.h"
#include "components/yee_content_blocking/rust/src/lib.rs.h"
#include "crypto/hash.h"

namespace yee::content_blocking {
namespace {
constexpr char kState[] = "state.json";
constexpr char kBackupState[] = "state.previous.json";
constexpr char kManifest[] = "manifest.json";
constexpr size_t kMetadataBytes = 64 * 1024;
constexpr size_t kMaxCompiledBytes = 64 * 1024 * 1024;
std::string Digest(std::string_view data) {
  return base::HexEncodeLower(crypto::hash::Sha256(data));
}
bool ValidGeneration(std::string_view value) {
  return value.size() == 64 &&
         base::ContainsOnlyChars(value, "0123456789abcdef");
}
bool HasExpectedTitle(std::string_view text, size_t index) {
  const std::string_view title =
      index == 0 ? "! Title: EasyList" : "! Title: EasyPrivacy";
  for (auto line :
       base::SplitStringPiece(text.substr(0, 4096), "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY))
    if (line == title)
      return true;
  return false;
}
bool SafeDirectory(const base::FilePath& directory) {
  if (!directory.IsAbsolute())
    return false;
  for (auto part = directory; part != part.DirName(); part = part.DirName()) {
    if (base::IsLink(part))
      return false;
  }
  return true;
}
std::optional<base::DictValue> ReadMetadata(const base::FilePath& path) {
  std::string text;
  if (base::IsLink(path) ||
      !base::ReadFileToStringWithMaxSize(path, &text, kMetadataBytes))
    return std::nullopt;
  auto result = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!result || (result->FindInt("schema_version") != 1 &&
                  result->FindInt("schema_version") != 2))
    return std::nullopt;
  return result;
}
BaselineListSnapshot ReadState(const base::FilePath& directory,
                               const base::DictValue& state) {
  BaselineListSnapshot snapshot;
  for (const char* key : {"current", "previous"}) {
    const auto* generation = state.FindString(key);
    if (!generation)
      continue;
    snapshot = ReadBaselineListGeneration(directory, *generation);
    if (!snapshot.generation.empty()) {
      snapshot.recovered = std::string_view(key) == "previous";
      const auto checked = state.FindDouble("checked_at");
      if (!snapshot.recovered && checked && std::isfinite(*checked) &&
          *checked >= 0)
        snapshot.checked_at =
            *checked > 0 ? base::Time::FromSecondsSinceUnixEpoch(*checked)
                         : base::Time();
      if (!snapshot.recovered) {
        const auto* checks = state.FindDict("list_checks");
        for (auto& info : snapshot.subscriptions) {
          const auto time =
              checks ? checks->FindDouble(info.url) : std::nullopt;
          if (time && std::isfinite(*time) && *time > 0)
            info.checked_at = base::Time::FromSecondsSinceUnixEpoch(*time);
        }
      }
      return snapshot;
    }
  }
  return snapshot;
}
std::string SubscriptionFile(std::string_view url) {
  return "subscription-" + Digest(url) + ".txt";
}
std::string ListTitle(std::string_view text, std::string_view fallback) {
  for (auto line :
       base::SplitStringPiece(text.substr(0, 4096), "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with("! Title:"))
      continue;
    const auto title =
        base::TrimWhitespaceASCII(line.substr(8), base::TRIM_ALL);
    if (!title.empty() && title.size() <= 256 && base::IsStringUTF8(title) &&
        std::ranges::none_of(
            title, [](unsigned char c) { return c < 32 || c == 127; }))
      return std::string(title);
  }
  return std::string(fallback);
}
std::string GenerationIdentity(bool downloaded,
                               const base::ListValue& entries) {
  std::string identity = downloaded ? "2\n1\n" : "2\n0\n";
  for (const auto& value : entries) {
    const auto& entry = value.GetDict();
    identity +=
        *entry.FindString("source") + "\n" + *entry.FindString("sha256") + "\n";
    identity += entry.FindBool("enabled").value_or(true) ? "1\n" : "0\n";
  }
  return Digest(identity);
}
}  // namespace

BaselineListSnapshot ReadBaselineListGeneration(const base::FilePath& directory,
                                                std::string_view generation) {
  BaselineListSnapshot snapshot;
  if (!SafeDirectory(directory) || !ValidGeneration(generation))
    return snapshot;
  const auto path =
      directory.AppendASCII("generations").AppendASCII(generation);
  if (!SafeDirectory(path))
    return snapshot;
  const auto manifest = ReadMetadata(path.AppendASCII(kManifest));
  if (!manifest)
    return snapshot;
  const auto* entries = manifest->FindList("lists");
  const auto time = manifest->FindDouble("checked_at");
  const bool legacy = manifest->FindInt("schema_version") == 1;
  if (!entries || entries->size() < 2 ||
      entries->size() > 2 + kMaxFilterSubscriptions ||
      (legacy && entries->size() != 2) || !time || !std::isfinite(*time) ||
      *time <= 0)
    return snapshot;
  snapshot.baseline_downloaded =
      manifest->FindBool("baseline_downloaded").value_or(true);
  size_t subscribed_bytes = 0;
  std::set<std::string> urls;
  std::array<std::string, 2> checksums;
  for (size_t index = 0; index < entries->size(); ++index) {
    if (!(*entries)[index].is_dict())
      return {};
    const auto& entry = (*entries)[index].GetDict();
    const auto* file = entry.FindString("file");
    const auto* url = entry.FindString("source");
    const auto* checksum = entry.FindString("sha256");
    if (!file || !url || !checksum || !ValidGeneration(*checksum))
      return {};
    const bool official = index < 2;
    if (official) {
      if (*file != kBaselineListFiles[index] ||
          *url != kBaselineListURLs[index])
        return {};
    } else if (CanonicalFilterSubscriptionURL(*url) != *url ||
               *file != SubscriptionFile(*url) || !urls.insert(*url).second ||
               !entry.FindBool("enabled").has_value()) {
      return {};
    }
    const auto file_path = path.AppendASCII(*file);
    std::string original;
    if (base::IsLink(file_path) ||
        !base::ReadFileToStringWithMaxSize(
            file_path, &original,
            official ? kMaxBaselineListBytes : kMaxSubscribedListBytes) ||
        Digest(original) != *checksum ||
        (official && !HasExpectedTitle(original, index)))
      return {};
    auto selected = official ? PreprocessBaselineList(original)
                             : PreprocessSubscribedList(original);
    if (!selected)
      return {};
    if (official) {
      checksums[index] = *checksum;
      snapshot.filters += *selected;
    } else {
      subscribed_bytes += original.size();
      if (subscribed_bytes > kMaxSubscriptionTotalBytes)
        return {};
      const bool enabled = *entry.FindBool("enabled");
      if (enabled)
        snapshot.filters += *selected;
      snapshot.subscriptions.push_back(
          {*url, ListTitle(original, *url), enabled,
           base::Time::FromSecondsSinceUnixEpoch(*time)});
    }
  }
  if ((legacy ? Digest(checksums[0] + "\n" + checksums[1])
              : GenerationIdentity(snapshot.baseline_downloaded, *entries)) !=
      generation)
    return {};
  snapshot.generation = std::string(generation);
  snapshot.checked_at = base::Time::FromSecondsSinceUnixEpoch(*time);
  const auto cache = ReadMetadata(path.AppendASCII("compiled.json"));
  if (cache && cache->FindString("bundle_generation") &&
      *cache->FindString("bundle_generation") == kBundleGeneration &&
      cache->FindString("community_generation") &&
      *cache->FindString("community_generation") ==
          CommunityFilterData().generation) {
    const auto* hash = cache->FindString("sha256");
    const auto binary = path.AppendASCII("compiled.dat");
    if (!hash || base::IsLink(binary) ||
        !base::ReadFileToStringWithMaxSize(binary, &snapshot.compiled_filters,
                                           kMaxCompiledBytes) ||
        Digest(snapshot.compiled_filters) != *hash)
      snapshot.compiled_filters.clear();
  }
  return snapshot;
}

BaselineListSnapshot ReadBaselineListStore(const base::FilePath& directory) {
  if (!SafeDirectory(directory))
    return {};
  for (const char* name : {kState, kBackupState}) {
    const auto state = ReadMetadata(directory.AppendASCII(name));
    if (!state)
      continue;
    auto snapshot = ReadState(directory, *state);
    if (!snapshot.generation.empty()) {
      snapshot.recovered |= name == kBackupState;
      return snapshot;
    }
  }
  return {};
}

namespace {
bool WriteFilterListSet(const base::FilePath& directory,
                        const FilterListSet& lists,
                        std::string_view running_generation) {
  if (!SafeDirectory(directory) ||
      lists.subscriptions.size() > kMaxFilterSubscriptions)
    return false;
  std::string selected_filters;
  base::ListValue entries;
  std::vector<std::string_view> originals;
  size_t subscribed_bytes = 0;
  for (size_t index = 0; index < 2 + lists.subscriptions.size(); ++index) {
    const bool official = index < 2;
    const auto& original = official ? lists.baseline[index]
                                    : lists.subscriptions[index - 2].original;
    const auto selected = official ? PreprocessBaselineList(original)
                                   : PreprocessSubscribedList(original);
    if (!selected || (official && !HasExpectedTitle(original, index)) ||
        !baseline_rules_valid(rust::Str(selected->data(), selected->size())))
      return false;
    const auto& info =
        official ? FilterSubscription{} : lists.subscriptions[index - 2].info;
    if (!official) {
      if (CanonicalFilterSubscriptionURL(info.url) != info.url)
        return false;
      subscribed_bytes += original.size();
      if (subscribed_bytes > kMaxSubscriptionTotalBytes)
        return false;
    }
    if (official || info.enabled)
      selected_filters += *selected;
    auto entry =
        base::DictValue()
            .Set("file", official ? std::string(kBaselineListFiles[index])
                                  : SubscriptionFile(info.url))
            .Set("source",
                 official ? std::string(kBaselineListURLs[index]) : info.url)
            .Set("sha256", Digest(original))
            .Set("enabled", official || info.enabled);
    if (official) {
      entry.Set("attribution", "The EasyList authors (https://easylist.to/)");
      entry.Set("license", "CC-BY-SA-3.0-or-later");
      entry.Set("license_url", "https://easylist.to/pages/licence.html");
    }
    entries.Append(std::move(entry));
    originals.push_back(original);
  }
  const auto generation =
      GenerationIdentity(lists.baseline_downloaded, entries);
  const auto previous = ReadBaselineListStore(directory);
  std::string rollback =
      previous.generation == generation ? "" : previous.generation;
  if (previous.generation == generation) {
    const auto old_state = ReadMetadata(directory.AppendASCII(kState));
    if (old_state) {
      const auto* old_previous = old_state->FindString("previous");
      if (old_previous && *old_previous != generation &&
          !ReadBaselineListGeneration(directory, *old_previous)
               .generation.empty())
        rollback = *old_previous;
    }
  }
  const auto generations = directory.AppendASCII("generations");
  if (!base::CreateDirectory(generations) || !SafeDirectory(generations))
    return false;
  auto existing = ReadBaselineListGeneration(directory, generation);
  if (existing.generation.empty()) {
    const auto target = generations.AppendASCII(generation);
    // A corrupt immutable generation is replaced only after all new files are
    // complete. It cannot be a valid current/previous/running selection.
    if (base::PathExists(target) && !base::DeletePathRecursively(target))
      return false;
    base::ScopedTempDir temporary;
    if (!temporary.CreateUniqueTempDirUnderPath(generations))
      return false;
    for (size_t index = 0; index < originals.size(); ++index) {
      if (!base::ImportantFileWriter::WriteFileAtomically(
              temporary.GetPath().AppendASCII(
                  *entries[index].GetDict().FindString("file")),
              originals[index]))
        return false;
    }
    auto manifest = base::WriteJson(
        base::DictValue()
            .Set("schema_version", 2)
            .Set("baseline_downloaded", lists.baseline_downloaded)
            .Set("checked_at", base::Time::Now().InSecondsFSinceUnixEpoch())
            .Set("lists", std::move(entries)));
    if (!manifest ||
        !base::ImportantFileWriter::WriteFileAtomically(
            temporary.GetPath().AppendASCII(kManifest), *manifest) ||
        !base::Move(temporary.GetPath(), target))
      return false;
  }
  // The optional cache is separate from immutable originals. Atomic replacement
  // and its own digest make an interrupted/stale cache fall back to parsing.
  const auto filters = selected_filters + "\n" + std::string(kOwnedFilters);
  const auto& community = CommunityFilterData();
  const auto compiled = compile_baseline_rules(
      rust::Str(filters.data(), filters.size()),
      rust::Str(community.filters.data(), community.filters.size()));
  const std::string_view compiled_view(
      reinterpret_cast<const char*>(compiled.data()), compiled.size());
  const auto target = generations.AppendASCII(generation);
  auto cache =
      base::WriteJson(base::DictValue()
                          .Set("schema_version", 1)
                          .Set("bundle_generation", kBundleGeneration)
                          .Set("community_generation", community.generation)
                          .Set("sha256", Digest(compiled_view)));
  if (compiled.size() <= kMaxCompiledBytes && cache &&
      base::ImportantFileWriter::WriteFileAtomically(
          target.AppendASCII("compiled.dat"), compiled_view))
    base::ImportantFileWriter::WriteFileAtomically(
        target.AppendASCII("compiled.json"), *cache);
  // Preserve a last-good pointer before publishing. Recovery reads this when
  // state.json itself is corrupt, not just when the selected text is damaged.
  auto backup = base::WriteJson(base::DictValue()
                                    .Set("schema_version", 1)
                                    .Set("current", previous.generation)
                                    .Set("previous", rollback));
  base::DictValue checks;
  for (const auto& list : lists.subscriptions)
    checks.Set(list.info.url, list.info.checked_at.InSecondsFSinceUnixEpoch());
  auto state = base::WriteJson(
      base::DictValue()
          .Set("schema_version", 1)
          .Set("current", generation)
          .Set("previous", rollback)
          .Set("checked_at", lists.checked_at.is_null()
                                 ? 0.0
                                 : lists.checked_at.InSecondsFSinceUnixEpoch())
          .Set("list_checks", std::move(checks)));
  if (!backup || !state ||
      (!previous.generation.empty() &&
       !base::ImportantFileWriter::WriteFileAtomically(
           directory.AppendASCII(kBackupState), *backup)) ||
      !base::ImportantFileWriter::WriteFileAtomically(
          directory.AppendASCII(kState), *state))
    return false;
  // Keep the running generation as well as the pending pair's rollback.
  base::FileEnumerator files(generations, false,
                             base::FileEnumerator::DIRECTORIES);
  for (auto path = files.Next(); !path.empty(); path = files.Next()) {
    const auto name = path.BaseName().MaybeAsASCII();
    if (ValidGeneration(name) && name != generation && name != rollback &&
        name != running_generation && !base::IsLink(path))
      base::DeletePathRecursively(path);
  }
  return true;
}

}  // namespace

FilterListSet ReadFilterListSet(const base::FilePath& directory) {
  FilterListSet lists;
  const auto snapshot = ReadBaselineListStore(directory);
  lists.baseline_downloaded = snapshot.baseline_downloaded;
  lists.checked_at = snapshot.checked_at;
  if (snapshot.generation.empty()) {
    for (size_t i = 0; i < 2; ++i)
      lists.baseline[i] = kBundledBaselineOriginals[i];
    return lists;
  }
  const auto path =
      directory.AppendASCII("generations").AppendASCII(snapshot.generation);
  for (size_t i = 0; i < 2; ++i)
    if (!base::ReadFileToStringWithMaxSize(
            path.AppendASCII(kBaselineListFiles[i]), &lists.baseline[i],
            kMaxBaselineListBytes))
      return {};
  for (const auto& info : snapshot.subscriptions) {
    std::string original;
    if (!base::ReadFileToStringWithMaxSize(
            path.AppendASCII(SubscriptionFile(info.url)), &original,
            kMaxSubscribedListBytes))
      return {};
    lists.subscriptions.push_back({info, std::move(original)});
  }
  return lists;
}

bool InstallBaselineLists(const base::FilePath& directory,
                          const std::array<std::string, 2>& originals,
                          base::Time checked_at,
                          std::string_view running_generation) {
  if (checked_at.is_null())
    return false;
  auto lists = ReadFilterListSet(directory);
  lists.baseline = originals;
  lists.baseline_downloaded = true;
  lists.checked_at = checked_at;
  return WriteFilterListSet(directory, lists, running_generation);
}

std::string AddFilterSubscription(const base::FilePath& directory,
                                  std::string_view input,
                                  std::string body,
                                  std::string_view running_generation) {
  const auto url = CanonicalFilterSubscriptionURL(input);
  if (!url ||
      std::ranges::find(kBaselineListURLs, *url) != kBaselineListURLs.end())
    return "invalid-list-url";
  auto lists = ReadFilterListSet(directory);
  if (std::ranges::any_of(lists.subscriptions, [&](const auto& list) {
        return list.info.url == *url;
      }))
    return "duplicate-list";
  if (lists.subscriptions.size() >= kMaxFilterSubscriptions)
    return "too-many-lists";
  const auto selected = PreprocessSubscribedList(body);
  if (!selected ||
      !baseline_rules_valid(rust::Str(selected->data(), selected->size())))
    return "invalid-list";
  size_t bytes = body.size();
  for (const auto& list : lists.subscriptions)
    bytes += list.original.size();
  if (bytes > kMaxSubscriptionTotalBytes)
    return "list-storage-limit";
  lists.subscriptions.push_back(
      {{*url, ListTitle(body, *url), true, base::Time::Now()},
       std::move(body)});
  std::ranges::sort(lists.subscriptions, {},
                    [](const auto& list) { return list.info.url; });
  return WriteFilterListSet(directory, lists, running_generation)
             ? ""
             : "list-save-failed";
}

std::string ChangeFilterSubscription(const base::FilePath& directory,
                                     std::string_view url,
                                     std::optional<bool> enabled,
                                     std::string_view running_generation) {
  auto lists = ReadFilterListSet(directory);
  const auto found = std::ranges::find(
      lists.subscriptions, url, [](const auto& list) { return list.info.url; });
  if (found == lists.subscriptions.end())
    return "list-missing";
  if (enabled)
    found->info.enabled = *enabled;
  else
    lists.subscriptions.erase(found);
  return WriteFilterListSet(directory, lists, running_generation)
             ? ""
             : "list-save-failed";
}

FilterListUpdateResult UpdateFilterLists(
    const base::FilePath& directory,
    std::array<std::string, 2> baseline,
    std::vector<FilterListDownload> downloads,
    base::Time checked_at,
    std::string_view running_generation) {
  auto lists = ReadFilterListSet(directory);
  FilterListUpdateResult result;
  bool valid_pair = true;
  for (size_t i = 0; i < 2; ++i) {
    const auto selected = PreprocessBaselineList(baseline[i]);
    if (!selected || !HasExpectedTitle(baseline[i], i) ||
        !baseline_rules_valid(rust::Str(selected->data(), selected->size()))) {
      valid_pair = false;
      result.failed_urls.emplace_back(kBaselineListURLs[i]);
    }
  }
  if (valid_pair) {
    lists.baseline = std::move(baseline);
    lists.baseline_downloaded = true;
    lists.checked_at = checked_at;
  }
  bool changed = valid_pair;
  size_t total_bytes = 0;
  for (const auto& list : lists.subscriptions)
    total_bytes += list.original.size();
  for (auto& download : downloads) {
    const auto found =
        std::ranges::find(lists.subscriptions, download.url,
                          [](const auto& list) { return list.info.url; });
    if (found == lists.subscriptions.end() || !found->info.enabled)
      continue;
    const auto selected = PreprocessSubscribedList(download.body);
    const size_t new_bytes =
        total_bytes - found->original.size() + download.body.size();
    if (!selected ||
        !baseline_rules_valid(rust::Str(selected->data(), selected->size())) ||
        new_bytes > kMaxSubscriptionTotalBytes) {
      result.failed_urls.push_back(download.url);
      continue;
    }
    total_bytes = new_bytes;
    found->original = std::move(download.body);
    found->info.checked_at = checked_at;
    changed = true;
  }
  result.succeeded = changed &&
                     WriteFilterListSet(directory, lists, running_generation) &&
                     result.failed_urls.empty();
  return result;
}
}  // namespace yee::content_blocking
