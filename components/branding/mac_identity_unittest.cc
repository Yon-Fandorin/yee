// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/yee_branding/mac_identity.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee::identity {

TEST(MacIdentityTest, InstalledAppUsesIndependentKeychainNames) {
  EXPECT_STREQ("Yee Safe Storage",
               KeychainServiceName(kMacBundleId, "Chromium Safe Storage"));
  EXPECT_STREQ("Yee", KeychainAccountName(kMacBundleId, "Chromium"));
}

TEST(MacIdentityTest, DevelopmentAndOtherBrowsersKeepExistingKeychainNames) {
  for (const auto* bundle_id : {"org.chromium.Chromium", "com.google.Chrome",
                                "io.github.yon-fandorin.yee.helper", ""}) {
    EXPECT_STREQ("Chromium Safe Storage",
                 KeychainServiceName(bundle_id, "Chromium Safe Storage"));
    EXPECT_STREQ("Chromium", KeychainAccountName(bundle_id, "Chromium"));
  }
}

}  // namespace yee::identity
