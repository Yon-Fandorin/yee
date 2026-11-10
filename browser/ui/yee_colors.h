// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_COLORS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_COLORS_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

namespace yee {

struct BrowserSurfaceHeaderColors {
  SkColor primary;
  SkColor secondary;
  SkColor disabled;
};

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

struct BrowserSurfaceBookmarkColors {
  SkColor fill;
  SkColor outline;
};

// 활성 별의 호박색 채움을 유지하고 실제 주소창에서 윤곽을 구분한다.
BrowserSurfaceBookmarkColors ResolveBrowserSurfaceBookmarkColors(SkColor surface);

// 창 닫기 버튼의 합성된 배경에 맞춰 X의 대비를 보정한다.
SkColor ResolveBrowserSurfaceCaptionGlyph(SkColor surface,
                                          SkColor background_overlay,
                                          SkColor preferred_glyph);

// Returns the subtle one-DIP separator shared by the combined Browser Surface
// and split Pane Headers. Deriving it from the resolved surface keeps the
// boundary legible across page-aware light and dark colors.
SkColor ResolveBrowserSurfaceSeparatorColor(SkColor surface_color);

// Native macOS glass and Yee's Views background share one opacity contract.
// The native host uses this tint value; Yee calculates the remaining surface
// alpha required to reach the product's effective shell opacity.
double GetNativeGlassTintOpacity(bool is_dark_mode);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_YEE_COLORS_H_
