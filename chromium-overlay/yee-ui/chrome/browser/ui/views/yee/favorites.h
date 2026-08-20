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

// Screen rect of the moving favorite. `grab_offset` is the pointer
// relative to the tile origin in pixels, captured from the real cell
// so the overlay does not jump at drag start.
gfx::Rect FavoritesDraggedTileInScreen(const gfx::Point& pointer_in_screen,
                                       const gfx::Vector2d& grab_offset,
                                       const gfx::Size& tile_size);

// Slot the moving tile is sitting on (0..item_count). Overlapping a
// cell returns that cell's index; past the last cell returns item_count.
std::optional<int> FavoritesInsertIndexForTile(const views::View& dock,
                                               const gfx::Rect& tile_in_screen,
                                               int item_count);

// The dock uses a dimmed well instead of a 1px separator.
bool FavoritesDockUsesSeparator();

void ApplyFavoritesDockStyle(views::View& dock);

// Idle / hover / active tile fill. Used by TabView and the drag preview.
void PaintFavoritesCellBackground(gfx::Canvas* canvas,
                                  const gfx::Rect& bounds,
                                  bool active,
                                  bool hovered);

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
