// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_

#include <array>
#include <optional>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/yee/multi_contents_geometry.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"

namespace yee {

// Surface geometry is deliberately narrower than Yee presentation enablement.
// Shared BrowserView/MCV/CCV classes must retain Chromium geometry in popups,
// apps, standalone DevTools, and picture-in-picture windows.
bool UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type browser_type,
                                   bool has_vertical_tab_strip);

// 제품 모드는 전체화면의 탭바 노출 여부나 Header를 호스팅하는 Widget에
// 영향을 받지 않는다.
enum class BrowserSurfaceMode {
  kCard,
  kConnectedTabs,
};

BrowserSurfaceMode GetBrowserSurfaceMode(const BrowserWindowInterface& browser);

// 맞춤설정은 탭 방향과 관계없이 창 Header 아래에 도킹한다.
bool UsesFullWidthBrowserSurfaceHeader(BrowserSurfaceMode mode,
                                       bool customization_panel);

// 수평 탭 행의 실제 높이를 먼저 배분하고 같은 Surface 여백을 한 번만 적용한다.
gfx::Rect ResolveBrowserSurfaceContentColumn(const gfx::Rect& client_bounds,
                                             int content_leading_inset,
                                             int horizontal_tab_strip_advance);

// 수평 탭이 있으면 native 행 높이에 맞추고, 없으면 기존 Header의 조작 축을
// 사용한다. 창 버튼이 탭 행을 늘리거나 카드의 곡률을 덮지 않게 한다.
gfx::Rect ResolveBrowserWindowControlsBounds(
    const gfx::Rect& frame_bounds,
    int controls_width,
    bool leading,
    int horizontal_tab_strip_height = 0);

// native 클릭 범위 안에서 hover/누름 배경만 공통 shell 조작 크기로 맞춘다.
gfx::Rect ResolveBrowserWindowControlHighlightBounds(gfx::Rect button_bounds);

// 같은 행에 겹치는 창 버튼 영역만 Header 조작의 가로 배치에서 제외한다.
gfx::Rect ExcludeBrowserWindowControls(gfx::Rect header_bounds,
                                       const gfx::Rect& controls_bounds);

// Native 조작의 폭과 명시적 여백만 전달한다. 없는 여백은 Yee 기본 간격이다.
struct SplitPaneHeaderControlMetrics {
  int width = 0;
  std::optional<gfx::Insets> margins;
  gfx::Insets internal_padding;
};

struct SplitPaneHeaderMetrics {
  // Sidebar, 뒤로, 앞으로, 새로고침, native 주소창, Pane 닫기 순서다.
  std::array<SplitPaneHeaderControlMetrics, 6> controls;
  bool active = false;
};

struct SplitPaneHeaderSpacing {
  int edge_padding = 0;
  int item_spacing = 0;

  bool operator==(const SplitPaneHeaderSpacing&) const = default;
};

SplitPaneHeaderSpacing ResolveSplitPaneHeaderSpacing(bool active, bool compact);

// Flex의 겹친 여백과 native 내부 padding을 같은 순서로 계산한다.
int MeasureSplitPaneHeaderMinimumWidth(const SplitPaneHeaderMetrics& metrics,
                                       const SplitPaneHeaderSpacing& spacing,
                                       int leading_reservation = 0,
                                       int trailing_reservation = 0);

struct SplitPaneHeaderLayoutInput {
  // 이전 View bounds가 아닌 이번 부모 배치의 논리 BrowserView 좌표다.
  gfx::Rect canvas_bounds;
  gfx::Rect window_controls_bounds;
  PaneSplitAxis axis = PaneSplitAxis::kHorizontal;
  double start_ratio = 0.5;
  int divider_size = 0;
  PaneDirectionalMinimums body_minimums;
  std::array<SplitPaneHeaderMetrics, 2> headers;
};

struct SplitPaneHeaderRowLayout {
  SplitPaneHeaderSpacing spacing;
  gfx::Insets interior_margin;
  int minimum_width = 0;

  bool operator==(const SplitPaneHeaderRowLayout&) const = default;
};

struct ResolvedSplitPaneHeaderLayout {
  PaneDirectionalMinimums pane_minimums;
  std::array<SplitPaneHeaderRowLayout, 2> rows;
  bool compact = false;
  bool supported = false;
};

// 창 버튼과 필수 Header 조작의 최소 폭을 기존 Pane 최소 배분에 합친다.
// 폭이 부족하면 같은 간격 규칙으로만 압축하며 저장된 분할 비율은 바꾸지 않는다.
ResolvedSplitPaneHeaderLayout ResolveSplitPaneHeaderLayout(
    const SplitPaneHeaderLayoutInput& input);

struct BrowserSurfaceEdgeStyle {
  int gutter = 0;
  float corner_radius = 0.0f;
};

BrowserSurfaceEdgeStyle ResolveBrowserSurfaceEdgeStyle(
    double content_fullscreen_fraction);

struct BrowserSurfaceSeedInput {
  gfx::Rect content_column_bounds;
  gfx::Insets surface_insets;

  bool split = false;
  bool header_participates = false;
  int header_height = 0;
  // 별도 native Header 아래에서 시작하는 본문은 같은 카드를 위로 연결한다.
  int header_above_content_height = 0;
};

struct ResolvedBrowserSurfaceSeed {
  gfx::Rect content_column_bounds;
  gfx::Rect surface_seed_bounds;
  gfx::Rect header_bounds;
  bool split = false;

  bool operator==(const ResolvedBrowserSurfaceSeed&) const = default;
};

// Resolves shell-owned content-column, Surface, and participating Header
// geometry before native top-container and Side Panel allocation.
ResolvedBrowserSurfaceSeed ResolveBrowserSurfaceSeed(
    const BrowserSurfaceSeedInput& input);

// The native Side Panel shadow box consumes padding from every physical edge
// of the remaining BrowserView allocation. In Yee's shared-row presentation,
// that padding is card spacing rather than an additional shell inset. Restore
// it before ResolveBrowserSurfaceSeed() applies Yee's one outer-gutter
// contract; the Side Panel's visible width remains allocated separately.
gfx::Rect RestoreBrowserSurfaceContentColumnAfterSidePanel(
    const gfx::Rect& native_remaining_bounds,
    const gfx::Insets& native_shadow_insets);

struct BrowserSurfaceFrameInput {
  ResolvedBrowserSurfaceSeed seed;

  // Native allocation after the top container, Side Panel, and shadow box,
  // before InfoBar flow or minimum-width content underlap.
  gfx::Rect native_notice_flow_allocation;

  // Native allocation for MCV after its minimum-width underlap rule. This may
  // be wider than the notice slot underneath an occluding Side Panel.
  gfx::Rect native_body_allocation;

  // 공용 Header가 페이지와 Side Panel을 함께 덮을 때만 전달한다. 수평 탭과
  // 세로 맞춤설정이 사용하며 native resize 영역은 공유 Surface 안에 유지한다.
  std::optional<gfx::Rect> connected_side_panel_resize_gutter = std::nullopt;
  BrowserSurfaceMode mode = BrowserSurfaceMode::kCard;
  bool side_panel_surface_visible = false;
  bool content_fullscreen_edges = false;
};

enum class BrowserSurfaceHeaderSeparatorOwner {
  kNone,
  kCombinedSurfaceOutline,
  kSplitPane,
};

// Paint and clipping decisions derived from the same structural frame as the
// Header, InfoBar flow, MCV, and combined outline bounds. Native layout glue
// consumes this value instead of inferring decoration from unrelated config
// carriers or already-applied View bounds.
struct ResolvedBrowserSurfaceDecoration {
  BrowserSurfaceMode mode = BrowserSurfaceMode::kCard;
  gfx::Rect combined_outline_bounds;
  // 연결 모드에서는 카드 윤곽·그림자 없이 구분선 호스트로만 표시할 수 있다.
  bool combined_outline_visible = false;
  bool draw_outer_outline = false;
  bool show_shadow = false;
  // 공용 Header와 실제로 이어질 때만 Side Panel의 위쪽 윤곽을 생략한다.
  bool side_panel_joins_header = false;
  BrowserSurfaceHeaderSeparatorOwner header_separator_owner =
      BrowserSurfaceHeaderSeparatorOwner::kNone;
  int combined_header_separator_offset = 0;
  std::optional<int> side_panel_divider_x = std::nullopt;
  bool side_panel_gutter_on_page_surface = false;
  bool show_native_multi_contents_separator = false;
  bool contain_infobar_shadow_horizontally = true;
  float infobar_top_corner_radius = 0.0f;

  bool operator==(const ResolvedBrowserSurfaceDecoration&) const = default;
};

struct ResolvedBrowserSurfaceFrame {
  gfx::Rect surface_seed_bounds;
  gfx::Rect main_surface_bounds;
  gfx::Rect header_bounds;
  gfx::Rect notice_flow_bounds;
  gfx::Rect multi_contents_bounds;
  ResolvedBrowserSurfaceDecoration decoration;
  bool split = false;
};

// Resolves structural BrowserView rectangles once. Native layout remains the
// owner of top-container, Side Panel, shadow, and underlap calculations; this
// function only intersects those returned allocations with Yee's one Surface
// inset contract.
ResolvedBrowserSurfaceFrame ResolveBrowserSurfaceFrame(
    const BrowserSurfaceFrameInput& input);

// Resolves the final split-card inset before it is handed to
// MultiContentsView. Chromium's native inset remains the fallback contract,
// while Yee owns the one Surface card inset.
gfx::Insets ResolveBrowserSurfaceSplitViewInsets(
    bool split,
    const gfx::Insets& native_insets);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
