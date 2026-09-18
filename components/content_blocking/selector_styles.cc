// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/selector_styles.h"

#include <algorithm>
#include <string_view>

namespace yee::content_blocking {
SelectorStyles::SelectorStyles() = default;
SelectorStyles::~SelectorStyles() = default;

std::vector<StyleUpdate> SelectorStyles::Add(
    const std::vector<std::string>& selectors) {
  constexpr std::string_view suffix = "{display:none!important;}\n";
  size_t first_changed = chunks_.size();
  bool changed = false;
  for (const auto& selector : selectors) {
    if (selector.empty() || selectors_.contains(selector))
      continue;
    const size_t size = selector.size() + suffix.size();
    if (size > kChunkBytes || bytes_ + size > kMaxBytes ||
        selectors_.size() >= kMaxSelectors)
      continue;
    selectors_.insert(selector);
    if (chunks_.empty() || chunks_.back().size() + size > kChunkBytes)
      chunks_.emplace_back();
    first_changed = std::min(first_changed, chunks_.size() - 1);
    chunks_.back().append(selector).append(suffix);
    bytes_ += size;
    changed = true;
  }
  std::vector<StyleUpdate> updates;
  if (changed) {
    for (size_t index = first_changed; index < chunks_.size(); ++index)
      updates.push_back({index, chunks_[index]});
  }
  return updates;
}
void SelectorStyles::Reset() {
  selectors_.clear();
  chunks_.clear();
  bytes_ = 0;
}
bool SelectorStyles::Contains(const std::string& selector) const {
  return selectors_.contains(selector);
}
}  // namespace yee::content_blocking
