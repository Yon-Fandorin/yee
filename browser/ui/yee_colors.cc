// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_colors.h"

#include <algorithm>

#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "ui/gfx/color_utils.h"

namespace {

// native tint 38%/72%와 Views의 22/255 tint를 합쳐 light 약 43%, dark 약 74%를
// 유지한다.
constexpr double kNativeGlassTintOpacityLight = 0.38;
constexpr double kNativeGlassTintOpacityDark = 0.72;
constexpr float kHeaderPrimaryPreferredOpacity = 0.78f;
constexpr float kHeaderPrimaryMinimumContrast = 7.0f;
constexpr float kHeaderSecondaryPreferredOpacity = 0.56f;
constexpr float kHeaderDisabledOpacity = 0.28f;
constexpr float kHeaderFocusStrokePreferredOpacity = 0.44f;
constexpr float kLocationHoverOpacity = 0.02f;
constexpr float kOmniboxPopupHoverOpacity = 0.06f;
constexpr float kOmniboxPopupSelectedOpacity = 0.14f;
constexpr int kSurfaceSeparatorAlpha = 0x14;
constexpr float kOmniboxPopupOutlineContrast = 1.15f;
constexpr SkAlpha kOmniboxPopupButtonHoverAlpha = 0x1A;
constexpr SkAlpha kOmniboxPopupButtonPressedAlpha = 0x29;

SkColor ResolveSurfaceSeparatorColor(SkColor surface_color) {
  return color_utils::BlendTowardMaxContrast(surface_color,
                                             kSurfaceSeparatorAlpha);
}

}  // namespace

namespace yee {

BrowserSurfaceHeaderColors ResolveBrowserSurfaceHeaderColors(
    SkColor surface_color) {
  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  // 작은 주요 문자·아이콘은 가능하면 7:1까지 대비를 확보한다.
  // 중간 밝기 면에서 부족하면 물리적 검정·흰색 끝점까지 사용한다.
  const SkColor endpoint = color_utils::PickContrastingColor(
      SK_ColorBLACK, SK_ColorWHITE, surface_color);
  const SkColor preferred_primary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderPrimaryPreferredOpacity);
  const SkColor preferred_secondary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderSecondaryPreferredOpacity);
  return {
      .primary = color_utils::BlendForMinContrast(preferred_primary,
                                                  surface_color, endpoint,
                                                  kHeaderPrimaryMinimumContrast)
                     .color,
      .secondary = color_utils::BlendForMinContrast(
                       preferred_secondary, surface_color, endpoint,
                       color_utils::kMinimumReadableContrastRatio)
                       .color,
      .disabled = color_utils::AlphaBlend(endpoint, surface_color,
                                          kHeaderDisabledOpacity),
  };
}

SkColor ResolveBrowserSurfaceFocusStrokeColor(
    SkColor surface_color,
    std::optional<SkColor> theme_accent) {
  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  const SkColor endpoint = color_utils::PickContrastingColor(
      SK_ColorBLACK, SK_ColorWHITE, surface_color);
  SkColor preferred = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderFocusStrokePreferredOpacity);
  if (theme_accent) {
    preferred = color_utils::AlphaBlend(*theme_accent, preferred, 0.18f);
  }
  return color_utils::BlendForMinContrast(
             preferred, surface_color, endpoint,
             color_utils::kMinimumVisibleContrastRatio)
      .color;
}

SkColor ResolveBrowserSurfaceSemanticColor(SkColor surface, SkColor semantic) {
  return color_utils::BlendForMinContrast(
             semantic, surface,
             color_utils::PickContrastingColor(SK_ColorBLACK, SK_ColorWHITE,
                                               surface),
             color_utils::kMinimumReadableContrastRatio)
      .color;
}

BrowserSurfaceBookmarkColors ResolveBrowserSurfaceBookmarkColors(
    SkColor surface) {
  return {
      .fill = SkColorSetRGB(0xE7, 0xAD, 0x48),
      .outline = color_utils::BlendForMinContrast(
                     SkColorSetRGB(0xBD, 0x84, 0x2B), surface,
                     color_utils::PickContrastingColor(SK_ColorBLACK,
                                                       SK_ColorWHITE, surface),
                     color_utils::kMinimumVisibleContrastRatio)
                     .color,
  };
}

SkColor ResolveBrowserSurfaceCaptionGlyph(SkColor surface,
                                          SkColor background_overlay,
                                          SkColor preferred_glyph) {
  const SkColor background =
      color_utils::GetResultingPaintColor(background_overlay, surface);
  return color_utils::BlendForMinContrast(
             preferred_glyph, background,
             color_utils::PickContrastingColor(SK_ColorBLACK, SK_ColorWHITE,
                                               background),
             color_utils::kMinimumVisibleContrastRatio)
      .color;
}

BrowserSurfacePresentation ResolveBrowserSurfacePresentation(
    SkColor surface,
    uint64_t source_id,
    uint64_t revision,
    uint64_t popup_revision,
    std::optional<SkColor> theme_accent) {
  surface = SkColorSetA(surface, SK_AlphaOPAQUE);
  const BrowserSurfaceHeaderColors colors =
      ResolveBrowserSurfaceHeaderColors(surface);
  const SkColor endpoint = color_utils::GetColorWithMaxContrast(surface);
  const SkColor text_endpoint =
      color_utils::PickContrastingColor(SK_ColorBLACK, SK_ColorWHITE, surface);
  const SkColor opposite_ink =
      text_endpoint == SK_ColorBLACK ? SK_ColorWHITE : SK_ColorBLACK;
  SkColor tint_endpoint = endpoint;
  if (color_utils::GetContrastRatio(
          text_endpoint,
          color_utils::AlphaBlend(tint_endpoint, surface,
                                  kOmniboxPopupSelectedOpacity)) <
      color_utils::kMinimumReadableContrastRatio) {
    tint_endpoint = opposite_ink;
  }
  SkColor popup_hover = color_utils::AlphaBlend(tint_endpoint, surface,
                                                kOmniboxPopupHoverOpacity);
  SkColor popup_selected = color_utils::AlphaBlend(
      tint_endpoint, surface, kOmniboxPopupSelectedOpacity);
  SkColor location_hover =
      color_utils::AlphaBlend(endpoint, surface, kLocationHoverOpacity);
  if (color_utils::GetContrastRatio(colors.secondary, location_hover) <
      color_utils::kMinimumReadableContrastRatio) {
    location_hover = color_utils::AlphaBlend(opposite_ink, surface,
                                            kLocationHoverOpacity);
    if (location_hover == surface) {
      // 8비트 반올림으로 hover가 사라지면 같은 방향으로 한 단계 이동한다.
      const auto step = [delta = opposite_ink == SK_ColorWHITE ? 1 : -1](
                            unsigned int channel) {
        return std::clamp(static_cast<int>(channel) + delta, 0, 255);
      };
      location_hover = SkColorSetRGB(step(SkColorGetR(surface)),
                                     step(SkColorGetG(surface)),
                                     step(SkColorGetB(surface)));
    }
  }
  if (theme_accent) {
    const SkColor neutral_hover = popup_hover;
    const SkColor neutral_selected = popup_selected;
    const SkColor neutral_location_hover = location_hover;
    location_hover =
        color_utils::AlphaBlend(*theme_accent, location_hover, 0.06f);
    for (SkColor ink : {colors.primary, colors.secondary}) {
      location_hover = color_utils::BlendForMinContrast(
                           location_hover, ink, neutral_location_hover,
                           color_utils::kMinimumReadableContrastRatio)
                           .color;
    }
    if (location_hover == surface) {
      location_hover = neutral_location_hover;
    }
    popup_hover = color_utils::AlphaBlend(*theme_accent, popup_hover, 0.06f);
    popup_selected =
        color_utils::AlphaBlend(*theme_accent, popup_selected, 0.10f);
    popup_hover = color_utils::BlendForMinContrast(
                      popup_hover, text_endpoint, neutral_hover,
                      color_utils::kMinimumReadableContrastRatio)
                      .color;
    popup_selected = color_utils::BlendForMinContrast(
                         popup_selected, text_endpoint, neutral_selected,
                         color_utils::kMinimumReadableContrastRatio)
                         .color;
    if (popup_hover == surface || popup_selected == surface ||
        popup_hover == popup_selected) {
      popup_hover = neutral_hover;
      popup_selected = neutral_selected;
    }
  }
  // 편집 중에도 페이지 바탕을 유지하므로 focus 선과 선택 텍스트도 같은 바탕의
  // 대비 계약을 따른다.
  SkColor focus_stroke =
      ResolveBrowserSurfaceFocusStrokeColor(surface, theme_accent);
  for (SkColor background :
       {surface, location_hover, popup_hover, popup_selected}) {
    focus_stroke = color_utils::BlendForMinContrast(
                       focus_stroke, background, text_endpoint,
                       color_utils::kMinimumVisibleContrastRatio)
                       .color;
  }
  const SkColor editor_selection_background = focus_stroke;
  const SkColor editor_selection_foreground =
      ResolveBrowserSurfaceHeaderColors(editor_selection_background).primary;
  // Protect the strongest state by adjusting popup ink, not by washing the
  // state fills out to white/black. Share this ink across popup rows and native
  // answer/chip children so moving selection never changes text polarity.
  auto selected_colors = colors;
  for (SkColor background : {popup_hover, popup_selected}) {
    selected_colors.primary =
        color_utils::BlendForMinContrast(
            selected_colors.primary, background, text_endpoint,
            color_utils::kMinimumReadableContrastRatio)
            .color;
    selected_colors.secondary =
        color_utils::BlendForMinContrast(
            selected_colors.secondary, background, text_endpoint,
            color_utils::kMinimumReadableContrastRatio)
            .color;
  }

  SkColor outline_background = surface;
  SkColor button_overlay = text_endpoint;
  // WebUI는 hover 위에 눌림 색을 얹는다.
  const auto pressed_background = [](SkColor background, SkColor overlay) {
    const SkColor hover = color_utils::AlphaBlend(
        overlay, background, kOmniboxPopupButtonHoverAlpha);
    return color_utils::AlphaBlend(overlay, hover,
                                   kOmniboxPopupButtonPressedAlpha);
  };
  for (SkColor background : {surface, popup_hover, popup_selected}) {
    if (color_utils::GetContrastRatio(text_endpoint, background) <
        color_utils::GetContrastRatio(text_endpoint, outline_background)) {
      outline_background = background;
    }
    if (color_utils::GetContrastRatio(
            text_endpoint, pressed_background(background, text_endpoint)) <
        color_utils::kMinimumReadableContrastRatio) {
      button_overlay = opposite_ink;
    }
  }
  // 검정·흰색 페이지에서 강조가 사라지지 않도록 합성된 면에 글자와 링을 맞춘다.
  for (SkColor row : {surface, popup_hover, popup_selected}) {
    const SkColor pressed = pressed_background(row, button_overlay);
    selected_colors.primary =
        color_utils::BlendForMinContrast(
            selected_colors.primary, pressed, text_endpoint,
            color_utils::kMinimumReadableContrastRatio)
            .color;
    focus_stroke =
        color_utils::BlendForMinContrast(
            focus_stroke, pressed, text_endpoint,
            color_utils::kMinimumVisibleContrastRatio)
            .color;
  }
  const SkColor popup_outline = color_utils::BlendForMinContrast(
                                    outline_background, outline_background,
                                    text_endpoint, kOmniboxPopupOutlineContrast)
                                    .color;
  const SkColor separator = ResolveSurfaceSeparatorColor(surface);
  return {
      .palette_mode = BrowserSurfacePresentation::PaletteMode::kCustomSurface,
      .source_id = source_id,
      .revision = revision,
      .popup_revision = popup_revision,
      .surface = surface,
      .primary = colors.primary,
      .secondary = colors.secondary,
      .disabled = colors.disabled,
      .location_hover = location_hover,
      .focus_stroke = focus_stroke,
      .editor_selection_background = editor_selection_background,
      .editor_selection_foreground = editor_selection_foreground,
      .header_separator = separator,
      .resting_divider = separator,
      .popup_hover = popup_hover,
      .popup_selected = popup_selected,
      .popup_selected_primary = selected_colors.primary,
      .popup_selected_secondary = selected_colors.secondary,
      .popup_outline = popup_outline,
      .popup_button_hover = SkColorSetA(button_overlay,
                                       kOmniboxPopupButtonHoverAlpha),
      .popup_button_pressed = SkColorSetA(button_overlay,
                                         kOmniboxPopupButtonPressedAlpha),
  };
}

BrowserSurfacePresentation UseNativeBrowserSurfaceColors(
    const BrowserSurfacePresentation& presentation) {
  BrowserSurfacePresentation native = presentation;
  native.palette_mode = BrowserSurfacePresentation::PaletteMode::kNativeColors;
  return native;
}

SkColor ResolveBrowserSurfaceSeparatorColor(SkColor surface_color) {
  return ResolveSurfaceSeparatorColor(surface_color);
}

double GetNativeGlassTintOpacity(bool is_dark_mode) {
  return is_dark_mode ? kNativeGlassTintOpacityDark
                      : kNativeGlassTintOpacityLight;
}

}  // namespace yee
