// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/multi_contents_geometry.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

TEST(MultiContentsGeometryTest, CurrentAndTargetGeometryAreTypeSeparated) {
  CurrentPaneGeometryInput current_input;
  current_input.available_space = gfx::Rect(0, 0, 900, 600);
  current_input.split = true;
  current_input.axis = PaneSplitAxis::kHorizontal;
  current_input.start_ratio = 0.4;
  current_input.divider_size = 8;
  current_input.minimums = {200, 200};
  current_input.start_body_insets = gfx::Insets::TLBR(42, 1, 1, 1);
  current_input.end_body_insets = gfx::Insets::TLBR(42, 1, 1, 1);

  const CurrentPaneGeometry current = ComputeCurrentPaneGeometry(current_input);
  EXPECT_TRUE(gfx::Rect(0, 0, 357, 600) == current.start);
  EXPECT_TRUE(gfx::Rect(357, 0, 8, 600) == current.divider);
  EXPECT_TRUE(gfx::Rect(365, 0, 535, 600) == current.end);
  EXPECT_TRUE(gfx::Rect(1, 42, 355, 557) == current.start_body);
  EXPECT_TRUE(gfx::Rect(366, 42, 533, 557) == current.end_body);

  TargetPaneGeometryInput target_input;
  target_input.available_space = gfx::Rect(0, 0, 1100, 600);
  target_input.split = true;
  target_input.axis = PaneSplitAxis::kHorizontal;
  target_input.start_ratio = 0.4;
  target_input.divider_size = 8;
  target_input.minimums = {200, 200};
  const TargetPaneGeometry target =
      ComputeTargetPaneGeometry(target_input, current);

  EXPECT_TRUE(gfx::Rect(0, 0, 437, 600) == target.start);
  EXPECT_TRUE(gfx::Rect(445, 0, 655, 600) == target.end);
  EXPECT_TRUE(gfx::Rect(0, 0, 357, 600) == current.start);
  EXPECT_TRUE(gfx::Rect(365, 0, 535, 600) == current.end);
}

TEST(MultiContentsGeometryTest, NoticeMinimumsAreDirectional) {
  SupportedNoticeLayoutMinimumInput input;
  input.split = true;
  input.native_pane_minimum = 200;
  input.notice_pane_index = 1;
  input.semantic_stack_height = 108;
  input.rigid_control_min_width = 260;
  input.body_top_inset = 42;
  input.body_leading_inset = 1;
  input.body_bottom_inset = 1;
  input.body_trailing_inset = 1;

  input.axis = PaneSplitAxis::kHorizontal;
  EXPECT_TRUE((SupportedNoticeLayoutMinimum{{200, 262}, 351}) ==
              ResolveSupportedNoticeLayoutMinimum(input));

  input.axis = PaneSplitAxis::kVertical;
  EXPECT_TRUE((SupportedNoticeLayoutMinimum{{200, 351}, 262}) ==
              ResolveSupportedNoticeLayoutMinimum(input));

  input.notice_pane_index = 0;
  EXPECT_TRUE((SupportedNoticeLayoutMinimum{{351, 200}, 262}) ==
              ResolveSupportedNoticeLayoutMinimum(input));
}

TEST(MultiContentsGeometryTest, SupportedFloorClampsWithoutChangingRatio) {
  CurrentPaneGeometryInput input;
  input.available_space = gfx::Rect(0, 0, 470, 600);
  input.split = true;
  input.start_ratio = 0.95;
  input.divider_size = 8;
  input.minimums = {200, 262};

  const CurrentPaneGeometry supported = ComputeCurrentPaneGeometry(input);
  EXPECT_EQ(200, supported.start.width());
  EXPECT_EQ(262, supported.end.width());

  input.available_space.set_width(400);
  const CurrentPaneGeometry below_floor = ComputeCurrentPaneGeometry(input);
  EXPECT_EQ(392, below_floor.start.width() + below_floor.end.width());
  EXPECT_GE(below_floor.start.width(), 0);
  EXPECT_GE(below_floor.end.width(), 0);
  EXPECT_DOUBLE_EQ(0.95, input.start_ratio);
}

TEST(MultiContentsGeometryTest, ExternalFlowRequiresOneFreshExactIdentity) {
  int manager = 0;
  int other = 0;
  ExternalInfoBarFlowPlanInput input;
  input.manager_identity = reinterpret_cast<uintptr_t>(&manager);
  input.pane_identities = {reinterpret_cast<uintptr_t>(&other),
                           reinterpret_cast<uintptr_t>(&manager)};
  input.pane_visible = {true, true};
  input.semantic_stack_height = 54;
  input.generation = 7;
  input.minimum_generation = 7;

  ExternalInfoBarFlowPlan plan = ResolveExternalInfoBarFlowPlan(input);
  EXPECT_EQ(1, plan.matched_pane_index);
  EXPECT_EQ((std::array<int, 2>{0, 54}), plan.pane_reservations);
  EXPECT_FALSE(plan.stale);

  input.pane_identities = {reinterpret_cast<uintptr_t>(&manager),
                           reinterpret_cast<uintptr_t>(&manager)};
  plan = ResolveExternalInfoBarFlowPlan(input);
  EXPECT_EQ(-1, plan.matched_pane_index);
  EXPECT_EQ((std::array<int, 2>{0, 0}), plan.pane_reservations);

  input.pane_identities = {reinterpret_cast<uintptr_t>(&other),
                           reinterpret_cast<uintptr_t>(&manager)};
  input.generation = 6;
  plan = ResolveExternalInfoBarFlowPlan(input);
  EXPECT_TRUE(plan.stale);
  EXPECT_EQ((std::array<int, 2>{0, 0}), plan.pane_reservations);
}

TEST(MultiContentsGeometryTest, InfoBarSlotUsesCurrentManagerPaneBody) {
  ExternalInfoBarSlotInput input;
  input.manager_body_bounds_in_browser = gfx::Rect(727, 48, 466, 745);
  input.notice_flow_bounds = gfx::Rect(250, 6, 944, 788);
  input.semantic_height = 54;

  EXPECT_TRUE(gfx::Rect(727, 48, 466, 54) == ResolveExternalInfoBarSlot(input));

  input.semantic_height = 800;
  EXPECT_TRUE(ResolveExternalInfoBarSlot(input).IsEmpty());
}

}  // namespace
}  // namespace yee
