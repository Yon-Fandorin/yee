// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_METRICS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_METRICS_H_

#include "build/build_config.h"

namespace yee {

// Geometry owned by Yee's vertical sidebar presentation. Keeping these values
// together lets Chromium's native tab and group views consume one stable
// visual contract without owning product-specific measurements.
struct SidebarMetrics {
  int expanded_width = 244;
  int collapsed_width = 8;
  int content_gutter = 6;
  int sidebar_flyout_inset = 12;
  int resize_gutter_width = 10;
  int resize_handle_thickness = 4;
  int resize_handle_length = 24;
  int content_corner_radius = 12;
  int browser_surface_outline_width = 1;
  int titlebar_height = 48;
  int toolbar_height = 40;
  int toolbar_leading_inset = 10;
  int toolbar_trailing_gap = 8;
  int toolbar_interior_inset = 4;
  int browser_surface_header_control_edge_inset = 8;
  int toolbar_extension_container_margin = 2;
  int location_bar_margin = 5;
  int location_bar_focus_stroke_inset = 2;
  int omnibox_popup_results_padding = 8;
  int omnibox_popup_row_inset = 8;
  int omnibox_popup_row_corner_radius = 8;
  int omnibox_popup_separator_height = 1;
  int split_card_inset = 0;
  int split_card_corner_radius = 12;
  int split_pane_header_height = 42;
  int split_pane_address_bar_inset = 4;
  int browser_surface_location_bar_height = 34;
  int split_pane_address_bar_gap = 4;
  int split_pane_content_stroke_inset = 1;
  int split_pane_inner_corner_radius = 11;
  int split_pane_address_bar_horizontal_padding = 8;
  int split_pane_address_bar_active_padding = 2;
  int split_pane_address_bar_item_spacing = 6;
  int shell_control_size = 30;
  int shell_control_corner_radius = 8;
  int shell_control_horizontal_margin = 1;
  int shell_control_separator_height = 18;
  int shell_control_separator_margin = 8;
  int sidebar_header_control_top_padding = 4;
  int sidebar_footer_row_height = 50;
  int sidebar_footer_top_spacing = 6;
  int sidebar_footer_corner_radius = 6;
  int sidebar_footer_icon_size = 20;
  int sidebar_footer_disclosure_size = 14;
  int sidebar_footer_icon_label_spacing = 8;
  int sidebar_footer_surface_outline_width = 1;
  int sidebar_footer_surface_padding_vertical = 4;
  int sidebar_footer_surface_padding_horizontal = 5;
  int sidebar_row_hover_animation_duration_ms = 50;
#if BUILDFLAG(IS_WIN)
  int sidebar_row_hover_fill_alpha = 24;
  int sidebar_row_active_fill_alpha = 40;
#else
  int sidebar_row_hover_fill_alpha = 18;
  int sidebar_row_active_fill_alpha = 30;
#endif
  int sidebar_footer_menu_gap = 8;
  int sidebar_footer_root_row_height = 44;
  int sidebar_footer_menu_row_height = 28;
  int sidebar_footer_menu_corner_radius = 6;
  int sidebar_footer_menu_inset = 6;
  int sidebar_footer_menu_min_width = 224;
  int sidebar_footer_menu_max_width = 288;
  int sidebar_footer_menu_surface_corner_radius = 10;
  int sidebar_footer_menu_icon_slot_size = 20;
  int sidebar_footer_menu_icon_size = 14;
  int sidebar_footer_menu_disclosure_size = 12;
  int sidebar_footer_menu_horizontal_inset = 10;
  int sidebar_footer_screen_fade_out_duration_ms = 45;
  int sidebar_footer_screen_fade_in_duration_ms = 70;

  constexpr int sidebar_header_controls_leading_inset() const {
    return tab_strip_horizontal_padding + tab_content_horizontal_padding +
           (tab_icon_design_width - shell_control_size) / 2 -
           shell_control_horizontal_margin;
  }

  constexpr int sidebar_caption_toolbar_inset() const {
    return shell_control_horizontal_margin - toolbar_interior_inset;
  }

  constexpr int sidebar_footer_surface_vertical_inset() const {
    return sidebar_footer_surface_outline_width +
           sidebar_footer_surface_padding_vertical;
  }
  constexpr int sidebar_footer_surface_horizontal_inset() const {
    return sidebar_footer_surface_outline_width +
           sidebar_footer_surface_padding_horizontal;
  }

  // The combined Browser Surface starts below the shell gutter, so its
  // Header is shorter than the window titlebar. Center the native Toolbar in
  // that visible Header instead of centering it against the window edge.
  constexpr int browser_surface_header_height() const {
    return titlebar_height - content_gutter;
  }
  constexpr int browser_surface_header_center_y() const {
    return content_gutter + browser_surface_header_height() / 2;
  }
  constexpr int browser_surface_toolbar_top_inset() const {
    return browser_surface_header_center_y() - toolbar_height / 2;
  }
  // AppKit centers native caption controls inside its private titlebar height.
  // Yee's visible Header starts below the outer gutter, so use a symmetric
  // native titlebar whose center lands on the same visual axis.
  constexpr int native_caption_titlebar_height() const {
    return 2 * browser_surface_header_center_y();
  }
  int tab_icon_design_width = 16;
  int tab_row_height = 32;
  int tab_row_corner_radius = 6;
  int tab_content_vertical_padding = 3;
  int tab_content_horizontal_padding = 8;
  int tab_strip_horizontal_padding = 8;
  int tab_list_top_inset = 8;
  int tab_to_content_gap = 8;
  int tab_row_margin = 2;
  int tab_hover_card_offset = 4;
  int tab_title_font_delta = -1;
  int tab_title_line_height = 13;
  int tab_subtitle_font_delta = -3;
  int tab_subtitle_line_height = 10;
  int tab_agent_indicator_size = 12;
  int tab_agent_indicator_trailing_inset = 4;
  int tab_agent_indicator_slot_width = 20;

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
  int horizontal_tab_minimum_icon_gap = 4;
  int horizontal_group_chip_height = 20;
  int horizontal_group_capsule_top_inset = 4;
  int horizontal_group_capsule_height = 32;
  int horizontal_group_collapsed_capsule_height = 24;
  int horizontal_group_capsule_padding = 2;
  float horizontal_group_tint_opacity = 0.24f;
  float horizontal_group_hover_tint_opacity = 0.34f;
  int group_header_horizontal_inset = 8;
  int group_mark_size = 8;
  int group_mark_slot_size = 16;
  int group_mark_trailing_margin = 4;
  int group_unread_dot_size = 5;

  int favorites_cell_size = 32;
  int favorites_cell_vertical_padding = 8;
  int favorites_cell_gap = 8;
  int favorites_max_columns = 4;
  int favorites_max_count = 12;
  int favorites_dock_corner_radius = 8;
  int favorites_dock_fill_alpha = 36;
  int favorites_cell_fill_alpha = 18;
  int favorites_cell_hover_fill_alpha = 30;
  int favorites_cell_active_fill_alpha = 42;
  int favorites_cell_drag_fill_alpha = 58;
  int favorites_cell_stroke_alpha = 28;
  int favorites_cell_active_stroke_alpha = 52;
  int favorites_cell_drag_stroke_alpha = 68;
  int favorites_drop_zone_height = 76;
  int favorites_drop_zone_open_duration_ms = 160;
  int favorites_drop_zone_surface_delay_ms = 30;
  int favorites_drop_zone_close_duration_ms = 120;
  int favorites_drop_zone_commit_fade_duration_ms = 90;
  int favorites_shift_duration_ms = 420;
  int tab_drag_geometry_duration_ms = 120;
  int cross_region_arrival_offset = 10;
  int cross_region_arrival_duration_ms = 240;
};

inline constexpr SidebarMetrics kSidebarMetrics;

static_assert(kSidebarMetrics.split_pane_inner_corner_radius ==
              kSidebarMetrics.split_card_corner_radius -
                  kSidebarMetrics.split_pane_content_stroke_inset);
static_assert(kSidebarMetrics.content_corner_radius ==
              kSidebarMetrics.split_card_corner_radius);
static_assert(kSidebarMetrics.browser_surface_header_height() ==
              kSidebarMetrics.split_pane_header_height);
static_assert(kSidebarMetrics.browser_surface_location_bar_height +
                  2 * kSidebarMetrics.split_pane_address_bar_inset ==
              kSidebarMetrics.browser_surface_header_height());
static_assert(kSidebarMetrics.browser_surface_header_control_edge_inset ==
              kSidebarMetrics.toolbar_trailing_gap -
                  kSidebarMetrics.content_gutter +
                  kSidebarMetrics.toolbar_interior_inset +
                  kSidebarMetrics.toolbar_extension_container_margin);
static_assert(kSidebarMetrics.browser_surface_outline_width ==
              kSidebarMetrics.split_pane_content_stroke_inset);
static_assert(kSidebarMetrics.sidebar_header_controls_leading_inset() >= 0);
static_assert(kSidebarMetrics.sidebar_footer_surface_outline_width > 0);
static_assert(kSidebarMetrics.sidebar_footer_disclosure_size <
              kSidebarMetrics.sidebar_footer_icon_size);
inline constexpr int kSidebarFavoritesLabelViewId = 92001;
inline constexpr int kSidebarBookmarksButtonViewId = 92002;
inline constexpr int kSidebarFavoritesDropZoneViewId = 92007;
inline constexpr int kSidebarFavoritesDockViewId = 92008;
inline constexpr int kSidebarFavoritesDragPreviewViewId = 92009;
inline constexpr int kSplitPaneEmphasisViewId = 92010;
inline constexpr int kSidebarHeaderActionsViewId = 92012;
inline constexpr int kSidebarHeaderCreateViewId = 92013;
inline constexpr int kSidebarHeaderAgentViewId = 92014;

inline constexpr int kCombinedSurfaceOutlineViewId = 92003;
inline constexpr int kSidePanelSurfaceViewId = 92018;
inline constexpr int kBrowserSurfaceFillViewId = 92019;

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_METRICS_H_
