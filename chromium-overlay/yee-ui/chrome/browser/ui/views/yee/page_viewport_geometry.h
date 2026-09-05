// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_GEOMETRY_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_GEOMETRY_H_

#include <cstdint>
#include <optional>

#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace yee {

enum class PageDevToolsDockedPlacement {
  kLeft,
  kRight,
  kBottom,
  kNone,
  kUnknown,
};

struct PageViewportGeometryInput {
  // Both rectangles use ContentsContainerView-local physical coordinates.
  // `page_environment_in_container` is after Pane Header and external InfoBar
  // reservation but before DevTools. `content_and_footer_in_container` is the
  // native non-DevTools page result before the optional NTP footer is removed
  // from the renderer viewport.
  gfx::Rect page_environment_in_container;
  gfx::Rect content_and_footer_in_container;
  int footer_height = 0;
  std::optional<gfx::Outsets> target_outsets;
  gfx::Point container_origin_in_screen;
  gfx::RoundedCornersF viewport_radii;
  bool is_rtl = false;
  uint64_t generation = 0;
};

struct PageViewportGeometry {
  gfx::Rect page_environment_in_container;
  gfx::Rect content_and_footer_in_container;
  gfx::Rect current_viewport_in_container;
  gfx::Rect target_stack_in_container;
  gfx::Rect current_clip_in_target;
  gfx::Rect current_viewport_in_screen;
  gfx::RoundedCornersF viewport_radii;
  uint64_t generation = 0;

  bool operator==(const PageViewportGeometry&) const = default;
};

// Produces one parent-independent page coordinate contract while all page
// families remain direct ContentsContainerView children. Target outsets retain
// Chromium's existing logical-RTL clip convention until PageTargetHost owns
// the renderer-sized stack.
PageViewportGeometry ResolvePageViewportGeometry(
    const PageViewportGeometryInput& input);

// Infers DevTools placement from two rectangles in the same explicit
// container coordinate space. This remains stable when Pane Header or InfoBar
// reservations move the page environment away from View::GetContentsBounds().
PageDevToolsDockedPlacement ResolvePageDevToolsDockedPlacement(
    const gfx::Rect& page_environment_in_container,
    const gfx::Rect& content_and_footer_in_container);

// Converts a visible rectangle and a direct child's bounds from their shared
// ContentsContainerView coordinate space into a clip local to that child. A
// direct family uses this until it passes the gates required to move under a
// persistent page host.
gfx::Rect ResolveDirectChildClipInLocalSpace(
    const gfx::Rect& child_bounds_in_container,
    const gfx::Rect& visible_bounds_in_container);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_GEOMETRY_H_
