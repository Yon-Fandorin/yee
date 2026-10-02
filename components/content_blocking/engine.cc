// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "components/yee_content_blocking/engine.h"

#include "base/check.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/sequence_checker.h"
#include "base/trace_event/trace_event.h"
#include "components/yee_content_blocking/bundled_rules.h"
#include "components/yee_content_blocking/filter_data.h"
#include "components/yee_content_blocking/rust/src/lib.rs.h"
#include "components/yee_content_blocking/settings.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hash.h"

namespace yee::content_blocking {
namespace {
rust::Str Str(std::string_view s) {
  return rust::Str(s.empty() ? "" : s.data(), s.size());
}
rust::Vec<rust::String> RustStrings(const std::vector<std::string>& strings) {
  rust::Vec<rust::String> result;
  for (const auto& s : strings)
    result.emplace_back(s);
  return result;
}
std::vector<std::string> Strings(const rust::Vec<rust::String>& strings) {
  std::vector<std::string> result;
  for (const auto& s : strings)
    result.emplace_back(s.data(), s.size());
  return result;
}
rust::Box<FilterEngine> CreateFilterEngine(
    std::string_view filters, std::string_view resources,
    std::string_view trusted_filters, std::string_view community_resources,
    std::string_view compiled_filters = {}) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.CreateEngine");
  return new_engine(Str(filters), Str(resources), Str(trusted_filters),
                    Str(community_resources),
                    rust::Slice<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(compiled_filters.data()),
                        compiled_filters.size()));
}
}  // namespace

struct Engine::Impl {
  Impl(std::string_view filters, std::string_view resources,
       std::string_view trusted_filters, std::string_view community_resources,
       std::string_view compiled_filters)
      : engine(CreateFilterEngine(filters, resources, trusted_filters,
                                  community_resources, compiled_filters)) {
    if (!engine->resources_valid() && !community_resources.empty()) {
      LOG(ERROR) << "Yee community resources conflict with engine resources; "
                    "using bundled baseline";
      engine = CreateFilterEngine(filters, resources, "", "");
    }
    CHECK(engine->resources_valid());
  }
  rust::Box<FilterEngine> engine;
  SEQUENCE_CHECKER(sequence_checker);
};

Engine::Engine(std::string_view filters, std::string_view resources,
               std::string_view trusted_filters,
               std::string_view community_resources,
               std::string_view compiled_filters)
    : impl_(std::make_unique<Impl>(filters, resources, trusted_filters,
                                  community_resources, compiled_filters)) {}
Engine::~Engine() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
}

bool Engine::ShouldBlock(std::string_view url,
                         std::string_view source,
                         std::string_view type,
                         std::string_view method) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
  return impl_->engine->check(Str(url), Str(source), Str(type), Str(method));
}

NetworkDecision Engine::Evaluate(std::string_view url,
                                  std::string_view source,
                                  std::string_view type,
                                  std::string_view method) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.MatchRequest");
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
  auto result = impl_->engine->evaluate(Str(url), Str(source), Str(type), Str(method));
  return {result.blocked,
          std::string(result.replacement.data(), result.replacement.size()),
          std::string(result.rewritten_url.data(), result.rewritten_url.size())};
}

PageRules Engine::RulesForPage(std::string_view url) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.RulesForPage");
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
  auto rules = impl_->engine->document_rules(Str(url));
  std::string script;
  if (!rules.script.empty()) {
    script = kScriptletRuntime;
    constexpr std::string_view marker = "/* YEE_SCRIPTLET_PROGRAM */";
    const auto offset = script.find(marker);
    CHECK_NE(offset, std::string::npos);
    script.replace(offset, marker.size(), rules.script.data(), rules.script.size());
  }
  return {Strings(rules.selectors), Strings(rules.procedural_actions),
          Strings(rules.exceptions),
          std::move(script),
          rules.generic_hide};
}

std::string Engine::CspDirectives(std::string_view url,
                                  std::string_view source,
                                  std::string_view type,
                                  std::string_view method) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.CspDirectives");
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
  const auto directives = impl_->engine->csp_directives(Str(url), Str(source),
                                                        Str(type), Str(method));
  return std::string(directives.data(), directives.size());
}

std::vector<std::string> Engine::GenericSelectors(
    const std::vector<std::string>& classes,
    const std::vector<std::string>& ids,
    const std::vector<std::string>& exceptions) {
  TRACE_EVENT0("loading", "Yee.ContentBlocking.GenericSelectors");
  DCHECK_CALLED_ON_VALID_SEQUENCE(impl_->sequence_checker);
  return Strings(impl_->engine->generic_selectors(
      RustStrings(classes), RustStrings(ids), RustStrings(exceptions)));
}

std::unique_ptr<Engine> CreateBundledEngine() {
  return std::make_unique<Engine>(
      TestRulesEnabled()
          ? std::string(kBundledFilters) + "\n" + std::string(kTestFilters)
          : std::string(kBundledFilters),
      TestRulesEnabled() ? kTestResources : kBundledResources,
      CommunityFilterData().filters, CommunityFilterData().resources,
      TestRulesEnabled() ? std::string_view()
                         : std::string_view(CommunityFilterData().compiled_filters));
}
Engine& BundledEngineForCurrentSequence() {
  // Browser request matching uses one dedicated sequence. Never transfer an
  // upstream single-thread engine; renderer document queries own a worker.
  thread_local base::NoDestructor<std::unique_ptr<Engine>> engine(
      CreateBundledEngine());
  return **engine;
}
std::string_view BundleGeneration() {
  static const base::NoDestructor<std::string> generation(
      CommunityFilterData().generation.empty()
          ? std::string(kBundleGeneration)
          : base::HexEncodeLower(crypto::hash::Sha256(
                std::string(kBundleGeneration) + "\n" +
                CommunityFilterData().generation)));
  return *generation;
}
}  // namespace yee::content_blocking
