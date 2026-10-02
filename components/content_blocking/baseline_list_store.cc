// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/baseline_list_store.h"

#include <cmath>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/shared_memory_switch.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
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
constexpr size_t kMetadataBytes = 16 * 1024;
constexpr size_t kMaxCompiledBytes = 64 * 1024 * 1024;
// Preprocessing can append a final newline to each original list.
constexpr size_t kMaxSelectedBytes = 2 * kMaxBaselineListBytes + 2;

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
  if (!result || result->FindInt("schema_version") != 1)
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
          *checked > 0)
        snapshot.checked_at = base::Time::FromSecondsSinceUnixEpoch(*checked);
      return snapshot;
    }
  }
  return snapshot;
}
struct StartupData {
  bool initialized = false;
  BaselineListSnapshot snapshot;
  base::ReadOnlySharedMemoryRegion region;
};
StartupData& Data() {
  static base::NoDestructor<StartupData> data;
  return *data;
}
}  // namespace

std::optional<std::string> PreprocessBaselineList(std::string_view text) {
  if (text.empty() || text.size() > kMaxBaselineListBytes ||
      !base::IsStringUTF8(text) || text.find('\0') != std::string_view::npos ||
      !text.starts_with("[Adblock Plus "))
    return std::nullopt;
  struct Branch {
    bool parent;
    std::optional<bool> condition;
    bool has_else;
  };
  std::vector<Branch> stack;
  bool active = true, has_rule = false;
  std::string output;
  for (auto line : base::SplitStringPiece(text, "\n", base::KEEP_WHITESPACE,
                                          base::SPLIT_WANT_ALL)) {
    const auto directive = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
    if (directive.starts_with("!#if ")) {
      auto name =
          base::TrimWhitespaceASCII(directive.substr(5), base::TRIM_ALL);
      const bool negate = name.starts_with('!');
      if (negate)
        name.remove_prefix(1);
      if (name.empty() || stack.size() >= 64)
        return std::nullopt;
      std::optional<bool> condition;
      for (const auto& [known, value] : kFilterConditions)
        if (name == known)
          condition = negate ? !value : value;
      stack.push_back({active, condition, false});
      active = active && condition != false;
    } else if (directive == "!#else") {
      if (stack.empty() || stack.back().has_else)
        return std::nullopt;
      stack.back().has_else = true;
      active = stack.back().parent && stack.back().condition != true;
    } else if (directive == "!#endif") {
      if (stack.empty())
        return std::nullopt;
      active = stack.back().parent;
      stack.pop_back();
    } else if (directive.starts_with("!#")) {
      // Includes require an independently validated download. Never silently
      // accept an incomplete list or malformed preprocessing directive.
      return std::nullopt;
    } else if (active) {
      output.append(line);
      output.push_back('\n');
      has_rule |=
          !directive.empty() && directive[0] != '!' && directive[0] != '[';
    }
  }
  if (!stack.empty() || !has_rule)
    return std::nullopt;
  return output;
}

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
  if (!entries || entries->size() != 2 || !time || !std::isfinite(*time) ||
      *time <= 0)
    return snapshot;
  std::array<std::string, 2> checksums;
  for (size_t index = 0; index < entries->size(); ++index) {
    if (!(*entries)[index].is_dict())
      return {};
    const auto& entry = (*entries)[index].GetDict();
    const auto* file = entry.FindString("file");
    const auto* url = entry.FindString("source");
    const auto* checksum = entry.FindString("sha256");
    if (!file || *file != kBaselineListFiles[index] || !url ||
        *url != kBaselineListURLs[index] || !checksum ||
        !ValidGeneration(*checksum))
      return {};
    const auto file_path = path.AppendASCII(*file);
    std::string original;
    if (base::IsLink(file_path) ||
        !base::ReadFileToStringWithMaxSize(file_path, &original,
                                           kMaxBaselineListBytes) ||
        Digest(original) != *checksum || !HasExpectedTitle(original, index))
      return {};
    auto selected = PreprocessBaselineList(original);
    if (!selected)
      return {};
    checksums[index] = *checksum;
    snapshot.filters += *selected;
  }
  if (Digest(checksums[0] + "\n" + checksums[1]) != generation)
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

bool InstallBaselineLists(const base::FilePath& directory,
                          const std::array<std::string, 2>& originals,
                          base::Time checked_at,
                          std::string_view running_generation) {
  if (!SafeDirectory(directory) || checked_at.is_null())
    return false;
  std::string selected_filters;
  for (size_t index = 0; index < originals.size(); ++index) {
    const auto& original = originals[index];
    const auto selected = PreprocessBaselineList(original);
    if (!selected || !HasExpectedTitle(original, index) ||
        !baseline_rules_valid(rust::Str(selected->data(), selected->size())))
      return false;
    selected_filters += *selected;
  }
  const auto previous = ReadBaselineListStore(directory);
  const std::array checksums = {Digest(originals[0]), Digest(originals[1])};
  const auto generation = Digest(checksums[0] + "\n" + checksums[1]);
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
    base::ListValue entries;
    for (size_t index = 0; index < originals.size(); ++index) {
      if (!base::ImportantFileWriter::WriteFileAtomically(
              temporary.GetPath().AppendASCII(kBaselineListFiles[index]),
              originals[index]))
        return false;
      entries.Append(
          base::DictValue()
              .Set("file", kBaselineListFiles[index])
              .Set("source", kBaselineListURLs[index])
              .Set("sha256", checksums[index])
              .Set("attribution", "The EasyList authors (https://easylist.to/)")
              .Set("license", "CC-BY-SA-3.0-or-later")
              .Set("license_url", "https://easylist.to/pages/licence.html"));
    }
    auto manifest = base::WriteJson(
        base::DictValue()
            .Set("schema_version", 1)
            .Set("checked_at", checked_at.InSecondsFSinceUnixEpoch())
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
  auto state = base::WriteJson(
      base::DictValue()
          .Set("schema_version", 1)
          .Set("current", generation)
          .Set("previous", rollback)
          .Set("checked_at", checked_at.InSecondsFSinceUnixEpoch()));
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

base::ReadOnlySharedMemoryRegion CreateBaselineListRegion(
    const BaselineListSnapshot& snapshot) {
  if ((!snapshot.generation.empty() && !ValidGeneration(snapshot.generation)) ||
      snapshot.filters.size() > kMaxSelectedBytes ||
      snapshot.compiled_filters.size() > kMaxCompiledBytes ||
      !base::IsStringUTF8(snapshot.filters) ||
      snapshot.filters.find('\0') != std::string::npos ||
      (!snapshot.generation.empty() && snapshot.filters.empty()) ||
      (snapshot.generation.empty() &&
       (!snapshot.filters.empty() || !snapshot.compiled_filters.empty())))
    return {};
  base::Pickle pickle;
  pickle.WriteUInt32(1);
  pickle.WriteString(snapshot.generation);
  pickle.WriteString(snapshot.filters);
  pickle.WriteString(snapshot.compiled_filters);
  auto memory = base::ReadOnlySharedMemoryRegion::Create(pickle.size());
  if (!memory.IsValid())
    return {};
  base::span(memory.mapping).copy_from(pickle.AsBytes());
  return std::move(memory.region);
}

std::optional<BaselineListSnapshot> ReadBaselineListRegion(
    const base::ReadOnlySharedMemoryRegion& region) {
  if (!region.IsValid() || region.GetSize() > kMaxBaselineListSnapshotBytes)
    return std::nullopt;
  const auto mapping = region.Map();
  if (!mapping.IsValid())
    return std::nullopt;
  auto reader = base::PickleIterator::WithData(base::span(mapping));
  uint32_t version;
  std::string_view generation, filters, compiled;
  if (!reader.ReadUInt32(&version) || version != 1 ||
      !reader.ReadStringPiece(&generation) ||
      !reader.ReadStringPiece(&filters) || !reader.ReadStringPiece(&compiled) ||
      !reader.ReachedEnd() ||
      (!generation.empty() && !ValidGeneration(generation)) ||
      filters.size() > kMaxSelectedBytes ||
      compiled.size() > kMaxCompiledBytes || !base::IsStringUTF8(filters) ||
      filters.find('\0') != std::string_view::npos ||
      (generation.empty() && (!filters.empty() || !compiled.empty())) ||
      (!generation.empty() && filters.empty()))
    return std::nullopt;
  return BaselineListSnapshot{std::string(generation), std::string(filters),
                              std::string(compiled)};
}

const base::ReadOnlySharedMemoryRegion& BaselineListRegion() {
  return Data().region;
}

void InitializeBaselineListsBeforeSandbox(const base::FilePath& user_data_dir,
                                          bool browser_process) {
  auto& data = Data();
  CHECK(!data.initialized);
  auto* command = base::CommandLine::ForCurrentProcess();
  if (browser_process && !user_data_dir.empty()) {
    const auto absolute = base::MakeAbsoluteFilePath(user_data_dir);
    if (absolute.empty()) {
      data.initialized = true;
      return;
    }
    const auto directory = absolute.AppendASCII("YeeContentBlockingLists");
    data.snapshot = ReadBaselineListStore(directory);
    data.region = CreateBaselineListRegion(data.snapshot);
    // Allocation failure must leave browser and renderer on the same baseline.
    if (!data.region.IsValid())
      data.snapshot = {};
    command->AppendSwitchPath(kBaselineListDirectorySwitch, directory);
    command->AppendSwitchASCII(kBaselineListGenerationSwitch,
                               data.snapshot.generation.empty()
                                   ? "bundled"
                                   : data.snapshot.generation);
  } else if (!browser_process) {
    auto region = base::shared_memory::ReadOnlySharedMemoryRegionFrom(
        command->GetSwitchValueASCII(kBaselineListHandleSwitch),
        kMaxBaselineListSnapshotBytes);
    if (region.has_value()) {
      auto snapshot = ReadBaselineListRegion(*region);
      if (snapshot &&
          (snapshot->generation.empty() ? "bundled" : snapshot->generation) ==
              command->GetSwitchValueASCII(kBaselineListGenerationSwitch))
        data.snapshot = std::move(*snapshot);
    }
  }
  data.initialized = true;
}
const BaselineListSnapshot& BaselineLists() {
  return Data().snapshot;
}
}  // namespace yee::content_blocking
