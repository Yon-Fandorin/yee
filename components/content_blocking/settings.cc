// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/settings.h"
#include "base/command_line.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/yee_content_blocking/baseline_list_store.h"
#include "components/yee_content_blocking/filter_data.h"

namespace yee::content_blocking {
BASE_FEATURE(kYeeContentBlocking,
             "YeeContentBlocking",
             base::FEATURE_ENABLED_BY_DEFAULT);
namespace {
constexpr char kDisabledSites[] = "yee-content-blocking-disabled-sites";
constexpr char kTestRules[] = "yee-content-blocking-test-rules";
}  // namespace
bool EnabledForSite(const GURL& site) {
  if (!base::FeatureList::IsEnabled(kYeeContentBlocking))
    return false;
  if (!site.SchemeIsHTTPOrHTTPS())
    return false;
  for (const auto& host : base::SplitString(
           base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
               kDisabledSites),
           ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    // Exact host only. Do not silently extend an exception to sibling sites.
    if (base::EqualsCaseInsensitiveASCII(site.host(), host))
      return false;
  }
  return true;
}
bool TestRulesEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(kTestRules);
}
void CopySettingsToChild(base::CommandLine* child) {
  constexpr const char* switches[] = {
      kDisabledSites, kTestRules, kCommunityFilterDirectorySwitch,
      kBaselineListDirectorySwitch, kBaselineListGenerationSwitch};
  child->CopySwitchesFrom(*base::CommandLine::ForCurrentProcess(), switches);
}
}  // namespace yee::content_blocking
