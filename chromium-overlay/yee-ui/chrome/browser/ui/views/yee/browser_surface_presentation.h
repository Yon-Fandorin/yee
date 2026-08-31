// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_H_

#include <cstdint>

#include "third_party/skia/include/core/SkColor.h"

namespace yee {

// A complete, immutable paint contract for one WebContents-backed Browser
// Surface Header. Consumers apply one revision atomically instead of resolving
// individual text, icon, and state colors on independent timelines.
struct BrowserSurfacePresentation {
  enum class PaletteMode {
    kCustomSurface,
    kNativeColors,
  };

  PaletteMode palette_mode = PaletteMode::kCustomSurface;
  uint64_t source_id = 0;
  uint64_t revision = 0;
  uint64_t popup_revision = 0;
  SkColor surface = SK_ColorTRANSPARENT;
  SkColor primary = SK_ColorTRANSPARENT;
  SkColor secondary = SK_ColorTRANSPARENT;
  SkColor disabled = SK_ColorTRANSPARENT;
  SkColor location_hover = SK_ColorTRANSPARENT;
  SkColor focus_stroke = SK_ColorTRANSPARENT;
  SkColor header_separator = SK_ColorTRANSPARENT;
  SkColor resting_divider = SK_ColorTRANSPARENT;
  SkColor popup_hover = SK_ColorTRANSPARENT;
  SkColor popup_outline = SK_ColorTRANSPARENT;

  bool operator==(const BrowserSurfacePresentation&) const = default;
};

BrowserSurfacePresentation ResolveBrowserSurfacePresentation(
    SkColor surface,
    uint64_t source_id,
    uint64_t revision,
    uint64_t popup_revision);

BrowserSurfacePresentation UseNativeBrowserSurfaceColors(
    const BrowserSurfacePresentation& presentation);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_PRESENTATION_H_
