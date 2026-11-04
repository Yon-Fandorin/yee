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
