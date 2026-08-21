// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/button.h"

class ToolbarButton;

namespace ui {
class ColorProvider;
}  // namespace ui

namespace views {
class Background;
class View;
}  // namespace views

namespace yee {

inline constexpr int kContentOutlineViewId = 92003;
inline constexpr float kContentCornerRadius = 8.0f;
inline constexpr int kShellControlSize = 30;
inline constexpr int kShellControlCornerRadius = 8;
inline constexpr int kShellControlHorizontalMargin = 1;

// Geometry owned by Yee's vertical sidebar presentation. Keeping these values
// together lets Chromium's native tab and group views consume one stable
// visual contract without owning product-specific measurements.
struct SidebarMetrics {
  int expanded_width = 244;
  int collapsed_width = 8;

  int tab_icon_design_width = 16;
  int tab_row_height = 32;
  int tab_content_vertical_padding = 3;
  int tab_content_horizontal_padding = 8;
  int tab_strip_horizontal_padding = 8;
  int tab_to_content_gap = 8;
  int tab_row_margin = 2;
  int tab_hover_card_offset = 4;
  int tab_title_font_delta = -1;
  int tab_title_line_height = 13;
  int tab_subtitle_font_delta = -3;
  int tab_subtitle_line_height = 10;

  int section_horizontal_inset = 8;
  int section_label_font_delta = -4;
  int section_label_height = 18;
  int section_label_bottom_spacing = 4;
  int section_gap = 10;

  int bookmarks_row_height = 30;
  int bookmarks_row_corner_radius = 6;
  int bookmarks_row_horizontal_padding = 6;
  int bookmarks_icon_size = 16;
  int bookmarks_image_label_spacing = 6;

  int group_header_corner_radius = 8;
  int group_header_horizontal_inset = 8;
  int group_mark_size = 8;
  int group_mark_slot_size = 16;
  int group_mark_trailing_margin = 4;
  int group_unread_dot_size = 5;
  int group_header_hover_alpha = 51;

  int favorites_cell_size = 32;
  int favorites_cell_vertical_padding = 8;
  int favorites_cell_gap = 8;
  int favorites_max_columns = 4;
  int favorites_max_count = 12;
  int favorites_dock_insets = 6;
  int favorites_dock_corner_radius = 8;
  int favorites_dock_fill_alpha = 36;
  int favorites_cell_fill_alpha = 110;
  int favorites_cell_hover_fill_alpha = 150;
  int favorites_cell_active_fill_alpha = 220;
  int favorites_cell_stroke_alpha = 72;
  int favorites_cell_active_stroke_alpha = 140;
  int favorites_drop_zone_height = 76;
  int favorites_drop_zone_open_duration_ms = 160;
  int favorites_drop_zone_surface_delay_ms = 30;
  int favorites_drop_zone_close_duration_ms = 120;
  int favorites_drop_zone_commit_fade_duration_ms = 90;
  int favorites_shift_duration_ms = 420;
  int cross_region_arrival_offset = 10;
  int cross_region_arrival_duration_ms = 240;
};

inline constexpr SidebarMetrics kSidebarMetrics;
inline constexpr int kSidebarFavoritesLabelViewId = 92001;
inline constexpr int kSidebarBookmarksButtonViewId = 92002;
inline constexpr int kSidebarFavoritesDropZoneViewId = 92007;
inline constexpr int kSidebarFavoritesDockViewId = 92008;
inline constexpr int kSidebarFavoritesDragPreviewViewId = 92009;

enum class ShellCreateAction {
  kNewTab,
  kNewGroup,
  kChat,
};

using ShellCreateCallback =
    base::RepeatingCallback<void(ShellCreateAction action, int event_flags)>;

std::unique_ptr<views::Background> CreateShellBackground();

// Returns an opaque, theme-resolved proxy for the shell surface. Translucent
// Yee surfaces use this to calculate contrast without sampling desktop pixels.
SkColor ResolveShellContrastBackground(const ui::ColorProvider& color_provider);

// Native macOS glass and Yee's Views background share one opacity contract.
// The native host uses this tint value; Yee calculates the remaining surface
// alpha required to reach the product's effective shell opacity.
double GetNativeGlassTintOpacity(bool is_dark_mode);

std::unique_ptr<views::View> CreateContentOutlineView();

void ApplyShellControlStyle(ToolbarButton& button);

bool IsShellEnabled();

// Yee's collapsed sidebar is an 8px edge target, not Chromium's 56px icon
// rail. Tab, group, and favorites presentation therefore stays on the
// expanded contract when the strip is pinned open or revealed on hover.
bool UsesExpandedSidebarPresentation();

// A lone tab starts as a sidebar organization drag so Favorites and the Tab
// list get first refusal. Leaving the sidebar still hands the drag back to
// Chromium's normal window move path. Group-header drags keep Chromium's
// all-tabs behavior.
bool ShouldPrioritizeSidebarTabDrag(int dragged_tab_count,
                                    int source_tab_count,
                                    bool is_group_drag,
                                    bool uses_vertical_tab_strip);

// `adding` is how many currently unpinned tabs would become favorites.
inline bool CanAddFavorite(int pinned_count, int adding = 1) {
  return adding <= 0 ||
         pinned_count + adding <= kSidebarMetrics.favorites_max_count;
}

// Nudges a vertical-tab hover card away from the sidebar. Chromium's slide
// animator reads View::GetAnchorBoundsInScreen(), so TabView applies this
// there instead of inside the bubble.
gfx::Rect AdjustVerticalTabHoverCardAnchor(const gfx::Rect& bounds);

std::unique_ptr<ToolbarButton> CreateShellToolbarButton(
    views::Button::PressedCallback callback);
std::unique_ptr<ToolbarButton> CreateShellAddButton(
    ShellCreateCallback callback);
std::unique_ptr<ToolbarButton> CreateAgentToolbarButton(
    views::Button::PressedCallback callback);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
