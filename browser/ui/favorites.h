// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_FAVORITES_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_FAVORITES_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/views/layout/layout_types.h"
#include "ui/views/layout/proposed_layout.h"

class TabStripModel;

namespace gfx {
class Canvas;
}

namespace views {
class View;
}

namespace yee {

enum class FavoritesDragIntent { kNone, kPin, kUnpin, kRejected };

// One policy decision shared by pointer hit-testing and native container-entry
// callbacks. Keeping these paths identical prevents a nested Group from
// overwriting a deferred unpin, and keeps a full dock rejection distinct from
// a normal same-region drag.
FavoritesDragIntent ResolveFavoritesDragIntent(bool dragging_pinned_tabs,
                                               bool over_favorites,
                                               int pinned_count,
                                               int adding);

// One cell in the Favorites icon dock. Drag fields match Chromium's
// `DraggedViewVisualData` so the pinned container can hand layout off.
struct FavoritesDockItem {
  raw_ptr<views::View> view = nullptr;
  bool hidden = false;
  bool floating = false;
  std::optional<gfx::Vector2d> drag_offset;
};

gfx::Size FavoritesDockMinimumSize(int item_count);

views::ProposedLayout CalculateFavoritesDockLayout(
    const std::vector<FavoritesDockItem>& items,
    const views::SizeBounds& size_bounds,
    bool contains_split,
    std::optional<int> incoming_slot = std::nullopt);

gfx::Size FavoritesTileSize(const views::View& dock);

// Size of a cell after the dock reaches `item_count`. This is the geometry to
// use for an incoming Tab because adding it can change the number of columns.
gfx::Size FavoritesTileSizeForItemCount(const views::View& dock,
                                        int item_count);

// Screen rect of the moving favorite. `grab_offset` is the pointer
// relative to the tile origin in pixels, captured from the real cell
// so the overlay does not jump at drag start.
gfx::Rect FavoritesDraggedTileInScreen(const gfx::Point& pointer_in_screen,
                                       const gfx::Vector2d& grab_offset,
                                       const gfx::Size& tile_size);

// Target bounds of the drag preview while it is over the Tab list, in
// `tab_strip` coordinates. Both painting and insertion hit-testing must use
// this geometry so the Favorite tile's original grab point cannot change the
// destination slot.
gfx::Rect TabDragPreviewBoundsInTabStrip(
    views::View& tab_strip,
    const gfx::Point& pointer_in_screen,
    int tab_leading_inset,
    const gfx::Vector2d& source_grab_offset,
    const gfx::Size& source_tile_size);

// Moving tile converted to the dock's eventual cell geometry without moving
// the pointer's relative grab position.
gfx::Rect FavoritesDraggedTileForDockInScreen(
    const views::View& dock,
    const gfx::Point& pointer_in_screen,
    const gfx::Vector2d& source_grab_offset,
    const gfx::Size& source_tile_size,
    int target_item_count);

// Slot the moving tile is sitting on (0..item_count). `reserve_incoming_slot`
// uses the resulting grid with one extra cell, which is required when a Tab
// crosses into Favorites. Same-dock reordering keeps the current grid.
std::optional<int> FavoritesInsertIndexForTile(
    const views::View& dock,
    const gfx::Rect& tile_in_screen,
    int item_count,
    bool reserve_incoming_slot = false);

// The dock uses a dimmed well instead of a 1px separator.
bool FavoritesDockUsesSeparator();

void ApplyFavoritesDockStyle(views::View& dock);

// Theme-aware tile state. Used by TabView, SplitTabView, and drag preview.
void PaintFavoritesCellBackground(gfx::Canvas* canvas,
                                  const gfx::Rect& bounds,
                                  SidebarItemVisualState state,
                                  double hover_progress,
                                  bool frame_active,
                                  const ui::ColorProvider& color_provider);

// Empty Favorites pad: dashed well plus “drag here” copy, full sidebar width.
// Chromium glue only lays it out while a tab drag is active.
std::unique_ptr<views::View> CreateFavoritesDropZone();
void SetFavoritesDropZoneOpen(views::View& drop_zone,
                              bool open,
                              bool animate = true);

// Dock, empty drop zone, and a content-side magnet so dragging a favorite
// past the last cell stays a last-slot drop instead of tearing out a window.
// The magnet never extends below the visible target, leaving the seam and the
// upper half of the first Tab available as the first Tab insertion gap.
bool PointHitsFavoritesDropTarget(const views::View& tab_strip,
                                  const gfx::Point& point_in_screen);

// Drag preview that is a child of the tab strip (not the clipped scroll
// views). Over Favorites it is a 32px icon cell; over the tab list it is a
// title row. Both follow the pointer.
void ShowFavoritesDragPreview(views::View& tab_strip,
                              const gfx::Point& point_in_screen,
                              bool over_favorites,
                              int tab_leading_inset,
                              int favorites_target_item_count,
                              const ui::ImageModel& favicon,
                              const std::u16string& title,
                              const gfx::Vector2d& grab_offset,
                              const gfx::Size& source_tile_size);
void HideFavoritesDragPreview(views::View& tab_strip);

// User-facing pin (context menu, keyboard). Unpin is always allowed.
// Session restore and APIs are not gated so Chromium can recover more
// than `favorites_max_count` pinned tabs.
bool CanFavoriteContext(const TabStripModel& model, int context_index);

// Dragging currently unpinned tabs onto the dock.
bool CanFavoriteDrop(int pinned_count, int adding);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_FAVORITES_H_
