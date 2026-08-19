// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/button/button.h"

class ToolbarButton;

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

  int group_header_corner_radius = 6;
  int group_header_horizontal_inset = 5;
};

inline constexpr SidebarMetrics kSidebarMetrics;
inline constexpr int kSidebarFavoritesLabelViewId = 92001;
inline constexpr int kSidebarBookmarksButtonViewId = 92002;

enum class ShellCreateAction {
  kNewTab,
  kNewGroup,
  kChat,
};

using ShellCreateCallback =
    base::RepeatingCallback<void(ShellCreateAction action, int event_flags)>;

std::unique_ptr<views::Background> CreateShellBackground();
std::unique_ptr<views::View> CreateContentOutlineView();

void ApplyShellControlStyle(ToolbarButton& button);

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
