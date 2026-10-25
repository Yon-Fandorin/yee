// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_CATALOG_FILTER_DATA_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_CATALOG_FILTER_DATA_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yee::content_blocking {
inline constexpr char kBraveCatalogURL[] =
    "https://raw.githubusercontent.com/brave/adblock-resources/master/"
    "filter_lists/list_catalog.json";
inline constexpr size_t kMaxCatalogBytes = 512 * 1024;
inline constexpr size_t kMaxCatalogSources = 64;
inline constexpr size_t kMaxCatalogSourceBytes = 4 * 1024 * 1024;
inline constexpr size_t kMaxCatalogTotalBytes = 24 * 1024 * 1024;
inline constexpr size_t kMaxCatalogDataBytes = 2 * kMaxCatalogTotalBytes;
inline constexpr int kCatalogCompatibilityVersion = 1;

struct CatalogFilterSource {
  std::string url;
  std::string title;
  bool trusted = false;
};
struct CatalogFilterDownload {
  std::string url;
  std::string body;
};
struct CatalogFilterData {
  std::string catalog;
  std::vector<CatalogFilterDownload> downloads;
};
struct PreparedCatalogFilters {
  std::string filters;
  std::string trusted_filters;
  int omitted_rules = 0;
  std::vector<std::string> omitted_examples;
};

// 공식 기본 광고·개인정보 목록과 한국어 목록만 선택한다. 네트워크나 저장소는
// 읽지 않는다.
std::optional<std::vector<CatalogFilterSource>> ParseBraveCatalog(
    std::string_view catalog);
// 원문은 보존하고, 설치된 실행 리소스와 호환되는 규칙만 엔진 입력으로 만든다.
std::optional<PreparedCatalogFilters> PrepareCatalogFilters(
    const CatalogFilterData& data);
std::optional<std::string> SerializeCatalogFilterData(
    const CatalogFilterData& data);
std::optional<CatalogFilterData> ParseCatalogFilterData(std::string_view text);
}  // namespace yee::content_blocking
#endif
