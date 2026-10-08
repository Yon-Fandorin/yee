// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_content_blocking/baseline_list_store.h"

#include <algorithm>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/shared_memory_switch.h"
#include "base/pickle.h"
#include "base/process/launch.h"
#include "build/build_config.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/engine.h"
#include "components/yee_content_blocking/filter_list_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {
std::array<std::string, 2> Lists(std::string_view revision) {
  return {"[Adblock Plus 2.0]\n! Title: EasyList\n||" + std::string(revision) +
              ".test^\n" + std::string(revision) +
              ".test##.yee-procedural:has-text(Advertisement)\n",
          "[Adblock Plus 1.1]\n! Title: EasyPrivacy\n||tracking.test^\n"};
}
class BaselineListStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    path_ = base::MakeAbsoluteFilePath(directory_.GetPath());
    ASSERT_FALSE(path_.empty());
  }
  bool Install(std::string_view revision,
               base::Time time = base::Time::Now(),
               std::string_view running = "") {
    return InstallBaselineLists(path_, Lists(revision), time, running);
  }
  BaselineListSnapshot Read() { return ReadBaselineListStore(path_); }
  base::FilePath Generation(std::string_view generation) {
    return path_.AppendASCII("generations").AppendASCII(generation);
  }
  base::ScopedTempDir directory_;
  base::FilePath path_;
};
TEST_F(BaselineListStoreTest, PublishesCompleteOriginalPairAndCompiledCache) {
  ASSERT_TRUE(Install("updated"));
  const auto snapshot = Read();
  ASSERT_FALSE(snapshot.generation.empty());
  ASSERT_FALSE(snapshot.compiled_filters.empty());
  std::string original;
  ASSERT_TRUE(base::ReadFileToString(
      Generation(snapshot.generation).AppendASCII("easylist.txt"), &original));
  EXPECT_EQ(original, Lists("updated")[0]);
  Engine engine(snapshot.filters + "\n" + std::string(kOwnedFilters),
                kBundledResources, "", "", snapshot.compiled_filters);
  EXPECT_TRUE(engine.ShouldBlock("https://updated.test/ad", "https://page.test",
                                 "script"));
  EXPECT_TRUE(engine.ShouldBlock("https://doubleclick.net/ad",
                                 "https://page.test", "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://normal.test/content",
                                  "https://page.test", "script"));
  EXPECT_EQ(
      1u,
      engine.RulesForPage("https://updated.test/").procedural_actions.size());
}
TEST_F(BaselineListStoreTest, FailedValidationLeavesPublishedStateIntact) {
  ASSERT_TRUE(Install("old"));
  const auto generation = Read().generation;
  for (const char* invalid :
       {"<html>error</html>", "[Adblock Plus 2.0]\n! empty\n",
        "[Adblock Plus 2.0]\n##\n",
        "[Adblock Plus 2.0]\n!#include other.txt\n"}) {
    auto lists = Lists("new");
    lists[1] = invalid;
    EXPECT_FALSE(InstallBaselineLists(path_, lists, base::Time::Now(), ""));
    EXPECT_EQ(generation, Read().generation);
  }
}
TEST_F(BaselineListStoreTest, CorruptCurrentTextRecoversPreviousGeneration) {
  ASSERT_TRUE(Install("old"));
  const auto old = Read().generation;
  ASSERT_TRUE(Install("new"));
  const auto current = Read().generation;
  ASSERT_TRUE(base::WriteFile(Generation(current).AppendASCII("easylist.txt"),
                              "corrupt"));
  EXPECT_EQ(old, Read().generation);
  EXPECT_TRUE(Read().recovered);
}
TEST_F(BaselineListStoreTest, SwappedOfficialListsDoNotReplaceWorkingPair) {
  ASSERT_TRUE(Install("old"));
  const auto original = Read().generation;
  auto swapped = Lists("new");
  std::swap(swapped[0], swapped[1]);
  EXPECT_FALSE(InstallBaselineLists(path_, swapped, base::Time::Now(), ""));
  EXPECT_EQ(original, Read().generation);
}
TEST_F(BaselineListStoreTest, CorruptStateRecoversBackupPointer) {
  ASSERT_TRUE(Install("old"));
  const auto old = Read().generation;
  ASSERT_TRUE(Install("new"));
  ASSERT_TRUE(base::WriteFile(path_.AppendASCII("state.json"), "broken"));
  EXPECT_EQ(old, Read().generation);
  EXPECT_TRUE(Read().recovered);
}
TEST_F(BaselineListStoreTest,
       UnchangedDownloadRetainsRollbackAndAdvancesCheckTime) {
  const auto first = base::Time::Now();
  ASSERT_TRUE(Install("old", first));
  const auto old = Read().generation;
  ASSERT_TRUE(Install("new", first));
  const auto current = Read().generation;
  ASSERT_TRUE(Install("new", first + base::Days(1)));
  EXPECT_EQ(first + base::Days(1), Read().checked_at);
  ASSERT_TRUE(base::WriteFile(
      Generation(current).AppendASCII("easyprivacy.txt"), "broken"));
  EXPECT_EQ(old, Read().generation);
}
TEST_F(BaselineListStoreTest, KeepsRunningPinAcrossMultiplePendingUpdates) {
  ASSERT_TRUE(Install("running"));
  const auto running = Read().generation;
  ASSERT_TRUE(Install("second", base::Time::Now(), running));
  const auto second = Read().generation;
  ASSERT_TRUE(Install("third", base::Time::Now(), running));
  const auto third = Read().generation;
  ASSERT_TRUE(Install("fourth", base::Time::Now(), running));
  EXPECT_EQ(running, ReadBaselineListGeneration(path_, running).generation);
  EXPECT_EQ(third, ReadBaselineListGeneration(path_, third).generation);
  EXPECT_TRUE(ReadBaselineListGeneration(path_, second).generation.empty());
  EXPECT_TRUE(
      ReadBaselineListGeneration(path_, "../../outside").generation.empty());
}
TEST_F(BaselineListStoreTest,
       RepeatedDownloadBackupRetainsPreviousWhenStateAndCurrentAreCorrupt) {
  ASSERT_TRUE(Install("old"));
  const auto old = Read().generation;
  ASSERT_TRUE(Install("new"));
  const auto current = Read().generation;
  ASSERT_TRUE(Install("new"));
  ASSERT_TRUE(base::WriteFile(path_.AppendASCII("state.json"), "broken"));
  ASSERT_TRUE(base::WriteFile(Generation(current).AppendASCII("easylist.txt"),
                              "broken"));
  EXPECT_EQ(old, Read().generation);
  EXPECT_TRUE(Read().recovered);
}
TEST_F(BaselineListStoreTest, InvalidCompiledCacheKeepsValidatedText) {
  ASSERT_TRUE(Install("updated"));
  const auto generation = Read().generation;
  ASSERT_TRUE(base::WriteFile(
      Generation(generation).AppendASCII("compiled.dat"), "invalid"));
  EXPECT_EQ(generation, Read().generation);
  EXPECT_TRUE(Read().compiled_filters.empty());
  EXPECT_NE(Read().filters.find("||updated.test^"), std::string::npos);
}
TEST_F(BaselineListStoreTest, ReadOnlySnapshotSurvivesStoreReplacement) {
  ASSERT_TRUE(Install("running"));
  const auto running = Read();
  auto region = CreateBaselineListRegion(running);
  ASSERT_TRUE(region.IsValid());
  ASSERT_TRUE(Install("pending"));
  ASSERT_TRUE(base::DeletePathRecursively(path_));
  const auto received = ReadBaselineListRegion(region);
  ASSERT_TRUE(received);
  EXPECT_EQ(running.generation, received->generation);
  EXPECT_EQ(running.filters, received->filters);
  EXPECT_EQ(running.compiled_filters, received->compiled_filters);
}
TEST_F(BaselineListStoreTest,
       SubscriptionAddsToBundledRulesAndSurvivesOfficialUpdate) {
  const std::string url = "https://filters.test/regional.txt";
  const std::string rules =
      "! Title: Regional rules\n||regional-ad.test^\npage.test##.regional-ad\n";
  ASSERT_EQ("", AddFilterSubscription(path_, url, rules, ""));
  auto snapshot = Read();
  EXPECT_FALSE(snapshot.baseline_downloaded);
  ASSERT_EQ(1u, snapshot.subscriptions.size());
  EXPECT_EQ("Regional rules", snapshot.subscriptions[0].title);
  EXPECT_TRUE(snapshot.subscriptions[0].enabled);
  auto region = CreateBaselineListRegion(snapshot);
  const auto received = ReadBaselineListRegion(region);
  ASSERT_TRUE(received);
  Engine engine(received->filters, kBundledResources, "", "",
                received->compiled_filters);
  EXPECT_TRUE(engine.ShouldBlock("https://regional-ad.test/banner",
                                 "https://page.test", "image"));
  EXPECT_TRUE(engine.ShouldBlock("https://doubleclick.net/ad",
                                 "https://page.test", "script"));
  const auto selectors = engine.RulesForPage("https://page.test/").selectors;
  EXPECT_NE(selectors.end(), std::ranges::find(selectors, ".regional-ad"));
  ASSERT_TRUE(Install("updated"));
  snapshot = Read();
  EXPECT_TRUE(snapshot.baseline_downloaded);
  EXPECT_EQ(1u, snapshot.subscriptions.size());
  EXPECT_NE(snapshot.filters.find("regional-ad.test"), std::string::npos);
}
TEST_F(BaselineListStoreTest,
       SubscriptionToggleAndRemovalPublishCompleteSelections) {
  ASSERT_TRUE(Install("old"));
  const std::string url = "https://filters.test/custom.txt";
  ASSERT_EQ("", AddFilterSubscription(path_, url, "||custom-ad.test^\n", ""));
  const auto enabled = Read().generation;
  ASSERT_EQ("", ChangeFilterSubscription(path_, url, false, enabled));
  EXPECT_FALSE(Read().subscriptions[0].enabled);
  EXPECT_EQ(Read().filters.find("custom-ad.test"), std::string::npos);
  EXPECT_EQ(1u, ReadFilterListSet(path_).subscriptions.size());
  ASSERT_EQ("", ChangeFilterSubscription(path_, url, true, enabled));
  EXPECT_EQ(enabled, Read().generation);
  ASSERT_EQ("", ChangeFilterSubscription(path_, url, std::nullopt, enabled));
  EXPECT_TRUE(Read().subscriptions.empty());
  EXPECT_EQ("list-missing",
            ChangeFilterSubscription(path_, url, true, enabled));
  EXPECT_EQ(enabled, ReadBaselineListGeneration(path_, enabled).generation);
}
TEST_F(BaselineListStoreTest, SubscriptionValidationCannotReplaceSavedState) {
  ASSERT_TRUE(Install("old"));
  const auto generation = Read().generation;
  const std::string url = "https://filters.test/custom.txt";
  for (const char* body : {"<html>error</html>", "! empty\n", "##\n",
                           "!#include missing.txt\n||ad.test^\n"}) {
    EXPECT_EQ("invalid-list", AddFilterSubscription(path_, url, body, ""));
    EXPECT_EQ(generation, Read().generation);
  }
  EXPECT_EQ("invalid-list",
            AddFilterSubscription(
                path_, url, std::string(kMaxSubscribedListBytes + 1, 'x'), ""));
  ASSERT_EQ("", AddFilterSubscription(path_, url, "||ad.test^\n", ""));
  EXPECT_EQ("duplicate-list",
            AddFilterSubscription(path_, url, "||other.test^\n", ""));
  for (const char* input : {"http://filters.test/rules.txt",
                            "https://user:pass@filters.test/rules.txt",
                            "https://filters.test/rules.txt#section"})
    EXPECT_FALSE(CanonicalFilterSubscriptionURL(input));
  EXPECT_EQ("https://filters.test/rules.txt",
            CanonicalFilterSubscriptionURL(" HTTPS://FILTERS.TEST/rules.txt "));
}
TEST_F(BaselineListStoreTest,
       FailedSubscriptionUpdateRetainsItsRulesWhileOtherListsAdvance) {
  ASSERT_TRUE(Install("old"));
  const std::string url = "https://filters.test/custom.txt";
  ASSERT_EQ("", AddFilterSubscription(path_, url, "||custom-old.test^\n", ""));
  const auto last_good = Read().subscriptions[0].checked_at;
  const auto now = base::Time::Now() + base::Days(1);
  auto result = UpdateFilterLists(path_, Lists("new"),
                                  {{url, "<html>error</html>"}}, now, "");
  EXPECT_FALSE(result.succeeded);
  EXPECT_EQ((std::vector<std::string>{url}), result.failed_urls);
  auto snapshot = Read();
  EXPECT_NE(snapshot.filters.find("||new.test^"), std::string::npos);
  EXPECT_NE(snapshot.filters.find("custom-old.test"), std::string::npos);
  EXPECT_EQ(last_good, snapshot.subscriptions[0].checked_at);
  result = UpdateFilterLists(path_, Lists("new"),
                             {{url, "||custom-new.test^\n"}}, now, "");
  EXPECT_TRUE(result.succeeded);
  snapshot = Read();
  EXPECT_EQ(now, snapshot.subscriptions[0].checked_at);
  EXPECT_NE(snapshot.filters.find("custom-new.test"), std::string::npos);
}
TEST_F(BaselineListStoreTest,
       CorruptSubscriptionRecoversEntirePreviousSelection) {
  ASSERT_TRUE(Install("old"));
  const auto old = Read().generation;
  ASSERT_EQ("", AddFilterSubscription(path_, "https://filters.test/custom.txt",
                                      "||custom.test^\n", old));
  const auto snapshot = Read();
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(
      Generation(snapshot.generation).AppendASCII("manifest.json"), &text));
  const auto manifest = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(manifest);
  const auto* file =
      (*manifest->FindList("lists"))[2].GetDict().FindString("file");
  ASSERT_TRUE(file);
  ASSERT_TRUE(base::WriteFile(
      Generation(snapshot.generation).AppendASCII(*file), "broken"));
  EXPECT_EQ(old, Read().generation);
  EXPECT_TRUE(Read().recovered);
}

TEST(BaselineListSharedMemory,
     SupportsBundledSelectionAndRejectsMalformedData) {
  auto region = CreateBaselineListRegion({});
  ASSERT_TRUE(region.IsValid());
  auto snapshot = ReadBaselineListRegion(region);
  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(snapshot->generation.empty());
  EXPECT_FALSE(ReadBaselineListRegion(base::ReadOnlySharedMemoryRegion()));
  base::Pickle invalid;
  invalid.WriteUInt32(1);
  invalid.WriteString("../../outside");
  invalid.WriteString("||ad.test^");
  invalid.WriteString("");
  auto memory = base::ReadOnlySharedMemoryRegion::Create(invalid.size());
  ASSERT_TRUE(memory.IsValid());
  base::span(memory.mapping).copy_from(invalid.AsBytes());
  EXPECT_FALSE(ReadBaselineListRegion(memory.region));
  base::Pickle truncated;
  truncated.WriteUInt32(1);
  auto short_memory =
      base::ReadOnlySharedMemoryRegion::Create(truncated.size());
  ASSERT_TRUE(short_memory.IsValid());
  base::span(short_memory.mapping).copy_from(truncated.AsBytes());
  EXPECT_FALSE(ReadBaselineListRegion(short_memory.region));
}
TEST(BaselineListSharedMemory, ExplicitTransportBoundSupportsLargeSnapshot) {
  BaselineListSnapshot original{std::string(64, 'a'), "||ad.test^\n",
                                std::string(9 * 1024 * 1024, 'b')};
  auto region = CreateBaselineListRegion(original);
  ASSERT_TRUE(region.IsValid());
  auto received = ReadBaselineListRegion(region);
  ASSERT_TRUE(received);
  EXPECT_EQ(original.compiled_filters, received->compiled_filters);
  base::CommandLine command(base::CommandLine::NO_PROGRAM);
  base::LaunchOptions options;
  base::shared_memory::SharedMemorySwitch transport(
      "yee-test-large-snapshot", 'yeet', 300, kMaxBaselineListSnapshotBytes);
  transport.AddToLaunchParameters(region, &command, &options);
  EXPECT_TRUE(command.HasSwitch("yee-test-large-snapshot"));
  base::shared_memory::SharedMemorySwitch standard("yee-test-default", 'yeed',
                                                   301);
  EXPECT_EQ(8u * 1024 * 1024, standard.maximum_region_size);
#if BUILDFLAG(IS_APPLE)
  // No child is launched in this unit test to consume the duplicate right.
  for (auto& [key, port] : options.mach_ports_for_rendezvous)
    port.Destroy();
#endif
}
TEST_F(BaselineListStoreTest,
       WrongManifestFilenameCannotReadOutsideGeneration) {
  ASSERT_TRUE(Install("updated"));
  const auto generation = Read().generation;
  const auto path = Generation(generation).AppendASCII("manifest.json");
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(path, &text));
  auto manifest = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(manifest);
  (*manifest->FindList("lists"))[0].GetDict().Set("file", "../easylist.txt");
  ASSERT_TRUE(base::WriteFile(path, base::WriteJson(*manifest).value()));
  EXPECT_TRUE(Read().generation.empty());
}
TEST(BaselineListPreprocessor, UsesBuildConditionsAndRejectsMalformedBranches) {
  const auto selected = PreprocessBaselineList(
      "[Adblock Plus 2.0]\n!#if ext_abp\n||abp.test^\n!#else\n"
      "!#if env_chromium\n||chromium.test^\n!#endif\n!#endif\n"
      "!#if unknown_future\n||first.test^\n!#else\n||second.test^\n!#endif\n");
  ASSERT_TRUE(selected);
  EXPECT_EQ(selected->find("abp.test"), std::string::npos);
  EXPECT_NE(selected->find("chromium.test"), std::string::npos);
  EXPECT_NE(selected->find("first.test"), std::string::npos);
  EXPECT_NE(selected->find("second.test"), std::string::npos);
  for (const char* body :
       {"!#else", "!#endif", "!#if false", "!#if\n", "!#include other.txt"})
    EXPECT_FALSE(PreprocessBaselineList(
        std::string("[Adblock Plus 2.0]\n||ad.test^\n") + body));
}
}  // namespace
}  // namespace yee::content_blocking
