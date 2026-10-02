// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/baseline_list_updater.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
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
}  // namespace
}  // namespace yee::content_blocking
