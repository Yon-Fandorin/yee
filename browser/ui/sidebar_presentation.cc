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
#include "ui/gfx/geometry/insets.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace yee {

SidebarFlyoutGeometry ResolveSidebarFlyoutGeometry(double pinned,
                                                   double hover,
                                                   bool toolbar_visible,
                                                   bool toolbar_above_content) {
  pinned = std::clamp(pinned, 0.0, 1.0);
  hover = std::clamp(hover, 0.0, 1.0);
  const int caption_height =
      toolbar_visible ? kSidebarMetrics.titlebar_height : 0;
  const int top_gap = (toolbar_visible || toolbar_above_content)
                          ? kSidebarMetrics.content_gutter
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

gfx::Rect AllocateSidebarControls(const views::View& sidebar,
                                  views::View* header,
                                  views::View* footer,
                                  views::ProposedLayout& layout,
                                  gfx::Rect available,
                                  bool height_bounded,
                                  int content_trailing_margin) {
  const auto preferred_height = [&](const views::View* view, int width) {
    if (!view || view->parent() != &sidebar || !view->GetVisible()) {
      return 0;
    }
    const views::SizeBounds bounds(width, {});
    if (const auto* flex = view->GetProperty(views::kFlexBehaviorKey)) {
      // immersive Header가 host를 옮기는 동안 사용하는 reveal 규칙을 유지한다.
      return flex->rule().Run(view, bounds).height();
    }
    return view->GetPreferredSize(bounds).height();
  };
  const bool footer_visible =
      footer && footer->parent() == &sidebar && footer->GetVisible();
  const auto* margins =
      footer_visible ? footer->GetProperty(views::kMarginsKey) : nullptr;
  const gfx::Insets footer_margins = margins ? *margins : gfx::Insets();
  const int footer_width =
      std::max(0, available.width() - footer_margins.width());
  const int header_height = preferred_height(header, available.width());
  const int footer_height = preferred_height(footer, footer_width);
  // native 본문 안의 탭 하단 여백과 Footer 상단 여백을 중복 예약하지 않는다.
  const int footer_spacing =
      std::max(0, footer_margins.top() - content_trailing_margin);
  const int footer_allocation =
      footer_height + footer_spacing + footer_margins.bottom();
  if (!height_bounded) {
    layout.host_size.Enlarge(0, header_height + footer_allocation);
    available.set_height(available.height() + header_height +
                         footer_allocation);
  }
  if (header && header->parent() == &sidebar) {
    const int height = std::min(header_height, available.height());
    layout.child_layouts.emplace_back(
        header, header->GetVisible(),
        gfx::Rect(available.x(), available.y(), available.width(), height));
    available.Inset(gfx::Insets::TLBR(height, 0, 0, 0));
  }
  if (footer && footer->parent() == &sidebar) {
    const int height = std::min(
        footer_height, std::max(0, available.height() - footer_spacing -
                                       footer_margins.bottom()));
    layout.child_layouts.emplace_back(
        footer, footer->GetVisible(),
        gfx::Rect(available.x() + footer_margins.left(),
                  available.bottom() - footer_margins.bottom() - height,
                  footer_width, height));
    available.Inset(gfx::Insets::TLBR(
        0, 0, height + footer_spacing + footer_margins.bottom(), 0));
  }
  return available;
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
