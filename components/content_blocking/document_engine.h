// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_DOCUMENT_ENGINE_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_DOCUMENT_ENGINE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "components/yee_content_blocking/engine.h"

namespace yee::content_blocking {

// Builds and owns the single-thread engine on a dedicated worker. Only copied
// rule results cross sequences; the worker never calls renderer or page code.
class DocumentEngine {
 public:
  DocumentEngine();
  explicit DocumentEngine(
      base::OnceCallback<std::unique_ptr<Engine>()> create_engine);
  ~DocumentEngine();
  DocumentEngine(const DocumentEngine&) = delete;
  DocumentEngine& operator=(const DocumentEngine&) = delete;

  // Must complete before the first page script. The embedder permits this
  // local wait at document-start; no nested message loop or browser IPC.
  PageRules RulesForPage(std::string url);
  void GenericSelectors(
      std::vector<std::string> classes,
      std::vector<std::string> ids,
      std::vector<std::string> exceptions,
      base::OnceCallback<void(std::vector<std::string>)> reply);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Renderer-process lifetime. Construct early to overlap initialization with
// renderer startup; first use also works if eager preparation was skipped.
DocumentEngine& RendererDocumentEngine();

}  // namespace yee::content_blocking
#endif
