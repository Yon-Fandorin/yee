// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_SELECTOR_STYLES_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_SELECTOR_STYLES_H_

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace yee::content_blocking {
struct StyleUpdate {
  size_t index;
  std::string css;
};

// One document's deduplicated styles, split into bounded replaceable sheets.
class SelectorStyles {
 public:
  SelectorStyles();
  ~SelectorStyles();

  static constexpr size_t kChunkBytes = 64 * 1024;
  static constexpr size_t kMaxBytes = 4 * 1024 * 1024;
  static constexpr size_t kMaxSelectors = 65536;
  std::vector<StyleUpdate> Add(const std::vector<std::string>& selectors);
  bool Contains(const std::string& selector) const;
  void Reset();

 private:
  std::set<std::string, std::less<>> selectors_;
  std::vector<std::string> chunks_;
  size_t bytes_ = 0;
};
}  // namespace yee::content_blocking
#endif
