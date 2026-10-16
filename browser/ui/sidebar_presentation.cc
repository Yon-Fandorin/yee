// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/sidebar_presentation.h"

#include <algorithm>

#include "base/check.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace yee {

SidebarFlyoutGeometry ResolveSidebarFlyoutGeometry(double pinned,
                                                   double hover,
                                                   bool toolbar_visible) {
  pinned = std::clamp(pinned, 0.0, 1.0);
  hover = std::clamp(hover, 0.0, 1.0);
  const int caption_height =
      toolbar_visible ? kSidebarMetrics.titlebar_height : 0;
  const int top_gap = toolbar_visible ? kSidebarMetrics.content_gutter
                                      : kSidebarMetrics.sidebar_flyout_inset;
  return {
      .leading_offset =
          base::ClampRound(kSidebarMetrics.sidebar_flyout_inset * hover),
      .top_inset =
          base::ClampRound(caption_height * (1.0 - pinned) + top_gap * hover),
      .header_inset = base::ClampRound(kSidebarMetrics.content_gutter * hover),
      .hit_test_top_inset = base::ClampRound(top_gap * hover),
      .bottom_inset = base::ClampRound((kSidebarMetrics.sidebar_flyout_inset -
                                        kSidebarMetrics.content_gutter) *
                                       hover),
  };
}

SkColor ResolveSidebarFlyoutColor(const ui::ColorProvider& colors,
                                  bool active) {
  const SkColor shell = ResolveShellBackgroundColor(colors, active);
  const SkColor surface =
      color_utils::AlphaBlend(shell, colors.GetColor(ui::kColorSysBase), 0.08f);
  const SkColor endpoint = color_utils::GetColorWithMaxContrast(shell);
  // 고정·hover가 같은 글자 역할을 사용하므로 기존 대비를 낮추지 않는다.
  return color_utils::GetContrastRatio(endpoint, surface) >=
                 color_utils::GetContrastRatio(endpoint, shell)
             ? surface
             : shell;
}

SkColor ResolveSidebarSurfaceColor(const views::View& view) {
  const auto* const colors = view.GetColorProvider();
  CHECK(colors);
  const bool active =
      !view.GetWidget() || view.GetWidget()->ShouldPaintAsActive();
  return ResolveSidebarFlyoutColor(*colors, active);
}

double ResolveSidebarReveal(double pinned, double hover) {
  return std::clamp(pinned + hover, 0.0, 1.0);
}

SidebarFlyoutPresentation ResolveSidebarFlyoutPresentation(double reveal) {
  reveal = std::clamp(reveal, 0.0, 1.0);
  return {
      .surface_opacity = static_cast<float>(reveal),
      .shadow_opacity = static_cast<float>(reveal),
      .corner_radius =
          base::ClampRound(reveal * kSidebarMetrics.content_corner_radius),
      .outline_color = ui::kColorSysNeutralOutline,
      .outline_opacity = static_cast<float>(0.35 * reveal),
  };
}

void UpdateSidebarExpandedPresentation(views::View* header,
                                       views::View* footer,
                                       bool expanded) {
  if (header) {
    SetSidebarHeaderActionsControlsVisible(*header, expanded);
  }
  if (footer) {
    SetSidebarFooterVisible(*footer, expanded);
  }
}

}  // namespace yee
