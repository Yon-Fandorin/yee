// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_native_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/tween.h"

namespace browser_view_layout {
namespace {

NativeHorizontalPlanInput BaseHorizontalInput(int width) {
  NativeHorizontalPlanInput input;
  input.browser_params.visual_client_area = gfx::Rect(0, 0, width, 600);
  input.content_column_bounds = input.browser_params.visual_client_area;
  input.tab_strip_type = NativeTabStripType::kHorizontal;
  input.toolbar_minimum_width = 400;
  input.side_panel_visible = true;
  input.side_panel_minimum_width = 300;
  input.side_panel_preferred_width = 500;
  input.side_panel_padding = 10;
  input.minimum_content_width = 200;
  input.separator_thickness = 1;
  return input;
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     HorizontalThresholdUsesStrictLessThanAndAuthoritativeRowMode) {
  NativeHorizontalPlanInput input = BaseHorizontalInput(710);
  NativeHorizontalPlan plan = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(NativeSidePanelRowMode::kSharedRow, plan.row_mode);
  EXPECT_FALSE(plan.force_top_container_to_top());
  EXPECT_TRUE(plan.top_container_shares_panel_row());
  EXPECT_EQ(400, plan.min_content_width);
  EXPECT_EQ(300, plan.side_panel_width);

  input.browser_params.visual_client_area.set_width(709);
  input.content_column_bounds.set_width(709);
  plan = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel, plan.row_mode);
  EXPECT_TRUE(plan.force_top_container_to_top());
  EXPECT_FALSE(plan.top_container_shares_panel_row());
  EXPECT_EQ(200, plan.min_content_width);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     PanelTypeTabModeAndVisibilityRemainIndependentInputs) {
  NativeHorizontalPlanInput input = BaseHorizontalInput(1000);
  input.side_panel_content_type = true;
  EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel,
            PlanNativeHorizontalLayout(input).row_mode);

  input.side_panel_row_policy = NativeSidePanelRowPolicy::kSharedRow;
  EXPECT_EQ(NativeSidePanelRowMode::kSharedRow,
            PlanNativeHorizontalLayout(input).row_mode);
  input.side_panel_row_policy = NativeSidePanelRowPolicy::kAutomatic;

  input.side_panel_content_type = false;
  input.tab_strip_type = NativeTabStripType::kVertical;
  EXPECT_TRUE(PlanNativeHorizontalLayout(input).force_top_container_to_top());

  input.vertical_tab_strip_inside_content_column = false;
  EXPECT_EQ(NativeSidePanelRowMode::kSharedRow,
            PlanNativeHorizontalLayout(input).row_mode);

  input.side_panel_content_type = true;
  EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel,
            PlanNativeHorizontalLayout(input).row_mode);

  input.side_panel_content_type = false;
  input.toolbar_minimum_width = 701;
  EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel,
            PlanNativeHorizontalLayout(input).row_mode);

  input.tab_strip_type = NativeTabStripType::kNone;
  input.side_panel_visible = false;
  const NativeHorizontalPlan no_panel = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(NativeSidePanelRowMode::kNoPanel, no_panel.row_mode);
  EXPECT_TRUE(no_panel.force_top_container_to_top());
  EXPECT_FALSE(no_panel.has_side_panel());
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     ExplicitPanelRowsPreserveHeaderPlacementAcrossTypeAndWidth) {
  for (int width : {600, 1000}) {
    for (bool content_type : {false, true}) {
      NativeHorizontalPlanInput input = BaseHorizontalInput(width);
      input.side_panel_content_type = content_type;
      input.side_panel_row_policy = NativeSidePanelRowPolicy::kTopAbovePanel;
      const auto above = PlanNativeHorizontalLayout(input);
      EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel, above.row_mode);
      EXPECT_TRUE(above.force_top_container_to_top());
      EXPECT_EQ(input.minimum_content_width, above.min_content_width);

      input.side_panel_row_policy = NativeSidePanelRowPolicy::kSharedRow;
      const auto beside = PlanNativeHorizontalLayout(input);
      EXPECT_EQ(NativeSidePanelRowMode::kSharedRow, beside.row_mode);
      EXPECT_FALSE(beside.force_top_container_to_top());
      EXPECT_EQ(input.toolbar_minimum_width, beside.min_content_width);
    }
  }
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     ContentPanelCapAndVerticalFirstFlexUseContentColumn) {
  NativeHorizontalPlanInput input = BaseHorizontalInput(900);
  input.content_column_bounds = gfx::Rect(244, 0, 600, 600);
  input.tab_strip_type = NativeTabStripType::kVertical;
  input.vertical_tab_strip_inside_content_column = false;
  input.vertical_tab_strip_minimum_width = 100;
  input.vertical_tab_strip_preferred_width = 200;
  input.side_panel_content_type = true;
  input.side_panel_restrict_max_width = true;
  input.side_panel_minimum_width = 100;
  input.side_panel_preferred_width = 500;
  input.toolbar_minimum_width = 100;
  const NativeHorizontalPlan outside_plan = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(200, outside_plan.vertical_tab_strip_width);
  EXPECT_EQ(390, outside_plan.side_panel_width);
  EXPECT_EQ(gfx::Rect(244, 0, 600, 600),
            outside_plan.content_column_params.visual_client_area);

  input.browser_params.visual_client_area = gfx::Rect(0, 0, 700, 600);
  input.content_column_bounds = input.browser_params.visual_client_area;
  input.vertical_tab_strip_inside_content_column = true;
  input.side_panel_content_type = false;
  input.side_panel_restrict_max_width = false;
  input.side_panel_preferred_width = 300;
  const NativeHorizontalPlan inside_plan = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(200, inside_plan.vertical_tab_strip_width);
  EXPECT_EQ(290, inside_plan.side_panel_width);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     GrabHandleRevisionPrecedesContentColumnExclusionConversion) {
  NativeHorizontalPlanInput input = BaseHorizontalInput(900);
  input.tab_strip_type = NativeTabStripType::kVertical;
  input.side_panel_visible = false;
  input.minimum_grab_handle_padding = 40;
  input.browser_params.leading_exclusion.content = gfx::SizeF(30, 20);
  input.content_column_bounds = gfx::Rect(50, 0, 850, 600);

  const NativeHorizontalPlan plan = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(40,
            plan.revised_browser_params.leading_exclusion.horizontal_padding);
  EXPECT_EQ(20,
            plan.content_column_params.leading_exclusion.horizontal_padding);
  EXPECT_TRUE(plan.content_column_params.leading_exclusion.content.IsEmpty());
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     SeparatorFactsFollowResolvedRowNotPanelType) {
  NativeHorizontalPlanInput input = BaseHorizontalInput(1000);
  input.split = true;
  NativeHorizontalPlan shared = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(NativeSidePanelRowMode::kSharedRow, shared.row_mode);
  EXPECT_EQ((NativeSeparatorState{/*top_container_separator=*/false,
                                  /*multi_contents_separator=*/true,
                                  /*shadow_box=*/true,
                                  /*adjust_for_missing_separator=*/false,
                                  /*side_panel_top_padding=*/true}),
            shared.separator);

  input.tab_strip_type = NativeTabStripType::kVertical;
  NativeHorizontalPlan forced = PlanNativeHorizontalLayout(input);
  EXPECT_EQ(NativeSidePanelRowMode::kTopAbovePanel, forced.row_mode);
  EXPECT_EQ((NativeSeparatorState{/*top_container_separator=*/false,
                                  /*multi_contents_separator=*/true,
                                  /*shadow_box=*/false,
                                  /*adjust_for_missing_separator=*/false,
                                  /*side_panel_top_padding=*/false}),
            forced.separator);

  input.infobar_visible = true;
  forced = PlanNativeHorizontalLayout(input);
  EXPECT_TRUE(forced.separator.shadow_box);
  EXPECT_TRUE(forced.separator.adjust_for_missing_separator);
  EXPECT_FALSE(forced.separator.top_container_separator);
  EXPECT_FALSE(forced.separator.multi_contents_separator);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     FinalHeaderLocalToolbarUsesNormalizedReservationExactlyOnce) {
  NativeTopContainerLayoutInput input;
  input.params.visual_client_area = gfx::Rect(0, 0, 900, 42);
  input.tab_strip_type = NativeTabStripType::kVertical;
  input.toolbar_parented = true;
  input.toolbar_visible = true;
  input.toolbar_uses_config = true;
  input.toolbar_insets = gfx::Insets::TLBR(1, 10, 0, 8);
  input.toolbar_height = 40;
  input.toolbar_reserved_height = 42;

  const NativeTopContainerLayout result =
      CalculateNativeTopContainerLayout(input);
  EXPECT_EQ(gfx::Rect(0, 0, 900, 42), result.bounds);
  EXPECT_EQ(gfx::Rect(10, 1, 882, 40), result.toolbar.bounds);
  EXPECT_TRUE(result.toolbar.visible);

  // exclusion을 처리한 뒤 caption 여백은 별도 행이 소유한다.
  input.params.leading_exclusion.content = gfx::SizeF(90, 8);
  input.params.trailing_exclusion.content = gfx::SizeF(60, 8);
  input.needs_exclusion = false;
  EXPECT_EQ(result.toolbar.bounds,
            CalculateNativeTopContainerLayout(input).toolbar.bounds);
  input.needs_exclusion = true;
  EXPECT_EQ(gfx::Rect(100, 1, 732, 40),
            CalculateNativeTopContainerLayout(input).toolbar.bounds);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     PanelRevealPreservesFloorRoundAndLocalClipConventions) {
  NativeSidePanelGeometryInput input;
  input.available_bounds = gfx::Rect(250, 48, 744, 746);
  input.unclipped_contents_region = input.available_bounds;
  input.panel_clip_bounds = input.available_bounds;
  input.target_width = 301;
  input.panel_padding = 7;
  input.leading = true;
  input.shadow_box = true;
  input.separator_thickness = 1;
  input.animation.reveal = 0.5;

  NativeSidePanelGeometry result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(150, result.visible_width);  // floor(150.5)
  EXPECT_EQ(gfx::Insets::TLBR(1, 0, 4, 4), result.shadow_insets);
  EXPECT_EQ(gfx::Rect(99, 48, 301, 746), result.current_bounds);
  EXPECT_EQ(gfx::Rect(250, 48, 301, 746), result.final_bounds);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(151, 0, 150, 746), *result.local_clip_bounds);
  EXPECT_TRUE(result.animating);

  input.animation.reveal = 0.0;
  result = ResolveNativeSidePanelGeometry(input);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(301, 0, 1, 746), *result.local_clip_bounds);

  input.animation.reveal = 0.999;
  result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(300, result.visible_width);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(1, 0, 300, 746), *result.local_clip_bounds);
  EXPECT_TRUE(result.animating);

  input.animation.reveal = 1.0;
  result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(301, result.visible_width);
  EXPECT_FALSE(result.local_clip_bounds);
  EXPECT_FALSE(result.animating);
  EXPECT_EQ(result.remaining_bounds, result.unclipped_contents_region);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     PanelPaintClipContainsEveryPhysicalEdge) {
  NativeSidePanelGeometryInput input;
  input.available_bounds = gfx::Rect(244, 0, 956, 800);
  input.unclipped_contents_region = input.available_bounds;
  input.panel_clip_bounds = gfx::Rect(250, 6, 944, 788);
  input.target_width = 300;
  input.animation.reveal = 1.0;

  input.leading = true;
  NativeSidePanelGeometry result = ResolveNativeSidePanelGeometry(input);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(6, 6, 294, 788), *result.local_clip_bounds);
  gfx::Rect visible_bounds = *result.local_clip_bounds;
  visible_bounds.Offset(result.current_bounds.OffsetFromOrigin());
  EXPECT_TRUE(input.panel_clip_bounds.Contains(visible_bounds));

  input.leading = false;
  result = ResolveNativeSidePanelGeometry(input);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(0, 6, 294, 788), *result.local_clip_bounds);
  visible_bounds = *result.local_clip_bounds;
  visible_bounds.Offset(result.current_bounds.OffsetFromOrigin());
  EXPECT_TRUE(input.panel_clip_bounds.Contains(visible_bounds));
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     TrailingPanelRevealAndContentProgressRemainIndependent) {
  NativeSidePanelGeometryInput input;
  input.available_bounds = gfx::Rect(10, 20, 600, 400);
  input.unclipped_contents_region = input.available_bounds;
  input.panel_clip_bounds = input.available_bounds;
  input.target_width = 200;
  input.leading = false;
  input.animation_content_visible = true;
  input.animation.reveal = 0.25;
  input.animation.content_starting_bounds = gfx::Rect(30, 40, 80, 100);
  input.animation.content_insets = gfx::Insets::TLBR(3, 5, 7, 11);
  input.animation.content_progress = 0.5;

  const NativeSidePanelGeometry result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(gfx::Rect(560, 20, 200, 400), result.current_bounds);
  EXPECT_EQ(gfx::Rect(410, 20, 200, 400), result.final_bounds);
  ASSERT_TRUE(result.local_clip_bounds);
  EXPECT_EQ(gfx::Rect(0, 0, 50, 400), *result.local_clip_bounds);
  ASSERT_TRUE(result.animation_content_bounds);

  EXPECT_EQ(gfx::Rect(223, 32, 132, 245), *result.animation_content_bounds);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     ContentTransitionRoundsEdgesLikeNative157) {
  NativeSidePanelGeometryInput input;
  input.available_bounds = gfx::Rect(101, 101, 101, 101);
  input.unclipped_contents_region = input.available_bounds;
  input.panel_clip_bounds = input.available_bounds;
  input.target_width = 101;
  input.leading = true;
  input.animation_content_visible = true;
  input.animation.reveal = 0.5;
  input.animation.content_progress = 0.5;
  input.animation.content_starting_bounds = gfx::Rect(0, 0, 100, 100);
  auto result = ResolveNativeSidePanelGeometry(input);
  ASSERT_TRUE(result.animation_content_bounds);
  // 홀수 edge를 각각 반올림해야 폭이 한 픽셀 흔들리지 않는다.
  EXPECT_EQ(gfx::Rect(51, 51, 100, 100), *result.animation_content_bounds);
  input.animation.content_progress = 0.0;
  result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(gfx::Rect(0, 0, 100, 100), *result.animation_content_bounds);
  input.animation.content_progress = 1.0;
  result = ResolveNativeSidePanelGeometry(input);
  EXPECT_EQ(gfx::Rect(101, 101, 101, 101), *result.animation_content_bounds);
}

TEST(YeeBrowserViewTabbedLayoutNativeGeometryTest,
     SplitInsetAndUnderlapThresholdsAreLiteralCurrentBehavior) {
  NativeHorizontalPlanInput horizontal_input = BaseHorizontalInput(1000);
  horizontal_input.split = true;
  const NativeHorizontalPlan plan =
      PlanNativeHorizontalLayout(horizontal_input);
  EXPECT_EQ(gfx::Insets::TLBR(0, 4, 4, 4),
            ResolveNativeSplitViewInsets(
                true, BrowserViewLayoutDelegate::WindowState::kNormal,
                /*infobar_visible=*/false, plan,
                /*side_panel_leading=*/true,
                /*side_panel_reveal=*/0.5, /*default_inset=*/4));

  NativeBodyAllocationInput body_input;
  body_input.notice_flow_allocation = gfx::Rect(250, 48, 200, 500);
  body_input.allowed_bounds = gfx::Rect(244, 0, 470, 600);
  body_input.panel_visible_region = gfx::Rect(244, 48, 6, 500);
  body_input.minimum_content_width = 200;
  NativeBodyAllocation body = ResolveNativeBodyAllocation(body_input);
  EXPECT_EQ(0, body.underlap_deficit);
  EXPECT_EQ(body_input.notice_flow_allocation, body.body_allocation);
  EXPECT_TRUE(body.body_occlusion_rect.IsEmpty());

  body_input.minimum_content_width = 201;
  body = ResolveNativeBodyAllocation(body_input);
  EXPECT_EQ(1, body.underlap_deficit);
  EXPECT_EQ(body_input.notice_flow_allocation, body.body_allocation);
  EXPECT_TRUE(body.body_occlusion_rect.IsEmpty());
  EXPECT_TRUE(body.has_contiguous_notice_flow);
}

}  // namespace
}  // namespace browser_view_layout
