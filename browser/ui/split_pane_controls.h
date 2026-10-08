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
class ResizeArea;
class ResizeAreaDelegate;
class View;
class ViewTargeter;
}

namespace yee {

inline constexpr int kSplitPaneControlsViewId = 92011;
inline constexpr int kSidePanelResizeGutterViewId = 92020;

// Adds a hover/focus-only marker to Chromium's native Sidebar resize delegate.
// Native resizing, keyboard commands, and state stay intact.
std::unique_ptr<views::ResizeArea>
CreateSidebarResizeArea(views::ResizeAreaDelegate* delegate);
gfx::Rect GetSidebarResizeAreaBounds(const gfx::Rect& sidebar,
                                   int resize_area_width);
std::unique_ptr<views::ViewTargeter>
CreateSidebarResizeAreaTargeter(views::ResizeArea& resize_area);

// A bounded diagonal corridor, not a halo over the whole page.
bool IsPointInSplitPaneControlsTransitRegion(const gfx::Point& point,
                                             const gfx::Rect& controls,
                                             const gfx::Rect& divider);

struct SidePanelResizeCallbacks {
  base::RepeatingCallback<void(int, bool)> resize;
  base::RepeatingClosure record_metrics;
  base::RepeatingCallback<void(bool)> set_keyboard_resized;
};
std::unique_ptr<views::View> CreateSidePanelResizeGutterView(
    SidePanelResizeCallbacks callbacks);
gfx::Rect GetSidePanelResizeGutterBounds(const gfx::Rect& panel, bool leading);
gfx::Rect ExcludeSidePanelResizeGutter(gfx::Rect bounds,
                                      const gfx::Rect& gutter, bool leading);

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

// Derives resize marker contrast from Yee's split canvas. Split and Side Panel
// dividers keep the resting marker; the Sidebar opts into hover/focus only.
void UpdateSplitResizeHandleAppearance(views::View& handle, bool emphasized,
                                       bool show_resting_marker = true);
void UpdateSplitResizeHandleAnchor(views::View& handle,
                                   const gfx::Vector2dF& offset,
                                   bool emphasized, bool animate,
                                   bool show_resting_marker = true);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SPLIT_PANE_CONTROLS_H_
