// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/filter_data.h"

#include <algorithm>

#include "base/base_paths.h"
#include "base/base64.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/engine.h"
#include "crypto/hash.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {
constexpr char kManifest[] = "YeeCommunityFilterManifest.json";
constexpr char kRules[] = "YeeCommunityFilters.txt";
constexpr char kResources[] = "YeeCommunityResources.json";
class CommunityFilterDataTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }
  void WritePack(std::string_view rules, std::string_view resources = "[]") {
    ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kResources), resources));
    ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kRules), rules));
    ASSERT_TRUE(base::WriteFile(
        directory_.GetPath().AppendASCII(kManifest),
        "{\"schema_version\":2,\"rules_file\":\"YeeCommunityFilters.txt\","
        "\"rules_sha256\":\"" +
            base::HexEncodeLower(crypto::hash::Sha256(rules)) +
            "\",\"resources_file\":\"YeeCommunityResources.json\",\"resources_sha256\":\"" +
            base::HexEncodeLower(crypto::hash::Sha256(resources)) + "\"}"));
  }
  FilterDataSnapshot Read() { return ReadCommunityFilterData(directory_.GetPath()); }
  base::ScopedTempDir directory_;
};
TEST_F(CommunityFilterDataTest, MissingAndRelativeDirectoriesHaveNoRules) {
  EXPECT_EQ(Read().status, FilterDataStatus::kMissing);
  EXPECT_EQ(ReadCommunityFilterData(base::FilePath()).status, FilterDataStatus::kMissing);
  EXPECT_EQ(ReadCommunityFilterData(base::FilePath(FILE_PATH_LITERAL("relative"))).status,
            FilterDataStatus::kInvalid);
  EXPECT_TRUE(Read().filters.empty());
}
TEST_F(CommunityFilterDataTest, ModifiedTextWorksWithoutBrowserRebuild) {
  WritePack("||community-only.test^\n");
  const auto first = Read();
  ASSERT_EQ(first.status, FilterDataStatus::kLoaded);
  Engine first_engine(first.filters);
  EXPECT_TRUE(first_engine.ShouldBlock("https://community-only.test/ad.js", "https://page.test/", "script"));
  WritePack("||replacement-only.test^\n");
  const auto second = Read();
  ASSERT_EQ(second.status, FilterDataStatus::kLoaded);
  EXPECT_NE(first.generation, second.generation);
  Engine second_engine(second.filters);
  EXPECT_FALSE(second_engine.ShouldBlock("https://community-only.test/ad.js", "https://page.test/", "script"));
  EXPECT_TRUE(second_engine.ShouldBlock("https://replacement-only.test/ad.js", "https://page.test/", "script"));
}
TEST_F(CommunityFilterDataTest, ChangedDataRejectsEntireSnapshot) {
  WritePack("||tracker.test^\n");
  ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kRules), "@@||tracker.test^\n"));
  const auto data = Read();
  EXPECT_EQ(data.status, FilterDataStatus::kInvalid);
  EXPECT_TRUE(data.filters.empty());
  EXPECT_TRUE(data.generation.empty());
}
TEST_F(CommunityFilterDataTest, ManifestCannotChooseAnotherFile) {
  WritePack("||tracker.test^\n");
  ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kManifest),
      "{\"schema_version\":2,\"rules_file\":\"../private.cc\",\"rules_sha256\":\"" + std::string(64, '0') + "\"}"));
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST_F(CommunityFilterDataTest, InvalidAndFutureManifestsAreRejected) {
  for (const auto* text : {"not json", "{}", "{\"schema_version\":3}"}) {
    ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kManifest), text));
    EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
  }
}
TEST_F(CommunityFilterDataTest, EmptyAndInvalidUtf8AreRejected) {
  for (const auto& rules : {std::string(), std::string("\xff\xfe", 2)}) {
    WritePack(rules);
    EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
  }
}
TEST_F(CommunityFilterDataTest, OversizedFilesAreRejected) {
  WritePack(std::string(16 * 1024 * 1024 + 1, 'x'));
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
  ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kManifest),
                             std::string(128 * 1024 + 1, ' ')));
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST_F(CommunityFilterDataTest, InvalidResourcesRejectFiltersToo) {
  for (const auto* resources : {"not json", "{}",
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"invalid!"}])",
      R"([{"name":"x.js","aliases":["x.js"],"kind":{"mime":"application/javascript"},"content":""}])",
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"","dependencies":["missing.fn"]}])",
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"","dependencies":["x.js"]}])"}) {
    WritePack("||tracker.test^\n", resources);
    const auto snapshot = Read();
    EXPECT_EQ(snapshot.status, FilterDataStatus::kInvalid);
    EXPECT_TRUE(snapshot.filters.empty());
    EXPECT_TRUE(snapshot.resources.empty());
  }
  WritePack("||tracker.test^\n");
  ASSERT_TRUE(base::WriteFile(directory_.GetPath().AppendASCII(kResources), "[ ]"));
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST_F(CommunityFilterDataTest, CanonicalFallbackConflictsAreRejected) {
  WritePack("||tracker.test^\n",
            R"([{"name":"yee-empty.js","kind":{"mime":"application/javascript"},"content":""}])");
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST_F(CommunityFilterDataTest, ExcessiveDependencyDepthIsRejectedBeforeInjection) {
  std::string resources = "[";
  for (int i = 0; i < 130; ++i) {
    if (i) resources += ",";
    resources += "{\"name\":\"d" + std::to_string(i) +
                 ".fn\",\"kind\":{\"mime\":\"application/javascript\"},\"content\":\"\"";
    if (i) resources += ",\"dependencies\":[\"d" + std::to_string(i - 1) + ".fn\"]";
    resources += "}";
  }
  WritePack("||tracker.test^\n", resources + "]");
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST_F(CommunityFilterDataTest, ResourcesContributeToGeneration) {
  WritePack("||tracker.test^\n", "[]");
  const auto first = Read();
  WritePack("||tracker.test^\n", "[ ]");
  const auto second = Read();
  ASSERT_EQ(second.status, FilterDataStatus::kLoaded);
  EXPECT_NE(first.generation, second.generation);
}
TEST_F(CommunityFilterDataTest, OversizedResourcesAreRejected) {
  WritePack("||tracker.test^\n", std::string(16 * 1024 * 1024 + 1, ' '));
  EXPECT_EQ(Read().status, FilterDataStatus::kInvalid);
}
TEST(CommunityFilterProductionData, PackExtendsEngineAndPreservesNormalPlayback) {
  base::FilePath executable_directory;
  ASSERT_TRUE(base::PathService::Get(base::DIR_EXE, &executable_directory));
  const auto data = ReadCommunityFilterData(
      executable_directory.AppendASCII("community-filter-test-data"));
  ASSERT_EQ(data.status, FilterDataStatus::kLoaded);
  EXPECT_EQ(data.generation.size(), 64u);
  Engine engine(kBundledFilters, kBundledResources, data.filters, data.resources);
  EXPECT_TRUE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?source=yt_ads&oad=1", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?source=youtube", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://www.youtube.com/youtubei/v1/player", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://www.youtube.com/youtubei/v1/get_watch?video=normal", "https://www.youtube.com/", "xmlhttprequest"));
  const auto rules = engine.RulesForPage("https://funnyand.com/");
  EXPECT_NE(std::find(rules.selectors.begin(), rules.selectors.end(), ".ad-unit-desktop"), rules.selectors.end());
  const auto youtube = engine.RulesForPage("https://www.youtube.com/").script;
  EXPECT_NE(youtube.find("function safeSelf"), std::string::npos);
  EXPECT_NE(youtube.find("setConstant("), std::string::npos);
  EXPECT_NE(youtube.find("serverContract"), std::string::npos);
  EXPECT_NE(youtube.find("trustedJsonEditXhrRequest("), std::string::npos);
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("yee-scriptlet-fixture-output")) {
    base::DictValue scripts;
    for (const auto* host : {"www.youtube.com", "m.youtube.com", "music.youtube.com", "tv.youtube.com",
                             "www.youtube-nocookie.com", "www.youtubekids.com"}) {
      scripts.Set(host, engine.RulesForPage(std::string("https://") + host + "/").script);
    }
    auto fixture_resources = base::JSONReader::ReadList(data.resources, base::JSON_PARSE_RFC);
    ASSERT_TRUE(fixture_resources);
    base::DictValue runtime;
    runtime.Set("name", "runtime-contract.js");
    base::DictValue kind;
    kind.Set("mime", "application/javascript");
    runtime.Set("kind", std::move(kind));
    runtime.Set("content", base::Base64Encode(
        "function yeeRuntimeContract() { 'use strict'; "
        "const fresh = !scriptletGlobals.has('fixture-state'); "
        "const get = scriptletGlobals.get, set = scriptletGlobals.set; "
        "set('fixture-state', 7); scriptletGlobals['fixture-property'] = 9; "
        "window.runtimeContract = fresh && scriptletGlobals['fixture-state'] === 7 && "
        "get('fixture-property') === 9 && deAmpEnabled === false && "
        "scriptletGlobals.canDebug === undefined; }"));
    fixture_resources->Append(std::move(runtime));
    Engine fixtures("", "",
        "fixture.test##+js(set, fixtureFlag, true)\n"
        "fixture.test##+js(json-prune, adSlots)\n"
        "fixture.test##+js(runtime-contract)\n"
        "fixture.test##+js(trusted-json-edit-xhr-request, .context.client+={\"clientScreen\":\"CHANNEL\"}, propsToMatch, /player)\n",
        base::WriteJson(*fixture_resources).value());
    scripts.Set("fixture.test", fixtures.RulesForPage("https://fixture.test/").script);
    ASSERT_TRUE(base::WriteFile(command_line->GetSwitchValuePath("yee-scriptlet-fixture-output"),
                               base::WriteJson(scripts).value()));
  }
}
TEST(CommunityFilterProductionData, TrustedPermissionsAndScriptletExceptions) {
  base::FilePath directory;
  ASSERT_TRUE(base::PathService::Get(base::DIR_EXE, &directory));
  const auto data = ReadCommunityFilterData(directory.AppendASCII("community-filter-test-data"));
  ASSERT_EQ(data.status, FilterDataStatus::kLoaded);
  constexpr char trusted_rule[] = "fixture.test##+js(trusted-set-constant, flag, true)\n";
  Engine untrusted(trusted_rule, "", "", data.resources);
  EXPECT_TRUE(untrusted.RulesForPage("https://fixture.test/").script.empty());
  Engine trusted("", kBundledResources, trusted_rule, data.resources);
  EXPECT_NE(trusted.RulesForPage("https://fixture.test/").script.find("trustedSetConstant("), std::string::npos);
  EXPECT_TRUE(trusted.RulesForPage("https://other.test/").script.empty());
  Engine excepted("fixture.test#@#+js(trusted-set-constant, flag, true)\n", "",
                  trusted_rule, data.resources);
  EXPECT_TRUE(excepted.RulesForPage("https://fixture.test/").script.empty());
  Engine redirect_engine("||fixture.test/noop$script,redirect=noopjs\n", kBundledResources,
                         "", data.resources);
  const auto redirect = redirect_engine.Evaluate("https://fixture.test/noop", "https://page.test/", "script");
  EXPECT_TRUE(redirect.blocked);
  EXPECT_EQ(redirect.replacement.find("data:application/javascript;base64,"), 0u);
}
TEST(CommunityFilterProductionData, EveryOriginalRedirectAndAliasPreservesBytes) {
  base::FilePath directory;
  ASSERT_TRUE(base::PathService::Get(base::DIR_EXE, &directory));
  const auto data = ReadCommunityFilterData(directory.AppendASCII("community-filter-test-data"));
  ASSERT_EQ(data.status, FilterDataStatus::kLoaded);
  const auto resources = base::JSONReader::ReadList(data.resources, base::JSON_PARSE_RFC);
  ASSERT_TRUE(resources);
  std::string filters;
  std::vector<std::pair<std::string, std::string>> expected;
  size_t redirect_count = 0;
  for (const auto& value : *resources) {
    const auto& resource = value.GetDict();
    if (resource.FindBool("scriptlet").value_or(false) ||
        resource.FindBool("brave_resource").value_or(false)) continue;
    ++redirect_count;
    const auto* name = resource.FindString("name");
    const auto* kind = resource.FindDict("kind");
    const auto* content = resource.FindString("content");
    ASSERT_TRUE(name && kind && content);
    const auto* mime = kind->FindString("mime");
    ASSERT_TRUE(mime);
    std::vector<std::string> identifiers{*name};
    if (const auto* aliases = resource.FindList("aliases")) {
      for (const auto& alias : *aliases) identifiers.push_back(alias.GetString());
    }
    for (const auto& identifier : identifiers) {
      const auto path = "/r" + std::to_string(expected.size()) + "/";
      filters += "||redirect-fixture.test" + path + "$redirect=" + identifier + "\n";
      expected.emplace_back("https://redirect-fixture.test" + path + "ad",
                            "data:" + *mime + ";base64," + *content);
    }
  }
  ASSERT_EQ(redirect_count, 45u);
  ASSERT_GT(expected.size(), redirect_count);
  Engine engine(filters, kBundledResources, "", data.resources);
  for (const auto& [url, replacement] : expected) {
    SCOPED_TRACE(url);
    const auto decision = engine.Evaluate(url, "https://page.test/", "xmlhttprequest");
    EXPECT_TRUE(decision.blocked);
    EXPECT_EQ(decision.replacement, replacement);
  }
}
}  // namespace
}  // namespace yee::content_blocking
