// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"

#include <algorithm>

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
constexpr char kCatalog[] = R"([
  {"uuid":"default","permission_mask":1,"sources":[{"url":"https://filters.test/default.txt","format":"Standard"}]},
  {"uuid":"4D715457-307C-4383-8873-73A2FD263F71","permission_mask":1,"sources":[{"url":"https://filters.test/privacy.txt","format":"Standard"}]},
  {"uuid":"45B3ED40-C607-454F-A623-195FDD084637","sources":[{"url":"https://filters.test/ko.txt","format":"Standard"}]}
])";
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
  void Respond(std::string_view url,
               std::string_view body,
               net::HttpStatusCode status = net::HTTP_OK) {
    ASSERT_TRUE(factory_.SimulateResponseForPendingRequest(
        std::string(url), std::string(body), status));
    tasks_.RunUntilIdle();
  }
  void FinishCatalog() {
    Respond(kBraveCatalogURL, kCatalog);
    Respond("https://filters.test/default.txt", "||catalog-ad.test^\n");
    Respond("https://filters.test/privacy.txt", "||catalog-tracker.test^\n");
    Respond("https://filters.test/ko.txt", "||catalog-ko.test^\n");
  }
  base::test::TaskEnvironment tasks_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir directory_;
  base::FilePath path_;
  network::TestURLLoaderFactory factory_;
};
// 마지막 지역 목록까지 받은 뒤에만 새 카탈로그를 저장한다.
TEST_F(BaselineListUpdaterTest,
       CatalogDownloadsDefaultsAndKoreanAsOneGeneration) {
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), kPrivacy);
  factory_.AddResponse(kBraveCatalogURL, R"([
    {"uuid":"default","permission_mask":1,"sources":[{"url":"https://filters.test/default.txt","format":"Standard"}]},
    {"uuid":"4D715457-307C-4383-8873-73A2FD263F71","permission_mask":1,"sources":[{"url":"https://filters.test/privacy.txt","format":"Standard"}]},
    {"uuid":"45B3ED40-C607-454F-A623-195FDD084637","sources":[{"url":"https://filters.test/ko.txt","format":"Standard"}]}
  ])");
  factory_.AddResponse("https://filters.test/default.txt",
                       "||catalog-ad.test^\n");
  factory_.AddResponse("https://filters.test/privacy.txt",
                       "||catalog-tracker.test^\n");
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  EXPECT_FALSE(ReadBaselineListStore(path_).catalog_active);
  ASSERT_EQ(1, factory_.NumPending());
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(GURL("https://filters.test/ko.txt"), request.url);
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  factory_.SimulateResponseForPendingRequest("https://filters.test/ko.txt",
                                             "||ssl.pstatic.net/tveta/libs/\n");
  tasks_.RunUntilIdle();
  EXPECT_TRUE(ReadBaselineListStore(path_).catalog_active);
  EXPECT_EQ(3, ReadBaselineListStore(path_).catalog_source_count);
  EXPECT_FALSE(updater->update_in_flight());
  const auto requests = factory_.total_requests();
  tasks_.FastForwardBy(base::Hours(23));
  EXPECT_EQ(requests, factory_.total_requests());
}
TEST_F(BaselineListUpdaterTest,
       DownloadsCookieFreePairAndPublishesOnlyAfterCatalogCompletes) {
  auto updater = Create();
  updater->Start();
  tasks_.RunUntilIdle();
  for (size_t i = 0; i < 2; ++i) {
    ASSERT_EQ(1, factory_.NumPending());
    const auto& request = factory_.GetPendingRequest(0)->request;
    EXPECT_EQ(GURL(kBaselineListURLs[i]), request.url);
    EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
    EXPECT_EQ(network::mojom::RedirectMode::kError, request.redirect_mode);
    Respond(kBaselineListURLs[i], i == 0 ? kList : kPrivacy);
    EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
    EXPECT_TRUE(updater->update_in_flight());
  }
  ASSERT_EQ(1, factory_.NumPending());
  const auto& request = factory_.GetPendingRequest(0)->request;
  EXPECT_EQ(GURL(kBraveCatalogURL), request.url);
  EXPECT_EQ(network::mojom::CredentialsMode::kOmit, request.credentials_mode);
  EXPECT_EQ(network::mojom::RedirectMode::kError, request.redirect_mode);
  Respond(kBraveCatalogURL, kCatalog);
  Respond("https://filters.test/default.txt", "||catalog-ad.test^\n");
  Respond("https://filters.test/privacy.txt", "||catalog-tracker.test^\n");
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
  EXPECT_TRUE(updater->update_in_flight());
  Respond("https://filters.test/ko.txt", "||catalog-ko.test^\n");
  const auto saved = ReadBaselineListStore(path_);
  EXPECT_FALSE(saved.generation.empty());
  EXPECT_TRUE(saved.baseline_downloaded);
  EXPECT_TRUE(saved.catalog_active);
  EXPECT_FALSE(updater->update_in_flight());
  EXPECT_TRUE(BaselineLists().generation.empty());
}
TEST_F(BaselineListUpdaterTest,
       FailedPairAndCatalogKeepLastGoodAndRetryAfterBackoff) {
  ASSERT_TRUE(
      InstallBaselineLists(path_, {kList, kPrivacy}, base::Time::Now(), ""));
  const auto original = ReadBaselineListStore(path_);
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), "server error",
                       net::HTTP_INTERNAL_SERVER_ERROR);
  auto updater = Create();
  base::test::TestFuture<bool> failed;
  updater->CheckNow(failed.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(failed.IsReady());
  EXPECT_TRUE(updater->update_in_flight());
  Respond(kBraveCatalogURL, "server error", net::HTTP_INTERNAL_SERVER_ERROR);
  ASSERT_TRUE(failed.IsReady());
  EXPECT_FALSE(failed.Get());
  EXPECT_FALSE(updater->update_in_flight());
  EXPECT_EQ(original.generation, ReadBaselineListStore(path_).generation);
  EXPECT_EQ(original.checked_at, ReadBaselineListStore(path_).checked_at);
  EXPECT_EQ(3u, factory_.total_requests());
  factory_.ClearResponses();
  tasks_.FastForwardBy(base::Hours(6) - base::Seconds(1));
  EXPECT_EQ(3u, factory_.total_requests());
  tasks_.FastForwardBy(base::Seconds(1));
  tasks_.RunUntilIdle();
  EXPECT_EQ(4u, factory_.total_requests());
  EXPECT_TRUE(updater->update_in_flight());
  base::test::TestFuture<bool> retried;
  updater->CheckNow(retried.GetCallback());
  Respond(kBaselineListURLs[0], kList);
  Respond(kBaselineListURLs[1], kPrivacy);
  FinishCatalog();
  ASSERT_TRUE(retried.IsReady());
  EXPECT_TRUE(retried.Get());
  EXPECT_TRUE(ReadBaselineListStore(path_).catalog_active);
  EXPECT_GT(ReadBaselineListStore(path_).checked_at, original.checked_at);
  const auto requests = factory_.total_requests();
  tasks_.FastForwardBy(base::Hours(6));
  EXPECT_EQ(requests, factory_.total_requests());
}
TEST_F(BaselineListUpdaterTest, RecentSuccessfulCheckDefersRequests) {
  auto updater = Create(base::Time::Now());
  updater->Start();
  tasks_.FastForwardBy(base::Hours(23));
  EXPECT_EQ(0u, factory_.total_requests());
  tasks_.FastForwardBy(base::Hours(1));
  tasks_.RunUntilIdle();
  EXPECT_EQ(1u, factory_.total_requests());
  EXPECT_TRUE(updater->update_in_flight());
  Respond(kBaselineListURLs[0], kList);
  Respond(kBaselineListURLs[1], kPrivacy);
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
  FinishCatalog();
  EXPECT_EQ(6u, factory_.total_requests());
  EXPECT_FALSE(updater->update_in_flight());
  EXPECT_TRUE(ReadBaselineListStore(path_).catalog_active);
  EXPECT_EQ(base::Time::Now(), ReadBaselineListStore(path_).checked_at);
}
TEST_F(BaselineListUpdaterTest,
       PartialPairIsRejectedWhileSuccessfulCatalogStillPublishes) {
  auto updater = Create();
  base::test::TestFuture<bool> checked;
  updater->CheckNow(checked.GetCallback());
  tasks_.RunUntilIdle();
  Respond(kBaselineListURLs[0], kList, net::HTTP_PARTIAL_CONTENT);
  EXPECT_FALSE(checked.IsReady());
  EXPECT_TRUE(updater->update_in_flight());
  EXPECT_EQ(2u, factory_.total_requests());
  ASSERT_EQ(1, factory_.NumPending());
  EXPECT_EQ(GURL(kBraveCatalogURL), factory_.GetPendingRequest(0)->request.url);
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
  FinishCatalog();
  ASSERT_TRUE(checked.IsReady());
  EXPECT_TRUE(checked.Get());
  const auto saved = ReadBaselineListStore(path_);
  EXPECT_TRUE(saved.catalog_active);
  EXPECT_FALSE(saved.baseline_downloaded);
  std::string original;
  ASSERT_TRUE(base::ReadFileToString(path_.AppendASCII("generations")
                                         .AppendASCII(saved.generation)
                                         .AppendASCII("easylist.txt"),
                                     &original));
  EXPECT_NE(original, kList);
  EXPECT_FALSE(saved.generation.empty());
  EXPECT_NE(saved.trusted_filters.find("catalog-ad.test"), std::string::npos);
  EXPECT_FALSE(updater->update_in_flight());
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
       ManualCheckBypassesScheduleAndJoinsThroughCatalog) {
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
  tasks_.RunUntilIdle();
  ASSERT_EQ(1, factory_.NumPending());
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[0]),
                                             kList);
  tasks_.RunUntilIdle();
  EXPECT_FALSE(first.IsReady());
  factory_.SimulateResponseForPendingRequest(std::string(kBaselineListURLs[1]),
                                             kPrivacy);
  tasks_.RunUntilIdle();
  EXPECT_FALSE(first.IsReady());
  updater->CheckNow(second.GetCallback());
  EXPECT_FALSE(second.IsReady());
  EXPECT_EQ(3u, factory_.total_requests());
  FinishCatalog();
  ASSERT_TRUE(first.IsReady());
  EXPECT_TRUE(first.Get());
  ASSERT_TRUE(second.IsReady());
  EXPECT_TRUE(second.Get());
  EXPECT_EQ(6u, factory_.total_requests());
  EXPECT_EQ((std::vector<bool>{true, false}), states);
}

TEST_F(BaselineListUpdaterTest, ManualCheckReportsFailureAndOwnerShutdown) {
  factory_.AddResponse(std::string(kBaselineListURLs[0]), "error",
                       net::HTTP_INTERNAL_SERVER_ERROR);
  auto updater = Create();
  base::test::TestFuture<bool> failed;
  updater->CheckNow(failed.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(failed.IsReady());
  Respond(kBraveCatalogURL, "error", net::HTTP_INTERNAL_SERVER_ERROR);
  ASSERT_TRUE(failed.IsReady());
  EXPECT_FALSE(failed.Get());
  factory_.ClearResponses();
  base::test::TestFuture<bool> closed;
  updater->CheckNow(closed.GetCallback());
  tasks_.RunUntilIdle();
  Respond(kBaselineListURLs[0], kList);
  Respond(kBaselineListURLs[1], kPrivacy);
  EXPECT_FALSE(closed.IsReady());
  ASSERT_EQ(1, factory_.NumPending());
  updater.reset();
  ASSERT_TRUE(closed.IsReady());
  EXPECT_FALSE(closed.Get());
  EXPECT_EQ(0, factory_.NumPending());
  EXPECT_TRUE(ReadBaselineListStore(path_).generation.empty());
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
  const std::string healthy = "https://filters.test/healthy.txt";
  ASSERT_TRUE(
      InstallBaselineLists(path_, {kList, kPrivacy}, base::Time::Now(), ""));
  ASSERT_EQ("",
            AddFilterSubscription(path_, enabled, "||enabled-ad.test^\n", ""));
  ASSERT_EQ(
      "", AddFilterSubscription(path_, disabled, "||disabled-ad.test^\n", ""));
  ASSERT_EQ("", ChangeFilterSubscription(path_, disabled, false, ""));
  ASSERT_EQ("",
            AddFilterSubscription(path_, healthy, "||old-healthy.test^\n", ""));
  const auto before = ReadBaselineListStore(path_);
  std::vector<GURL> requested;
  factory_.SetInterceptor(
      base::BindLambdaForTesting([&](const network::ResourceRequest& request) {
        requested.push_back(request.url);
      }));
  factory_.AddResponse(std::string(kBaselineListURLs[0]), kList);
  factory_.AddResponse(std::string(kBaselineListURLs[1]), kPrivacy);
  factory_.AddResponse(enabled, "error", net::HTTP_INTERNAL_SERVER_ERROR);
  factory_.AddResponse(healthy, "||new-healthy.test^\n");
  auto updater = Create();
  base::test::TestFuture<bool> checked;
  updater->CheckNow(checked.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(checked.IsReady());
  FinishCatalog();
  ASSERT_TRUE(checked.IsReady());
  EXPECT_FALSE(checked.Get());
  EXPECT_EQ(8u, factory_.total_requests());
  EXPECT_EQ(0, std::ranges::count(requested, GURL(disabled)));
  const auto snapshot = ReadBaselineListStore(path_);
  EXPECT_EQ(3u, snapshot.subscriptions.size());
  EXPECT_TRUE(snapshot.catalog_active);
  EXPECT_NE(snapshot.generation, before.generation);
  EXPECT_NE(snapshot.filters.find("enabled-ad.test"), std::string::npos);
  EXPECT_NE(snapshot.filters.find("new-healthy.test"), std::string::npos);
  EXPECT_EQ(snapshot.filters.find("old-healthy.test"), std::string::npos);
  EXPECT_EQ(snapshot.filters.find("disabled-ad.test"), std::string::npos);
  const auto stored = ReadFilterListSet(path_);
  ASSERT_EQ(stored.subscriptions.size(), 3u);
  for (const auto& subscription : stored.subscriptions) {
    if (subscription.info.url == enabled) {
      EXPECT_EQ(subscription.original, "||enabled-ad.test^\n");
      const auto previous = std::ranges::find(before.subscriptions, enabled,
                                              &FilterSubscription::url);
      ASSERT_NE(previous, before.subscriptions.end());
      EXPECT_EQ(subscription.info.checked_at, previous->checked_at);
    }
    if (subscription.info.url == disabled) {
      EXPECT_FALSE(subscription.info.enabled);
      EXPECT_EQ(subscription.original, "||disabled-ad.test^\n");
    }
  }
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
