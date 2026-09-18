// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/page_viewport_migration.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

TEST(PageViewportMigrationTest, ApprovedFamiliesUseTheirPersistentHosts) {
  size_t approved_count = 0;
  size_t target_host_count = 0;
  size_t overlay_host_count = 0;

  for (size_t index = 0; index < kPageViewportChildFamilyCount; ++index) {
    const auto family = static_cast<PageViewportChildFamily>(index);
    const PageViewportChildPolicy policy = GetPageViewportChildPolicy(family);
    if (policy.status == PageViewportMigrationStatus::kApproved) {
      ++approved_count;
    }
    if (policy.host == PageViewportChildHost::kPageTargetHost) {
      ++target_host_count;
    }
    if (policy.host == PageViewportChildHost::kViewportOverlayHost) {
      ++overlay_host_count;
    }
  }

  EXPECT_EQ(8U, approved_count);
  EXPECT_EQ(5U, target_host_count);
  EXPECT_EQ(3U, overlay_host_count);

  for (PageViewportChildFamily family : {
           PageViewportChildFamily::kContentsWebView,
           PageViewportChildFamily::kDataProtectionOverlay,
           PageViewportChildFamily::kIndigoOverlay,
           PageViewportChildFamily::kReadAnythingImmersive,
           PageViewportChildFamily::kActorOverlay,
       }) {
    EXPECT_EQ((PageViewportChildPolicy{PageViewportChildHost::kPageTargetHost,
                                       PageViewportMigrationStatus::kApproved}),
              GetPageViewportChildPolicy(family));
  }

  for (PageViewportChildFamily family : {
           PageViewportChildFamily::kAiOverlayDialog,
           PageViewportChildFamily::kGlicSelection,
           PageViewportChildFamily::kToastAnchor,
       }) {
    EXPECT_EQ(
        (PageViewportChildPolicy{PageViewportChildHost::kViewportOverlayHost,
                                 PageViewportMigrationStatus::kApproved}),
        GetPageViewportChildPolicy(family));
  }
}

TEST(PageViewportMigrationTest, CardChromeRemainsPermanentlyDirect) {
  for (PageViewportChildFamily family : {
           PageViewportChildFamily::kGlicContextBorder,
           PageViewportChildFamily::kPaneHeader,
           PageViewportChildFamily::kPaneEmphasis,
           PageViewportChildFamily::kContainerOutline,
           PageViewportChildFamily::kCaptureBorder,
       }) {
    EXPECT_EQ((PageViewportChildPolicy{
                  PageViewportChildHost::kContentsContainer,
                  PageViewportMigrationStatus::kPermanentDirect}),
              GetPageViewportChildPolicy(family));
  }
}

TEST(PageViewportMigrationTest, NativePartitionFamiliesRemainDirect) {
  for (PageViewportChildFamily family : {
           PageViewportChildFamily::kDevToolsWebView,
           PageViewportChildFamily::kDevToolsScrim,
           PageViewportChildFamily::kNtpFooter,
           PageViewportChildFamily::kContentsScrim,
       }) {
    EXPECT_EQ((PageViewportChildPolicy{
                  PageViewportChildHost::kContentsContainer,
                  PageViewportMigrationStatus::kBlockedByFamilyGates}),
              GetPageViewportChildPolicy(family));
  }
}

}  // namespace
}  // namespace yee
