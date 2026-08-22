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
constexpr base::TimeDelta kInitialSampleDelay = base::Milliseconds(160);
constexpr base::TimeDelta kVerificationDelay = base::Milliseconds(75);
constexpr base::TimeDelta kCaptureTimeout = base::Milliseconds(500);
constexpr base::TimeDelta kScrollSampleInterval = base::Milliseconds(140);
constexpr base::TimeDelta kScrollSamplingDuration = base::Milliseconds(1680);
constexpr base::TimeDelta kScrollColorTransitionDuration =
    base::Milliseconds(200);
constexpr base::TimeDelta kColorTransitionFrameInterval =
    base::Milliseconds(16);
constexpr int kMaximumSampleAttempts = 5;
constexpr int kRequiredStablePageSamples = 3;
constexpr int kRequiredStableScrollSamples = 2;
constexpr int kMinimumOpaqueAlpha = 230;
constexpr double kMinimumDominantShare = 0.55;
constexpr int kMaximumStableChannelDelta = 12;

// Stores the last color that completed Yee's stability gate on this tab. The
// cache is owned by WebContents so closing a background tab cannot leave a
// dangling lookup entry, and returning to an already sampled tab can restore
// its Header without showing another tab's color during the sampling delay.
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

void BrowserSurfaceColorController::SetWebContents(
    content::WebContents* web_contents) {
  if (web_contents == this->web_contents()) {
    return;
  }
  StopColorTransition();
  Observe(web_contents);
  waiting_for_load_completion_ = web_contents && web_contents->IsLoading();
  RestartPageSampling();

  std::optional<SkColor> next_color = committed_color_;
  if (!web_contents) {
    next_color.reset();
  } else if (const BrowserSurfaceColorCache* cache =
                 BrowserSurfaceColorCache::FromWebContents(web_contents);
             cache && cache->color().has_value()) {
    next_color = cache->color();
  }
  const bool presentation_changed = presented_color_ != next_color;
  committed_color_ = next_color;
  presented_color_ = next_color;
  if (presentation_changed) {
    color_changed_.Run();
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
    RestartPageSampling();
  }
}

void BrowserSurfaceColorController::DidStopLoading() {
  waiting_for_load_completion_ = false;
  RestartPageSampling();
}

void BrowserSurfaceColorController::RenderViewReady() {
  RestartPageSampling();
}

void BrowserSurfaceColorController::DidChangeThemeColor() {
  RestartPageSampling();
}

void BrowserSurfaceColorController::OnBackgroundColorChanged() {
  RestartPageSampling();
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

void BrowserSurfaceColorController::RestartPageSampling() {
  sample_timer_.Stop();
  scroll_sample_timer_.Stop();
  scroll_sampling_timeout_timer_.Stop();
  weak_ptr_factory_.InvalidateWeakPtrs();
  ++generation_;
  ResetCandidateSequence();
  sample_attempt_ = 0;
  capture_in_flight_ = false;
  is_scroll_sampling_ = false;
  if (web_contents()) {
    ScheduleSample(kInitialSampleDelay);
  }
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
      CommitFallbackIfReady();
    }
    return;
  }

  const gfx::Size viewport = view->GetVisibleViewportSize();
  if (viewport.IsEmpty()) {
    if (++sample_attempt_ < kMaximumSampleAttempts) {
      ScheduleSample(kVerificationDelay);
    } else {
      CommitFallbackIfReady();
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

    const int required_stable_samples = is_scroll_sampling_
                                            ? kRequiredStableScrollSamples
                                            : kRequiredStablePageSamples;
    if (stable_candidate_count_ >= required_stable_samples) {
      if (!waiting_for_load_completion_) {
        CommitColor(*candidate_color_);
      }
      return;
    }
  }

  if (sample_attempt_ < kMaximumSampleAttempts) {
    ScheduleSample(kVerificationDelay);
  } else {
    CommitFallbackIfReady();
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

  if (is_scroll_sampling_ && presented_color_.has_value()) {
    StartColorTransition(color);
  } else {
    StopColorTransition();
    SetPresentedColor(color);
  }
}

void BrowserSurfaceColorController::StartColorTransition(SkColor target_color) {
  if (!presented_color_.has_value() || *presented_color_ == target_color) {
    StopColorTransition();
    SetPresentedColor(target_color);
    return;
  }

  transition_start_color_ = presented_color_;
  transition_target_color_ = target_color;
  transition_start_time_ = base::TimeTicks::Now();
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
          kScrollColorTransitionDuration.InMillisecondsF(),
      0.0, 1.0);
  if (progress >= 1.0) {
    const SkColor target_color = *transition_target_color_;
    StopColorTransition();
    SetPresentedColor(target_color);
    return;
  }

  // Smoothstep avoids a visible jump at either end. If a later scroll sample
  // selects a new target, StartColorTransition() begins from the color that is
  // currently on screen, so the motion remains continuous instead of adding
  // another discrete midpoint.
  const double eased_progress = progress * progress * (3.0 - 2.0 * progress);
  SetPresentedColor(InterpolateColors(
      *transition_start_color_, *transition_target_color_, eased_progress));
}

void BrowserSurfaceColorController::StopColorTransition() {
  color_transition_timer_.Stop();
  transition_start_color_.reset();
  transition_target_color_.reset();
  transition_start_time_ = base::TimeTicks();
}

void BrowserSurfaceColorController::SetPresentedColor(SkColor color) {
  color = SkColorSetA(color, SK_AlphaOPAQUE);
  if (!presented_color_.has_value() || *presented_color_ != color) {
    presented_color_ = color;
    color_changed_.Run();
  }
}

void BrowserSurfaceColorController::CommitFallbackIfReady() {
  if (waiting_for_load_completion_ || is_scroll_sampling_) {
    return;
  }
  if (const std::optional<SkColor> fallback = GetFallbackColor()) {
    CommitColor(*fallback);
  }
}

std::optional<SkColor> BrowserSurfaceColorController::GetFallbackColor() const {
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
