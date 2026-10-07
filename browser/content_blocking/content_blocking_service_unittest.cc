// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_content_blocking/content_blocking_service.h"

#include <memory>

#include "base/test/scoped_feature_list.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/yee_content_blocking/settings.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {

class ContentBlockingServiceTest : public testing::Test {
 public:
  ContentBlockingServiceTest() {
    feature_list_.InitAndEnableFeature(kYeeContentBlocking);
    prefs_.registry()->RegisterListPref(kDisabledSitesPref);
    prefs_.registry()->RegisterListPref(kBlockedDomainsPref);
    RecreateService();
  }

 protected:
  void RecreateService() {
    service_ = std::make_unique<ContentBlockingService>(&prefs_, false);
  }

  base::test::ScopedFeatureList feature_list_;
  TestingPrefServiceSimple prefs_;
  std::unique_ptr<ContentBlockingService> service_;
};

TEST_F(ContentBlockingServiceTest, PersistsExactHostException) {
  const GURL site("https://www.example.test/watch");
  const GURL same_host("http://www.example.test/elsewhere");
  const GURL sibling("https://media.example.test/watch");
  const scoped_refptr<ContentBlockingSettingsSnapshot> snapshot =
      service_->settings_snapshot();

  EXPECT_TRUE(service_->EnabledForSite(site));
  service_->SetEnabledForSite(site, false);
  EXPECT_FALSE(service_->EnabledForSite(site));
  EXPECT_FALSE(snapshot->EnabledForSite(same_host));
  EXPECT_TRUE(service_->EnabledForSite(sibling));

  RecreateService();
  EXPECT_FALSE(service_->EnabledForSite(site));
  service_->SetEnabledForSite(site, true);
  EXPECT_TRUE(service_->EnabledForSite(site));
}

TEST_F(ContentBlockingServiceTest, BuildsHostRuleBeforeDefaultRule) {
  const GURL site("https://www.example.test/watch");
  service_->SetEnabledForSite(site, false);

  const ContentSettingsForOneType rules = service_->GetRendererRules();
  ASSERT_EQ(2u, rules.size());
  EXPECT_TRUE(rules[0].primary_pattern.Matches(site));
  EXPECT_FALSE(rules[0].primary_pattern.Matches(
      GURL("https://media.example.test/watch")));
  EXPECT_EQ(CONTENT_SETTING_BLOCK,
            content_settings::ValueToContentSetting(rules[0].setting_value));
  EXPECT_TRUE(rules[1].primary_pattern.MatchesAllHosts());
  EXPECT_EQ(CONTENT_SETTING_ALLOW,
            content_settings::ValueToContentSetting(rules[1].setting_value));
}

TEST_F(ContentBlockingServiceTest, RejectsUnsupportedUrls) {
  EXPECT_FALSE(service_->EnabledForSite(GURL("file:///tmp/page.html")));
  EXPECT_FALSE(service_->EnabledForSite(GURL("about:blank")));
  service_->SetEnabledForSite(GURL("file:///tmp/page.html"), false);
  EXPECT_TRUE(prefs_.GetList(kDisabledSitesPref).empty());
}

TEST_F(ContentBlockingServiceTest, DomainScopeAndChangesReachExistingSnapshot) {
  const auto snapshot = service_->settings_snapshot();
  EXPECT_TRUE(service_->SetBlockedDomain("Ads.Example.test.", false));
  EXPECT_TRUE(
      snapshot->IsBlockedDomain(GURL("https://ads.example.test/request")));
  EXPECT_TRUE(
      snapshot->IsBlockedDomain(GURL("https://ads.example.test./request")));
  EXPECT_FALSE(snapshot->IsBlockedDomain(
      GURL("https://child.ads.example.test/request")));
  EXPECT_FALSE(
      snapshot->IsBlockedDomain(GURL("https://notads.example.test/request")));
  EXPECT_TRUE(service_->SetBlockedDomain("ads.example.test", true));
  EXPECT_TRUE(snapshot->IsBlockedDomain(
      GURL("http://child.ads.example.test:8000/request")));
  EXPECT_FALSE(
      snapshot->IsBlockedDomain(GURL("file://ads.example.test/request")));
  RecreateService();
  EXPECT_EQ((std::vector<BlockedDomain>{{"ads.example.test", true}}),
            service_->BlockedDomains());
  service_->RemoveBlockedDomain("ADS.EXAMPLE.TEST");
  EXPECT_TRUE(service_->BlockedDomains().empty());
  EXPECT_FALSE(service_->settings_snapshot()->IsBlockedDomain(
      GURL("https://ads.example.test/")));
}

TEST_F(ContentBlockingServiceTest,
       ImportMergesWithoutOverwritingExistingScope) {
  ASSERT_TRUE(service_->SetBlockedDomain("ads.example.test", false));
  EXPECT_EQ(1u, service_->ImportBlockedDomains(
                    {{"ADS.example.test.", true}, {"new.example.test", true}}));
  EXPECT_EQ((std::vector<BlockedDomain>{{"ads.example.test", false},
                                        {"new.example.test", true}}),
            service_->BlockedDomains());
  EXPECT_EQ(0u, service_->ImportBlockedDomains({{"new.example.test", false}}));
  EXPECT_FALSE(service_->ImportBlockedDomains(
      {{"other.example", true}, {"bad/path", true}}));
  EXPECT_EQ(2u, service_->BlockedDomains().size());
}

TEST_F(ContentBlockingServiceTest, CapacityRejectsTheEntireImportBatch) {
  std::vector<BlockedDomain> rules;
  for (size_t i = 0; i < kMaxBlockedDomains; ++i) {
    rules.push_back({"domain" + std::to_string(i) + ".example", false});
  }
  ASSERT_EQ(kMaxBlockedDomains, service_->ImportBlockedDomains(rules));
  EXPECT_FALSE(service_->ImportBlockedDomains({{"another.example", true}}));
  EXPECT_FALSE(service_->SetBlockedDomain("another.example", true));
  EXPECT_EQ(kMaxBlockedDomains, service_->BlockedDomains().size());
  EXPECT_TRUE(service_->SetBlockedDomain("domain0.example", true));
}

}  // namespace
}  // namespace yee::content_blocking
