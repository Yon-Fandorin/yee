// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_

#include <cstdint>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/skia/include/core/SkColor.h"

namespace yee {

// The one page-surface source attached to each WebContents. Sampling,
// stability, committed target, and the presented transition live here so the
// single Toolbar and split Pane Header can never create competing timelines
// for the same tab. Views only subscribe and atomically consume snapshots.
class BrowserSurfaceColorController
    : public content::WebContentsObserver,
      public content::WebContentsUserData<BrowserSurfaceColorController> {
 public:
  BrowserSurfaceColorController(const BrowserSurfaceColorController&) = delete;
  BrowserSurfaceColorController& operator=(
      const BrowserSurfaceColorController&) = delete;
  ~BrowserSurfaceColorController() override;

  base::CallbackListSubscription AddPresentationChangedCallback(
      base::RepeatingClosure callback);

  void SetThemeFallbackColor(SkColor color);

  // Seeds a tab-switch transition from the color already on screen. Structural
  // moves of the same source do not call this, so single/split handoff keeps
  // the exact in-flight presentation.
  void ActivateFrom(std::optional<SkColor> current_surface);

  const std::optional<BrowserSurfacePresentation>& GetPresentation() const;
  std::optional<SkColor> GetColor() const;
  uint64_t source_id() const { return source_id_; }

  // Deterministic production-path injection for browser/UI tests. Sampling is
  // quiesced and the complete snapshot is published synchronously so tests can
  // inspect actual consumers without racing compositor capture.
  void SetPageSurfaceColorForTesting(SkColor color);

  // Starts the same transition used by a newly committed page sample. Tests
  // use this after deterministic injection to verify that popup consumers
  // ignore intermediate animation revisions and refresh once at completion.
  void TransitionToPageSurfaceColorForTesting(SkColor color);

 private:
  friend class content::WebContentsUserData<BrowserSurfaceColorController>;
  friend class BrowserSurfaceColorControllerTest;

  explicit BrowserSurfaceColorController(content::WebContents* web_contents);

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidStopLoading() override;
  void RenderViewReady() override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void DidChangeThemeColor() override;
  void OnBackgroundColorChanged() override;
  void OnVisibilityChanged(content::Visibility visibility) override;
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
  void OnTopStripCaptured(uint64_t sampling_epoch,
                          const content::CopyFromSurfaceResult& result);
  void CommitColor(SkColor color);
  void StartColorTransition(SkColor target_color, base::TimeDelta duration);
  void AdvanceColorTransition();
  void StopColorTransition();
  void SetPresentedColor(SkColor color, bool popup_policy_changed);
  void PublishPresentation(bool popup_policy_changed);
  void CommitPageMetadataColorIfReady();
  std::optional<SkColor> GetPageMetadataColor() const;

  base::RepeatingClosureList presentation_changed_callbacks_;
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
  std::optional<BrowserSurfacePresentation> presentation_;
  base::TimeTicks transition_start_time_;
  base::TimeDelta transition_duration_;
  int stable_candidate_count_ = 0;
  int sample_attempt_ = 0;
  int pending_page_settling_samples_ = 0;
  bool capture_in_flight_ = false;
  bool waiting_for_load_completion_ = false;
  bool first_visually_non_empty_paint_seen_ = false;
  bool is_scroll_sampling_ = false;
  const uint64_t source_id_;
  uint64_t revision_ = 0;
  uint64_t popup_revision_ = 0;
  uint64_t sampling_epoch_ = 0;
  base::WeakPtrFactory<BrowserSurfaceColorController> weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_COLOR_CONTROLLER_H_
