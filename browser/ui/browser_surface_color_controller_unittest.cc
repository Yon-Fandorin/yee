// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"

#include <cstdint>
#include <memory>
#include <optional>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "ui/gfx/animation/animation_test_api.h"

namespace yee {

class BrowserSurfaceColorControllerTest
    : public ChromeRenderViewHostTestHarness {
 public:
  BrowserSurfaceColorControllerTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

 protected:
  gfx::AnimationTestApi::RenderModeResetter rich_animation_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  gfx::AnimationTestApi::PrefersReducedMotionResetter reduced_motion_ =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);

  BrowserSurfaceColorController* SourceFor(content::WebContents* contents) {
    return BrowserSurfaceColorController::GetOrCreateForWebContents(contents);
  }

  void CommitForTesting(BrowserSurfaceColorController& controller,
                        SkColor color) {
    controller.CommitColor(color);
  }

  void StopSamplingForTesting(BrowserSurfaceColorController& controller) {
    controller.sample_timer_.Stop();
    controller.late_page_sample_timer_.Stop();
    controller.scroll_sample_timer_.Stop();
    controller.scroll_sampling_timeout_timer_.Stop();
  }

  base::TimeDelta CurrentSampleDelay(
      BrowserSurfaceColorController& controller) {
    return controller.sample_timer_.GetCurrentDelay();
  }

  bool IsPageSampleScheduled(BrowserSurfaceColorController& controller) {
    return controller.sample_timer_.IsRunning();
  }

  bool ScheduleNextSettlingSample(BrowserSurfaceColorController& controller) {
    return controller.ScheduleNextPageSettlingSample();
  }

  void StartScrollSamplingForTesting(
      BrowserSurfaceColorController& controller) {
    controller.StartScrollSampling();
  }

  bool IsScrollSamplingForTesting(BrowserSurfaceColorController& controller) {
    return controller.is_scroll_sampling_;
  }

  bool IsScrollTimerRunningForTesting(
      BrowserSurfaceColorController& controller) {
    return controller.scroll_sample_timer_.IsRunning();
  }

  bool IsScrollTimeoutRunningForTesting(
      BrowserSurfaceColorController& controller) {
    return controller.scroll_sampling_timeout_timer_.IsRunning();
  }

  base::TimeDelta CurrentScrollTimeoutDelayForTesting(
      BrowserSurfaceColorController& controller) {
    return controller.scroll_sampling_timeout_timer_.GetCurrentDelay();
  }

  void DidFirstVisuallyNonEmptyPaintForTesting(
      BrowserSurfaceColorController& controller) {
    controller.DidFirstVisuallyNonEmptyPaint();
  }

  void DidStopLoadingForTesting(BrowserSurfaceColorController& controller) {
    controller.DidStopLoading();
  }

  void ResolveFallbackForTesting(BrowserSurfaceColorController& controller) {
    controller.CommitPageMetadataColorIfReady();
  }

  bool IsColorTransitionRunning(BrowserSurfaceColorController& controller) {
    return controller.color_transition_timer_.IsRunning();
  }

  uint64_t SamplingEpochForTesting(BrowserSurfaceColorController& controller) {
    return controller.sampling_epoch_;
  }

  void RestartPageSamplingForTesting(BrowserSurfaceColorController& controller,
                                     base::TimeDelta delay) {
    controller.RestartPageSampling(delay);
  }

  void SetCandidateColorForTesting(BrowserSurfaceColorController& controller,
                                   SkColor color) {
    controller.candidate_color_ = color;
  }

  void SetStableCandidateCountForTesting(
      BrowserSurfaceColorController& controller,
      int stable_candidate_count) {
    controller.stable_candidate_count_ = stable_candidate_count;
  }

  int StableCandidateCountForTesting(
      BrowserSurfaceColorController& controller) {
    return controller.stable_candidate_count_;
  }

  void SetPageReadyForCommitForTesting(
      BrowserSurfaceColorController& controller) {
    controller.waiting_for_load_completion_ = false;
    controller.first_visually_non_empty_paint_seen_ = true;
  }

  void SetPageLoadingForTesting(BrowserSurfaceColorController& controller) {
    controller.waiting_for_load_completion_ = true;
  }

  std::optional<SkColor> CandidateColorForTesting(
      BrowserSurfaceColorController& controller) {
    return controller.candidate_color_;
  }

  void SetCaptureInFlightForTesting(BrowserSurfaceColorController& controller,
                                    bool capture_in_flight) {
    controller.capture_in_flight_ = capture_in_flight;
  }

  bool CaptureInFlightForTesting(BrowserSurfaceColorController& controller) {
    return controller.capture_in_flight_;
  }

  void DeliverCaptureForTesting(BrowserSurfaceColorController& controller,
                                uint64_t sampling_epoch,
                                const content::CopyFromSurfaceResult& result) {
    controller.OnTopStripCaptured(sampling_epoch, result);
  }

  bool HasLateVerification(BrowserSurfaceColorController& controller) {
    return controller.late_page_sample_timer_.IsRunning();
  }

  void ClickForTesting(BrowserSurfaceColorController& controller) {
    controller.DidGetUserInteraction(blink::WebMouseEvent(
        blink::WebInputEvent::Type::kMouseDown, 0, base::TimeTicks::Now()));
  }

  void ResizeForTesting(BrowserSurfaceColorController& controller) {
    controller.PrimaryMainFrameWasResized(true);
  }
};

TEST(BrowserSurfaceColorSampleTest, AdjacentColorsCrossQuantizationBoundaries) {
  for (int gray = 1; gray < 256; ++gray) {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(96, 8);
    bitmap.eraseColor(SkColorSetRGB(gray, gray, gray));
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 48; ++x) {
        *bitmap.getAddr32(x, y) = SkColorSetRGB(gray - 1, gray - 1, gray - 1);
      }
    }
    const auto result = ResolveBrowserSurfaceColorSample(bitmap);
    ASSERT_TRUE(result) << gray;
    EXPECT_GE(static_cast<int>(SkColorGetR(*result)), gray - 1);
    EXPECT_LE(static_cast<int>(SkColorGetR(*result)), gray);
  }
}

TEST(BrowserSurfaceColorSampleTest, BoundaryOwnsColorNotLowerMajority) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SK_ColorRED);
  bitmap.erase(SK_ColorBLUE, SkIRect::MakeWH(96, 2));
  EXPECT_EQ(SK_ColorBLUE, ResolveBrowserSurfaceColorSample(bitmap));
}

TEST(BrowserSurfaceColorSampleTest, ConvertsDisplayP3ReadbackToHeaderSrgb) {
  const auto p3 = SkColorSpace::MakeRGB(SkNamedTransferFn::kSRGB,
                                       SkNamedGamut::kDisplayP3);
  for (SkColor color : {SkColorSetRGB(80, 100, 120),
                       SkColorSetRGB(200, 70, 40),
                       SkColorSetRGB(40, 170, 90)}) {
    SkBitmap original;
    original.allocPixels(SkImageInfo::MakeN32Premul(
        96, 8, SkColorSpace::MakeSRGB()));
    original.eraseColor(color);
    SkBitmap readback;
    readback.allocPixels(SkImageInfo::MakeN32Premul(96, 8, p3));
    ASSERT_TRUE(original.readPixels(readback.pixmap()));
    // The old raw-RGB path must fail this fixture, not silently test sRGB.
    EXPECT_NE(color, readback.getColor(0, 0));
    const auto resolved = ResolveBrowserSurfaceColorSample(readback);
    ASSERT_TRUE(resolved);
    EXPECT_NEAR(SkColorGetR(color), SkColorGetR(*resolved), 1);
    EXPECT_NEAR(SkColorGetG(color), SkColorGetG(*resolved), 1);
    EXPECT_NEAR(SkColorGetB(color), SkColorGetB(*resolved), 1);
  }
}

TEST(BrowserSurfaceColorSampleTest, ConvertsLinearReadbackAndPreservesSrgb) {
  const SkColor color = SkColorSetRGB(80, 100, 120);
  SkBitmap original;
  original.allocPixels(SkImageInfo::MakeN32Premul(
      96, 8, SkColorSpace::MakeSRGB()));
  original.eraseColor(color);
  EXPECT_EQ(color, ResolveBrowserSurfaceColorSample(original));
  SkBitmap linear;
  linear.allocPixels(SkImageInfo::Make(
      96, 8, kRGBA_F16_SkColorType, kPremul_SkAlphaType,
      SkColorSpace::MakeSRGBLinear()));
  ASSERT_TRUE(original.readPixels(linear.pixmap()));
  EXPECT_NE(color, linear.getColor(0, 0));
  EXPECT_EQ(color, ResolveBrowserSurfaceColorSample(linear));
}

TEST(BrowserSurfaceColorSampleTest, SparseOpaqueEdgeDoesNotBecomeDominant) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  bitmap.erase(SK_ColorRED, SkIRect::MakeWH(1, 8));
  EXPECT_FALSE(ResolveBrowserSurfaceColorSample(bitmap));
}

TEST(BrowserSurfaceColorSampleTest, AmbiguousRowsAndBroadGradientsFallBack) {
  SkBitmap bitmap;
  EXPECT_FALSE(ResolveBrowserSurfaceColorSample(bitmap));
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SK_ColorWHITE);
  bitmap.erase(SK_ColorBLACK, SkIRect::MakeWH(96, 1));
  EXPECT_FALSE(ResolveBrowserSurfaceColorSample(bitmap));
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 96; ++x) {
      const int gray = x * 255 / 95;
      *bitmap.getAddr32(x, y) = SkColorSetRGB(gray, gray, gray);
    }
  }
  EXPECT_FALSE(ResolveBrowserSurfaceColorSample(bitmap));
}

TEST_F(BrowserSurfaceColorControllerTest,
       ClickAndResponsiveResizeRestartBoundedSampling) {
  auto* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  const auto epoch = SamplingEpochForTesting(*source);
  ClickForTesting(*source);
  EXPECT_GT(SamplingEpochForTesting(*source), epoch);
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  EXPECT_TRUE(HasLateVerification(*source));
  StopSamplingForTesting(*source);
  ResizeForTesting(*source);
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  EXPECT_TRUE(HasLateVerification(*source));
  web_contents()->WasHidden();
  EXPECT_FALSE(HasLateVerification(*source));
  EXPECT_FALSE(IsPageSampleScheduled(*source));
  ClickForTesting(*source);
  EXPECT_FALSE(HasLateVerification(*source));
  EXPECT_FALSE(IsPageSampleScheduled(*source));
}

TEST_F(BrowserSurfaceColorControllerTest, LateVerificationIsOneShotNotPolling) {
  auto* source = SourceFor(web_contents());
  DidStopLoadingForTesting(*source);
  EXPECT_TRUE(HasLateVerification(*source));
  task_environment()->FastForwardBy(base::Seconds(2));
  EXPECT_FALSE(HasLateVerification(*source));
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  task_environment()->FastForwardBy(base::Seconds(4));
  EXPECT_FALSE(HasLateVerification(*source));
  EXPECT_FALSE(IsPageSampleScheduled(*source));
}

TEST_F(BrowserSurfaceColorControllerTest,
       InjectedGeometryFixtureSurvivesResizeAndLateVerification) {
  auto* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  source->SetPageSurfaceColorForTesting(SK_ColorBLUE);
  ResizeForTesting(*source);
  task_environment()->FastForwardBy(base::Seconds(3));
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());
  EXPECT_FALSE(IsPageSampleScheduled(*source));
  EXPECT_FALSE(HasLateVerification(*source));
}

TEST_F(BrowserSurfaceColorControllerTest,
       LateVerificationCommitsChangedBoundaryWithoutMetadata) {
  auto* source = SourceFor(web_contents());
  source->SetPageSurfaceColorForTesting(SK_ColorBLACK);
  DidStopLoadingForTesting(*source);
  task_environment()->FastForwardBy(base::Seconds(2));
  SetPageReadyForCommitForTesting(*source);
  const auto epoch = SamplingEpochForTesting(*source);
  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SK_ColorBLUE);
  for (int i = 0; i < 2; ++i) {
    DeliverCaptureForTesting(*source, epoch,
                             viz::CopyOutputBitmapWithMetadata(bitmap));
    StopSamplingForTesting(*source);
  }
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());
  EXPECT_FALSE(HasLateVerification(*source));
}

TEST_F(BrowserSurfaceColorControllerTest,
       WebContentsOwnsOneStablePresentationSource) {
  BrowserSurfaceColorController* first = SourceFor(web_contents());
  BrowserSurfaceColorController* same = SourceFor(web_contents());
  auto second_contents = CreateTestWebContents();
  BrowserSurfaceColorController* second = SourceFor(second_contents.get());

  EXPECT_EQ(first, same);
  EXPECT_NE(first, second);
  EXPECT_NE(first->source_id(), second->source_id());
}

TEST_F(BrowserSurfaceColorControllerTest,
       ContainerBindingPublishesOnlyItsCurrentWebContentsSource) {
  BrowserSurfacePresentationBinding binding;
  binding.SetThemeFallbackColor(SkColorSetRGB(0xEE, 0xEC, 0xE8));

  int observer_calls = 0;
  auto subscription =
      binding.AddPresentationChangedCallback(base::BindRepeating(
          [](int* calls) { ++*calls; }, base::Unretained(&observer_calls)));
  binding.BindWebContents(web_contents());
  ASSERT_TRUE(binding.GetPresentation().has_value());
  BrowserSurfaceColorController* first_source = SourceFor(web_contents());
  const uint64_t first_source_id = first_source->source_id();
  EXPECT_EQ(first_source_id, binding.source_id());
  EXPECT_EQ(first_source_id, binding.GetPresentation()->source_id);

  auto second_contents = CreateTestWebContents();
  binding.BindWebContents(second_contents.get());
  ASSERT_TRUE(binding.GetPresentation().has_value());
  const uint64_t second_source_id = binding.source_id();
  EXPECT_NE(first_source_id, second_source_id);
  EXPECT_EQ(second_source_id, binding.GetPresentation()->source_id);
  const int calls_after_rebind = observer_calls;

  first_source->TransitionToPageSurfaceColorForTesting(SK_ColorBLACK);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  ASSERT_TRUE(binding.GetPresentation().has_value());
  EXPECT_EQ(second_source_id, binding.GetPresentation()->source_id);
  EXPECT_EQ(calls_after_rebind, observer_calls);

  second_contents.reset();
  EXPECT_EQ(nullptr, binding.web_contents());
  EXPECT_EQ(0u, binding.source_id());
  EXPECT_FALSE(binding.GetPresentation().has_value());
  EXPECT_EQ(calls_after_rebind + 1, observer_calls);
}

TEST_F(BrowserSurfaceColorControllerTest,
       ContainerBindingClearsPresentationWhenContentsDetach) {
  BrowserSurfacePresentationBinding binding;
  binding.SetThemeFallbackColor(SK_ColorWHITE);
  binding.BindWebContents(web_contents());
  ASSERT_TRUE(binding.GetPresentation().has_value());

  int observer_calls = 0;
  auto subscription =
      binding.AddPresentationChangedCallback(base::BindRepeating(
          [](int* calls) { ++*calls; }, base::Unretained(&observer_calls)));
  binding.BindWebContents(nullptr);

  EXPECT_EQ(nullptr, binding.web_contents());
  EXPECT_EQ(0u, binding.source_id());
  EXPECT_FALSE(binding.GetPresentation().has_value());
  EXPECT_EQ(1, observer_calls);
}

TEST_F(BrowserSurfaceColorControllerTest,
       TabActivationTransitionsFromTheCurrentlyPresentedColor) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  const SkColor target = SkColorSetRGB(0x12, 0x14, 0x18);
  CommitForTesting(*source, target);
  ASSERT_EQ(target, source->GetColor());

  const SkColor previous_tab = SkColorSetRGB(0xF2, 0xF0, 0xEA);
  source->ActivateFrom(previous_tab);
  EXPECT_EQ(previous_tab, source->GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(160));
  EXPECT_EQ(target, source->GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       RetargetingStartsFromTheCurrentlyPresentedColor) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SkColorSetRGB(0xF4, 0xF2, 0xED));
  StopSamplingForTesting(*source);

  const SkColor first_target = SkColorSetRGB(0x12, 0x14, 0x18);
  CommitForTesting(*source, first_target);
  task_environment()->FastForwardBy(base::Milliseconds(48));
  const std::optional<SkColor> color_before_retarget = source->GetColor();

  const SkColor final_target = SkColorSetRGB(0xE2, 0xEB, 0xF7);
  CommitForTesting(*source, final_target);
  EXPECT_EQ(color_before_retarget, source->GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(final_target, source->GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       FirstPaintAndLoadCompletionStartBoundedSettlingSamples) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  EXPECT_EQ(base::Milliseconds(32), CurrentSampleDelay(*source));

  DidFirstVisuallyNonEmptyPaintForTesting(*source);
  EXPECT_EQ(base::Milliseconds(16), CurrentSampleDelay(*source));
  EXPECT_TRUE(ScheduleNextSettlingSample(*source));
  EXPECT_EQ(base::Milliseconds(80), CurrentSampleDelay(*source));
  EXPECT_TRUE(ScheduleNextSettlingSample(*source));
  EXPECT_EQ(base::Milliseconds(100), CurrentSampleDelay(*source));
  EXPECT_FALSE(ScheduleNextSettlingSample(*source));

  DidStopLoadingForTesting(*source);
  EXPECT_EQ(base::Milliseconds(16), CurrentSampleDelay(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       HiddenTabSamplesWhenItBecomesVisibleAsASplitPane) {
  web_contents()->WasHidden();
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  EXPECT_FALSE(IsPageSampleScheduled(*source));

  web_contents()->WasShown();
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  EXPECT_EQ(base::Milliseconds(16), CurrentSampleDelay(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       ScrollSamplingTimeoutReturnsToPageSamplingMode) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  const uint64_t page_epoch = SamplingEpochForTesting(*source);

  StartScrollSamplingForTesting(*source);
  EXPECT_EQ(page_epoch + 1, SamplingEpochForTesting(*source));
  EXPECT_TRUE(IsScrollSamplingForTesting(*source));
  EXPECT_TRUE(IsScrollTimerRunningForTesting(*source));
  EXPECT_TRUE(IsScrollTimeoutRunningForTesting(*source));

  const base::TimeDelta timeout = CurrentScrollTimeoutDelayForTesting(*source);
  task_environment()->FastForwardBy(timeout);
  EXPECT_EQ(page_epoch + 2, SamplingEpochForTesting(*source));
  EXPECT_FALSE(IsScrollSamplingForTesting(*source));
  EXPECT_FALSE(IsScrollTimerRunningForTesting(*source));
  EXPECT_FALSE(IsScrollTimeoutRunningForTesting(*source));

  // The final post-scroll capture can schedule ordinary page verification,
  // but it must never restart the scroll mode or either scroll timer.
  task_environment()->FastForwardBy(base::Milliseconds(600));
  EXPECT_FALSE(IsScrollSamplingForTesting(*source));
  EXPECT_FALSE(IsScrollTimerRunningForTesting(*source));
  EXPECT_FALSE(IsScrollTimeoutRunningForTesting(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       RepeatedScrollSignalsPreserveTheActiveSamplingEpoch) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);

  StartScrollSamplingForTesting(*source);
  const uint64_t scroll_epoch = SamplingEpochForTesting(*source);
  const base::TimeDelta timeout =
      CurrentScrollTimeoutDelayForTesting(*source);
  const SkColor candidate = SkColorSetRGB(0x24, 0x68, 0x92);
  SetCandidateColorForTesting(*source, candidate);
  SetStableCandidateCountForTesting(*source, 1);
  SetCaptureInFlightForTesting(*source, true);

  task_environment()->FastForwardBy(timeout / 2);
  StartScrollSamplingForTesting(*source);
  task_environment()->FastForwardBy(timeout / 2);

  EXPECT_EQ(scroll_epoch, SamplingEpochForTesting(*source));
  EXPECT_EQ(candidate, CandidateColorForTesting(*source));
  EXPECT_EQ(1, StableCandidateCountForTesting(*source));
  EXPECT_TRUE(CaptureInFlightForTesting(*source));
  EXPECT_TRUE(IsScrollTimerRunningForTesting(*source));
  EXPECT_TRUE(IsScrollTimeoutRunningForTesting(*source));

  task_environment()->FastForwardBy(timeout / 2);
  EXPECT_FALSE(IsScrollSamplingForTesting(*source));
  EXPECT_FALSE(IsScrollTimerRunningForTesting(*source));
  EXPECT_FALSE(IsScrollTimeoutRunningForTesting(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       ThemeChangesRevalidatePagesWithoutDiscardingTheirColor) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  const SkColor initial_theme = SkColorSetRGB(0xF4, 0xF2, 0xED);
  source->SetThemeFallbackColor(initial_theme);
  StopSamplingForTesting(*source);
  ASSERT_EQ(initial_theme, source->GetColor());

  const SkColor updated_theme = SkColorSetRGB(0x20, 0x22, 0x26);
  source->SetThemeFallbackColor(updated_theme);
  // This harness has no compositor pixels. Test theme transitions separately
  // from the exhausted-capture fallback covered below.
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  StopSamplingForTesting(*source);
  task_environment()->FastForwardBy(base::Milliseconds(160));
  ASSERT_EQ(updated_theme, source->GetColor());

  const SkColor page = SkColorSetRGB(0xD8, 0xE5, 0xF4);
  CommitForTesting(*source, page);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  source->SetThemeFallbackColor(SK_ColorBLACK);
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  EXPECT_EQ(page, source->GetColor());
  StopSamplingForTesting(*source);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(page, source->GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       ReducedMotionPublishesOnlyTheFinalTabAndPageColors) {
  auto reduced_motion =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetPageSurfaceColorForTesting(SK_ColorBLACK);
  const uint64_t revision = source->GetPresentation()->revision;

  source->ActivateFrom(SK_ColorWHITE);
  EXPECT_EQ(SK_ColorBLACK, source->GetColor());
  EXPECT_EQ(revision + 1, source->GetPresentation()->revision);
  EXPECT_FALSE(IsColorTransitionRunning(*source));

  CommitForTesting(*source, SK_ColorBLUE);
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());
  EXPECT_FALSE(IsColorTransitionRunning(*source));
}

TEST_F(BrowserSurfaceColorControllerTest,
       EnablingReducedMotionFinishesAnInFlightTransition) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetPageSurfaceColorForTesting(SK_ColorWHITE);
  CommitForTesting(*source, SK_ColorBLACK);
  task_environment()->FastForwardBy(base::Milliseconds(32));
  ASSERT_TRUE(IsColorTransitionRunning(*source));
  const uint64_t popup_revision = source->GetPresentation()->popup_revision;

  auto reduced_motion =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(true);
  task_environment()->FastForwardBy(base::Milliseconds(16));
  EXPECT_EQ(SK_ColorBLACK, source->GetColor());
  EXPECT_FALSE(IsColorTransitionRunning(*source));
  EXPECT_EQ(popup_revision + 1, source->GetPresentation()->popup_revision);
}

TEST_F(BrowserSurfaceColorControllerTest,
       HidingTabCancelsScrollAndRejectsPendingCapture) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetPageSurfaceColorForTesting(SK_ColorBLACK);
  StartScrollSamplingForTesting(*source);
  const uint64_t epoch = SamplingEpochForTesting(*source);
  SetCaptureInFlightForTesting(*source, true);
  CommitForTesting(*source, SK_ColorBLUE);

  web_contents()->WasHidden();
  EXPECT_GT(SamplingEpochForTesting(*source), epoch);
  EXPECT_FALSE(IsPageSampleScheduled(*source));
  EXPECT_FALSE(IsScrollTimerRunningForTesting(*source));
  EXPECT_FALSE(IsScrollTimeoutRunningForTesting(*source));
  EXPECT_FALSE(CaptureInFlightForTesting(*source));
  EXPECT_FALSE(IsColorTransitionRunning(*source));
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());

  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SK_ColorRED);
  DeliverCaptureForTesting(*source, epoch,
                           viz::CopyOutputBitmapWithMetadata(bitmap));
  StartScrollSamplingForTesting(*source);
  task_environment()->FastForwardBy(base::Seconds(2));
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());
  EXPECT_FALSE(IsPageSampleScheduled(*source));
  EXPECT_FALSE(IsScrollTimerRunningForTesting(*source));

  web_contents()->WasShown();
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       MissingPageColorFallsBackWithoutCachingTheOldDocumentOrTheme) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  source->SetPageSurfaceColorForTesting(SK_ColorBLUE);
  SetPageReadyForCommitForTesting(*source);
  ResolveFallbackForTesting(*source);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(SK_ColorWHITE, source->GetColor());

  source->SetThemeFallbackColor(SK_ColorBLACK);
  StopSamplingForTesting(*source);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(SK_ColorBLACK, source->GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       MissingColorDuringNavigationPreservesPreviousPresentation) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  source->SetPageSurfaceColorForTesting(SK_ColorBLACK);
  SetPageLoadingForTesting(*source);
  ResolveFallbackForTesting(*source);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(SK_ColorBLACK, source->GetColor());
  EXPECT_FALSE(IsColorTransitionRunning(*source));
}

TEST_F(BrowserSurfaceColorControllerTest,
       ThemeChangeOnHiddenTabWaitsForVisibilityBeforeSampling) {
  web_contents()->WasHidden();
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  source->SetPageSurfaceColorForTesting(SK_ColorBLUE);
  source->SetThemeFallbackColor(SK_ColorBLACK);
  EXPECT_FALSE(IsPageSampleScheduled(*source));
  EXPECT_EQ(SK_ColorBLUE, source->GetColor());

  web_contents()->WasShown();
  EXPECT_TRUE(IsPageSampleScheduled(*source));
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       SplitPaneSourcesKeepIndependentSurfaceColors) {
  auto second_contents = CreateTestWebContents();
  BrowserSurfaceColorController* first = SourceFor(web_contents());
  BrowserSurfaceColorController* second = SourceFor(second_contents.get());
  StopSamplingForTesting(*first);
  StopSamplingForTesting(*second);

  const SkColor first_color = SkColorSetRGB(0x12, 0x14, 0x18);
  const SkColor second_color = SkColorSetRGB(0xE2, 0xEB, 0xF7);
  CommitForTesting(*first, first_color);
  CommitForTesting(*second, second_color);

  EXPECT_EQ(first_color, first->GetColor());
  EXPECT_EQ(second_color, second->GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       IgnoresLateCaptureFromPreviousSamplingEpochOnSameWebContents) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  CommitForTesting(*source, SkColorSetRGB(0x10, 0x20, 0x30));
  ASSERT_TRUE(source->GetPresentation().has_value());
  const uint64_t old_epoch = SamplingEpochForTesting(*source);
  RestartPageSamplingForTesting(*source, base::Milliseconds(1));
  StopSamplingForTesting(*source);

  const SkColor current_candidate = SkColorSetRGB(4, 5, 6);
  SetCandidateColorForTesting(*source, current_candidate);
  SetStableCandidateCountForTesting(*source, 1);
  SetPageReadyForCommitForTesting(*source);
  SetCaptureInFlightForTesting(*source, true);
  const BrowserSurfacePresentation presentation_before =
      *source->GetPresentation();
  int observer_calls = 0;
  auto subscription =
      source->AddPresentationChangedCallback(base::BindRepeating(
          [](int* calls) { ++*calls; }, base::Unretained(&observer_calls)));

  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(SkColorSetRGB(0xD0, 0xE0, 0xF0));
  const content::CopyFromSurfaceResult successful_stale_capture =
      viz::CopyOutputBitmapWithMetadata(bitmap);
  DeliverCaptureForTesting(*source, old_epoch, successful_stale_capture);

  EXPECT_EQ(current_candidate, CandidateColorForTesting(*source));
  EXPECT_EQ(1, StableCandidateCountForTesting(*source));
  EXPECT_TRUE(CaptureInFlightForTesting(*source));
  ASSERT_TRUE(source->GetPresentation().has_value());
  EXPECT_EQ(presentation_before.revision, source->GetPresentation()->revision);
  EXPECT_EQ(presentation_before.popup_revision,
            source->GetPresentation()->popup_revision);
  EXPECT_EQ(0, observer_calls);
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       StableCurrentCapturePublishesPagePresentation) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  SetPageReadyForCommitForTesting(*source);
  const uint64_t epoch = SamplingEpochForTesting(*source);
  const SkColor sampled_surface = SkColorSetRGB(0x2A, 0x64, 0x91);

  SkBitmap bitmap;
  bitmap.allocN32Pixels(96, 8);
  bitmap.eraseColor(sampled_surface);
  const content::CopyFromSurfaceResult capture =
      viz::CopyOutputBitmapWithMetadata(bitmap);

  SetCaptureInFlightForTesting(*source, true);
  DeliverCaptureForTesting(*source, epoch, capture);
  ASSERT_FALSE(source->GetPresentation().has_value());
  EXPECT_EQ(1, StableCandidateCountForTesting(*source));
  StopSamplingForTesting(*source);

  SetCaptureInFlightForTesting(*source, true);
  DeliverCaptureForTesting(*source, epoch, capture);

  ASSERT_TRUE(source->GetPresentation().has_value());
  EXPECT_EQ(sampled_surface, source->GetPresentation()->surface);
  EXPECT_EQ(1u, source->GetPresentation()->popup_revision);
  StopSamplingForTesting(*source);
}

TEST_F(BrowserSurfaceColorControllerTest,
       CommitStartingTransitionDoesNotRefreshPopupUntilCompletion) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  StopSamplingForTesting(*source);
  const uint64_t initial_popup_revision =
      source->GetPresentation()->popup_revision;

  CommitForTesting(*source, SK_ColorBLACK);
  EXPECT_EQ(initial_popup_revision, source->GetPresentation()->popup_revision);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(initial_popup_revision + 1,
            source->GetPresentation()->popup_revision);
}

TEST_F(BrowserSurfaceColorControllerTest,
       RetargetedTransitionRefreshesPopupOnceForLatestTarget) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  source->SetThemeFallbackColor(SK_ColorWHITE);
  StopSamplingForTesting(*source);
  const uint64_t initial_popup_revision =
      source->GetPresentation()->popup_revision;

  CommitForTesting(*source, SK_ColorBLACK);
  task_environment()->FastForwardBy(base::Milliseconds(48));
  CommitForTesting(*source, SkColorSetRGB(0x20, 0x40, 0x80));
  EXPECT_EQ(initial_popup_revision, source->GetPresentation()->popup_revision);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(initial_popup_revision + 1,
            source->GetPresentation()->popup_revision);
}

TEST_F(BrowserSurfaceColorControllerTest,
       ImmediateCommitRefreshesPopupExactlyOnce) {
  BrowserSurfaceColorController* source = SourceFor(web_contents());
  StopSamplingForTesting(*source);
  ASSERT_FALSE(source->GetPresentation().has_value());

  CommitForTesting(*source, SkColorSetRGB(0x20, 0x40, 0x80));
  ASSERT_TRUE(source->GetPresentation().has_value());
  EXPECT_EQ(1u, source->GetPresentation()->popup_revision);
}

}  // namespace yee
