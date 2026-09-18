// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_layout.h"

#include <algorithm>

#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/gfx/geometry/outsets.h"

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

gfx::Rect RestoreBrowserSurfaceContentColumnAfterSidePanel(
    const gfx::Rect& native_remaining_bounds,
    const gfx::Insets& native_shadow_insets) {
  gfx::Rect content_column = native_remaining_bounds;
  content_column.Outset(gfx::Outsets::TLBR(
      native_shadow_insets.top(), native_shadow_insets.left(),
      native_shadow_insets.bottom(), native_shadow_insets.right()));
  return content_column;
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

  result.decoration.combined_outline_bounds = result.main_surface_bounds;
  result.decoration.combined_outline_visible =
      !result.split && !result.main_surface_bounds.IsEmpty();
  if (result.split) {
    result.decoration.header_separator_owner =
        BrowserSurfaceHeaderSeparatorOwner::kSplitPane;
  } else if (!result.header_bounds.IsEmpty()) {
    result.decoration.header_separator_owner =
        BrowserSurfaceHeaderSeparatorOwner::kCombinedSurfaceOutline;
    result.decoration.combined_header_separator_offset =
        result.header_bounds.bottom() - result.main_surface_bounds.y();
  } else {
    // With no participating Header, the first native InfoBar (if any) becomes
    // the top content surface. Clip only that bar's top corners; its separate
    // ContentShadow remains free to paint below the semantic stack.
    result.decoration.infobar_top_corner_radius = std::max(
        0.0f,
        static_cast<float>(kSidebarMetrics.content_corner_radius -
                           kSidebarMetrics.browser_surface_outline_width));
  }

  return result;
}

gfx::Insets ResolveBrowserSurfaceSplitViewInsets(
    bool split,
    const gfx::Insets& native_insets) {
  return split ? gfx::Insets(kSidebarMetrics.split_card_inset) : native_insets;
}

}  // namespace yee
