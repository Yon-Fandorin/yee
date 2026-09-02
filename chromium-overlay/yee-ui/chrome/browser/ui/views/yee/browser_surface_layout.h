// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"

namespace yee {

// Surface geometry is deliberately narrower than Yee presentation enablement.
// Shared BrowserView/MCV/CCV classes must retain Chromium geometry in popups,
// apps, standalone DevTools, and picture-in-picture windows.
bool UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type browser_type,
                                   bool has_vertical_tab_strip);

struct BrowserSurfaceSeedInput {
  gfx::Rect content_column_bounds;
  gfx::Insets surface_insets;

  bool split = false;
  bool header_participates = false;
  int header_height = 0;
};

struct ResolvedBrowserSurfaceSeed {
  gfx::Rect content_column_bounds;
  gfx::Rect surface_seed_bounds;
  gfx::Rect header_bounds;
  bool split = false;

  bool operator==(const ResolvedBrowserSurfaceSeed&) const = default;
};

// Resolves shell-owned content-column, Surface, and participating Header
// geometry before native top-container and Side Panel allocation.
ResolvedBrowserSurfaceSeed ResolveBrowserSurfaceSeed(
    const BrowserSurfaceSeedInput& input);

struct BrowserSurfaceFrameInput {
  ResolvedBrowserSurfaceSeed seed;

  // Native allocation after the top container, Side Panel, and shadow box,
  // before InfoBar flow or minimum-width content underlap.
  gfx::Rect native_notice_flow_allocation;

  // Native allocation for MCV after its minimum-width underlap rule. This may
  // be wider than the notice slot underneath an occluding Side Panel.
  gfx::Rect native_body_allocation;
};

struct ResolvedBrowserSurfaceFrame {
  gfx::Rect surface_seed_bounds;
  gfx::Rect main_surface_bounds;
  gfx::Rect header_bounds;
  gfx::Rect notice_flow_bounds;
  gfx::Rect multi_contents_bounds;
  bool split = false;
};

// Resolves structural BrowserView rectangles once. Native layout remains the
// owner of top-container, Side Panel, shadow, and underlap calculations; this
// function only intersects those returned allocations with Yee's one Surface
// inset contract.
ResolvedBrowserSurfaceFrame ResolveBrowserSurfaceFrame(
    const BrowserSurfaceFrameInput& input);

// Resolves the final split-card inset before it is handed to
// MultiContentsView. Chromium's native inset remains the fallback contract,
// while Yee owns the one Surface card inset.
gfx::Insets ResolveBrowserSurfaceSplitViewInsets(
    bool split,
    const gfx::Insets& native_insets);

enum class PaneSplitAxis {
  kHorizontal,
  kVertical,
};

struct CurrentPaneGeometryInput {
  gfx::Rect available_space;
  bool split = false;
  PaneSplitAxis axis = PaneSplitAxis::kHorizontal;
  double start_ratio = 0.5;
  int divider_size = 0;
  int minimum_pane_size = 0;
};

struct CurrentPaneGeometry {
  gfx::Rect start;
  gfx::Rect divider;
  gfx::Rect end;
};

// Pure current-frame pane calculation. Target-size renderer geometry is a
// separate concern and must never be used to place direct BrowserView chrome.
CurrentPaneGeometry ComputeCurrentPaneGeometry(
    const CurrentPaneGeometryInput& input);

struct ExternalInfoBarSlotInput {
  gfx::Rect multi_contents_bounds;
  gfx::Rect active_pane_bounds_in_multi_contents;
  gfx::Rect notice_flow_bounds;
  bool split = false;
  int split_header_height = 0;
  int split_body_horizontal_inset = 0;
  int semantic_height = 0;
  int paint_offset = 0;
};

// Places Chromium's one direct InfoBar inside the current active page body.
// The returned rectangle is in BrowserView coordinates. An empty result means
// the current native notice slot and active body do not form a contiguous
// visible region for this frame.
gfx::Rect ResolveExternalInfoBarSlot(const ExternalInfoBarSlotInput& input);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
