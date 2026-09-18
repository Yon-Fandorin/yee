// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/selector_styles.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
TEST(ContentBlockingStyles, RepeatedDiscoveryNeverAddsDuplicateRules) {
  SelectorStyles styles;
  EXPECT_EQ(styles.Add({".ad", ".ad"}).size(), 1u);
  for (int cycle = 0; cycle < 100; ++cycle)
    EXPECT_TRUE(styles.Add({".ad"}).empty());
  auto updates = styles.Add({".new-ad"});
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].index, 0u);
  EXPECT_EQ(updates[0].css,
            ".ad{display:none!important;}\n.new-ad{display:none!important;}\n");
}
TEST(ContentBlockingStyles, UpdatesOnlyMutableTailAndNewChunks) {
  SelectorStyles styles;
  const std::string large = "." + std::string(40000, 'a');
  auto first = styles.Add({large});
  ASSERT_EQ(first.size(), 1u);
  auto second = styles.Add({"." + std::string(40000, 'b')});
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].index, 1u);
  auto third = styles.Add({".small"});
  ASSERT_EQ(third.size(), 1u);
  EXPECT_EQ(third[0].index, 1u);
  EXPECT_LE(third[0].css.size(), SelectorStyles::kChunkBytes);
}
TEST(ContentBlockingStyles, DocumentResetAllowsSameSelectorInNextDocument) {
  SelectorStyles styles;
  styles.Add({".ad"});
  styles.Reset();
  auto updates = styles.Add({".ad"});
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].index, 0u);
}
TEST(ContentBlockingStyles, OversizedRuleDoesNotDiscardValidSelectors) {
  SelectorStyles styles;
  auto updates =
      styles.Add({std::string(SelectorStyles::kChunkBytes, 'x'), ".ad"});
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].css, ".ad{display:none!important;}\n");
}
TEST(ContentBlockingStyles, TotalCssAndSheetCountStayBounded) {
  SelectorStyles styles;
  std::vector<std::string> selectors;
  for (int i = 0; i < 300; ++i)
    selectors.push_back(".s" + std::to_string(i) + std::string(40000, 'x'));
  auto updates = styles.Add(selectors);
  size_t bytes = 0;
  for (const auto& update : updates) {
    bytes += update.css.size();
    EXPECT_LE(update.css.size(), SelectorStyles::kChunkBytes);
  }
  EXPECT_LE(bytes, SelectorStyles::kMaxBytes);
  EXPECT_LT(updates.size(), selectors.size());
}
}  // namespace yee::content_blocking
