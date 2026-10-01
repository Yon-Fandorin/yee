// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/engine.h"

#include <algorithm>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::content_blocking {
namespace {
base::DictValue MakeResource(std::string_view name, std::string_view source,
                            int permission = 0,
                            const std::vector<std::string>& dependencies = {},
                            const std::vector<std::string>& aliases = {}) {
  base::DictValue resource;
  resource.Set("name", name);
  base::DictValue kind;
  kind.Set("mime", "application/javascript");
  resource.Set("kind", std::move(kind));
  resource.Set("content", base::Base64Encode(source));
  resource.Set("permission", permission);
  base::ListValue deps;
  for (const auto& dependency : dependencies) deps.Append(dependency);
  resource.Set("dependencies", std::move(deps));
  base::ListValue alias_list;
  for (const auto& alias : aliases) alias_list.Append(alias);
  resource.Set("aliases", std::move(alias_list));
  return resource;
}
}  // namespace

// Contracts exercised by Brave's AdBlockService browser tests, at Yee's
// unchanged Rust engine boundary. Renderer lifecycle is a separate app gate.
TEST(ContentBlockingEngine, FirstPartyUsesRegistrableDomain) {
  Engine engine("||a.co.uk^$third-party\n");
  EXPECT_FALSE(engine.ShouldBlock("https://cdn.a.co.uk/ad", "https://www.a.co.uk/", "script"));
  EXPECT_TRUE(engine.ShouldBlock("https://a.co.uk/ad", "https://b.co.uk/", "script"));
  EXPECT_TRUE(engine.ShouldBlock("https://a.co.uk/ad", "https://nota.co.uk/", "script"));
}
TEST(ContentBlockingEngine, ExceptionsApplyAcrossBothLists) {
  Engine engine("||baseline.test^\n@@||community.test^\n@@||important.test^\n", "",
                "@@||baseline.test^\n||community.test^\n||important.test^$important\n");
  EXPECT_FALSE(engine.ShouldBlock("https://baseline.test/ad", "https://page.test/", "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://community.test/ad", "https://page.test/", "script"));
  EXPECT_TRUE(engine.ShouldBlock("https://important.test/ad", "https://page.test/", "script"));
}
TEST(ContentBlockingEngine, CosmeticExceptionsAndGenerichideAcrossLists) {
  Engine engine("page.test##.baseline\npage.test#@#.community\n##div.generic\n##.generic-ad\n", "",
                "page.test#@#.baseline\npage.test##.community\npage.test##.kept\n@@||page.test^$generichide\n");
  const auto rules = engine.RulesForPage("https://page.test/");
  EXPECT_EQ(rules.selectors, std::vector<std::string>{".kept"});
  EXPECT_FALSE(rules.generic_hide);
  EXPECT_TRUE(engine.RulesForPage("https://other.test/").generic_hide);
  EXPECT_NE(std::find(rules.exceptions.begin(), rules.exceptions.end(), ".community"), rules.exceptions.end());
}
TEST(ContentBlockingEngine, ScriptletPermissionRequiresEveryBit) {
  base::ListValue resources;
  for (int mask : {0, 1, 2, 3, 5}) {
    resources.Append(MakeResource("mask" + std::to_string(mask) + ".js",
        "window.allowed" + std::to_string(mask) + " = true;", mask));
  }
  const auto json = base::WriteJson(resources).value();
  for (int mask : {0, 1, 2, 3, 5}) {
    const auto rule = "page.test##+js(mask" + std::to_string(mask) + ")\n";
    Engine untrusted(rule, json);
    EXPECT_EQ(!untrusted.RulesForPage("https://page.test/").script.empty(), mask == 0);
    // Identical calls in both lists may inherit bit 1, never the other bits.
    Engine trusted(rule, "", rule, json);
    EXPECT_EQ(!trusted.RulesForPage("https://page.test/").script.empty(), mask == 0 || mask == 1);
    const auto redirect_rule = "||ads.test^$redirect=mask" + std::to_string(mask) + ".js\n";
    Engine redirect(redirect_rule, json);
    const auto decision = redirect.Evaluate("https://ads.test/ad", "https://page.test/", "script");
    EXPECT_TRUE(decision.blocked);
    EXPECT_EQ(!decision.replacement.empty(), mask == 0);
  }
}
TEST(ContentBlockingEngine, ScriptletDependencyCannotEscalatePermission) {
  base::ListValue resources;
  resources.Append(MakeResource("entry.js", "function entry() { window.result = helper(); }", 0, {"helper.fn"}));
  resources.Append(MakeResource("helper.fn", "function helper() { return 7; }", 1));
  const auto json = base::WriteJson(resources).value();
  constexpr char rule[] = "page.test##+js(entry)\n";
  Engine untrusted(rule, json);
  EXPECT_TRUE(untrusted.RulesForPage("https://page.test/").script.empty());
  Engine trusted("", "", rule, json);
  const auto script = trusted.RulesForPage("https://page.test/").script;
  EXPECT_NE(script.find("function helper"), std::string::npos);
  EXPECT_NE(script.find("entry()"), std::string::npos);
}
TEST(ContentBlockingEngine, ScriptletAliasesAndNamesPreserveCase) {
  base::ListValue resources;
  resources.Append(MakeResource("case.js", "window.lower = true;", 0, {}, {"alias.js"}));
  resources.Append(MakeResource("CaSe.js", "window.mixed = true;"));
  const auto json = base::WriteJson(resources).value();
  Engine lower("page.test##+js(alias)\n", json);
  EXPECT_NE(lower.RulesForPage("https://page.test/").script.find("window.lower"), std::string::npos);
  Engine mixed("page.test##+js(CaSe)\n", json);
  EXPECT_NE(mixed.RulesForPage("https://page.test/").script.find("window.mixed"), std::string::npos);
  Engine unmatched("page.test##+js(CASE)\n", json);
  EXPECT_TRUE(unmatched.RulesForPage("https://page.test/").script.empty());
}
TEST(ContentBlockingEngine, WholeScriptletExceptionPreservesCosmeticsAndNetwork) {
  base::ListValue resources;
  resources.Append(MakeResource("one.js", "window.one = true;"));
  resources.Append(MakeResource("two.js", "window.two = true;", 1));
  Engine engine("page.test#@#+js()\npage.test##.ad\n||ads.test^\n", "",
                "page.test##+js(one)\npage.test##+js(two)\n", base::WriteJson(resources).value());
  const auto rules = engine.RulesForPage("https://page.test/");
  EXPECT_TRUE(rules.script.empty());
  EXPECT_EQ(rules.selectors, std::vector<std::string>{".ad"});
  EXPECT_TRUE(engine.ShouldBlock("https://ads.test/ad", "https://page.test/", "script"));
}
TEST(ContentBlockingEngine, CspMergesListsAndUsesThirdPartyContext) {
  Engine engine("||policy.test^$csp=img-src 'none'\n", "",
                "||policy.test^$csp=media-src 'none',third-party\n");
  const auto cross_site = engine.CspDirectives("https://policy.test/frame", "https://page.test/", "subdocument");
  EXPECT_NE(cross_site.find("img-src 'none'"), std::string::npos);
  EXPECT_NE(cross_site.find("media-src 'none'"), std::string::npos);
  EXPECT_EQ(engine.CspDirectives("https://policy.test/page", "https://policy.test/", "document"), "img-src 'none'");
}
TEST(ContentBlockingEngine, CspFixturePathMatchesPortedNavigationOnly) {
  Engine engine("||yee-fixture.test^*/csp-frame^$csp=img-src 'none'\n");
  EXPECT_EQ(engine.CspDirectives("http://yee-fixture.test:63333/csp-frame",
                                 "http://yee-fixture.test:63333/fixture",
                                 "subdocument"),
            "img-src 'none'");
  EXPECT_TRUE(engine.CspDirectives("http://yee-fixture.test:63333/fixture",
                                    "http://yee-fixture.test:63333/fixture",
                                    "document")
                  .empty());
}
TEST(ContentBlockingEngine, HostBoundaryAndException) {
  Engine engine("||ads.test^\n@@||ads.test/allowed^\n");
  EXPECT_TRUE(engine.ShouldBlock("https://ads.test/ad.js", "https://page.test/",
                                 "script"));
  EXPECT_TRUE(engine.ShouldBlock("https://sub.ads.test/ad.js",
                                 "https://page.test/", "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://notads.test/ad.js",
                                  "https://page.test/", "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://ads.test/allowed/file.js",
                                  "https://page.test/", "script"));
}
TEST(ContentBlockingEngine, DomainTypeMethodAndImportant) {
  Engine engine(
      "||ads.test^$script,domain=page.test,method=POST\n"
      "||important.test^$important\n@@||important.test^\n");
  EXPECT_TRUE(engine.ShouldBlock("https://ads.test/x", "https://page.test/",
                                 "script", "POST"));
  EXPECT_FALSE(engine.ShouldBlock("https://ads.test/x", "https://other.test/",
                                  "script", "POST"));
  EXPECT_FALSE(engine.ShouldBlock("https://ads.test/x", "https://page.test/",
                                  "image", "POST"));
  EXPECT_FALSE(engine.ShouldBlock("https://ads.test/x", "https://page.test/",
                                  "script", "GET"));
  EXPECT_TRUE(engine.ShouldBlock("https://important.test/x",
                                 "https://page.test/", "script"));
}
TEST(ContentBlockingEngine, CosmeticAndGenericExceptions) {
  Engine engine(
      "page.test##.ad\n##.generic-ad\n##.exception\npage.test#@#.exception\n");
  auto rules = engine.RulesForPage("https://page.test/");
  EXPECT_EQ(rules.selectors, std::vector<std::string>{".ad"});
  EXPECT_EQ(engine.GenericSelectors({"generic-ad", "exception"}, {},
                                    rules.exceptions),
            std::vector<std::string>{".generic-ad"});
}
TEST(ContentBlockingEngine, ResourceScriptlet) {
  Engine engine(
      "yee-fixture.test##+js(yee-fixture.js)\n",
      R"([{"name":"yee-fixture.js","kind":{"mime":"application/javascript"},"content":"d2luZG93Ll9feWVlRmlsdGVyRml4dHVyZSA9IHRydWU7"}])");
  auto rules = engine.RulesForPage("https://yee-fixture.test/");
  EXPECT_NE(rules.script.find("__yeeFilterFixture"), std::string::npos);
  EXPECT_EQ(BundleGeneration().size(), 64u);
}
TEST(ContentBlockingEngine, RedirectAndRemoveparamDecisions) {
  Engine engine("||ads.test^$redirect=blank-js\n"
                "||rule-only.test^$redirect-rule=blank-js\n"
                "||query.test^$removeparam=tracking\n",
      R"([{"name":"blank-js","kind":{"mime":"application/javascript"},"content":"LyogZW1wdHkgKi8="}])");
  const auto redirect = engine.Evaluate("https://ads.test/ad", "https://page.test/", "script");
  EXPECT_TRUE(redirect.blocked);
  EXPECT_EQ(redirect.replacement, "data:application/javascript;base64,LyogZW1wdHkgKi8=");
  const auto rule = engine.Evaluate("https://rule-only.test/ad", "https://page.test/", "script");
  EXPECT_FALSE(rule.blocked);
  EXPECT_TRUE(rule.replacement.empty());
  const auto query = engine.Evaluate("https://query.test/?keep=9007199254740993&tracking=1&other=%2f", "https://page.test/", "xmlhttprequest");
  EXPECT_FALSE(query.blocked);
  EXPECT_EQ(query.rewritten_url, "https://query.test/?keep=9007199254740993&other=%2f");
}
TEST(ContentBlockingEngine, InvalidResourcesFailClosedAtConstruction) {
  for (const auto* resources : {
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"invalid!"}])",
      R"([{"name":"x.js","aliases":["x.js"],"kind":{"mime":"application/javascript"},"content":""}])",
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"","dependencies":["missing.js"]}])",
      R"([{"name":"x.js","kind":{"mime":"application/javascript"},"content":"","dependencies":["x.js"]}])"}) {
    EXPECT_DEATH_IF_SUPPORTED({ Engine engine("", resources); }, "resources");
  }
}
TEST(ContentBlockingEngine, CommunityConflictKeepsOnlyBundledBaseline) {
  base::ListValue bundled;
  bundled.Append(MakeResource("base.js", "window.baseline = true;"));
  base::ListValue conflicting;
  conflicting.Append(MakeResource("base.js", "window.community = true;"));
  Engine engine("||baseline.test^\npage.test##+js(base)\n", base::WriteJson(bundled).value(),
                "||community.test^\n", base::WriteJson(conflicting).value());
  EXPECT_TRUE(engine.ShouldBlock("https://baseline.test/ad", "https://page.test/", "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://community.test/ad", "https://page.test/", "script"));
  const auto script = engine.RulesForPage("https://page.test/").script;
  EXPECT_NE(script.find("window.baseline"), std::string::npos);
  EXPECT_EQ(script.find("window.community"), std::string::npos);
}
TEST(ContentBlockingEngine, ProductionPlaybackResourceAndAdOnlyRules) {
  auto& engine = BundledEngineForCurrentSequence();
  EXPECT_TRUE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?foo=1&source=yt_ads&oad=1", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?foo=1&source=youtube", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_TRUE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?source=youtube&c=TVHTML5&oad=1", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_TRUE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?oad=1&c=TVHTML5", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?c=TVHTML5", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?abc=TVHTML5&broad=1", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/initplayback?c=TVHTML5_extra&oad=1", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_TRUE(engine.ShouldBlock("https://cdn.fixture.test/video_ad_fixture.mp4", "https://www.youtube.com/", "media"));
  EXPECT_FALSE(engine.ShouldBlock("https://cdn.fixture.test/video_ad_fixture.mp4", "https://other.test/", "media"));
  EXPECT_TRUE(engine.ShouldBlock("https://r1.googlevideo.com/videoplayback?ctier=3p_ad_foo", "https://www.youtube.com/", "media"));
  EXPECT_FALSE(engine.ShouldBlock("https://r1.googlevideo.com/videoplayback?ctier=ordinary", "https://www.youtube.com/", "media"));
  EXPECT_TRUE(engine.ShouldBlock("https://www.youtube.com/youtubei/v1/log_event", "https://www.youtube.com/", "xmlhttprequest"));
  EXPECT_FALSE(engine.ShouldBlock("https://www.youtube.com/youtubei/v1/player", "https://www.youtube.com/", "xmlhttprequest"));
}
TEST(ContentBlockingEngine, InvalidCompiledEngineFallsBackToText) {
  Engine engine("||tracker.test^\npage.test##.ad\n", "", "", "",
                "invalid binary cache");
  EXPECT_TRUE(engine.ShouldBlock("https://tracker.test/ad", "https://page.test/",
                                "script"));
  EXPECT_FALSE(engine.ShouldBlock("https://normal.test/app", "https://page.test/",
                                 "script"));
  const auto rules = engine.RulesForPage("https://page.test/");
  EXPECT_NE(std::find(rules.selectors.begin(), rules.selectors.end(), ".ad"),
            rules.selectors.end());
}
TEST(ContentBlockingEngine, ProductionBundleExcludesTestRules) {
  auto& engine = BundledEngineForCurrentSequence();
  EXPECT_FALSE(engine.ShouldBlock("https://yee-block.test/ad.js",
                                  "https://page.test/", "script"));
  EXPECT_TRUE(engine.RulesForPage("https://yee-fixture.test/").script.empty());
  EXPECT_TRUE(
      engine
          .GenericSelectors({"yee-generic-ad", "yee-generic-exception"}, {}, {})
          .empty());
}
TEST(ContentBlockingEngine, ProductionConditionalTrackerExceptionExcluded) {
  auto& engine = BundledEngineForCurrentSequence();
  EXPECT_TRUE(engine.ShouldBlock("https://bam.nr-data.net/events", "https://abema.tv/", "xmlhttprequest"));
  EXPECT_TRUE(engine.ShouldBlock("https://js-agent.newrelic.com/nr-loader.js", "https://abema.tv/", "script"));
}
TEST(ContentBlockingEngine, CspRulesPreserveExceptionsAndResourceScope) {
  Engine engine(
      "||policy.test^$csp=img-src 'none'\n"
      "@@||policy.test/except^$csp\n");
  EXPECT_EQ(engine.CspDirectives("https://policy.test/page",
                                 "https://page.test/", "document"),
            "img-src 'none'");
  EXPECT_EQ(engine.CspDirectives("https://policy.test/frame",
                                 "https://page.test/", "subdocument"),
            "img-src 'none'");
  EXPECT_TRUE(engine
                  .CspDirectives("https://policy.test/image",
                                 "https://page.test/", "image")
                  .empty());
  EXPECT_TRUE(engine
                  .CspDirectives("https://policy.test/except/page",
                                 "https://page.test/", "document")
                  .empty());
}
}  // namespace yee::content_blocking
