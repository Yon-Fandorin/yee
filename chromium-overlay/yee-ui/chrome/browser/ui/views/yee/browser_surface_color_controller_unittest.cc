// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee {

class BrowserSurfaceColorControllerTest
    : public ChromeRenderViewHostTestHarness {
public:
  BrowserSurfaceColorControllerTest()
      : ChromeRenderViewHostTestHarness(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

protected:
  void CommitForTesting(BrowserSurfaceColorController &controller,
                        SkColor color) {
    controller.CommitColor(color);
  }

  void StopSamplingForTesting(BrowserSurfaceColorController &controller) {
    controller.sample_timer_.Stop();
    controller.scroll_sample_timer_.Stop();
    controller.scroll_sampling_timeout_timer_.Stop();
  }

  base::TimeDelta
  CurrentSampleDelay(BrowserSurfaceColorController &controller) {
    return controller.sample_timer_.GetCurrentDelay();
  }

  void
  NotifyFirstVisuallyNonEmptyPaint(BrowserSurfaceColorController &controller) {
    controller.DidFirstVisuallyNonEmptyPaint();
  }

  void NotifyLoadingStopped(BrowserSurfaceColorController &controller) {
    controller.DidStopLoading();
  }

  bool ScheduleNextSettlingSample(BrowserSurfaceColorController &controller) {
    return controller.ScheduleNextPageSettlingSample();
  }
};

TEST_F(BrowserSurfaceColorControllerTest,
       UncachedTabTransitionsToThemeFallbackAndCachedTabRestoresItsColor) {
  auto uncached_contents = CreateTestWebContents();
  const SkColor theme_color = SkColorSetRGB(0xF4, 0xF2, 0xED);
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  controller.SetThemeFallbackColor(theme_color);

  controller.SetWebContents(web_contents());
  StopSamplingForTesting(controller);
  ASSERT_EQ(theme_color, controller.GetColor());
  const SkColor dark_page_color = SkColorSetRGB(0x12, 0x14, 0x18);
  CommitForTesting(controller, dark_page_color);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  ASSERT_EQ(dark_page_color, controller.GetColor());

  controller.SetWebContents(uncached_contents.get());
  StopSamplingForTesting(controller);
  EXPECT_EQ(dark_page_color, controller.GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(60));
  ASSERT_TRUE(controller.GetColor().has_value());
  EXPECT_NE(dark_page_color, *controller.GetColor());
  EXPECT_NE(theme_color, *controller.GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(100));
  EXPECT_EQ(theme_color, controller.GetColor());

  const SkColor light_page_color = SkColorSetRGB(0xF2, 0xF0, 0xEA);
  CommitForTesting(controller, light_page_color);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  ASSERT_EQ(light_page_color, controller.GetColor());

  controller.SetWebContents(web_contents());
  StopSamplingForTesting(controller);
  EXPECT_EQ(light_page_color, controller.GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(160));
  EXPECT_EQ(dark_page_color, controller.GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       RetargetingStartsFromTheCurrentlyPresentedColor) {
  auto next_contents = CreateTestWebContents();
  const SkColor theme_color = SkColorSetRGB(0xF4, 0xF2, 0xED);
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  controller.SetThemeFallbackColor(theme_color);
  controller.SetWebContents(web_contents());
  StopSamplingForTesting(controller);

  const SkColor dark_page_color = SkColorSetRGB(0x12, 0x14, 0x18);
  CommitForTesting(controller, dark_page_color);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  controller.SetWebContents(next_contents.get());
  StopSamplingForTesting(controller);
  task_environment()->FastForwardBy(base::Milliseconds(48));
  const std::optional<SkColor> color_before_retarget = controller.GetColor();

  const SkColor resolved_page_color = SkColorSetRGB(0xE2, 0xEB, 0xF7);
  CommitForTesting(controller, resolved_page_color);
  EXPECT_EQ(color_before_retarget, controller.GetColor());
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(resolved_page_color, controller.GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       FirstVisuallyNonEmptyPaintStartsBoundedSettlingSamples) {
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  controller.SetThemeFallbackColor(SK_ColorWHITE);
  controller.SetWebContents(web_contents());
  EXPECT_EQ(base::Milliseconds(32), CurrentSampleDelay(controller));

  NotifyFirstVisuallyNonEmptyPaint(controller);
  EXPECT_EQ(base::Milliseconds(16), CurrentSampleDelay(controller));

  EXPECT_TRUE(ScheduleNextSettlingSample(controller));
  EXPECT_EQ(base::Milliseconds(80), CurrentSampleDelay(controller));
  EXPECT_TRUE(ScheduleNextSettlingSample(controller));
  EXPECT_EQ(base::Milliseconds(100), CurrentSampleDelay(controller));
  EXPECT_FALSE(ScheduleNextSettlingSample(controller));
  StopSamplingForTesting(controller);
}

TEST_F(BrowserSurfaceColorControllerTest,
       LoadCompletionAlsoStartsBoundedSettlingSamples) {
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  controller.SetThemeFallbackColor(SK_ColorWHITE);
  controller.SetWebContents(web_contents());

  NotifyLoadingStopped(controller);
  EXPECT_EQ(base::Milliseconds(16), CurrentSampleDelay(controller));
  EXPECT_TRUE(ScheduleNextSettlingSample(controller));
  EXPECT_EQ(base::Milliseconds(80), CurrentSampleDelay(controller));
  EXPECT_TRUE(ScheduleNextSettlingSample(controller));
  EXPECT_EQ(base::Milliseconds(100), CurrentSampleDelay(controller));
  EXPECT_FALSE(ScheduleNextSettlingSample(controller));
  StopSamplingForTesting(controller);
}

TEST_F(BrowserSurfaceColorControllerTest,
       FallbackCanBeInjectedAfterWebContentsAttachment) {
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  controller.SetWebContents(web_contents());
  StopSamplingForTesting(controller);
  EXPECT_FALSE(controller.GetColor().has_value());

  const SkColor theme_color = SkColorSetRGB(0xF4, 0xF2, 0xED);
  controller.SetThemeFallbackColor(theme_color);
  EXPECT_EQ(theme_color, controller.GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       ThemeChangesOnlyRetargetUnresolvedTabs) {
  BrowserSurfaceColorController controller(base::BindRepeating([] {}));
  const SkColor initial_theme_color = SkColorSetRGB(0xF4, 0xF2, 0xED);
  controller.SetThemeFallbackColor(initial_theme_color);
  controller.SetWebContents(web_contents());
  StopSamplingForTesting(controller);
  ASSERT_EQ(initial_theme_color, controller.GetColor());

  const SkColor updated_theme_color = SkColorSetRGB(0x20, 0x22, 0x26);
  controller.SetThemeFallbackColor(updated_theme_color);
  task_environment()->FastForwardBy(base::Milliseconds(160));
  ASSERT_EQ(updated_theme_color, controller.GetColor());

  const SkColor page_color = SkColorSetRGB(0xD8, 0xE5, 0xF4);
  CommitForTesting(controller, page_color);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  ASSERT_EQ(page_color, controller.GetColor());

  controller.SetThemeFallbackColor(SK_ColorBLACK);
  task_environment()->FastForwardBy(base::Milliseconds(200));
  EXPECT_EQ(page_color, controller.GetColor());
}

TEST_F(BrowserSurfaceColorControllerTest,
       SplitPaneControllersKeepIndependentSurfaceColors) {
  auto second_contents = CreateTestWebContents();
  BrowserSurfaceColorController first_controller(base::BindRepeating([] {}));
  BrowserSurfaceColorController second_controller(base::BindRepeating([] {}));
  const SkColor theme_color = SkColorSetRGB(0xF4, 0xF2, 0xED);
  first_controller.SetThemeFallbackColor(theme_color);
  second_controller.SetThemeFallbackColor(theme_color);
  first_controller.SetWebContents(web_contents());
  second_controller.SetWebContents(second_contents.get());
  StopSamplingForTesting(first_controller);
  StopSamplingForTesting(second_controller);

  const SkColor first_page_color = SkColorSetRGB(0x12, 0x14, 0x18);
  const SkColor second_page_color = SkColorSetRGB(0xE2, 0xEB, 0xF7);
  CommitForTesting(first_controller, first_page_color);
  CommitForTesting(second_controller, second_page_color);
  task_environment()->FastForwardBy(base::Milliseconds(200));

  EXPECT_EQ(first_page_color, first_controller.GetColor());
  EXPECT_EQ(second_page_color, second_controller.GetColor());
}

} // namespace yee
