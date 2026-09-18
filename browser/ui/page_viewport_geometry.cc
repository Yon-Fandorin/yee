// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/page_viewport_geometry.h"

#include <algorithm>

#include "ui/gfx/geometry/insets.h"

namespace yee {

PageViewportGeometry ResolvePageViewportGeometry(
    const PageViewportGeometryInput& input) {
  PageViewportGeometry result;
  result.page_environment_in_container = input.page_environment_in_container;
  result.content_and_footer_in_container =
      input.content_and_footer_in_container;
  result.current_viewport_in_container = input.content_and_footer_in_container;
  result.current_viewport_in_container.set_height(
      std::max(0, result.current_viewport_in_container.height() -
                      std::max(0, input.footer_height)));
  result.target_stack_in_container = result.current_viewport_in_container;
  if (input.target_outsets) {
    result.target_stack_in_container.Outset(*input.target_outsets);
    result.current_clip_in_target =
        gfx::Rect(result.target_stack_in_container.size());
    gfx::Insets clip_insets = -input.target_outsets->ToInsets();
    if (input.is_rtl) {
      clip_insets.set_left_right(clip_insets.right(), clip_insets.left());
    }
    result.current_clip_in_target.Inset(clip_insets);
  }
  result.current_viewport_in_screen = result.current_viewport_in_container;
  result.current_viewport_in_screen.Offset(
      input.container_origin_in_screen.OffsetFromOrigin());
  result.viewport_radii = input.viewport_radii;
  result.generation = input.generation;
  return result;
}

PageDevToolsDockedPlacement ResolvePageDevToolsDockedPlacement(
    const gfx::Rect& page_environment_in_container,
    const gfx::Rect& content_and_footer_in_container) {
  if (content_and_footer_in_container == page_environment_in_container) {
    return PageDevToolsDockedPlacement::kNone;
  }
  if (content_and_footer_in_container.x() > page_environment_in_container.x() &&
      content_and_footer_in_container.y() ==
          page_environment_in_container.y() &&
      content_and_footer_in_container.height() ==
          page_environment_in_container.height()) {
    return PageDevToolsDockedPlacement::kLeft;
  }
  if (content_and_footer_in_container.origin() ==
          page_environment_in_container.origin() &&
      content_and_footer_in_container.height() ==
          page_environment_in_container.height()) {
    return PageDevToolsDockedPlacement::kRight;
  }
  if (content_and_footer_in_container.origin() ==
          page_environment_in_container.origin() &&
      content_and_footer_in_container.width() ==
          page_environment_in_container.width()) {
    return PageDevToolsDockedPlacement::kBottom;
  }
  return PageDevToolsDockedPlacement::kUnknown;
}

gfx::Rect ResolveDirectChildClipInLocalSpace(
    const gfx::Rect& child_bounds_in_container,
    const gfx::Rect& visible_bounds_in_container) {
  gfx::Rect clip = child_bounds_in_container;
  clip.Intersect(visible_bounds_in_container);
  clip.Offset(-child_bounds_in_container.OffsetFromOrigin());
  return clip;
}

}  // namespace yee
