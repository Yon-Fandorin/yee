// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/page_viewport_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

TEST(PageViewportGeometryTest, PreservesExplicitContainerAndScreenSpaces) {
  PageViewportGeometryInput input;
  input.page_environment_in_container = gfx::Rect(1, 97, 798, 502);
  input.content_and_footer_in_container = gfx::Rect(201, 97, 598, 502);
  input.footer_height = 53;
  input.container_origin_in_screen = gfx::Point(250, 6);
  input.viewport_radii = gfx::RoundedCornersF(0, 0, 11, 11);
  input.generation = 17;

  const PageViewportGeometry geometry = ResolvePageViewportGeometry(input);
  EXPECT_TRUE(gfx::Rect(1, 97, 798, 502) ==
              geometry.page_environment_in_container);
  EXPECT_TRUE(gfx::Rect(201, 97, 598, 502) ==
              geometry.content_and_footer_in_container);
  EXPECT_TRUE(gfx::Rect(201, 97, 598, 449) ==
              geometry.current_viewport_in_container);
  EXPECT_TRUE(gfx::Rect(451, 103, 598, 449) ==
              geometry.current_viewport_in_screen);
  EXPECT_TRUE(geometry.current_viewport_in_container ==
              geometry.target_stack_in_container);
  EXPECT_TRUE(geometry.current_clip_in_target.IsEmpty());
  EXPECT_TRUE(input.viewport_radii == geometry.viewport_radii);
  EXPECT_EQ(17U, geometry.generation);
}

TEST(PageViewportGeometryTest, TargetClipRetainsCurrentRtlConvention) {
  PageViewportGeometryInput input;
  input.page_environment_in_container = gfx::Rect(1, 43, 798, 556);
  input.content_and_footer_in_container = input.page_environment_in_container;
  input.target_outsets = gfx::Outsets::TLBR(2, 7, 3, 19);

  PageViewportGeometry geometry = ResolvePageViewportGeometry(input);
  EXPECT_TRUE(gfx::Rect(-6, 41, 824, 561) ==
              geometry.target_stack_in_container);
  EXPECT_TRUE(gfx::Rect(7, 2, 798, 556) == geometry.current_clip_in_target);

  input.is_rtl = true;
  geometry = ResolvePageViewportGeometry(input);
  EXPECT_TRUE(gfx::Rect(-6, 41, 824, 561) ==
              geometry.target_stack_in_container);
  EXPECT_TRUE(gfx::Rect(19, 2, 798, 556) == geometry.current_clip_in_target);
}

TEST(PageViewportGeometryTest, FooterCannotProduceNegativeViewport) {
  PageViewportGeometryInput input;
  input.page_environment_in_container = gfx::Rect(0, 0, 400, 40);
  input.content_and_footer_in_container = input.page_environment_in_container;
  input.footer_height = 53;

  const PageViewportGeometry geometry = ResolvePageViewportGeometry(input);
  EXPECT_EQ(0, geometry.current_viewport_in_container.height());
  EXPECT_GE(geometry.target_stack_in_container.height(), 0);
}

TEST(PageViewportGeometryTest, DevToolsPlacementUsesPageEnvironment) {
  const gfx::Rect environment(1, 97, 798, 502);
  EXPECT_EQ(PageDevToolsDockedPlacement::kNone,
            ResolvePageDevToolsDockedPlacement(environment, environment));
  EXPECT_EQ(PageDevToolsDockedPlacement::kLeft,
            ResolvePageDevToolsDockedPlacement(environment,
                                               gfx::Rect(301, 97, 498, 502)));
  EXPECT_EQ(PageDevToolsDockedPlacement::kRight,
            ResolvePageDevToolsDockedPlacement(environment,
                                               gfx::Rect(1, 97, 498, 502)));
  EXPECT_EQ(PageDevToolsDockedPlacement::kBottom,
            ResolvePageDevToolsDockedPlacement(environment,
                                               gfx::Rect(1, 97, 798, 302)));
  EXPECT_EQ(PageDevToolsDockedPlacement::kUnknown,
            ResolvePageDevToolsDockedPlacement(environment,
                                               gfx::Rect(20, 120, 500, 300)));
}

TEST(PageViewportGeometryTest, DirectChildClipUsesChildLocalCoordinates) {
  EXPECT_TRUE(gfx::Rect(0, 0, 400, 300) ==
              ResolveDirectChildClipInLocalSpace(gfx::Rect(40, 70, 400, 300),
                                                 gfx::Rect(40, 70, 400, 300)));
  EXPECT_TRUE(gfx::Rect(30, 20, 150, 100) ==
              ResolveDirectChildClipInLocalSpace(gfx::Rect(10, 20, 300, 200),
                                                 gfx::Rect(40, 40, 150, 100)));
}

TEST(PageViewportGeometryTest, DirectChildClipIntersectsVisibleBounds) {
  EXPECT_TRUE(gfx::Rect(50, 0, 150, 90) ==
              ResolveDirectChildClipInLocalSpace(gfx::Rect(-50, 10, 300, 100),
                                                 gfx::Rect(0, 0, 150, 100)));
  EXPECT_TRUE(ResolveDirectChildClipInLocalSpace(gfx::Rect(0, 0, 100, 100),
                                                 gfx::Rect(200, 200, 20, 20))
                  .IsEmpty());
}

}  // namespace
}  // namespace yee
