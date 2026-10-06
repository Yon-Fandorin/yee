// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/yee_branding/internal_urls.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/url_util.h"

namespace yee::branding {
namespace {

class InternalURLsTest : public testing::Test {
 protected:
  InternalURLsTest() {
    url::AddStandardScheme("chrome", url::SCHEME_WITH_HOST);
    url::AddStandardScheme(InternalURLScheme(), url::SCHEME_WITH_HOST);
  }
  url::ScopedSchemeRegistryForTests registry_;
};

TEST_F(InternalURLsTest, CanonicalizesConfiguredSchemeAndPreservesComponents) {
  const GURL branded(std::string(InternalURLScheme()) +
                     "://settings/content-blocking?name=a%20b#section");
  const GURL canonical(
      "chrome://yee-settings/content-blocking?name=a%20b#section");
  EXPECT_EQ(canonical, CanonicalInternalURL(branded));
  EXPECT_EQ(branded, DisplayInternalURL(canonical));
  EXPECT_EQ(canonical, CanonicalInternalURL(DisplayInternalURL(canonical)));
  EXPECT_TRUE(IsInternalURLScheme(base::ToUpperASCII(InternalURLScheme())));
}

TEST_F(InternalURLsTest, LeavesOtherOriginsAndInvalidURLsUnchanged) {
  for (const char* input :
       {"https://settings/chrome://version", "chrome-untrusted://test/",
        "chrome-search://local-ntp/", "chrome-extension://test/",
        "file:///tmp/test", "about:blank", "javascript:alert(1)",
        "broken://"}) {
    const GURL url(input);
    EXPECT_EQ(url, CanonicalInternalURL(url));
    EXPECT_EQ(url, DisplayInternalURL(url));
  }
  EXPECT_FALSE(IsInternalURLScheme("chrome"));
  EXPECT_FALSE(
      IsInternalURLScheme(std::string(InternalURLScheme()) + "-other"));
}

TEST_F(InternalURLsTest, FormatsOnlyActualWebUIAddresses) {
  const GURL url("chrome://yee-settings/content-blocking?name=a%20b#section");
  const std::u16string branded =
      base::ASCIIToUTF16(InternalURLScheme()) +
      u"://settings/content-blocking?name=a%20b#section";
  EXPECT_EQ(
      branded,
      DisplayInternalURLText(
          url, u"chrome://yee-settings/content-blocking?name=a%20b#section"));
  EXPECT_EQ(u"https://example.com/chrome://settings",
            DisplayInternalURLText(GURL("https://example.com"),
                                   u"https://example.com/chrome://settings"));
  EXPECT_EQ(u"chrome-untrusted://test/",
            DisplayInternalURLText(GURL("chrome-untrusted://test/"),
                                   u"chrome-untrusted://test/"));
  EXPECT_EQ(u"settings", DisplayInternalURLText(url, u"settings"));
}

TEST_F(InternalURLsTest, PreservesNativeSettingsAndMapsItsOwnAlias) {
  const GURL native("chrome://settings/content?name=a%20b#section");
  const GURL alias(std::string(InternalURLScheme()) +
                   "://chromium-settings/content?name=a%20b#section");
  EXPECT_EQ(native, CanonicalInternalURL(native));
  EXPECT_EQ(native, CanonicalInternalURL(alias));
  EXPECT_EQ(alias, DisplayInternalURL(native));
  EXPECT_EQ(base::UTF8ToUTF16(alias.spec()),
            DisplayInternalURLText(native, base::UTF8ToUTF16(native.spec())));
  EXPECT_EQ(GURL("chrome://yee-settings/"), SettingsURL());
}

}  // namespace
}  // namespace yee::branding
