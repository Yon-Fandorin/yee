// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/settings.h"
#include "components/yee_content_blocking/filter_data.h"

#include "base/command_line.h"
#include "base/test/scoped_command_line.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
TEST(ContentBlockingSettings, ExactHostExceptionIsCaseInsensitive) {
  base::test::ScopedCommandLine command;
  command.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", " EXAMPLE.com , other.test ");
  EXPECT_FALSE(EnabledForSite(GURL("https://example.com/path")));
  EXPECT_TRUE(EnabledForSite(GURL("https://sub.example.com/path")));
  EXPECT_TRUE(EnabledForSite(GURL("https://notexample.com/path")));
  EXPECT_FALSE(EnabledForSite(GURL()));
  EXPECT_FALSE(EnabledForSite(GURL("chrome://settings/")));
}
TEST(ContentBlockingSettings, DisabledFeatureAndChildSettings) {
  base::test::ScopedFeatureList feature;
  feature.InitAndDisableFeature(kYeeContentBlocking);
  EXPECT_FALSE(EnabledForSite(GURL("https://page.test/")));
  base::test::ScopedCommandLine command;
  command.GetProcessCommandLine()->AppendSwitchASCII(
      "yee-content-blocking-disabled-sites", "page.test");
  command.GetProcessCommandLine()->AppendSwitch(
      "yee-content-blocking-test-rules");
  const base::FilePath filter_path(FILE_PATH_LITERAL("/tmp/Yee filters"));
  command.GetProcessCommandLine()->AppendSwitchPath(
      kCommunityFilterDirectorySwitch, filter_path);
  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  CopySettingsToChild(&child);
  EXPECT_EQ(child.GetSwitchValueASCII("yee-content-blocking-disabled-sites"),
            "page.test");
  EXPECT_TRUE(child.HasSwitch("yee-content-blocking-test-rules"));
  EXPECT_EQ(child.GetSwitchValuePath(kCommunityFilterDirectorySwitch),
            filter_path);
}
}  // namespace yee::content_blocking
