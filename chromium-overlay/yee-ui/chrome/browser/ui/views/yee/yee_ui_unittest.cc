// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"

namespace yee {
namespace {

TEST(YeeSurfaceColorTest, HeaderRolesRemainReadableOnLightAndDarkPages) {
  for (SkColor surface :
       {SkColorSetRGB(0xFA, 0xF4, 0xE5), SkColorSetRGB(0x18, 0x1A, 0x1F)}) {
    const BrowserSurfaceHeaderColors colors =
        ResolveBrowserSurfaceHeaderColors(surface);
    EXPECT_EQ(SkColorGetA(colors.primary), SK_AlphaOPAQUE);
    EXPECT_EQ(SkColorGetA(colors.secondary), SK_AlphaOPAQUE);
    EXPECT_GE(color_utils::GetContrastRatio(colors.primary, surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_GE(color_utils::GetContrastRatio(colors.secondary, surface),
              color_utils::kMinimumVisibleContrastRatio);
    EXPECT_LT(color_utils::GetContrastRatio(colors.disabled, surface),
              color_utils::GetContrastRatio(colors.primary, surface));
  }
}

TEST(YeeSurfaceColorTest, FocusStrokeKeepsNonTextContrast) {
  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7A, 0x75, 0x6C)}) {
    const SkColor stroke = ResolveBrowserSurfaceFocusStrokeColor(surface);
    EXPECT_GE(color_utils::GetContrastRatio(stroke, surface),
              color_utils::kMinimumVisibleContrastRatio);
  }
}

TEST(YeeSurfaceColorTest, OmniboxPopupThemeIdentityIsStablePerOpaqueColor) {
  auto* first = GetBrowserSurfaceOmniboxPopupTheme(
      SkColorSetARGB(0x40, 0x12, 0x34, 0x56));
  auto* same_opaque = GetBrowserSurfaceOmniboxPopupTheme(
      SkColorSetARGB(0xFF, 0x12, 0x34, 0x56));
  auto* different =
      GetBrowserSurfaceOmniboxPopupTheme(SkColorSetRGB(0x12, 0x34, 0x57));

  EXPECT_EQ(first, same_opaque);
  EXPECT_NE(first, different);
}

TEST(YeeSurfaceGeometryTest, SplitAndSingleSurfacesShareCornerContract) {
  const gfx::RoundedCornersF corners = ResolveSplitPaneRoundedCorners();
  EXPECT_EQ(corners.upper_left(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.upper_right(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.lower_right(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.lower_left(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(kSidebarMetrics.split_pane_inner_corner_radius,
            kSidebarMetrics.content_corner_radius -
                kSidebarMetrics.split_pane_content_stroke_inset);
}

TEST(YeeSurfaceGeometryTest, HoverCardAnchorOnlyAddsContentSideClearance) {
  const gfx::Rect original(10, 20, 100, 32);
  const gfx::Rect adjusted = AdjustVerticalTabHoverCardAnchor(original);

  EXPECT_EQ(adjusted.x(), original.x());
  EXPECT_EQ(adjusted.y(), original.y());
  EXPECT_EQ(adjusted.height(), original.height());
  EXPECT_EQ(adjusted.right(),
            original.right() + kSidebarMetrics.tab_hover_card_offset);
}

TEST(YeeSurfaceColorTest, DarkGlassUsesStrongerNativeTint) {
  const double light = GetNativeGlassTintOpacity(/*is_dark_mode=*/false);
  const double dark = GetNativeGlassTintOpacity(/*is_dark_mode=*/true);
  EXPECT_GT(light, 0.0);
  EXPECT_LT(dark, 1.0);
  EXPECT_GT(dark, light);
}

TEST(YeeSidebarItemColorTest, EveryStateSharesOneThemeAwarePalette) {
  for (const bool dark : {false, true}) {
    ui::ColorProvider provider;
    const SkColor elevated = dark ? SkColorSetRGB(0x2B, 0x2D, 0x31)
                                  : SkColorSetRGB(0xF7, 0xF8, 0xFA);
    const SkColor inactive_surface =
        dark ? SkColorSetRGB(0x22, 0x23, 0x27)
             : SkColorSetRGB(0xF0, 0xF1, 0xF3);
    const SkColor outline = dark ? SkColorSetRGB(0xA8, 0xAA, 0xB0)
                                 : SkColorSetRGB(0x5E, 0x60, 0x66);
    const SkColor foreground = dark ? SK_ColorWHITE : SK_ColorBLACK;
    const SkColor inactive_foreground =
        dark ? SkColorSetRGB(0xC4, 0xC6, 0xCC)
             : SkColorSetRGB(0x55, 0x57, 0x5D);
    const SkColor hover = SkColorSetARGB(
        0x28, SkColorGetR(foreground), SkColorGetG(foreground),
        SkColorGetB(foreground));
    provider.SetColorForTesting(ui::kColorSysBaseContainerElevated, elevated);
    provider.SetColorForTesting(ui::kColorSysBaseContainer,
                                inactive_surface);
    provider.SetColorForTesting(ui::kColorSysNeutralOutline, outline);
    provider.SetColorForTesting(ui::kColorSysOnSurface, foreground);
    provider.SetColorForTesting(ui::kColorSysOnSurfaceSecondary,
                                inactive_foreground);
    provider.SetColorForTesting(ui::kColorSysStateHoverOnSubtle, hover);

    const SidebarItemColors resting_row = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kResting, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(resting_row.fill)));
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(resting_row.stroke)));
    EXPECT_EQ(foreground, resting_row.foreground);

    const SidebarItemColors resting_favorite = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kResting, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/true);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_fill_alpha,
              static_cast<int>(SkColorGetA(resting_favorite.fill)));
    EXPECT_EQ(kSidebarMetrics.favorites_cell_stroke_alpha,
              static_cast<int>(SkColorGetA(resting_favorite.stroke)));

    const SidebarItemColors hovered_row = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(static_cast<int>(SkColorGetA(hover)),
              static_cast<int>(SkColorGetA(hovered_row.fill)));
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(hovered_row.stroke)));

    const SidebarItemColors hovered_favorite = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
        /*frame_active=*/true, /*persistent_surface=*/true);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_hover_fill_alpha,
              static_cast<int>(SkColorGetA(hovered_favorite.fill)));
    EXPECT_GT(static_cast<int>(SkColorGetA(hovered_favorite.stroke)),
              static_cast<int>(SkColorGetA(resting_favorite.stroke)));

    const SidebarItemColors active = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kActive, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    const SidebarItemColors dragging = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kDragging, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_active_fill_alpha,
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_EQ(kSidebarMetrics.favorites_cell_active_stroke_alpha,
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.fill)),
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.stroke)),
              static_cast<int>(SkColorGetA(active.stroke)));

    const SidebarItemColors inactive = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kActive, /*hover_progress=*/0.0,
        /*frame_active=*/false, /*persistent_surface=*/false);
    EXPECT_LT(static_cast<int>(SkColorGetA(inactive.fill)),
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_LT(static_cast<int>(SkColorGetA(inactive.stroke)),
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_EQ(inactive_foreground, inactive.foreground);
  }
}

TEST(YeeSidebarItemColorTest, HoverProgressClampsAtBothEnds) {
  ui::ColorProvider provider;
  provider.SetColorForTesting(ui::kColorSysBaseContainerElevated,
                              SK_ColorWHITE);
  provider.SetColorForTesting(ui::kColorSysBaseContainer, SK_ColorWHITE);
  provider.SetColorForTesting(ui::kColorSysNeutralOutline, SK_ColorBLACK);
  provider.SetColorForTesting(ui::kColorSysOnSurface, SK_ColorBLACK);
  provider.SetColorForTesting(ui::kColorSysOnSurfaceSecondary, SK_ColorGRAY);
  provider.SetColorForTesting(ui::kColorSysStateHoverOnSubtle,
                              SkColorSetARGB(0x30, 0, 0, 0));

  const SidebarItemColors below_zero = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/-1.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors at_zero = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/0.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors above_one = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/2.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors at_one = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
      /*frame_active=*/true, /*persistent_surface=*/true);

  EXPECT_EQ(at_zero.fill, below_zero.fill);
  EXPECT_EQ(at_zero.stroke, below_zero.stroke);
  EXPECT_EQ(at_one.fill, above_one.fill);
  EXPECT_EQ(at_one.stroke, above_one.stroke);
}

}  // namespace
}  // namespace yee
