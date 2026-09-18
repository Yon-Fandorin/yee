#include "chrome/browser/ui/views/yee/agent_request_ledger.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {
const char kResponse[] =
    R"({"id":"req-1","ok":true,"execution_settled":true,"saved":"Cedar"})";

TEST(AgentRequestLedgerTest, DuplicateBeforeResultNeverReadmitted) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_TRUE(ClaimAgentRequest(directory.GetPath(), "req-1").fresh);
  const auto duplicate = ClaimAgentRequest(directory.GetPath(), "req-1");
  EXPECT_FALSE(duplicate.fresh);
  EXPECT_TRUE(duplicate.response.empty());
  EXPECT_TRUE(ClaimAgentRequest(directory.GetPath(), "req-2").fresh);
}

TEST(AgentRequestLedgerTest, CompletedResultReplayedFromDiskWithoutMemory) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  ASSERT_TRUE(ClaimAgentRequest(directory.GetPath(), "req-1").fresh);
  ASSERT_TRUE(StoreAgentResponse(directory.GetPath(), "req-1", kResponse));
  for (int i = 0; i < 3; ++i) {
    const auto duplicate = ClaimAgentRequest(directory.GetPath(), "req-1");
    EXPECT_FALSE(duplicate.fresh);
    EXPECT_EQ(kResponse, duplicate.response);
  }
  EXPECT_FALSE(StoreAgentResponse(directory.GetPath(), "req-1", kResponse));
}

TEST(AgentRequestLedgerTest, InvalidOrMissingResultsFailClosed) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_FALSE(StoreAgentResponse(directory.GetPath(), "req-1", kResponse));
  ASSERT_TRUE(ClaimAgentRequest(directory.GetPath(), "req-1").fresh);
  EXPECT_FALSE(StoreAgentResponse(directory.GetPath(), "req-1", "{}"));
  EXPECT_FALSE(StoreAgentResponse(directory.GetPath(), "req-1",
      R"({"id":"other","ok":true,"execution_settled":true})"));
  const auto result = directory.GetPath().AppendASCII(
      "native-request-" + base::HexEncode(std::string_view("req-1")) + ".result");
  ASSERT_TRUE(base::WriteFile(result, "truncated"));
  EXPECT_FALSE(ClaimAgentRequest(directory.GetPath(), "req-1").fresh);
  EXPECT_TRUE(ClaimAgentRequest(directory.GetPath(), "req-1").response.empty());
}

TEST(AgentRequestLedgerTest, InvalidIdAndUnavailableDirectoryNeverAdmit) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_FALSE(ClaimAgentRequest(directory.GetPath(), "").fresh);
  EXPECT_FALSE(ClaimAgentRequest(directory.GetPath(), std::string(65, 'x')).fresh);
  EXPECT_FALSE(ClaimAgentRequest(directory.GetPath().AppendASCII("missing"), "req-1").fresh);
  // IDs are hex encoded, so even path-looking IDs cannot escape the mailbox.
  EXPECT_TRUE(ClaimAgentRequest(directory.GetPath(), "../../req-1").fresh);
  EXPECT_FALSE(ClaimAgentRequest(directory.GetPath(), "../../req-1").fresh);
}

TEST(AgentRequestLedgerTest, SessionStopSurvivesNewReaderAndCannotBeReplaced) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  EXPECT_TRUE(ReadAgentSessionStop(directory.GetPath()).empty());
  ASSERT_TRUE(StoreAgentSessionStop(directory.GetPath(), "first", "user_cancelled"));
  ASSERT_TRUE(StoreAgentSessionStop(directory.GetPath(), "later", "user_takeover"));
  EXPECT_EQ("user_cancelled", ReadAgentSessionStop(directory.GetPath()));
  EXPECT_FALSE(StoreAgentSessionStop(directory.GetPath(), "model", "resume"));
  EXPECT_FALSE(StoreAgentSessionStop(directory.GetPath(), "", "user_cancelled"));
}

TEST(AgentRequestLedgerTest, ClientCancellationBecomesImmutableNativeStop) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto control = directory.GetPath().AppendASCII("client-stop.json");
  ASSERT_TRUE(base::WriteFile(control,
      R"({"id":"cancelled-request","reason":"client_cancelled"})"));
  EXPECT_EQ("client_cancelled", ReadAgentSessionStop(directory.GetPath()));
  ASSERT_TRUE(base::DeleteFile(control));
  EXPECT_EQ("client_cancelled", ReadAgentSessionStop(directory.GetPath()));
}

TEST(AgentRequestLedgerTest, CorruptOrUnsupportedStopNeverAllowsResume) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const auto control = directory.GetPath().AppendASCII("client-stop.json");
  for (const auto* value : {"truncated",
                           R"({"id":"model","reason":"resume"})",
                           R"({"id":"model","reason":"user_cancelled"})",
                           R"({"id":"model","reason":"client_cancelled","clear":true})"}) {
    ASSERT_TRUE(base::WriteFile(control, value));
    EXPECT_EQ("stop_state_unavailable", ReadAgentSessionStop(directory.GetPath()));
  }
  ASSERT_TRUE(base::DeleteFile(control));
  const auto native = directory.GetPath().AppendASCII("native-session-stop.json");
  ASSERT_TRUE(base::WriteFile(native, ""));
  EXPECT_EQ("stop_state_unavailable", ReadAgentSessionStop(directory.GetPath()));
  EXPECT_FALSE(StoreAgentSessionStop(directory.GetPath(), "next", "user_cancelled"));
}
}  // namespace
}  // namespace yee
