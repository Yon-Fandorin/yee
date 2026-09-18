// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_browser_contract.h"
#include "chrome/browser/ui/views/yee/agent_request_timing.h"
#include <algorithm>

#include "base/strings/string_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace yee {
namespace {

TEST(AgentBrowserContractTest, PageLocationPreservesExactPathQueryAndFragment) {
  const GURL url("https://example.test/orders/7?view=details#returns");
  const auto location = GetAgentPageLocation(url);
  EXPECT_EQ(url.spec(), location.url);
  EXPECT_FALSE(location.truncated);
  EXPECT_FALSE(location.credentials_redacted);
  const auto file = GetAgentPageLocation(GURL("file:///tmp/readme.html#section"));
  EXPECT_EQ("file:///tmp/readme.html#section", file.url);
}

TEST(AgentBrowserContractTest, PageLocationRedactsCredentialsAndMarksClipping) {
  const auto location = GetAgentPageLocation(
      GURL("https://private-user:private-password@example.test/orders?view=1"));
  EXPECT_EQ("https://example.test/orders?view=1", location.url);
  EXPECT_TRUE(location.credentials_redacted);
  EXPECT_FALSE(location.truncated);
  const auto long_url = GetAgentPageLocation(
      GURL("https://example.test/" + std::string(4096, 'x')));
  EXPECT_EQ(2048u, long_url.url.size());
  EXPECT_TRUE(long_url.truncated);
  EXPECT_FALSE(long_url.credentials_redacted);
}

TEST(AgentBrowserContractTest, PageLocationOmitsUnsupportedAndInvalidURLs) {
  for (const char* value : {"not a url", "javascript:alert(1)",
                            "chrome://settings/", "data:text/plain,secret"}) {
    const auto location = GetAgentPageLocation(GURL(value));
    EXPECT_TRUE(location.url.empty());
  }
}

TEST(AgentBrowserContractTest, ContentWaitIgnoresOnlyRefsAndKeepsDeltaIdentity) {
  AgentSemanticSnapshot original;
  original.document_ref = "doc";
  original.title = "Tickets";
  original.origin = "http://127.0.0.1:1234";
  AgentSemanticNode node;
  node.ref = "doc_1";
  node.role = AgentSemanticRole::kTextField;
  node.name = "Details";
  node.value = std::string(2000, 'x');
  original.nodes.push_back(node);
  auto current = original;
  current.nodes[0].ref = "doc_2";
  EXPECT_TRUE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  const auto delta = SerializeAgentSemanticSnapshot(current, &original, 16000);
  EXPECT_NE(delta.find("doc_2"), std::string::npos);
  EXPECT_NE(delta.find("doc_1"), std::string::npos);
  current.nodes[0].value.back() = 'y';
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  for (auto field : {&AgentSemanticNode::enabled, &AgentSemanticNode::checked,
                     &AgentSemanticNode::focused, &AgentSemanticNode::visible,
                     &AgentSemanticNode::secret}) {
    current = original;
    current.nodes[0].*field = !(current.nodes[0].*field);
    EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  }
  current = original;
  current.document_ref = "other";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  current = original;
  current.title = "Other";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  current = original;
  current.origin = "http://127.0.0.1:5678";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
  node.name = "Second";
  original.nodes.push_back(node);
  current = original;
  std::reverse(current.nodes.begin(), current.nodes.end());
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current, false));
}

TEST(AgentBrowserContractTest, WaitComparisonIgnoresRevisionAndRedactedValues) {
  AgentSemanticSnapshot original;
  original.document_ref = "document";
  original.title = "Policy";
  AgentSemanticNode node;
  node.ref = "document_1";
  node.role = AgentSemanticRole::kTextField;
  node.name = "Password";
  node.secret = true;
  node.value = "first secret";
  original.nodes.push_back(node);
  auto current = original;
  current.revision = 42;
  current.nodes[0].value = "different secret";
  EXPECT_TRUE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  current.nodes[0].focused = true;
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
}

TEST(AgentBrowserContractTest, WaitComparisonRetainsMetadataIdentityAndFullValues) {
  AgentSemanticSnapshot original;
  original.document_ref = "document";
  original.title = "Policy";
  AgentSemanticNode node;
  node.ref = "document_1";
  node.role = AgentSemanticRole::kTextField;
  node.name = "Details";
  node.value = std::string(2000, 'x');
  original.nodes.push_back(node);
  auto current = original;
  current.nodes[0].value.back() = 'y';
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  current = original;
  current.nodes[0].ref = "replacement_1";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  current = original;
  current.title = "Changed";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  current = original;
  current.document_ref = "other document";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
  current = original;
  current.nodes.clear();
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(original, current));
}

TEST(AgentBrowserContractTest, TimingSeparatesMultipleWaitsAndResets) {
  AgentRequestTiming timing;
  const auto start = base::TimeTicks() + base::Seconds(1);
  timing.Start(start);
  timing.BeginUserWait(start + base::Milliseconds(2));
  timing.BeginUserWait(start + base::Milliseconds(3));
  timing.EndUserWait(start + base::Milliseconds(12));
  timing.EndUserWait(start + base::Milliseconds(13));
  timing.BeginUserWait(start + base::Milliseconds(15));
  timing.EndUserWait(start + base::Milliseconds(20));
  EXPECT_EQ(base::Milliseconds(15), timing.UserWait());
  EXPECT_EQ(base::Milliseconds(25), timing.Elapsed(start + base::Milliseconds(25)));
  timing.Start(start + base::Seconds(1));
  EXPECT_EQ(base::TimeDelta(), timing.UserWait());
}

TEST(AgentBrowserContractTest, TimingCanFinishPendingWaitOnCancelOrExpiry) {
  AgentRequestTiming timing;
  const auto start = base::TimeTicks() + base::Seconds(1);
  timing.Start(start);
  timing.BeginUserWait(start);
  timing.EndUserWait(start + base::Seconds(120));
  EXPECT_EQ(base::Seconds(120), timing.UserWait());
  timing.Start(start);
  timing.EndUserWait(start + base::Milliseconds(5));
  EXPECT_EQ(base::TimeDelta(), timing.UserWait());
}

AgentSemanticSnapshot ExampleSnapshot() {
  return {"tab-7/frame-0",
          4,
          "Checkout",
          "shop.example",
          {{"101", AgentSemanticRole::kHeading, "Checkout"},
           {"102", AgentSemanticRole::kText, "decorative copy"},
           {"103", AgentSemanticRole::kTextField, "Email", "me@example.com"},
           {"104", AgentSemanticRole::kTextField, "Card number", "4242", true,
            false, false, true, true},
           {"106", AgentSemanticRole::kComboBox, "Country", "South Korea"},
           {"105", AgentSemanticRole::kButton, "Pay now"}}};
}

TEST(AgentBrowserContractTest, FullSnapshotKeepsActionsAndRedactsSecrets) {
  const std::string output =
      SerializeAgentSemanticSnapshot(ExampleSnapshot(), nullptr);

  EXPECT_NE(std::string::npos,
            output.find("@103 field \"Email\" value=\"me@example.com\""));
  EXPECT_NE(std::string::npos,
            output.find("@104 field \"Card number\" value=<redacted>"));
  EXPECT_NE(std::string::npos,
            output.find("@106 combobox \"Country\" value=\"South Korea\""));
  EXPECT_NE(std::string::npos, output.find("@105 button \"Pay now\""));
  EXPECT_EQ(std::string::npos, output.find("4242"));
  EXPECT_NE(std::string::npos, output.find("@102 text \"decorative copy\""));
}

TEST(AgentBrowserContractTest, DeltaOmitsUnchangedNodesAndReportsRemoval) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  AgentSemanticSnapshot after = before;
  after.revision = 5;
  after.nodes[2].value = "other@example.com";
  after.nodes.pop_back();

  const std::string output = SerializeAgentSemanticSnapshot(after, &before);

  EXPECT_NE(std::string::npos, output.find(" delta"));
  EXPECT_NE(std::string::npos,
            output.find("~@103 field \"Email\" value=\"other@example.com\""));
  EXPECT_NE(std::string::npos, output.find("base_rev=4"));
  EXPECT_NE(std::string::npos, output.find("-@105"));
  EXPECT_EQ(std::string::npos, output.find("@101 heading"));
}

TEST(AgentBrowserContractTest, NewDocumentIsAlwaysACompleteSnapshot) {
  AgentSemanticSnapshot old_snapshot = ExampleSnapshot();
  AgentSemanticSnapshot new_snapshot = ExampleSnapshot();
  new_snapshot.document_ref = "tab-7/frame-1";

  const std::string output =
      SerializeAgentSemanticSnapshot(new_snapshot, &old_snapshot);

  EXPECT_EQ(std::string::npos, output.find(" delta"));
  EXPECT_NE(std::string::npos, output.find("+@101 heading \"Checkout\""));
  EXPECT_EQ(std::string::npos, output.find("-@101"));
}

TEST(AgentBrowserContractTest, RedactedValueOnlyChangesAreOmitted) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  AgentSemanticSnapshot after = before;
  after.revision = 5;
  after.nodes[3].value = "different-secret";

  const std::string output = SerializeAgentSemanticSnapshot(after, &before);

  EXPECT_NE(std::string::npos, output.find(" delta"));
  EXPECT_NE(std::string::npos, output.find("base_rev=4"));
  EXPECT_EQ(std::string::npos, output.find("~@104"));
  EXPECT_EQ(std::string::npos, output.find("different-secret"));
}

TEST(AgentBrowserContractTest, RegressingRevisionResetsToCompleteSnapshot) {
  AgentSemanticSnapshot old_snapshot = ExampleSnapshot();
  AgentSemanticSnapshot new_snapshot = ExampleSnapshot();
  new_snapshot.revision = old_snapshot.revision - 1;

  const std::string output =
      SerializeAgentSemanticSnapshot(new_snapshot, &old_snapshot);

  EXPECT_EQ(std::string::npos, output.find(" delta"));
  EXPECT_EQ(std::string::npos, output.find("base_rev="));
  EXPECT_NE(std::string::npos, output.find("+@101 heading \"Checkout\""));
}

TEST(AgentBrowserContractTest, FieldTruncationPreservesUTF8Boundaries) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  std::string long_name;
  for (size_t i = 0; i < 100; ++i) {
    long_name.append("한");
  }
  snapshot.nodes[0].name = long_name;

  const std::string output = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  std::string expected_name;
  for (size_t i = 0; i < 53; ++i) {
    expected_name.append("한");
  }
  expected_name.append("\xE2\x80\xA6");

  EXPECT_TRUE(base::IsStringUTF8(output));
  EXPECT_NE(std::string::npos, output.find(expected_name));
  EXPECT_EQ(std::string::npos, output.find("\xEF\xBF\xBD"));
}

TEST(AgentBrowserContractTest, OutputHasHardBudget) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes[0].name.assign(500, 'x');

  EXPECT_LE(SerializeAgentSemanticSnapshot(snapshot, nullptr, 80).size(), 80U);
}

TEST(AgentBrowserContractTest, LongFieldValueIsCompleteAndPreservesEscapedWhitespace) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes[2].value = std::string(2000, 'x') + "\nline\tquote\"\\";
  const auto output = SerializeAgentSemanticSnapshot(snapshot, nullptr, 16000);
  EXPECT_NE(std::string::npos, output.find(std::string(2000, 'x') + "\\nline\\tquote\\\"\\\\"));
  EXPECT_EQ(std::string::npos, output.find("value_truncated"));
  snapshot.nodes[2].secret = true;
  const auto redacted = SerializeAgentSemanticSnapshot(snapshot, nullptr, 16000);
  EXPECT_EQ(std::string::npos, redacted.find(std::string(2000, 'x')));
}

TEST(AgentBrowserContractTest, OversizedFieldValueSignalsClippingAndKeepsHardBudget) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes[2].value = std::string(4096, 'x') + "tail";
  const auto output = SerializeAgentSemanticSnapshot(snapshot, nullptr, 16000);
  EXPECT_NE(std::string::npos, output.find("value_truncated"));
  EXPECT_EQ(std::string::npos, output.find("tail"));
  EXPECT_LE(SerializeAgentSemanticSnapshot(snapshot, nullptr, 300).size(), 300U);
}

TEST(AgentBrowserContractTest, OutputSignalsTruncationAtEveryBudget) {
  constexpr char kTruncated[] = "!truncated request_full_or_narrower_scope\n";
  AgentSemanticSnapshot snapshot = ExampleSnapshot();

  EXPECT_EQ(kTruncated, SerializeAgentSemanticSnapshot(snapshot, nullptr, 80));
  EXPECT_EQ(kTruncated, SerializeAgentSemanticSnapshot(snapshot, nullptr,
                                                       sizeof(kTruncated) - 1));
  EXPECT_EQ("!\n", SerializeAgentSemanticSnapshot(snapshot, nullptr, 2));
  EXPECT_EQ("!", SerializeAgentSemanticSnapshot(snapshot, nullptr, 1));
  EXPECT_LE(SerializeAgentSemanticSnapshot(snapshot, nullptr, 2).size(), 2U);
}

TEST(AgentBrowserContractTest, OnlyExternalEffectsNeedApproval) {
  EXPECT_FALSE(AgentBrowserEffectRequiresApproval(AgentBrowserEffect::kRead));
  EXPECT_FALSE(AgentBrowserEffectRequiresApproval(
      AgentBrowserEffect::kFillNonSensitiveField));
  EXPECT_TRUE(
      AgentBrowserEffectRequiresApproval(AgentBrowserEffect::kUseCredential));
  EXPECT_TRUE(
      AgentBrowserEffectRequiresApproval(AgentBrowserEffect::kPurchase));
}

// Release gates: references must remain unambiguous, and display truncation
// must not suppress real document changes. These intentionally exercise cases
// missing from the original happy-path suite.
TEST(AgentBrowserContractTest, GateLongReferencesRemainDistinct) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  const std::string prefix(160, 'r');
  snapshot.nodes = {{prefix + "A", AgentSemanticRole::kButton, "First"},
                    {prefix + "B", AgentSemanticRole::kButton, "Second"}};
  const std::string output = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_NE(std::string::npos, output.find("@" + prefix + "A "));
  EXPECT_NE(std::string::npos, output.find("@" + prefix + "B "));
}

TEST(AgentBrowserContractTest, GateChangesBeyondDisplayPrefixRemainObservable) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  before.nodes[0].name = std::string(160, 'x') + "before";
  AgentSemanticSnapshot after = before;
  after.revision++;
  after.nodes[0].name = std::string(160, 'x') + "after";
  const std::string output = SerializeAgentSemanticSnapshot(after, &before);
  EXPECT_NE(std::string::npos, output.find("~@101"));
}

TEST(AgentBrowserContractTest,
     ReferencesEscapeDelimitersForDocumentsAndRemovals) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  before.document_ref = "tab 7/frame\n0%done";
  before.nodes = {{"node A\t%1", AgentSemanticRole::kButton, "Keep"},
                  {"node B", AgentSemanticRole::kLink, "Remove"}};
  AgentSemanticSnapshot after = before;
  after.revision++;
  after.nodes[0].name = "Changed";
  after.nodes.pop_back();

  const std::string output = SerializeAgentSemanticSnapshot(after, &before);

  EXPECT_NE(std::string::npos, output.find("page @tab%207%2Fframe%0A0%25done"));
  EXPECT_NE(std::string::npos, output.find("~@node%20A%09%251"));
  EXPECT_NE(std::string::npos, output.find("-@node%20B\n"));
  EXPECT_EQ(std::string::npos, output.find("page @tab 7/frame"));
}

TEST(AgentBrowserContractTest, NonSecretValueSuffixChangesRemainObservable) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  before.nodes[2].value = std::string(160, 'x') + "before";
  AgentSemanticSnapshot after = before;
  after.revision++;
  after.nodes[2].value = std::string(160, 'x') + "after";

  const std::string output = SerializeAgentSemanticSnapshot(after, &before);

  EXPECT_NE(std::string::npos, output.find("~@103"));
}

TEST(AgentBrowserContractTest, LabeledProseIncludesBodyWithoutExtraRead) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes = {{"1", AgentSemanticRole::kText, "Article",
                     std::string(300, 'x') + " TAIL_MARKER"}};
  const std::string output = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_NE(std::string::npos, output.find("text=\""));
  EXPECT_NE(std::string::npos, output.find("TAIL_MARKER\""));
  EXPECT_EQ(std::string::npos, output.find("text_truncated"));
  snapshot.nodes[0].name = snapshot.nodes[0].value;
  const std::string deduplicated =
      SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_EQ(std::string::npos, deduplicated.find("text=\""));
  EXPECT_NE(std::string::npos, deduplicated.find("TAIL_MARKER\""));
}

TEST(AgentBrowserContractTest, ProseTruncationIsExplicitAndBudgeted) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes = {{"1", AgentSemanticRole::kText, "Article",
                     std::string(4095, 'x') + "한TAIL"}};
  const std::string output = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_TRUE(base::IsStringUTF8(output));
  EXPECT_NE(std::string::npos, output.find("text_truncated"));
  EXPECT_EQ(std::string::npos, output.find("TAIL"));
  EXPECT_LE(SerializeAgentSemanticSnapshot(snapshot, nullptr, 100).size(), 100U);
  snapshot.nodes[0].secret = true;
  snapshot.nodes[0].name = "private body";
  EXPECT_EQ(std::string::npos,
            SerializeAgentSemanticSnapshot(snapshot, nullptr).find("private body"));
  EXPECT_EQ(std::string::npos,
            SerializeAgentSemanticSnapshot(snapshot, nullptr).find("text=\""));
}

TEST(AgentBrowserContractTest, LabeledProseBodyChangeProducesDelta) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  before.nodes = {{"1", AgentSemanticRole::kText, "Article", "before"}};
  AgentSemanticSnapshot after = before;
  after.revision++;
  after.nodes[0].value = "after";
  EXPECT_NE(std::string::npos,
            SerializeAgentSemanticSnapshot(after, &before).find(
                "~@1 text \"Article\" text=\"after\""));
}

TEST(AgentBrowserContractTest, LinkDestinationIsEscapedBoundedAndSecretSafe) {
  AgentSemanticSnapshot snapshot = ExampleSnapshot();
  snapshot.nodes = {{"link", AgentSemanticRole::kLink, "Policy"}};
  auto& node = snapshot.nodes[0];
  node.href = "https://example.test/policy?q=\"한글\"\nnext\\path";
  const std::string output = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_NE(std::string::npos, output.find("href=\"https://example.test/policy?q=\\\"한글\\\"\\nnext\\\\path\""));
  EXPECT_EQ(std::string::npos, output.find("href_truncated"));
  node.href = std::string(4095, 'x') + "한TAIL";
  const std::string clipped = SerializeAgentSemanticSnapshot(snapshot, nullptr);
  EXPECT_TRUE(base::IsStringUTF8(clipped));
  EXPECT_NE(std::string::npos, clipped.find("href_truncated"));
  EXPECT_EQ(std::string::npos, clipped.find("TAIL"));
  EXPECT_LE(SerializeAgentSemanticSnapshot(snapshot, nullptr, 100).size(), 100U);
  node.secret = true;
  node.href = "https://example.test/private-credential";
  EXPECT_EQ(std::string::npos, SerializeAgentSemanticSnapshot(snapshot, nullptr).find("private-credential"));
  EXPECT_NE(std::string::npos, SerializeAgentSemanticSnapshot(snapshot, nullptr).find("href=<redacted>"));
}

TEST(AgentBrowserContractTest, LinkDestinationChangeIsObservableWithoutLabelChange) {
  AgentSemanticSnapshot before = ExampleSnapshot();
  before.nodes = {{"link", AgentSemanticRole::kLink, "Policy"}};
  before.nodes[0].href = "https://example.test/before";
  auto after = before;
  after.revision++;
  after.nodes[0].href = "https://example.test/after";
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(before, after));
  EXPECT_FALSE(AgentSnapshotsHaveSameObservableSemantics(before, after, false));
  EXPECT_NE(std::string::npos, SerializeAgentSemanticSnapshot(after, &before).find("~@link link \"Policy\" href=\"https://example.test/after\""));
  before.nodes[0].secret = true;
  after.nodes[0].secret = true;
  EXPECT_TRUE(AgentSnapshotsHaveSameObservableSemantics(before, after));
}

}  // namespace
}  // namespace yee
