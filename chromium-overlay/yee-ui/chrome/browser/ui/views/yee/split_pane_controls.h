// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SPLIT_PANE_CONTROLS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SPLIT_PANE_CONTROLS_H_

#include <memory>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace views {
class View;
}

namespace yee {

inline constexpr int kSplitPaneControlsViewId = 92011;

struct SplitPaneControlCallbacks {
  base::RepeatingClosure toggle_layout;
  base::RepeatingClosure reverse_order;
  base::RepeatingClosure exit_split;
  base::RepeatingCallback<void(bool)> set_resize_handle_anchored;
};

// Creates Yee's non-layout-affecting control surface for Chromium split tabs.
// Chromium continues to own resizing and the split-tab model; the callbacks
// are the only bridge from this presentation back to those native commands.
std::unique_ptr<views::View> CreateSplitPaneControlsView(
    SplitPaneControlCallbacks callbacks,
    views::View* resize_anchor);

void SetSplitPaneControlsEnabled(views::View& controls, bool enabled);
void UpdateSplitPaneControlsAnchor(views::View& controls,
                                   const gfx::Point& anchor_in_parent);
void SetSplitPaneControlsAnchorHovered(views::View& controls, bool hovered);
void DismissSplitPaneControls(views::View& controls);
void UpdateSplitPaneControls(views::View& controls,
                             bool side_by_side,
                             bool active_at_start);
gfx::Rect GetSplitPaneControlsBounds(views::View& controls,
                                     const gfx::Rect& parent_bounds);

// Keeps Chromium's keyboard-accessible resize handle visible at rest while
// deriving its contrast from Yee's split canvas.
void UpdateSplitResizeHandleAppearance(views::View& handle, bool emphasized);
void UpdateSplitResizeHandleAnchor(views::View& handle,
                                   const gfx::Vector2dF& offset,
                                   bool emphasized,
                                   bool animate);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SPLIT_PANE_CONTROLS_H_
