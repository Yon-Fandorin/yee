// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/blocked_domains.h"

#include "base/strings/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {

TEST(BlockedDomainsTest, CanonicalizesCaseTrailingDotAndIdns) {
  EXPECT_EQ("ads.example.com", CanonicalBlockedDomain(" Ads.Example.COM. "));
  EXPECT_EQ("xn--bcher-kva.example", CanonicalBlockedDomain("b\xc3\xbc"
                                                            "cher.example"));
  for (const char* input :
       {"https://ads.example.com", "example.com:443", "ads.example.com/path",
        "*.example.com", "127.0.0.1", "[::1]", "0x7f000001", "bad site.com",
        "user@example.com", "bad_.com", "-bad.com", "bad-.com", "example..com",
        "||example.com^", "example.com%2f"}) {
    EXPECT_FALSE(CanonicalBlockedDomain(input)) << input;
  }
}

TEST(BlockedDomainsTest, CsvReportsInvalidRowsAndCanonicalDuplicates) {
  const auto parsed = ParseDomainImport(
      "\xef\xbb\xbf"
      "domain,include_subdomains\r\n\"Ads.Example.COM\",false\r\n"
      "ads.example.com.,true\r\nwrong/path,true\r\nvalid.example,no\r\n"
      "missing.example\r\nchild.example,1\r\n",
      DomainImportFormat::kCsv);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(6u, parsed.rows.size());
  EXPECT_EQ(2u, parsed.rows[0].line);
  EXPECT_EQ((BlockedDomain{"ads.example.com", false}), parsed.rows[0].rule);
  EXPECT_EQ(DomainImportRowError::kDuplicate, parsed.rows[1].error);
  EXPECT_EQ(DomainImportRowError::kInvalidDomain, parsed.rows[2].error);
  EXPECT_EQ(DomainImportRowError::kInvalidScope, parsed.rows[3].error);
  EXPECT_EQ(DomainImportRowError::kInvalidColumns, parsed.rows[4].error);
  EXPECT_EQ((BlockedDomain{"child.example", true}), parsed.rows[5].rule);
}

TEST(BlockedDomainsTest, TextPreservesLineNumbersAndSkipsComments) {
  const auto parsed = ParseDomainImport(
      "# local list\r\n\r\nads.example.com\r\nADS.example.com.\r\n"
      "https://wrong.example\r\n",
      DomainImportFormat::kText);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(3u, parsed.rows.size());
  EXPECT_EQ(3u, parsed.rows[0].line);
  EXPECT_EQ((BlockedDomain{"ads.example.com", true}), parsed.rows[0].rule);
  EXPECT_EQ(DomainImportRowError::kDuplicate, parsed.rows[1].error);
  EXPECT_EQ(DomainImportRowError::kInvalidDomain, parsed.rows[2].error);
}

TEST(BlockedDomainsTest, CsvSupportsReorderedAndOptionalScopeColumns) {
  auto parsed = ParseDomainImport("include_subdomains,domain\n0,ads.example\n",
                                  DomainImportFormat::kCsv);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(1u, parsed.rows.size());
  EXPECT_EQ((BlockedDomain{"ads.example", false}), parsed.rows[0].rule);
  parsed =
      ParseDomainImport("domain\nexact.example\n", DomainImportFormat::kCsv);
  ASSERT_EQ(1u, parsed.rows.size());
  EXPECT_EQ((BlockedDomain{"exact.example", true}), parsed.rows[0].rule);
}

TEST(BlockedDomainsTest, RejectsMalformedCsvAsAWhole) {
  for (const char* text :
       {"domain\nads.example\n\"unterminated", "domain\n\"ads.example\"suffix",
        "domain,domain\nfirst.example,last.example",
        "url\nhttps://example.com/list", "domain,unknown\nexample.com,value"}) {
    const auto parsed = ParseDomainImport(text, DomainImportFormat::kCsv);
    EXPECT_FALSE(parsed.error.empty()) << text;
    EXPECT_TRUE(parsed.rows.empty());
  }
}

TEST(BlockedDomainsTest, QuotedMultilineFieldsKeepLaterLineNumbers) {
  const auto parsed = ParseDomainImport(
      "domain\n\"not\na domain\"\nvalid.example\n", DomainImportFormat::kCsv);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(2u, parsed.rows.size());
  EXPECT_EQ(DomainImportRowError::kInvalidDomain, parsed.rows[0].error);
  EXPECT_EQ(4u, parsed.rows[1].line);
}

TEST(BlockedDomainsTest, BoundsFilesAndRowsAndRejectsBinaryData) {
  EXPECT_EQ("file-too-large",
            ParseDomainImport(std::string(kMaxDomainImportBytes + 1, 'a'),
                              DomainImportFormat::kText)
                .error);
  EXPECT_EQ("invalid-file",
            ParseDomainImport(std::string("a\0b", 3), DomainImportFormat::kText)
                .error);
  EXPECT_EQ("empty-file",
            ParseDomainImport("# empty\n", DomainImportFormat::kText).error);
  std::string csv = "domain\n";
  std::string text;
  for (size_t i = 0; i <= kMaxBlockedDomains; ++i) {
    csv += "example.com\n";
    text += "example.com\n";
  }
  EXPECT_EQ("too-many-domains",
            ParseDomainImport(csv, DomainImportFormat::kCsv).error);
  EXPECT_EQ("too-many-domains",
            ParseDomainImport(text, DomainImportFormat::kText).error);
}

TEST(BlockedDomainsTest, TruncatedPreviewLabelsRemainValidUtf8) {
  std::string input;
  for (size_t i = 0; i < 200; ++i) {
    input += "\xe6\xbc\xa2";
  }
  const auto parsed = ParseDomainImport(input, DomainImportFormat::kText);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(1u, parsed.rows.size());
  EXPECT_EQ(DomainImportRowError::kInvalidDomain, parsed.rows[0].error);
  EXPECT_EQ(510u, parsed.rows[0].input.size());
  EXPECT_TRUE(base::IsStringUTF8(parsed.rows[0].input));
}

TEST(BlockedDomainsTest, AcceptsCsvExportOfMaximumSizeSavedList) {
  std::string csv =
      "\xef\xbb\xbf"
      "domain,include_subdomains\r\n";
  for (size_t i = 0; i < kMaxBlockedDomains; ++i) {
    std::string domain = std::to_string(i);
    domain.append(63 - domain.size(), 'a');
    domain += "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." +
              std::string(61, 'd');
    ASSERT_EQ(253u, domain.size());
    csv += domain + ",false\r\n";
  }
  ASSERT_GT(csv.size(), 1024u * 1024u);
  const auto parsed = ParseDomainImport(csv, DomainImportFormat::kCsv);
  ASSERT_TRUE(parsed.error.empty());
  ASSERT_EQ(kMaxBlockedDomains, parsed.rows.size());
  for (const auto& row : parsed.rows) {
    EXPECT_EQ(DomainImportRowError::kNone, row.error);
    ASSERT_TRUE(row.rule);
    EXPECT_FALSE(row.rule->include_subdomains);
  }
}

}  // namespace
}  // namespace yee::content_blocking
