// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/yee_content_blocking/baseline_list_store.h"
#include "mojo/core/embedder/embedder.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {
constexpr char kList[] = "[Adblock Plus 2.0]\n! Title: EasyList\n||ad.test^\n";
constexpr char kPrivacy[] =
    "[Adblock Plus 1.1]\n! Title: EasyPrivacy\n||tracker.test^\n";
class BaselineListUpdaterTest : public testing::Test {
 public:
  static void SetUpTestSuite() { mojo::core::Init(); }

 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    path_ = base::MakeAbsoluteFilePath(directory_.GetPath());
    ASSERT_FALSE(path_.empty());
  }
  std::unique_ptr<BaselineListUpdater> Create(base::Time checked = {}) {
    return std::make_unique<BaselineListUpdater>(
        path_, factory_.GetSafeWeakWrapper(), "", checked);
  }
  base::test::TaskEnvironment tasks_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath path_;
  network::TestURLLoaderFactory factory_;
};
TEST_F(BaselineListUpdaterTest,
       DownloadsCookieFreePairAndPublishesOnlyAfterBoth) {
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  ASSERT_EQ(1, factory_.NumPending());
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(GURL(kBaselineListURLs[0]), request.url);
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  EXPECT_EQ(network::mojom::RedirectMode::kError, request.redirect_mode);
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[0]),
                                             kList);
  tasks_.RunUntilIdle();
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
  ASSERT_EQ(1, factory_.NumPending());
  EXPECT_EQ(GURL(kBaselineListURLs[1]),
            factory_.GetPendingRequest(0)->request.url);
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[1]),
                                             kPrivacy);
  tasks_.RunUntilIdle();
  EXPECT_FALSE(ReadBaselineListStore(path_).generation.empty());
  EXPECT_FALSE(updater->update_in_flight());
  EXPECT_TRUE(
      BaselineLists().generation.empty());  // Running snapshot unchanged.
}
TEST_F(BaselineListUpdaterTest, FailedSecondDownloadKeepsLastGoodAndBacksOff) {
  ASSERT_TRUE(
      InstallBaselineLists(path_, {kList, kPrivacy}, base::Time::Now(), ""));
  const auto original = ReadBaselineListStore(path_).generation;
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), "server error",
                       net::HTTP_INTERNAL_SERVER_ERROR);
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  EXPECT_EQ(original, ReadBaselineListStore(path_).generation);
  EXPECT_FALSE(updater->update_in_flight());
  const auto requests = factory_.total_requests();
  tasks_.FastForwardBy(base::Hours(5));
  EXPECT_EQ(requests, factory_.total_requests());
  tasks_.FastForwardBy(base::Hours(1));
  EXPECT_GT(factory_.total_requests(), requests);
}
TEST_F(BaselineListUpdaterTest, RecentSuccessfulCheckDefersRequests) {
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), kPrivacy);
  auto updater = Create(base::Time::Now());
  updater->Start();
  tasks_.FastForwardBy(base::Hours(23));
  EXPECT_EQ(0u, factory_.total_requests());
  tasks_.FastForwardBy(base::Hours(1));
  tasks_.RunUntilIdle();
  EXPECT_EQ(2u, factory_.total_requests());
}
TEST_F(BaselineListUpdaterTest, PartialSuccessfulResponseCannotPublishPair) {
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList,
                       net::HTTP_PARTIAL_CONTENT);
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  EXPECT_EQ(1u, factory_.total_requests());
  EXPECT_FALSE(updater->update_in_flight());
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
}
TEST_F(BaselineListUpdaterTest,
       DestructionCancelsPendingNetworkBeforePublishing) {
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  updater.reset();
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[0]),
                                             kList);
  tasks_.RunUntilIdle();
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
}

TEST_F(BaselineListUpdaterTest,
       ManualCheckBypassesScheduleAndJoinsInFlightPair) {
  auto updater = Create(base::Time::Now());
  std::vector<bool> states;
  auto subscription =
      BaselineListUpdater::AddChangedCallback(base::BindLambdaForTesting(
          [&]() { states.push_back(updater->update_in_flight()); }));
  updater->Start();
  EXPECT_EQ(0u, factory_.total_requests());
  base::test::TestFuture<bool> first;
  base::test::TestFuture<bool> second;
  updater->CheckNow(first.GetCallback());
  updater->CheckNow(second.GetCallback());
  tasks_.RunUntilIdle();
  ASSERT_EQ(1, factory_.NumPending());
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[0]),
                                             kList);
  tasks_.RunUntilIdle();
  EXPECT_FALSE(first.IsReady());
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[1]),
                                             kPrivacy);
  tasks_.RunUntilIdle();
  EXPECT_TRUE(first.Get());
  EXPECT_TRUE(second.Get());
  EXPECT_EQ(2u, factory_.total_requests());
  EXPECT_EQ((std::vector<bool>{true, false}), states);
}

TEST_F(BaselineListUpdaterTest, ManualCheckReportsFailureAndOwnerShutdown) {
  factory_.AddResponse(std::string(kBaselineListURLs[0]), "error",
                       net::HTTP_INTERNAL_SERVER_ERROR);
  auto updater = Create();
  base::test::TestFuture<bool> failed;
  updater->CheckNow(failed.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(failed.Get());
  factory_.ClearResponses();
  base::test::TestFuture<bool> closed;
  updater->CheckNow(closed.GetCallback());
  tasks_.RunUntilIdle();
  updater.reset();
  EXPECT_FALSE(closed.Get());
}
TEST_F(BaselineListUpdaterTest,
       AddsSubscriptionWithoutCookiesAndReportsDuplicate) {
  const std::string url = "https://filters.test/custom.txt";
  auto updater = Create();
  base::test::TestFuture<std::string> added;
  updater->AddSubscriptionNow(url, added.GetCallback());
  tasks_.RunUntilIdle();
  ASSERT_EQ(1, factory_.NumPending());
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  EXPECT_EQ(network::mojom::RedirectMode::kFollow, request.redirect_mode);
  factory_.SimulateResponseForPendingRequest(
      url, "! Title: Custom\n||custom-ad.test^\n");
  tasks_.RunUntilIdle();
  EXPECT_EQ("", added.Get());
  EXPECT_EQ(1u, ReadBaselineListStore(path_).subscriptions.size());
  base::test::TestFuture<std::string> duplicate;
  updater->AddSubscriptionNow(url, duplicate.GetCallback());
  tasks_.RunUntilIdle();
  factory_.SimulateResponseForPendingRequest(url, "||new-ad.test^\n");
  tasks_.RunUntilIdle();
  EXPECT_EQ("duplicate-list", duplicate.Get());
  base::test::TestFuture<std::string> invalid;
  const std::string bad_url = "https://filters.test/error.txt";
  updater->AddSubscriptionNow(bad_url, invalid.GetCallback());
  tasks_.RunUntilIdle();
  factory_.SimulateResponseForPendingRequest(bad_url, "<html>not rules</html>");
  tasks_.RunUntilIdle();
  EXPECT_EQ("invalid-list", invalid.Get());
  EXPECT_EQ(1u, ReadBaselineListStore(path_).subscriptions.size());
}
TEST_F(BaselineListUpdaterTest, ChecksEnabledSubscriptionsAndKeepsFailedCopy) {
  const std::string enabled = "https://filters.test/enabled.txt";
  const std::string disabled = "https://filters.test/disabled.txt";
  ASSERT_TRUE(
      InstallBaselineLists(path_, {kList, kPrivacy}, base::Time::Now(), ""));
  ASSERT_EQ("",
            AddFilterSubscription(path_, enabled, "||enabled-ad.test^\n", ""));
  ASSERT_EQ(
      "", AddFilterSubscription(path_, disabled, "||disabled-ad.test^\n", ""));
  ASSERT_EQ("", ChangeFilterSubscription(path_, disabled, false, ""));
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), kPrivacy);
  factory_.AddResponse(enabled, "error", net::HTTP_INTERNAL_SERVER_ERROR);
  auto updater = Create();
  base::test::TestFuture<bool> checked;
  updater->CheckNow(checked.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(checked.Get());
  EXPECT_EQ(3u, factory_.total_requests());
  const auto snapshot = ReadBaselineListStore(path_);
  EXPECT_EQ(2u, snapshot.subscriptions.size());
  EXPECT_NE(snapshot.filters.find("enabled-ad.test"), std::string::npos);
}
TEST_F(BaselineListUpdaterTest,
       SubscriptionBusyAndOwnerShutdownResolveCallbacks) {
  auto updater = Create();
  base::test::TestFuture<std::string> first;
  updater->AddSubscriptionNow("https://filters.test/first.txt",
                              first.GetCallback());
  base::test::TestFuture<std::string> busy;
  updater->ChangeSubscriptionNow("https://filters.test/first.txt", false,
                                 busy.GetCallback());
  EXPECT_EQ("lists-busy", busy.Get());
  updater.reset();
  EXPECT_EQ("updates-unavailable", first.Get());
  EXPECT_TRUE(ReadBaselineListStore(path_).subscriptions.empty());
}
}  // namespace
}  // namespace yee::content_blocking
