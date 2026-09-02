// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_layout.h"

#include <algorithm>
#include <cmath>

#include "chrome/browser/ui/views/yee/yee_ui.h"

namespace yee {

bool UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type browser_type,
                                   bool has_vertical_tab_strip) {
  return IsShellEnabled() &&
         browser_type == BrowserWindowInterface::Type::TYPE_NORMAL &&
         has_vertical_tab_strip;
}

ResolvedBrowserSurfaceSeed ResolveBrowserSurfaceSeed(
    const BrowserSurfaceSeedInput& input) {
  ResolvedBrowserSurfaceSeed result;
  result.split = input.split;
  result.content_column_bounds = input.content_column_bounds;
  result.surface_seed_bounds = input.content_column_bounds;
  result.surface_seed_bounds.Inset(input.surface_insets);
  if (!input.split && input.header_participates &&
      !result.surface_seed_bounds.IsEmpty()) {
    result.header_bounds = gfx::Rect(
        result.surface_seed_bounds.x(), result.surface_seed_bounds.y(),
        result.surface_seed_bounds.width(),
        std::min(input.header_height, result.surface_seed_bounds.height()));
  }
  return result;
}

ResolvedBrowserSurfaceFrame ResolveBrowserSurfaceFrame(
    const BrowserSurfaceFrameInput& input) {
  ResolvedBrowserSurfaceFrame result;
  result.split = input.seed.split;
  result.surface_seed_bounds = input.seed.surface_seed_bounds;
  result.header_bounds = input.seed.header_bounds;

  result.notice_flow_bounds = input.native_notice_flow_allocation;
  result.notice_flow_bounds.Intersect(result.surface_seed_bounds);

  result.multi_contents_bounds = input.native_body_allocation;
  result.multi_contents_bounds.Intersect(result.surface_seed_bounds);

  result.main_surface_bounds = result.notice_flow_bounds;
  if (!result.main_surface_bounds.IsEmpty()) {
    const int bottom = std::max(result.notice_flow_bounds.bottom(),
                                result.multi_contents_bounds.bottom());
    result.main_surface_bounds.set_y(result.surface_seed_bounds.y());
    result.main_surface_bounds.set_height(
        std::max(0, bottom - result.surface_seed_bounds.y()));
  }

  return result;
}

gfx::Insets ResolveBrowserSurfaceSplitViewInsets(
    bool split,
    const gfx::Insets& native_insets) {
  return split ? gfx::Insets(kSidebarMetrics.split_card_inset) : native_insets;
}

CurrentPaneGeometry ComputeCurrentPaneGeometry(
    const CurrentPaneGeometryInput& input) {
  CurrentPaneGeometry result;
  if (input.available_space.IsEmpty()) {
    return result;
  }
  if (!input.split) {
    result.start = input.available_space;
    return result;
  }

  const bool horizontal = input.axis == PaneSplitAxis::kHorizontal;
  const int available_size = horizontal ? input.available_space.width()
                                        : input.available_space.height();
  const int divider_size = std::clamp(input.divider_size, 0, available_size);
  const int pane_space = available_size - divider_size;
  int start_size = static_cast<int>(
      std::round(std::clamp(input.start_ratio, 0.0, 1.0) * pane_space));
  int end_size = pane_space - start_size;

  // If the available space itself cannot support both requested minima, split
  // the deficit deterministically without producing a negative rectangle.
  const int effective_minimum =
      std::min(std::max(0, input.minimum_pane_size), pane_space / 2);
  if (start_size < effective_minimum) {
    start_size = effective_minimum;
    end_size = pane_space - start_size;
  } else if (end_size < effective_minimum) {
    end_size = effective_minimum;
    start_size = pane_space - end_size;
  }

  if (horizontal) {
    result.start =
        gfx::Rect(input.available_space.origin(),
                  gfx::Size(start_size, input.available_space.height()));
    result.divider =
        gfx::Rect(result.start.top_right(),
                  gfx::Size(divider_size, input.available_space.height()));
    result.end = gfx::Rect(result.divider.top_right(),
                           gfx::Size(end_size, input.available_space.height()));
  } else {
    result.start =
        gfx::Rect(input.available_space.origin(),
                  gfx::Size(input.available_space.width(), start_size));
    result.divider =
        gfx::Rect(result.start.bottom_left(),
                  gfx::Size(input.available_space.width(), divider_size));
    result.end = gfx::Rect(result.divider.bottom_left(),
                           gfx::Size(input.available_space.width(), end_size));
  }
  return result;
}

gfx::Rect ResolveExternalInfoBarSlot(const ExternalInfoBarSlotInput& input) {
  if (input.semantic_height <= 0 || input.multi_contents_bounds.IsEmpty() ||
      input.active_pane_bounds_in_multi_contents.IsEmpty()) {
    return gfx::Rect();
  }

  gfx::Rect active_body = input.active_pane_bounds_in_multi_contents;
  active_body.Offset(input.multi_contents_bounds.OffsetFromOrigin());
  if (input.split) {
    active_body.Inset(gfx::Insets::TLBR(input.split_header_height,
                                        input.split_body_horizontal_inset, 0,
                                        input.split_body_horizontal_inset));
  }
  if (active_body.height() < input.semantic_height) {
    return gfx::Rect();
  }

  gfx::Rect slot(active_body.x(), active_body.y() + input.paint_offset,
                 active_body.width(), input.semantic_height);
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
