// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/disclosure_arrow.h"

#include "base/functional/bind.h"
#include "base/i18n/test/scoped_rtl_for_testing.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_scale_factor.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"

namespace yee {

class DisclosureArrowTest : public testing::Test {
 protected:
  void Step(DisclosureArrow& arrow, base::TimeDelta elapsed) {
    gfx::AnimationTestApi animation(&arrow.animation_);
    const auto start = base::TimeTicks::Now();
    animation.SetStartTime(start);
    animation.Step(start + elapsed);
  }
  base::test::SingleThreadTaskEnvironment task_environment_;
  ui::test::ScopedSetSupportedResourceScaleFactors supported_scale_factors_{
      {ui::k100Percent, ui::k200Percent}};
};

// 고정된 슬롯 안에서 중간 각도를 그리고 빠른 반대 입력도 현재 각도에서 잇는다.
TEST_F(DisclosureArrowTest, RotatesAndReversesInBothDirections) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  for (bool rtl : {false, true}) {
    base::i18n::ScopedRTLForTesting direction(rtl);
    int repaints = 0;
    DisclosureArrow arrow(
        base::BindRepeating([](int* count) { ++*count; }, &repaints));
    const auto raster = [&] {
      return arrow.GetImage(SK_ColorWHITE).GetRepresentation(1).GetBitmap();
    };
    arrow.SetExpanded(true, false);
    const auto expanded = raster();
    arrow.SetExpanded(false, false);
    const auto collapsed = raster();
    EXPECT_FALSE(gfx::test::AreBitmapsEqual(expanded, collapsed));
    arrow.SetExpanded(true, true);
    EXPECT_TRUE(gfx::test::AreBitmapsEqual(collapsed, raster()));
    Step(arrow, base::Milliseconds(80));
    const auto middle = raster();
    EXPECT_FALSE(gfx::test::AreBitmapsEqual(collapsed, middle));
    EXPECT_FALSE(gfx::test::AreBitmapsEqual(expanded, middle));
    EXPECT_EQ(collapsed.dimensions(), middle.dimensions());
    arrow.SetExpanded(false, true);
    EXPECT_TRUE(gfx::test::AreBitmapsEqual(middle, raster()));
    Step(arrow, base::Milliseconds(160));
    EXPECT_TRUE(gfx::test::AreBitmapsEqual(collapsed, raster()));
    EXPECT_GT(repaints, 0);
  }
}

// 동작 줄이기에서는 타이머를 시작하지 않고 최종 방향을 바로 표시한다.
TEST_F(DisclosureArrowTest, ReducedMotionUsesFinalDirectionImmediately) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  DisclosureArrow arrow(base::BindRepeating([] {}));
  arrow.SetExpanded(true, false);
  const auto expanded = arrow.GetImage(SK_ColorWHITE);
  arrow.SetExpanded(false, false);
  arrow.SetExpanded(true, true);
  EXPECT_TRUE(gfx::test::AreImagesEqual(
      gfx::Image(expanded), gfx::Image(arrow.GetImage(SK_ColorWHITE))));
}

}  // namespace yee
