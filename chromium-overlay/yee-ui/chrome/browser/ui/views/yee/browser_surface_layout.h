// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/yee/multi_contents_geometry.h"
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

enum class BrowserSurfaceHeaderSeparatorOwner {
  kNone,
  kCombinedSurfaceOutline,
  kSplitPane,
};

// Paint and clipping decisions derived from the same structural frame as the
// Header, InfoBar flow, MCV, and combined outline bounds. Native layout glue
// consumes this value instead of inferring decoration from unrelated config
// carriers or already-applied View bounds.
struct ResolvedBrowserSurfaceDecoration {
  gfx::Rect combined_outline_bounds;
  bool combined_outline_visible = false;
  BrowserSurfaceHeaderSeparatorOwner header_separator_owner =
      BrowserSurfaceHeaderSeparatorOwner::kNone;
  int combined_header_separator_offset = 0;
  bool show_native_multi_contents_separator = false;
  bool contain_infobar_shadow_horizontally = true;
  float infobar_top_corner_radius = 0.0f;

  bool operator==(const ResolvedBrowserSurfaceDecoration&) const = default;
};

struct ResolvedBrowserSurfaceFrame {
  gfx::Rect surface_seed_bounds;
  gfx::Rect main_surface_bounds;
  gfx::Rect header_bounds;
  gfx::Rect notice_flow_bounds;
  gfx::Rect multi_contents_bounds;
  ResolvedBrowserSurfaceDecoration decoration;
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

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
