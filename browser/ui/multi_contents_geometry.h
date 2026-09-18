// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_MULTI_CONTENTS_GEOMETRY_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_MULTI_CONTENTS_GEOMETRY_H_

#include <array>
#include <cstdint>

#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"

namespace yee {

enum class PaneSplitAxis {
  kHorizontal,
  kVertical,
};

struct PaneDirectionalMinimums {
  int start = 0;
  int end = 0;

  bool operator==(const PaneDirectionalMinimums&) const = default;
};

struct SupportedNoticeLayoutMinimumInput {
  bool split = false;
  PaneSplitAxis axis = PaneSplitAxis::kHorizontal;
  int notice_pane_index = -1;
  int native_pane_minimum = 0;
  int semantic_stack_height = 0;
  int rigid_control_min_width = 0;
  int body_top_inset = 0;
  int body_leading_inset = 0;
  int body_bottom_inset = 0;
  int body_trailing_inset = 0;
};

struct SupportedNoticeLayoutMinimum {
  PaneDirectionalMinimums split_axis;
  int cross_axis = 0;

  bool operator==(const SupportedNoticeLayoutMinimum&) const = default;
};

// Converts native InfoBar body-space requirements into asymmetric pane-space
// minima plus a diagnostic cross-axis support floor. Outer MCV insets and the
// divider are deliberately not included.
SupportedNoticeLayoutMinimum ResolveSupportedNoticeLayoutMinimum(
    const SupportedNoticeLayoutMinimumInput& input);

struct CurrentPaneGeometryInput {
  gfx::Rect available_space;
  bool split = false;
  PaneSplitAxis axis = PaneSplitAxis::kHorizontal;
  double start_ratio = 0.5;
  int divider_size = 0;
  PaneDirectionalMinimums minimums;
  gfx::Insets start_body_insets;
  gfx::Insets end_body_insets;
};

struct CurrentPaneGeometry {
  gfx::Rect start;
  gfx::Rect start_body;
  gfx::Rect divider;
  gfx::Rect end;
  gfx::Rect end_body;

  bool operator==(const CurrentPaneGeometry&) const = default;
};

// Pure current-frame pane calculation. BrowserView chrome may consume this
// type; target animation geometry is intentionally a different type.
CurrentPaneGeometry ComputeCurrentPaneGeometry(
    const CurrentPaneGeometryInput& input);

struct TargetPaneGeometryInput {
  gfx::Rect available_space;
  bool split = false;
  PaneSplitAxis axis = PaneSplitAxis::kHorizontal;
  double start_ratio = 0.5;
  int divider_size = 0;
  PaneDirectionalMinimums minimums;
};

struct TargetPaneGeometry {
  gfx::Rect start;
  gfx::Rect end;
};

// Pure renderer-target calculation. Only MCV's target-animation path may
// consume this result; direct BrowserView chrome must use current geometry.
TargetPaneGeometry ComputeTargetPaneGeometry(
    const TargetPaneGeometryInput& input,
    const CurrentPaneGeometry& current_geometry);

struct ExternalInfoBarFlowPlanInput {
  uintptr_t manager_identity = 0;
  std::array<uintptr_t, 2> pane_identities = {};
  std::array<bool, 2> pane_visible = {};
  int semantic_stack_height = 0;
  uint64_t generation = 0;
  uint64_t minimum_generation = 0;
};

struct ExternalInfoBarFlowPlan {
  int matched_pane_index = -1;
  std::array<int, 2> pane_reservations = {};
  uint64_t generation = 0;
  bool stale = false;
};

// Resolves one exact visible pane for a prepared manager identity. Duplicate,
// unmapped, null, and stale identities reserve no pane rather than falling back
// to an active index.
ExternalInfoBarFlowPlan ResolveExternalInfoBarFlowPlan(
    const ExternalInfoBarFlowPlanInput& input);

struct ExternalInfoBarSlotInput {
  gfx::Rect manager_body_bounds_in_browser;
  gfx::Rect notice_flow_bounds;
  int semantic_height = 0;
  int paint_offset = 0;
};

// Places Chromium's one direct InfoBar inside the current manager-owned page
// body. An empty result means the native notice slot and body are not a
// contiguous visible region for this frame.
gfx::Rect ResolveExternalInfoBarSlot(const ExternalInfoBarSlotInput& input);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_MULTI_CONTENTS_GEOMETRY_H_
