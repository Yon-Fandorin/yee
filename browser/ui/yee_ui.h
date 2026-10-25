// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_

#include <memory>
#include <optional>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/sidebar_presentation.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/button.h"

class ToolbarButton;

namespace ui {
class ColorProvider;
class NativeTheme;
}  // namespace ui

namespace views {
class Background;
class View;
}  // namespace views

namespace yee {

struct ResolvedBrowserSurfaceDecoration;

inline constexpr int kCombinedSurfaceOutlineViewId = 92003;
inline constexpr int kSidePanelSurfaceViewId = 92018;
inline constexpr int kBrowserSurfaceFillViewId = 92019;

// Header·그룹 chip·크기 조절 컨트롤이 공통 포커스 대비를 사용한다.
ui::ColorId ResolveBrowserSurfaceFocusColorId(
    const ui::ColorProvider& color_provider,
    SkColor background,
    ui::ColorId preferred);

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
  int horizontal_group_chip_height = 20;
  int horizontal_group_capsule_top_inset = 4;
  int horizontal_group_capsule_height = 32;
  int horizontal_group_capsule_padding = 2;
  float horizontal_group_tint_opacity = 0.12f;
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

// 행·Favorite·드래그 미리보기는 같은 테마와 전경 역할을 쓴다.
// 평평한 행의 선택 면은 지속 카드보다 옅게 표현한다.
enum class SidebarItemVisualState {
  kResting,
  kHovered,
  kActive,
  kDragging,
};

struct SidebarItemColors {
  SkColor fill = SK_ColorTRANSPARENT;
  SkColor stroke = SK_ColorTRANSPARENT;
  SkColor foreground = SK_ColorBLACK;
};

SidebarItemColors ResolveSidebarItemColors(
    const ui::ColorProvider& color_provider,
    SidebarItemVisualState state,
    double hover_progress,
    bool frame_active,
    bool persistent_surface);
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

enum class ShellCreateAction {
  kNewTab,
  kNewGroup,
  kChat,
};

using ShellCreateCallback =
    base::RepeatingCallback<void(ShellCreateAction action, int event_flags)>;

using BrowserSurfacePresentationCallback =
    base::RepeatingCallback<std::optional<BrowserSurfacePresentation>()>;

struct BrowserSurfaceHeaderColors {
  SkColor primary;
  SkColor secondary;
  SkColor disabled;
};

// Uses the page's rendered surface color as the Browser Surface Header, falling
// back to the current Toolbar color when the page does not provide one.
SkColor ResolveBrowserSurfaceHeaderColor(
    const ui::ColorProvider& color_provider,
    std::optional<SkColor> page_surface_color);

// Derives readable text and control colors from the exact page-aware Header
// surface. Primary and secondary roles keep minimum contrast guarantees while
// disabled controls intentionally remain quieter.
BrowserSurfaceHeaderColors ResolveBrowserSurfaceHeaderColors(
    SkColor surface_color);

// 페이지를 따르는 Header에서 읽을 수 있는 명암으로 1 DIP focus 선을 정한다.
SkColor ResolveBrowserSurfaceFocusStrokeColor(
    SkColor surface_color,
    std::optional<SkColor> theme_accent = std::nullopt);

// Keeps native semantic colors unchanged when readable, otherwise adjusts
// luminance toward a contrasting endpoint without substituting a neutral role.
SkColor ResolveBrowserSurfaceSemanticColor(SkColor surface, SkColor semantic);

// 대비 선호와 별개인 강제 색 모드에서도 시스템 색상을 우선한다.
bool ShouldUseNativeBrowserSurfaceColors(const ui::NativeTheme& theme);

// Paints the compact Omnibox's full-height surface while keeping its optional
// one-DIP focus stroke inset from the control edge. This makes focus read as an
// internal state without changing native bounds or hit testing.
std::unique_ptr<views::Background> CreateBrowserSurfaceOmniboxBackground(
    SkColor background_color,
    SkColor focus_stroke_color);

// Native 강조색 계산에 실제 컨트롤 바탕을 전달한다.
void SetBrowserSurfaceControlBackground(views::View& view,
                                        std::optional<SkColor> surface);

// Adds Yee's exact neutral popup roles after Chromium has populated an
// uncached provider with every registered platform, component, and Chrome
// mixer. High-contrast callers skip this function so native semantic and
// forced colors remain authoritative.
void AddBrowserSurfaceOmniboxPopupColorMixer(
    ui::ColorProvider& provider,
    const BrowserSurfacePresentation& presentation);
std::unique_ptr<ui::ColorProvider>
CreateBrowserSurfaceOmniboxPopupColorProvider(
    ui::ColorProviderKey key,
    std::optional<BrowserSurfacePresentation> presentation);

std::unique_ptr<views::Background> CreateShellBackground();

// Resolves the one opaque frame-derived material used by native glass, the
// Views shell, Sidebar items, and the split canvas. Dark mode retains the theme
// hue while bringing overly bright frame colors into a calm dark tonal range;
// light mode preserves the supplied frame color.
SkColor ResolveShellBackgroundColor(const ui::ColorProvider& color_provider,
                                    bool frame_active);

// Returns an opaque, theme-resolved proxy for the shell surface. Translucent
// Yee surfaces use this to calculate contrast without sampling desktop pixels.
SkColor ResolveShellContrastBackground(const ui::ColorProvider& color_provider);

// Returns the opaque Chrome Surface color inherited by Yee's split backing,
// pane outlines, and split controls. Split presentation deliberately does not
// follow either page's sampled color.
SkColor ResolveSplitCanvasColor(const ui::ColorProvider& color_provider);

// Returns the subtle one-DIP separator shared by the combined Browser Surface
// and split Pane Headers. Deriving it from the resolved surface keeps the
// boundary legible across page-aware light and dark colors.
SkColor ResolveBrowserSurfaceSeparatorColor(SkColor surface_color);

// Native macOS glass and Yee's Views background share one opacity contract.
// The native host uses this tint value; Yee calculates the remaining surface
// alpha required to reach the product's effective shell opacity.
double GetNativeGlassTintOpacity(bool is_dark_mode);

std::unique_ptr<views::View> CreateCombinedSurfaceOutlineView(
    BrowserSurfacePresentationCallback presentation_callback);

// Creates the page-aware fill hosted by Chromium's existing animated main
// background region. The host owns compositor clipping and geometry; this
// non-interactive Yee child owns only Browser Surface color presentation.
std::unique_ptr<views::View> CreateBrowserSurfaceFillView(
    std::optional<BrowserSurfacePresentation> presentation);
void UpdateBrowserSurfaceFillView(
    views::View& view,
    std::optional<BrowserSurfacePresentation> presentation);

// Creates the non-interactive card backing for Chromium's native Side Panel.
// The native SidePanel remains the owner of content, focus, resizing, and
// commands; this view only supplies Yee's shared surface fill, outline, and
// shadow treatment.
std::unique_ptr<views::View> CreateSidePanelSurfaceView();

// 분할 구조가 바뀌면 다음 레이아웃의 최종 장식 적용 전에 공통 윤곽·그림자를
// 숨긴다.
void UpdateCombinedSurfaceOutlineView(views::View& view,
                                      bool split_presentation);

// native 배치 후 계산된 Browser Surface 장식을 적용한다. 구분선 좌표는
// 윤곽 호스트 기준이며, 그리기 단계에서 부모·형제의 구조로 정책을 추론하지
// 않는다.
void ApplyCombinedSurfaceDecoration(
    views::View& view,
    const ResolvedBrowserSurfaceDecoration& decoration);

// Side Panel의 채움·경계에 같은 표면 모드를 적용한다.
// native SidePanel이 공간 배분·가시성·입력·콘텐츠를 계속 소유한다.
void ApplySidePanelSurfaceDecoration(
    views::View& view,
    const ResolvedBrowserSurfaceDecoration& decoration);

// Split panes are independent cards aligned directly to Yee's Browser Surface.
// Chromium continues to own split layout, resizing, and semantic highlights.
gfx::RoundedCornersF ResolveSplitPaneRoundedCorners();
std::unique_ptr<views::View> CreateSplitPaneEmphasisView();
void UpdateSplitPaneEmphasisView(views::View &view, bool visible,
                                 bool emphasized);

// Non-interactive resting presentation layered over Chromium's real Omnibox.
// The native editor remains mounted underneath and is revealed on focus.
std::unique_ptr<views::View> CreateOmniboxRestingTextView();
void UpdateOmniboxRestingTextView(views::View& view,
                                  std::u16string_view title,
                                  std::u16string_view origin,
                                  SkColor background_color,
                                  bool visible);
void UpdateOmniboxRestingTextView(
    views::View& view,
    std::u16string_view title,
    std::u16string_view origin,
    const BrowserSurfacePresentation& presentation,
    bool visible);

void ApplyShellControlStyle(ToolbarButton& button);

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
    ShellCreateCallback callback,
    SidebarPopupLockCallback acquire_popup_lock = {});
std::unique_ptr<ToolbarButton> CreateAgentToolbarButton(
    views::Button::PressedCallback callback);

// Window-global Sidebar Header actions. This view owns one Create control and
// one Agent status control regardless of whether the Browser Surface is single
// or split. Chromium supplies command callbacks and the platform caption-button
// exclusion; Yee owns their presentation and geometry.
std::unique_ptr<views::View> CreateSidebarHeaderActionsView(
    ShellCreateCallback create_callback,
    views::Button::PressedCallback agent_callback,
    SidebarPopupLockCallback acquire_popup_lock = {});
// 최소 폭은 움직이지 않는 창 버튼 영역을 기준으로 하여 hover 중에도 유지한다.
int GetSidebarHeaderMinimumWidth(int leading_exclusion);
void SetSidebarHeaderActionsLeadingExclusion(views::View &view,
                                             int leading_exclusion,
                                             int leading_offset = 0,
                                             int top_inset = 0);
void SetSidebarHeaderActionsControlsVisible(views::View &view, bool visible);
bool IsSidebarHeaderActionsPositionInWindowCaption(const views::View &view,
                                                   const gfx::Point &point);

} // namespace yee

#endif // CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
