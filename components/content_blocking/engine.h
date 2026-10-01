// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_ENGINE_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_ENGINE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yee::content_blocking {

struct PageRules {
  std::vector<std::string> selectors;
  std::vector<std::string> exceptions;
  std::string script;
  bool generic_hide = false;
};
struct NetworkDecision {
  bool blocked = false;
  std::string replacement;
  std::string rewritten_url;
};

// Owns one upstream engine. All calls and destruction must use its sequence.
class Engine {
 public:
  Engine(std::string_view filters,
         std::string_view resources = {},
         std::string_view trusted_filters = {},
         std::string_view community_resources = {},
         std::string_view compiled_filters = {});
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  bool ShouldBlock(std::string_view url,
                   std::string_view source,
                   std::string_view type,
                   std::string_view method = "GET");
  NetworkDecision Evaluate(std::string_view url,
                           std::string_view source,
                           std::string_view type,
                           std::string_view method = "GET");
  PageRules RulesForPage(std::string_view url);
  std::string CspDirectives(std::string_view url,
                            std::string_view source,
                            std::string_view type,
                            std::string_view method = "GET");
  std::vector<std::string> GenericSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<Engine> CreateBundledEngine();
Engine& BundledEngineForCurrentSequence();
std::string_view BundleGeneration();

}  // namespace yee::content_blocking
#endif
