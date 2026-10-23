// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_LAYOUT_H_

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

// 수평 탭 행의 실제 높이를 먼저 배분하고 같은 Surface 여백을 한 번만 적용한다.
gfx::Rect ResolveBrowserSurfaceContentColumn(const gfx::Rect& client_bounds,
                                             int content_leading_inset,
                                             int horizontal_tab_strip_advance);

// 창 버튼을 기존 Header의 조작 축에 맞추고 카드의 곡률 안쪽에 배치한다.
gfx::Rect ResolveBrowserWindowControlsBounds(const gfx::Rect& frame_bounds,
                                             int controls_width,
                                             bool leading);

// native 클릭 범위 안에서 hover/누름 배경만 공통 shell 조작 크기로 맞춘다.
gfx::Rect ResolveBrowserWindowControlHighlightBounds(gfx::Rect button_bounds);

// 같은 행에 겹치는 창 버튼 영역만 Header 조작의 가로 배치에서 제외한다.
gfx::Rect ExcludeBrowserWindowControls(gfx::Rect header_bounds,
                                       const gfx::Rect& controls_bounds);

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

  // 수평 공용 Header는 페이지와 Side Panel을 함께 덮는다. native resize 영역은
  // 유지하되 두 영역이 공유하는 Surface 안에 표시한다.
  std::optional<gfx::Rect> connected_side_panel_resize_gutter = std::nullopt;
  BrowserSurfaceMode mode = BrowserSurfaceMode::kCard;
  bool side_panel_surface_visible = false;
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
