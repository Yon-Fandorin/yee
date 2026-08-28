// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_

#include <optional>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkColor.h"

namespace yee {

// Resolves the color shared by Yee's Browser Surface Header and the visible
// top edge of the active page. A short, low-resolution surface sample lets the
// header follow pages whose painted top band differs from their CSS document
// background. During navigation the last committed color remains visible until
// a new candidate survives a short stability gate. Switching to an uncached
// WebContents immediately selects the current theme's toolbar color as the new
// presentation target rather than carrying over another tab's color. A bounded,
// throttled burst follows a user scroll without capturing every frame; accepted
// scroll colors are presented through one retargetable transition instead of
// discrete intermediate commits.
class BrowserSurfaceColorController : public content::WebContentsObserver {
 public:
  explicit BrowserSurfaceColorController(base::RepeatingClosure color_changed);
  BrowserSurfaceColorController(const BrowserSurfaceColorController&) = delete;
  BrowserSurfaceColorController& operator=(
      const BrowserSurfaceColorController&) = delete;
  ~BrowserSurfaceColorController() override;

  void SetThemeFallbackColor(SkColor color);
  void SetWebContents(content::WebContents* web_contents);
  std::optional<SkColor> GetColor() const;

 private:
  friend class BrowserSurfaceColorControllerTest;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidStopLoading() override;
  void RenderViewReady() override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void DidChangeThemeColor() override;
  void OnBackgroundColorChanged() override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;
  void DidChangeVerticalScrollDirection(
      viz::VerticalScrollDirection scroll_direction) override;

  void RestartPageSampling(base::TimeDelta initial_delay);
  void BeginPageSettling();
  bool ScheduleNextPageSettlingSample();
  void ResetCandidateSequence();
  void ScheduleSample(base::TimeDelta delay);
  void StartScrollSampling();
  void StopScrollSampling();
  void CaptureTopStrip();
  void OnTopStripCaptured(int generation,
                          const content::CopyFromSurfaceResult& result);
  void CommitColor(SkColor color);
  void StartColorTransition(SkColor target_color, base::TimeDelta duration);
  void AdvanceColorTransition();
  void StopColorTransition();
  void SetPresentedColor(SkColor color);
  void ClearPresentedColor();
  void CommitPageMetadataColorIfReady();
  std::optional<SkColor> GetPageMetadataColor() const;

  base::RepeatingClosure color_changed_;
  base::OneShotTimer sample_timer_;
  base::RepeatingTimer scroll_sample_timer_;
  base::OneShotTimer scroll_sampling_timeout_timer_;
  base::RepeatingTimer color_transition_timer_;
  std::optional<SkColor> committed_color_;
  std::optional<SkColor> presented_color_;
  std::optional<SkColor> theme_fallback_color_;
  std::optional<SkColor> candidate_color_;
  std::optional<SkColor> transition_start_color_;
  std::optional<SkColor> transition_target_color_;
  base::TimeTicks transition_start_time_;
  base::TimeDelta transition_duration_;
  int stable_candidate_count_ = 0;
  int sample_attempt_ = 0;
  int generation_ = 0;
  int pending_page_settling_samples_ = 0;
  bool capture_in_flight_ = false;
  bool waiting_for_load_completion_ = false;
  bool first_visually_non_empty_paint_seen_ = false;
  bool is_scroll_sampling_ = false;
  base::WeakPtrFactory<BrowserSurfaceColorController> weak_ptr_factory_{this};
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_
