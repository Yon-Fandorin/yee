// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "base/time/time.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace {

constexpr int kTopStripHeight = 32;
constexpr gfx::Size kSampleSize(96, 8);
constexpr base::TimeDelta kInitialSampleDelay = base::Milliseconds(32);
constexpr base::TimeDelta kFirstPaintSampleDelay = base::Milliseconds(16);
constexpr base::TimeDelta kVerificationDelay = base::Milliseconds(48);
constexpr base::TimeDelta kFirstSettlingSampleDelay = base::Milliseconds(80);
constexpr base::TimeDelta kSecondSettlingSampleDelay = base::Milliseconds(100);
constexpr base::TimeDelta kCaptureTimeout = base::Milliseconds(500);
constexpr base::TimeDelta kScrollSampleInterval = base::Milliseconds(140);
constexpr base::TimeDelta kScrollSamplingDuration = base::Milliseconds(1680);
constexpr base::TimeDelta kScrollColorTransitionDuration =
    base::Milliseconds(200);
constexpr base::TimeDelta kTabSwitchColorTransitionDuration =
    base::Milliseconds(120);
constexpr base::TimeDelta kPageColorTransitionDuration =
    base::Milliseconds(160);
constexpr base::TimeDelta kColorTransitionFrameInterval =
    base::Milliseconds(16);
constexpr int kMaximumSampleAttempts = 5;
constexpr int kRequiredStablePageSamples = 2;
constexpr int kRequiredStableScrollSamples = 2;
constexpr int kPageSettlingSampleCount = 2;
constexpr int kMinimumOpaqueAlpha = 230;
constexpr double kMinimumDominantShare = 0.55;
constexpr int kMaximumStableChannelDelta = 12;

// Stores the last color that completed Yee's stability gate on this tab. The
// cache is owned by WebContents so closing a background tab cannot leave a
// dangling lookup entry. Returning to an already sampled tab can therefore
// select its own Header target immediately and transition continuously from the
// color currently on screen.
class BrowserSurfaceColorCache
    : public content::WebContentsUserData<BrowserSurfaceColorCache> {
 public:
  ~BrowserSurfaceColorCache() override = default;

  std::optional<SkColor> color() const { return color_; }
  void set_color(SkColor color) { color_ = color; }

 private:
  explicit BrowserSurfaceColorCache(content::WebContents* web_contents)
      : content::WebContentsUserData<BrowserSurfaceColorCache>(*web_contents) {}

  friend class content::WebContentsUserData<BrowserSurfaceColorCache>;
  WEB_CONTENTS_USER_DATA_KEY_DECL();

  std::optional<SkColor> color_;
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(BrowserSurfaceColorCache);

struct ColorBucket {
  int count = 0;
  int red = 0;
  int green = 0;
  int blue = 0;
};

std::optional<SkColor> FindDominantFlatColor(const SkBitmap& bitmap) {
  if (bitmap.drawsNothing()) {
    return std::nullopt;
  }

  std::array<ColorBucket, 4096> buckets;
  int considered = 0;
  int winner = 0;
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      const SkColor color = bitmap.getColor(x, y);
      if (SkColorGetA(color) < kMinimumOpaqueAlpha) {
        continue;
      }

      const int bucket_index = (SkColorGetR(color) >> 4) << 8 |
                               (SkColorGetG(color) >> 4) << 4 |
                               (SkColorGetB(color) >> 4);
      ColorBucket& bucket = buckets[bucket_index];
      ++bucket.count;
      bucket.red += SkColorGetR(color);
      bucket.green += SkColorGetG(color);
      bucket.blue += SkColorGetB(color);
      ++considered;
      if (bucket.count > buckets[winner].count) {
        winner = bucket_index;
      }
    }
  }

  const ColorBucket& dominant = buckets[winner];
  if (considered == 0 || dominant.count < considered * kMinimumDominantShare) {
    return std::nullopt;
  }
  return SkColorSetRGB(dominant.red / dominant.count,
                       dominant.green / dominant.count,
                       dominant.blue / dominant.count);
}

bool ColorsAreStable(SkColor first, SkColor second) {
  const auto channel_delta = [](uint8_t first_channel, uint8_t second_channel) {
    return std::abs(static_cast<int>(first_channel) -
                    static_cast<int>(second_channel));
  };
  return std::max({channel_delta(SkColorGetR(first), SkColorGetR(second)),
                   channel_delta(SkColorGetG(first), SkColorGetG(second)),
                   channel_delta(SkColorGetB(first), SkColorGetB(second))}) <=
         kMaximumStableChannelDelta;
}

SkColor AverageColors(SkColor first, SkColor second) {
  return SkColorSetRGB((SkColorGetR(first) + SkColorGetR(second)) / 2,
                       (SkColorGetG(first) + SkColorGetG(second)) / 2,
                       (SkColorGetB(first) + SkColorGetB(second)) / 2);
}

SkColor InterpolateColors(SkColor start, SkColor target, double progress) {
  const auto interpolate_channel = [progress](uint8_t start_channel,
                                              uint8_t target_channel) {
    return static_cast<uint8_t>(std::lround(
        start_channel + (target_channel - start_channel) * progress));
  };
  return SkColorSetRGB(
      interpolate_channel(SkColorGetR(start), SkColorGetR(target)),
      interpolate_channel(SkColorGetG(start), SkColorGetG(target)),
      interpolate_channel(SkColorGetB(start), SkColorGetB(target)));
}

bool IsScrollInteraction(const blink::WebInputEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kGestureScrollBegin ||
      event.GetType() == blink::WebInputEvent::Type::kMouseWheel) {
    return true;
  }
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) {
    return false;
  }

  const int key_code =
      static_cast<const blink::WebKeyboardEvent&>(event).windows_key_code;
  return key_code == ui::VKEY_UP || key_code == ui::VKEY_DOWN ||
         key_code == ui::VKEY_PRIOR || key_code == ui::VKEY_NEXT ||
         key_code == ui::VKEY_HOME || key_code == ui::VKEY_END ||
         key_code == ui::VKEY_SPACE;
}

}  // namespace

namespace yee {

BrowserSurfaceColorController::BrowserSurfaceColorController(
    base::RepeatingClosure color_changed)
    : color_changed_(std::move(color_changed)) {}

BrowserSurfaceColorController::~BrowserSurfaceColorController() = default;

void BrowserSurfaceColorController::SetThemeFallbackColor(SkColor color) {
  color = SkColorSetA(color, SK_AlphaOPAQUE);
  if (theme_fallback_color_ == color) {
    return;
  }
  theme_fallback_color_ = color;
  if (web_contents() && !committed_color_.has_value()) {
    StartColorTransition(color, kTabSwitchColorTransitionDuration);
  }
}

void BrowserSurfaceColorController::SetWebContents(
    content::WebContents* web_contents) {
  if (web_contents == this->web_contents()) {
    return;
  }
  StopColorTransition();
  Observe(web_contents);
  waiting_for_load_completion_ = web_contents && web_contents->IsLoading();
  first_visually_non_empty_paint_seen_ = false;
  pending_page_settling_samples_ = 0;
  RestartPageSampling(kInitialSampleDelay);

  // A newly selected WebContents must never inherit another tab's resolved
  // page color. Leaving this empty makes the Header use the current theme's
  // toolbar color until this tab produces its own stable sample. Returning to
  // an already sampled tab selects that tab's cached color as the next target.
  std::optional<SkColor> next_color;
  if (web_contents) {
    if (const BrowserSurfaceColorCache* cache =
            BrowserSurfaceColorCache::FromWebContents(web_contents);
        cache && cache->color().has_value()) {
      next_color = cache->color();
    }
  }
  committed_color_ = next_color;
  if (!web_contents) {
    ClearPresentedColor();
  } else if (next_color.has_value()) {
    StartColorTransition(*next_color, kTabSwitchColorTransitionDuration);
  } else if (theme_fallback_color_.has_value()) {
    StartColorTransition(*theme_fallback_color_,
                         kTabSwitchColorTransitionDuration);
  } else {
    ClearPresentedColor();
  }
}

std::optional<SkColor> BrowserSurfaceColorController::GetColor() const {
  return presented_color_;
}

void BrowserSurfaceColorController::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInPrimaryMainFrame() &&
      navigation_handle->HasCommitted()) {
    waiting_for_load_completion_ = !navigation_handle->IsSameDocument();
    if (!navigation_handle->IsSameDocument()) {
      first_visually_non_empty_paint_seen_ = false;
      pending_page_settling_samples_ = 0;
    }
    RestartPageSampling(kInitialSampleDelay);
  }
}

void BrowserSurfaceColorController::DidStopLoading() {
  waiting_for_load_completion_ = false;
  BeginPageSettling();
}

void BrowserSurfaceColorController::RenderViewReady() {
  RestartPageSampling(kInitialSampleDelay);
}

void BrowserSurfaceColorController::DidFirstVisuallyNonEmptyPaint() {
  first_visually_non_empty_paint_seen_ = true;
  BeginPageSettling();
}

void BrowserSurfaceColorController::DidChangeThemeColor() {
  RestartPageSampling(kFirstPaintSampleDelay);
}

void BrowserSurfaceColorController::OnBackgroundColorChanged() {
  RestartPageSampling(kFirstPaintSampleDelay);
}

void BrowserSurfaceColorController::OnVisibilityChanged(
    content::Visibility visibility) {
  if (visibility == content::Visibility::VISIBLE) {
    // A background tab can enter split view after its first paint callbacks
    // have already fired. Sample when Chromium makes that WebContents visible
    // instead of waiting for the first subsequent scroll interaction.
    BeginPageSettling();
  }
}

void BrowserSurfaceColorController::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  if (IsScrollInteraction(event)) {
    StartScrollSampling();
  }
}

void BrowserSurfaceColorController::DidChangeVerticalScrollDirection(
    viz::VerticalScrollDirection) {
  StartScrollSampling();
}

void BrowserSurfaceColorController::RestartPageSampling(
    base::TimeDelta initial_delay) {
  sample_timer_.Stop();
  scroll_sample_timer_.Stop();
  scroll_sampling_timeout_timer_.Stop();
  weak_ptr_factory_.InvalidateWeakPtrs();
  ++generation_;
  ResetCandidateSequence();
  sample_attempt_ = 0;
  capture_in_flight_ = false;
  is_scroll_sampling_ = false;
  // Hidden tabs do not have a dependable compositor surface to copy. Preserve
  // their cached/fallback presentation and let OnVisibilityChanged() begin a
  // fresh bounded sequence when Chromium exposes them as a split pane.
  if (web_contents() &&
      web_contents()->GetVisibility() == content::Visibility::VISIBLE) {
    ScheduleSample(initial_delay);
  }
}

void BrowserSurfaceColorController::BeginPageSettling() {
  pending_page_settling_samples_ = kPageSettlingSampleCount;
  RestartPageSampling(kFirstPaintSampleDelay);
}

bool BrowserSurfaceColorController::ScheduleNextPageSettlingSample() {
  if (is_scroll_sampling_ || pending_page_settling_samples_ <= 0) {
    return false;
  }

  const base::TimeDelta delay =
      pending_page_settling_samples_ == kPageSettlingSampleCount
          ? kFirstSettlingSampleDelay
          : kSecondSettlingSampleDelay;
  --pending_page_settling_samples_;
  ScheduleSample(delay);
  return true;
}

void BrowserSurfaceColorController::ResetCandidateSequence() {
  candidate_color_.reset();
  stable_candidate_count_ = 0;
}

void BrowserSurfaceColorController::ScheduleSample(base::TimeDelta delay) {
  sample_timer_.Start(FROM_HERE, delay, this,
                      &BrowserSurfaceColorController::CaptureTopStrip);
}

void BrowserSurfaceColorController::StartScrollSampling() {
  if (!web_contents()) {
    return;
  }
  pending_page_settling_samples_ = 0;
  ResetCandidateSequence();
  is_scroll_sampling_ = true;
  CaptureTopStrip();
  scroll_sample_timer_.Start(FROM_HERE, kScrollSampleInterval, this,
                             &BrowserSurfaceColorController::CaptureTopStrip);
  scroll_sampling_timeout_timer_.Start(
      FROM_HERE, kScrollSamplingDuration, this,
      &BrowserSurfaceColorController::StopScrollSampling);
}

void BrowserSurfaceColorController::StopScrollSampling() {
  scroll_sample_timer_.Stop();
  CaptureTopStrip();
}

void BrowserSurfaceColorController::CaptureTopStrip() {
  if (capture_in_flight_) {
    return;
  }
  content::RenderWidgetHostView* const view =
      web_contents() ? web_contents()->GetRenderWidgetHostView() : nullptr;
  if (!view || !view->IsSurfaceAvailableForCopy()) {
    if (++sample_attempt_ < kMaximumSampleAttempts) {
      ScheduleSample(kVerificationDelay);
    } else {
      CommitPageMetadataColorIfReady();
    }
    return;
  }

  const gfx::Size viewport = view->GetVisibleViewportSize();
  if (viewport.IsEmpty()) {
    if (++sample_attempt_ < kMaximumSampleAttempts) {
      ScheduleSample(kVerificationDelay);
    } else {
      CommitPageMetadataColorIfReady();
    }
    return;
  }

  ++sample_attempt_;
  capture_in_flight_ = true;
  const gfx::Rect top_strip(0, 0, viewport.width(),
                            std::min(viewport.height(), kTopStripHeight));
  view->CopyFromSurface(top_strip, kSampleSize, kCaptureTimeout,
                        base::BindPostTaskToCurrentDefault(base::BindOnce(
                            &BrowserSurfaceColorController::OnTopStripCaptured,
                            weak_ptr_factory_.GetWeakPtr(), generation_)));
}

void BrowserSurfaceColorController::OnTopStripCaptured(
    int generation,
    const content::CopyFromSurfaceResult& result) {
  capture_in_flight_ = false;
  if (generation != generation_ || !web_contents()) {
    return;
  }

  std::optional<SkColor> candidate;
  if (result.has_value()) {
    candidate = FindDominantFlatColor(result->bitmap);
  }

  if (candidate.has_value()) {
    if (candidate_color_.has_value() &&
        ColorsAreStable(*candidate_color_, *candidate)) {
      candidate_color_ = AverageColors(*candidate_color_, *candidate);
      ++stable_candidate_count_;
    } else {
      candidate_color_ = candidate;
      stable_candidate_count_ = 1;
    }

    const std::optional<SkColor> page_metadata_color = GetPageMetadataColor();
    const bool matches_page_metadata =
        page_metadata_color.has_value() &&
        ColorsAreStable(*page_metadata_color, *candidate_color_);
    const int required_stable_samples =
        is_scroll_sampling_
            ? kRequiredStableScrollSamples
            : (matches_page_metadata ? 1 : kRequiredStablePageSamples);
    const bool has_meaningful_page_frame =
        !waiting_for_load_completion_ || first_visually_non_empty_paint_seen_;
    if (stable_candidate_count_ >= required_stable_samples &&
        has_meaningful_page_frame) {
      CommitColor(*candidate_color_);
      ScheduleNextPageSettlingSample();
      return;
    }
  }

  if (sample_attempt_ < kMaximumSampleAttempts) {
    ScheduleSample(kVerificationDelay);
  } else {
    CommitPageMetadataColorIfReady();
  }
}

void BrowserSurfaceColorController::CommitColor(SkColor color) {
  color = SkColorSetA(color, SK_AlphaOPAQUE);
  BrowserSurfaceColorCache::GetOrCreateForWebContents(web_contents())
      ->set_color(color);

  if (committed_color_.has_value() && *committed_color_ == color) {
    return;
  }
  committed_color_ = color;

  if (presented_color_.has_value()) {
    StartColorTransition(color, is_scroll_sampling_
                                    ? kScrollColorTransitionDuration
                                    : kPageColorTransitionDuration);
  } else {
    StopColorTransition();
    SetPresentedColor(color);
  }
}

void BrowserSurfaceColorController::StartColorTransition(
    SkColor target_color,
    base::TimeDelta duration) {
  if (!presented_color_.has_value() || *presented_color_ == target_color) {
    StopColorTransition();
    SetPresentedColor(target_color);
    return;
  }

  transition_start_color_ = presented_color_;
  transition_target_color_ = target_color;
  transition_start_time_ = base::TimeTicks::Now();
  transition_duration_ = duration;
  if (!color_transition_timer_.IsRunning()) {
    color_transition_timer_.Start(
        FROM_HERE, kColorTransitionFrameInterval, this,
        &BrowserSurfaceColorController::AdvanceColorTransition);
  }
}

void BrowserSurfaceColorController::AdvanceColorTransition() {
  if (!transition_start_color_.has_value() ||
      !transition_target_color_.has_value()) {
    StopColorTransition();
    return;
  }

  const double progress = std::clamp(
      (base::TimeTicks::Now() - transition_start_time_).InMillisecondsF() /
          transition_duration_.InMillisecondsF(),
      0.0, 1.0);
  if (progress >= 1.0) {
    const SkColor target_color = *transition_target_color_;
    StopColorTransition();
    SetPresentedColor(target_color);
    return;
  }

  // Smoothstep avoids a visible jump at either end. If a later sample selects
  // a new target, StartColorTransition() begins from the color currently on
  // screen, so the motion remains continuous instead of adding another
  // discrete midpoint.
  const double eased_progress = progress * progress * (3.0 - 2.0 * progress);
  SetPresentedColor(InterpolateColors(
      *transition_start_color_, *transition_target_color_, eased_progress));
}

void BrowserSurfaceColorController::StopColorTransition() {
  color_transition_timer_.Stop();
  transition_start_color_.reset();
  transition_target_color_.reset();
  transition_start_time_ = base::TimeTicks();
  transition_duration_ = base::TimeDelta();
}

void BrowserSurfaceColorController::ClearPresentedColor() {
  if (presented_color_.has_value()) {
    presented_color_.reset();
    color_changed_.Run();
  }
}

void BrowserSurfaceColorController::SetPresentedColor(SkColor color) {
  color = SkColorSetA(color, SK_AlphaOPAQUE);
  if (!presented_color_.has_value() || *presented_color_ != color) {
    presented_color_ = color;
    color_changed_.Run();
  }
}

void BrowserSurfaceColorController::CommitPageMetadataColorIfReady() {
  if (waiting_for_load_completion_ || is_scroll_sampling_) {
    return;
  }
  if (const std::optional<SkColor> page_metadata_color =
          GetPageMetadataColor()) {
    CommitColor(*page_metadata_color);
  }
}

std::optional<SkColor> BrowserSurfaceColorController::GetPageMetadataColor()
    const {
  if (!web_contents()) {
    return std::nullopt;
  }

  std::optional<SkColor> color = web_contents()->GetBackgroundColor();
  if (!color.has_value() || SkColorGetA(*color) == SK_AlphaTRANSPARENT) {
    color = web_contents()->GetThemeColor();
  }
  if (color.has_value()) {
    return SkColorSetA(*color, SK_AlphaOPAQUE);
  }
  return std::nullopt;
}

}  // namespace yee
