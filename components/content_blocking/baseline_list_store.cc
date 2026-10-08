// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/baseline_list_store.h"

#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/memory/shared_memory_switch.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "url/gurl.h"

namespace yee::content_blocking {
namespace {
constexpr size_t kMaxCompiledBytes = 64 * 1024 * 1024;
constexpr size_t kMaxSelectedBytes =
    2 * kMaxBaselineListBytes + kMaxSubscriptionTotalBytes + 4096;
bool ValidGeneration(std::string_view value) {
  return value.size() == 64 &&
         base::ContainsOnlyChars(value, "0123456789abcdef");
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

namespace {
std::optional<std::string> PreprocessList(std::string_view text,
                                          bool require_header) {
  if (text.empty() || text.size() > kMaxBaselineListBytes ||
      !base::IsStringUTF8(text) || text.find('\0') != std::string_view::npos ||
      (require_header && !text.starts_with("[Adblock Plus ")))
    return std::nullopt;
  const auto content = base::TrimWhitespaceASCII(text, base::TRIM_ALL);
  if (content.starts_with('<') || content.starts_with('{'))
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

}  // namespace

std::optional<std::string> PreprocessBaselineList(std::string_view text) {
  return PreprocessList(text, true);
}
std::optional<std::string> PreprocessSubscribedList(std::string_view text) {
  if (text.size() > kMaxSubscribedListBytes)
    return std::nullopt;
  if (text.starts_with("\xef\xbb\xbf"))
    text.remove_prefix(3);
  return PreprocessList(text, false);
}
std::optional<std::string> CanonicalFilterSubscriptionURL(
    std::string_view input) {
  const GURL url(base::TrimWhitespaceASCII(input, base::TRIM_ALL));
  if (input.size() > 2048 || !url.is_valid() || !url.SchemeIs("https") ||
      url.host().empty() || url.has_username() || url.has_password() ||
      url.has_ref())
    return std::nullopt;
  return url.spec();
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
