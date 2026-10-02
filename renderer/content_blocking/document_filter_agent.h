// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef CHROME_RENDERER_YEE_CONTENT_BLOCKING_DOCUMENT_FILTER_AGENT_H_
#define CHROME_RENDERER_YEE_CONTENT_BLOCKING_DOCUMENT_FILTER_AGENT_H_
#include "base/memory/weak_ptr.h"
#include "components/yee_content_blocking/selector_styles.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "third_party/blink/public/web/web_document.h"
#include "v8/include/v8-forward.h"
namespace yee::content_blocking {
class DocumentFilterAgent
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<DocumentFilterAgent> {
 public:
  explicit DocumentFilterAgent(content::RenderFrame* frame);
  ~DocumentFilterAgent() override;
  static void PrepareEngine();
  // Initial creation needs frame lifetime; normal document-start also guards
  // document replacement before Chromium continues its extension callbacks.
  static bool ApplyAtDocumentStart(content::RenderFrame* frame,
                                   bool initial_empty_document = false);
  void DidCreateNewDocument() override;
  void OnDestruct() override;

 private:
  void Apply();
  void InsertSelectors(const std::vector<std::string>& selectors);
  void InsertGenericSelectors(std::vector<std::string> selectors);
  static void ApplyGeneric(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void InsertProceduralStyle(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ProceduralEnabled(const v8::FunctionCallbackInfo<v8::Value>& args);
  bool applied_ = false;
  SelectorStyles styles_;
  std::vector<blink::WebStyleSheetKey> style_keys_;
  std::vector<blink::WebStyleSheetKey> procedural_style_keys_;
  size_t procedural_style_bytes_ = 0;
  base::WeakPtrFactory<DocumentFilterAgent> weak_factory_{this};
  // Document replacement cancels old work, but does not destroy the frame.
  base::WeakPtrFactory<DocumentFilterAgent> lifetime_factory_{this};
};
}  // namespace yee::content_blocking
#endif
