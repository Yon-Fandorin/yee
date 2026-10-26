// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_layout.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/gfx/geometry/outsets.h"

namespace yee {

bool UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type browser_type,
                                   bool has_vertical_tab_strip) {
  return IsShellEnabled() &&
         browser_type == BrowserWindowInterface::Type::TYPE_NORMAL &&
         has_vertical_tab_strip;
}

BrowserSurfaceMode GetBrowserSurfaceMode(
    const BrowserWindowInterface& browser) {
  const auto* const state =
      tabs::VerticalTabStripStateController::From(&browser);
  return UsesYeeBrowserSurfaceGeometry(browser.GetType(), state != nullptr) &&
                 !state->ShouldDisplayVerticalTabs()
             ? BrowserSurfaceMode::kConnectedTabs
             : BrowserSurfaceMode::kCard;
}

gfx::Rect ResolveBrowserSurfaceContentColumn(const gfx::Rect& client_bounds,
                                             int content_leading_inset,
                                             int horizontal_tab_strip_advance) {
  gfx::Rect result = client_bounds;
  const int leading =
      std::clamp(content_leading_inset - kSidebarMetrics.content_gutter, 0,
                 client_bounds.width());
  const int top =
      std::clamp(horizontal_tab_strip_advance - kSidebarMetrics.content_gutter,
                 0, client_bounds.height());
  result.Inset(gfx::Insets::TLBR(top, leading, 0, 0));
  return result;
}

gfx::Rect ResolveBrowserWindowControlsBounds(const gfx::Rect& frame_bounds,
                                             int controls_width,
                                             bool leading,
                                             int horizontal_tab_strip_height) {
  // 수평 탭은 본문 외곽선에, Sidebar는 Header 내부 조작 여백에 맞춘다.
  const int edge_inset =
      kSidebarMetrics.content_gutter +
      (horizontal_tab_strip_height > 0
           ? 0
           : kSidebarMetrics.browser_surface_header_control_edge_inset);
  const int width = std::clamp(
      controls_width, 0, std::max(0, frame_bounds.width() - 2 * edge_inset));
  return gfx::Rect(
      leading ? frame_bounds.x() + edge_inset
              : frame_bounds.right() - edge_inset - width,
      frame_bounds.y() +
          (horizontal_tab_strip_height > 0
               ? std::max(0, (horizontal_tab_strip_height -
                              kSidebarMetrics.toolbar_height) /
                                 2)
               : kSidebarMetrics.browser_surface_toolbar_top_inset()),
      width, kSidebarMetrics.toolbar_height);
}

gfx::Rect ResolveBrowserWindowControlHighlightBounds(gfx::Rect button_bounds) {
  button_bounds.ClampToCenteredSize(gfx::Size(
      kSidebarMetrics.shell_control_size, kSidebarMetrics.shell_control_size));
  return button_bounds;
}

gfx::Rect ExcludeBrowserWindowControls(gfx::Rect header_bounds,
                                       const gfx::Rect& controls_bounds) {
  if (!header_bounds.Intersects(controls_bounds)) {
    return header_bounds;
  }
  if (controls_bounds.CenterPoint().x() >= header_bounds.CenterPoint().x()) {
    header_bounds.set_width(
        std::max(0, controls_bounds.x() - header_bounds.x()));
  } else {
    header_bounds.SetHorizontalBounds(
        std::min(header_bounds.right(), controls_bounds.right()),
        header_bounds.right());
  }
  return header_bounds;
}

ResolvedBrowserSurfaceSeed ResolveBrowserSurfaceSeed(
    const BrowserSurfaceSeedInput& input) {
  ResolvedBrowserSurfaceSeed result;
  result.split = input.split;
  result.content_column_bounds = input.content_column_bounds;
  result.surface_seed_bounds = input.content_column_bounds;
  gfx::Insets insets = input.surface_insets;
  if (!input.split && input.header_above_content_height > 0) {
    insets.set_top(-input.header_above_content_height);
  }
  result.surface_seed_bounds.Inset(insets);
  if (!input.split &&
      (input.header_participates || input.header_above_content_height > 0) &&
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
  result.decoration.mode = input.mode;
  result.decoration.combined_outline_visible =
      !result.split && !result.main_surface_bounds.IsEmpty();
  result.decoration.draw_outer_outline =
      result.decoration.combined_outline_visible &&
      input.mode == BrowserSurfaceMode::kCard;
  result.decoration.show_shadow =
      result.decoration.draw_outer_outline && !input.side_panel_surface_visible;
  result.decoration.side_panel_joins_header =
      input.mode == BrowserSurfaceMode::kConnectedTabs &&
      result.decoration.combined_outline_visible &&
      !result.header_bounds.IsEmpty();
  if (result.decoration.side_panel_joins_header &&
      input.connected_side_panel_resize_gutter) {
    gfx::Rect gutter = *input.connected_side_panel_resize_gutter;
    gutter.Intersect(result.surface_seed_bounds);
    if (!gutter.IsEmpty()) {
      auto& bounds = result.decoration.combined_outline_bounds;
      bounds.set_x(result.header_bounds.x());
      bounds.set_width(result.header_bounds.width());
      result.decoration.side_panel_divider_x =
          gutter.CenterPoint().x() - bounds.x();
      result.decoration.side_panel_gutter_on_page_surface =
          bounds.Contains(*input.connected_side_panel_resize_gutter);
    }
  }
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
