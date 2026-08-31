// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_BINDING_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_BINDING_H_

#include <cstdint>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkColor.h"

namespace content {
class WebContents;
}

namespace yee {

class BrowserSurfaceColorController;

// The presentation outlet owned by one persistent ContentsContainerView.
// It is the only subscriber that bridges a WebContents-owned color source into
// the pane's view tree. Pane chrome and BrowserView's active-pane router may
// both observe this stable outlet without opening competing source bindings.
class BrowserSurfacePresentationBinding : public content::WebContentsObserver {
 public:
  BrowserSurfacePresentationBinding();
  BrowserSurfacePresentationBinding(const BrowserSurfacePresentationBinding&) =
      delete;
  BrowserSurfacePresentationBinding& operator=(
      const BrowserSurfacePresentationBinding&) = delete;
  ~BrowserSurfacePresentationBinding() override;

  void BindWebContents(content::WebContents* web_contents);
  void SetThemeFallbackColor(SkColor color);
  void ActivateFrom(std::optional<SkColor> current_surface);

  base::CallbackListSubscription AddPresentationChangedCallback(
      base::RepeatingClosure callback);

  const std::optional<BrowserSurfacePresentation>& GetPresentation() const;
  uint64_t source_id() const { return source_id_; }
  content::WebContents* web_contents() const { return web_contents_; }

 private:
  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

  void ClearBinding(bool notify_observers);
  void OnSourcePresentationChanged(uint64_t expected_source_id);
  void PublishCurrentPresentation();

  base::RepeatingClosureList presentation_changed_callbacks_;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  raw_ptr<BrowserSurfaceColorController> source_ = nullptr;
  base::CallbackListSubscription source_subscription_;
  std::optional<BrowserSurfacePresentation> presentation_;
  std::optional<SkColor> theme_fallback_color_;
  uint64_t source_id_ = 0;
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_BINDING_H_
