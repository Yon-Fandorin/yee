// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/catalog_filter_data.h"

#include <set>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "components/yee_content_blocking/baseline_list_store.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/filter_data.h"
#include "components/yee_content_blocking/rust/src/lib.rs.h"
#include "url/gurl.h"

namespace yee::content_blocking {
namespace {
constexpr std::string_view kPrivacy = "4D715457-307C-4383-8873-73A2FD263F71";
constexpr std::string_view kKorean = "45B3ED40-C607-454F-A623-195FDD084637";
rust::Str Str(std::string_view text) {
  return {text.data(), text.size()};
}
}  // namespace

std::optional<std::vector<CatalogFilterSource>> ParseBraveCatalog(
    std::string_view catalog) {
  if (catalog.size() > kMaxCatalogBytes || !base::IsStringUTF8(catalog))
    return std::nullopt;
  const auto entries =
      base::JSONReader::ReadList(catalog, base::JSON_PARSE_RFC);
  if (!entries || entries->size() > 256)
    return std::nullopt;
  std::vector<CatalogFilterSource> result;
  std::set<std::string> groups, urls;
  for (const auto& value : *entries) {
    if (!value.is_dict())
      return std::nullopt;
    const auto& entry = value.GetDict();
    const auto* uuid = entry.FindString("uuid");
    if (!uuid || (*uuid != "default" && *uuid != kPrivacy && *uuid != kKorean))
      continue;
    if (!groups.insert(*uuid).second)
      return std::nullopt;
    const auto* sources = entry.FindList("sources");
    const auto permission = entry.FindInt("permission_mask").value_or(0);
    if (!sources || sources->empty() || permission < 0 || permission > 1)
      return std::nullopt;
    for (const auto& source : *sources) {
      if (!source.is_dict())
        return std::nullopt;
      const auto* url = source.GetDict().FindString("url");
      const auto* format = source.GetDict().FindString("format");
      if (!url || CanonicalFilterSubscriptionURL(*url) != *url || !format ||
          *format != "Standard" || !urls.insert(*url).second)
        return std::nullopt;
      const auto* title = source.GetDict().FindString("title");
      result.push_back(
          {*url, title ? title->substr(0, 256) : *uuid, permission == 1});
      if (result.size() > kMaxCatalogSources)
        return std::nullopt;
    }
  }
  if (groups.size() != 3)
    return std::nullopt;
  return result;
}

std::optional<PreparedCatalogFilters> PrepareCatalogFilters(
    const CatalogFilterData& data) {
  const auto sources = ParseBraveCatalog(data.catalog);
  if (!sources || data.downloads.size() != sources->size())
    return std::nullopt;
  PreparedCatalogFilters result;
  size_t total = 0;
  for (size_t i = 0; i < sources->size(); ++i) {
    const auto& source = (*sources)[i];
    const auto& download = data.downloads[i];
    total += download.body.size();
    if (download.url != source.url || download.body.empty() ||
        download.body.size() > kMaxCatalogSourceBytes ||
        total > kMaxCatalogTotalBytes)
      return std::nullopt;
    const auto selected = PreprocessCatalogList(download.body);
    if (!selected) {
      LOG(ERROR) << "Invalid Yee catalog filter text: " << source.url;
      return std::nullopt;
    }
    std::string expanded;
    for (auto line : base::SplitStringPiece(
             *selected, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
      const auto directive = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
      if (directive.starts_with("!#include ")) {
        const auto included = GURL(source.url)
                                  .Resolve(base::TrimWhitespaceASCII(
                                      directive.substr(10), base::TRIM_ALL));
        // 같은 카탈로그에 등록된 원문만 한 번 합친다. 외부 include는 묶음
        // 전체를 거부한다.
        bool found = false;
        for (const auto& candidate : *sources)
          found |= candidate.url == included.spec();
        if (!found)
          return std::nullopt;
        continue;
      }
      expanded.append(line);
      expanded.push_back('\n');
    }
    const auto compatible = select_catalog_rules(
        Str(expanded), Str(kBundledResources),
        Str(CommunityFilterData().resources), source.trusted);
    if (!compatible.valid) {
      LOG(ERROR) << "Incompatible Yee catalog filter syntax: " << source.url;
      return std::nullopt;
    }
    (source.trusted ? result.trusted_filters : result.filters)
        .append(compatible.rules.data(), compatible.rules.size());
    result.omitted_rules += compatible.omitted_rules;
    for (const auto& example : compatible.omitted_examples)
      if (result.omitted_examples.size() < 20)
        result.omitted_examples.push_back(
            source.url + ": " + std::string(example.data(), example.size()));
  }
  return result;
}

std::optional<std::string> SerializeCatalogFilterData(
    const CatalogFilterData& data) {
  base::ListValue downloads;
  for (const auto& source : data.downloads)
    downloads.Append(
        base::DictValue().Set("url", source.url).Set("body", source.body));
  auto text = base::WriteJson(base::DictValue()
                                  .Set("catalog", data.catalog)
                                  .Set("downloads", std::move(downloads)));
  if (!text || text->size() > kMaxCatalogDataBytes)
    return std::nullopt;
  return text;
}

std::optional<CatalogFilterData> ParseCatalogFilterData(std::string_view text) {
  if (text.size() > kMaxCatalogDataBytes)
    return std::nullopt;
  const auto dict = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  if (!dict)
    return std::nullopt;
  const auto* catalog = dict->FindString("catalog");
  const auto* downloads = dict->FindList("downloads");
  if (!catalog || !downloads || downloads->size() > kMaxCatalogSources)
    return std::nullopt;
  CatalogFilterData result{*catalog, {}};
  for (const auto& value : *downloads) {
    if (!value.is_dict())
      return std::nullopt;
    const auto* url = value.GetDict().FindString("url");
    const auto* body = value.GetDict().FindString("body");
    if (!url || !body)
      return std::nullopt;
    result.downloads.push_back({*url, *body});
  }
  return result;
}
}  // namespace yee::content_blocking
