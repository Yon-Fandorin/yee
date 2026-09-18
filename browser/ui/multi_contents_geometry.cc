// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/multi_contents_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "ui/gfx/geometry/insets.h"

namespace yee {
namespace {

int SaturatedAdd(int first, int second) {
  return static_cast<int>(std::min<int64_t>(
      std::numeric_limits<int>::max(),
      static_cast<int64_t>(std::max(0, first)) + std::max(0, second)));
}

CurrentPaneGeometry ComputePaneGeometry(const gfx::Rect& available_space,
                                        bool split,
                                        PaneSplitAxis axis,
                                        double start_ratio,
                                        int divider_size,
                                        const PaneDirectionalMinimums& minimums,
                                        const gfx::Insets& start_body_insets,
                                        const gfx::Insets& end_body_insets) {
  CurrentPaneGeometry result;
  if (available_space.IsEmpty()) {
    return result;
  }
  if (!split) {
    result.start = available_space;
    result.start_body = result.start;
    result.start_body.Inset(start_body_insets);
    return result;
  }

  const bool horizontal = axis == PaneSplitAxis::kHorizontal;
  const int available_size =
      horizontal ? available_space.width() : available_space.height();
  divider_size = std::clamp(divider_size, 0, available_size);
  const int pane_space = available_size - divider_size;
  int start_size = static_cast<int>(
      std::round(std::clamp(start_ratio, 0.0, 1.0) * pane_space));

  const int start_minimum = std::max(0, minimums.start);
  const int end_minimum = std::max(0, minimums.end);
  if (static_cast<int64_t>(start_minimum) + end_minimum <= pane_space) {
    start_size =
        std::clamp(start_size, start_minimum, pane_space - end_minimum);
  } else {
    // Below the supported floor, preserve the requested ratio and guarantee
    // nonnegative rectangles. The product does not silently rewrite the saved
    // split ratio or invent an active-pane priority policy.
    start_size = std::clamp(start_size, 0, pane_space);
  }
  const int end_size = pane_space - start_size;

  if (horizontal) {
    result.start = gfx::Rect(available_space.origin(),
                             gfx::Size(start_size, available_space.height()));
    result.divider =
        gfx::Rect(result.start.top_right(),
                  gfx::Size(divider_size, available_space.height()));
    result.end = gfx::Rect(result.divider.top_right(),
                           gfx::Size(end_size, available_space.height()));
  } else {
    result.start = gfx::Rect(available_space.origin(),
                             gfx::Size(available_space.width(), start_size));
    result.divider =
        gfx::Rect(result.start.bottom_left(),
                  gfx::Size(available_space.width(), divider_size));
    result.end = gfx::Rect(result.divider.bottom_left(),
                           gfx::Size(available_space.width(), end_size));
  }
  result.start_body = result.start;
  result.start_body.Inset(start_body_insets);
  result.end_body = result.end;
  result.end_body.Inset(end_body_insets);
  return result;
}

}  // namespace

SupportedNoticeLayoutMinimum ResolveSupportedNoticeLayoutMinimum(
    const SupportedNoticeLayoutMinimumInput& input) {
  SupportedNoticeLayoutMinimum result;
  if (!input.split) {
    return result;
  }

  result.split_axis.start = result.split_axis.end =
      std::max(0, input.native_pane_minimum);
  result.cross_axis = std::max(0, input.native_pane_minimum);
  if (input.notice_pane_index < 0 || input.notice_pane_index > 1 ||
      input.semantic_stack_height <= 0) {
    return result;
  }

  const int notice_width = SaturatedAdd(
      SaturatedAdd(
          input.body_leading_inset,
          std::max(input.native_pane_minimum, input.rigid_control_min_width)),
      input.body_trailing_inset);
  const int notice_height = SaturatedAdd(
      SaturatedAdd(
          input.body_top_inset,
          SaturatedAdd(input.semantic_stack_height, input.native_pane_minimum)),
      input.body_bottom_inset);
  const int notice_minimum =
      input.axis == PaneSplitAxis::kHorizontal ? notice_width : notice_height;
  result.cross_axis =
      input.axis == PaneSplitAxis::kHorizontal ? notice_height : notice_width;
  if (input.notice_pane_index == 0) {
    result.split_axis.start = notice_minimum;
  } else {
    result.split_axis.end = notice_minimum;
  }
  return result;
}

CurrentPaneGeometry ComputeCurrentPaneGeometry(
    const CurrentPaneGeometryInput& input) {
  return ComputePaneGeometry(input.available_space, input.split, input.axis,
                             input.start_ratio, input.divider_size,
                             input.minimums, input.start_body_insets,
                             input.end_body_insets);
}

TargetPaneGeometry ComputeTargetPaneGeometry(
    const TargetPaneGeometryInput& input,
    const CurrentPaneGeometry& /*current_geometry*/) {
  const CurrentPaneGeometry target = ComputePaneGeometry(
      input.available_space, input.split, input.axis, input.start_ratio,
      input.divider_size, input.minimums, gfx::Insets(), gfx::Insets());
  return TargetPaneGeometry{target.start, target.end};
}

ExternalInfoBarFlowPlan ResolveExternalInfoBarFlowPlan(
    const ExternalInfoBarFlowPlanInput& input) {
  ExternalInfoBarFlowPlan result;
  result.generation = input.generation;
  if (input.generation < input.minimum_generation) {
    result.stale = true;
    return result;
  }
  if (!input.manager_identity || input.semantic_stack_height <= 0) {
    return result;
  }

  int match_count = 0;
  for (int index = 0; index < 2; ++index) {
    if (input.pane_visible[index] &&
        input.pane_identities[index] == input.manager_identity) {
      result.matched_pane_index = index;
      ++match_count;
    }
  }
  if (match_count != 1) {
    result.matched_pane_index = -1;
    return result;
  }
  result.pane_reservations[result.matched_pane_index] =
      input.semantic_stack_height;
  return result;
}

gfx::Rect ResolveExternalInfoBarSlot(const ExternalInfoBarSlotInput& input) {
  if (input.semantic_height <= 0 ||
      input.manager_body_bounds_in_browser.IsEmpty()) {
    return gfx::Rect();
  }

  if (input.manager_body_bounds_in_browser.height() < input.semantic_height) {
    return gfx::Rect();
  }

  gfx::Rect slot(input.manager_body_bounds_in_browser.x(),
                 input.manager_body_bounds_in_browser.y() + input.paint_offset,
                 input.manager_body_bounds_in_browser.width(),
                 input.semantic_height);
  gfx::Rect horizontal_notice_clip = input.notice_flow_bounds;
  horizontal_notice_clip.set_y(slot.y());
  horizontal_notice_clip.set_height(slot.height());
  slot.Intersect(horizontal_notice_clip);
  if (slot.height() != input.semantic_height) {
    return gfx::Rect();
  }
  return slot;
}

}  // namespace yee
