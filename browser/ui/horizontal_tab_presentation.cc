// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/bit_cast.h"
#include "base/check.h"
#include "base/functional/callback.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/grit/theme_resources.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/class_properties.h"
#include "ui/base/class_property.h"
#include "ui/base/theme_provider.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/views/background.h"
#include "ui/views/view.h"

namespace yee {
struct HorizontalTabColorRefreshCallback {
  base::RepeatingClosure callback;
  bool initial_layout_pending = true;
};
}  // namespace yee

DECLARE_UI_CLASS_PROPERTY_TYPE(yee::HorizontalTabColorRefreshCallback*)
DEFINE_UI_CLASS_PROPERTY_TYPE(yee::HorizontalTabColorRefreshCallback*)
DECLARE_UI_CLASS_PROPERTY_TYPE(yee::BrowserSurfaceMode)
DEFINE_UI_CLASS_PROPERTY_TYPE(yee::BrowserSurfaceMode)

namespace yee {
namespace {
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(HorizontalTabColorRefreshCallback,
                                   kHorizontalTabColorRefreshKey)
DEFINE_UI_CLASS_PROPERTY_KEY(int, kHorizontalTabIconForegroundKey, 0)
DEFINE_UI_CLASS_PROPERTY_KEY(int, kHorizontalTabHeaderSurfaceKey, 0)
DEFINE_UI_CLASS_PROPERTY_KEY(bool, kOwnsHorizontalTabHeaderClipKey, false)
DEFINE_UI_CLASS_PROPERTY_KEY(BrowserSurfaceMode,
                             kAppliedHeaderSurfaceModeKey,
                             BrowserSurfaceMode::kCard)

SkColor ResolveHorizontalTabHeaderSurface(const views::View& header) {
  const int color = header.GetProperty(kHorizontalTabHeaderSurfaceKey);
  return color ? base::bit_cast<SkColor>(color)
               : header.GetColorProvider()->GetColor(kColorToolbar);
}

void RefreshTabColors(views::View& root, bool after_initial_layout) {
  if (auto* refresh = root.GetProperty(kHorizontalTabColorRefreshKey)) {
    if (!after_initial_layout || refresh->initial_layout_pending) {
      if (after_initial_layout) {
        refresh->initial_layout_pending = false;
      }
      refresh->callback.Run();
    }
    return;
  }
  for (views::View* child : root.children()) {
    RefreshTabColors(*child, after_initial_layout);
  }
}
}  // namespace

bool ShouldUseNativeHorizontalTabTheme(const ui::NativeTheme& native_theme,
                                       const ui::ThemeProvider* theme) {
  if (ShouldUseNativeBrowserSurfaceColors(native_theme)) {
    return true;
  }
  if (!theme) {
    return false;
  }
  // native가 frame에서 생성하거나 비활성·시크릿 창에 복사하는 이미지도
  // 포함한다.
  for (int id : {IDR_THEME_TOOLBAR, IDR_THEME_FRAME, IDR_THEME_FRAME_INACTIVE,
                 IDR_THEME_FRAME_INCOGNITO, IDR_THEME_FRAME_INCOGNITO_INACTIVE,
                 IDR_THEME_TAB_BACKGROUND, IDR_THEME_TAB_BACKGROUND_INACTIVE,
                 IDR_THEME_TAB_BACKGROUND_INCOGNITO,
                 IDR_THEME_TAB_BACKGROUND_INCOGNITO_INACTIVE}) {
    if (theme->HasCustomImage(id)) {
      return true;
    }
  }
  return false;
}

gfx::Insets ResolveHorizontalTabToolbarInsets(gfx::Insets insets) {
  const int edge = kSidebarMetrics.browser_surface_header_control_edge_inset -
                   kSidebarMetrics.toolbar_interior_inset -
                   kSidebarMetrics.shell_control_horizontal_margin;
  insets.set_left(edge);
  insets.set_right(edge);
  return insets;
}

void UpdateHorizontalTabHeaderBackground(
    views::View& header,
    const std::optional<BrowserSurfacePresentation>& presentation) {
  auto* const background = header.background()->AsA<CustomCornersBackground>();
  CHECK(background);
  const bool connected = header.GetProperty(kAppliedHeaderSurfaceModeKey) ==
                         BrowserSurfaceMode::kConnectedTabs;
  if (!connected) {
    return;
  }
  const bool custom_surface =
      presentation &&
      !ShouldUseNativeBrowserSurfaceColors(*header.GetNativeTheme()) &&
      presentation->palette_mode ==
          BrowserSurfacePresentation::PaletteMode::kCustomSurface;
  const int color =
      custom_surface ? base::bit_cast<int>(presentation->surface) : 0;
  if (header.GetProperty(kHorizontalTabHeaderSurfaceKey) != color) {
    header.SetProperty(kHorizontalTabHeaderSurfaceKey, color);
    header.SchedulePaint();
  }
  background->SetPrimaryColor(
      CustomCorners::ColorChoice(&ResolveHorizontalTabHeaderSurface));
}

void ApplyBrowserSurfaceHeaderDecoration(
    views::View& header,
    const ResolvedBrowserSurfaceDecoration& decoration,
    const gfx::Rect& paint_bounds,
    const std::optional<BrowserSurfacePresentation>& presentation) {
  header.SetProperty(kAppliedHeaderSurfaceModeKey, decoration.mode);
  const bool connected = decoration.mode == BrowserSurfaceMode::kConnectedTabs;
  auto* const background = header.background()->AsA<CustomCornersBackground>();
  CHECK(background);
  background->SetVisible(connected);
  if (connected) {
    CustomCornersBackground::Corners corners;
    for (auto corner :
         {CornerOrientation::kTopLeading, CornerOrientation::kTopTrailing}) {
      corners[corner] = {CustomCornersBackground::CornerType::kRounded,
                         kSidebarMetrics.content_corner_radius};
    }
    background->SetCorners(corners);
    background->SetOutline({});
    background->SetCutoutFrom({});
    background->SetUseBackgroundBlur(false);
  }
  UpdateHorizontalTabHeaderBackground(header, presentation);
  if (!connected && !header.GetProperty(kOwnsHorizontalTabHeaderClipKey)) {
    return;
  }

  const SkRect bounds = gfx::RectToSkRect(paint_bounds);
  SkPath clip;
  if (connected) {
    const float radius = kSidebarMetrics.content_corner_radius;
    const SkVector radii[4] = {
        {radius, radius}, {radius, radius}, {0, 0}, {0, 0}};
    SkRRect rounded;
    rounded.setRectRadii(bounds, radii);
    clip = SkPath::RRect(rounded);
  } else {
    // 빈 path는 layer가 있는 View를 가린다. 전체 사각형을 유지하고 이후
    // 세로·분할·전체화면 배치에서도 크기를 갱신한다.
    clip = SkPath::Rect(bounds);
  }
  if (header.clip_path() != clip) {
    header.SetClipPath(clip);
  }
  header.SetProperty(kOwnsHorizontalTabHeaderClipKey, true);
}

void SetHorizontalTabColorRefresh(views::View& tab,
                                  base::RepeatingClosure callback) {
  if (!callback) {
    tab.ClearProperty(kHorizontalTabColorRefreshKey);
    return;
  }
  tab.SetProperty(kHorizontalTabColorRefreshKey,
                  std::make_unique<HorizontalTabColorRefreshCallback>(
                      HorizontalTabColorRefreshCallback{std::move(callback)}));
}

void RefreshHorizontalTabColors(views::View& root) {
  RefreshTabColors(root, false);
}

void InitializeHorizontalTabColorsAfterLayout(views::View& root) {
  RefreshTabColors(root, true);
}

void SetHorizontalTabIconForeground(views::View& icon, SkColor foreground) {
  const int value = base::bit_cast<int>(foreground);
  if (icon.GetProperty(kHorizontalTabIconForegroundKey) != value) {
    icon.SetProperty(kHorizontalTabIconForegroundKey, value);
    icon.SchedulePaint();
  }
}

std::optional<SkColor> GetHorizontalTabIconForeground(const views::View& icon) {
  const int value = icon.GetProperty(kHorizontalTabIconForegroundKey);
  return value ? std::make_optional(base::bit_cast<SkColor>(value))
               : std::nullopt;
}

HorizontalTabColors ResolveHorizontalTabColors(
    const ui::ColorProvider& color_provider,
    const BrowserSurfacePresentation& presentation,
    bool active,
    bool selected,
    double hover_progress,
    bool frame_active,
    std::optional<SkColor> group_color,
    bool split) {
  if (active) {
    return {presentation.surface, presentation.primary};
  }
  const SidebarItemColors colors = ResolveSidebarItemColors(
      color_provider,
      selected ? SidebarItemVisualState::kActive
               : (hover_progress > 0.0 ? SidebarItemVisualState::kHovered
                                       : SidebarItemVisualState::kResting),
      hover_progress, frame_active, /*persistent_surface=*/false);
  SkColor background = color_utils::GetResultingPaintColor(
      colors.fill, ResolveShellBackgroundColor(color_provider, frame_active));
  if (split) {
    background = color_utils::AlphaBlend(colors.foreground, background, 0.04f);
  }
  if (group_color) {
    background =
        color_utils::AlphaBlend(*group_color, background,
                                kSidebarMetrics.horizontal_group_tint_opacity);
  }
  return {background, color_utils::BlendForMinContrast(
                          colors.foreground, background, std::nullopt,
                          color_utils::kMinimumReadableContrastRatio)
                          .color};
}

void PaintHorizontalSplitTabDivider(gfx::Canvas& canvas,
                                    const gfx::RectF& bounds,
                                    const HorizontalTabColors& colors) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(
      color_utils::AlphaBlend(colors.foreground, colors.background, 0.16f));
  canvas.DrawRoundRect(bounds, bounds.width() / 2.f, flags);
}

bool ShouldShowHorizontalSplitTabCloseButton(int available_width,
                                             int leading_icon_width,
                                             int close_button_width,
                                             bool close_button_focused) {
  return close_button_focused ||
         available_width >= leading_icon_width + close_button_width;
}

SkColor ResolveHorizontalTabSeparatorColor(
    const ui::ColorProvider& color_provider,
    bool frame_active) {
  const auto colors = ResolveSidebarItemColors(
      color_provider, SidebarItemVisualState::kResting, 0.0, frame_active,
      /*persistent_surface=*/false);
  return SkColorSetA(colors.foreground, 0x40);
}

}  // namespace yee
