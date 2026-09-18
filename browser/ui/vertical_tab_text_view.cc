// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/vertical_tab_text_view.h"

#include <algorithm>

#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"

namespace yee {
namespace {

constexpr SkColor kAgentWorkingColor = SkColorSetRGB(15, 108, 92);
constexpr SkColor kAgentNeedsInputColor = SkColorSetRGB(166, 105, 48);

}  // namespace

VerticalTabTextView::VerticalTabTextView() {
  SetCanProcessEventsWithinSubtree(false);
  title_ = AddChildView(std::make_unique<TabTitle>());
  title_->SetElideBehavior(gfx::FADE_TAIL);
  title_->SetSubpixelRenderingEnabled(false);
  title_->SetFontList(
      title_->font_list().Derive(kSidebarMetrics.tab_title_font_delta,
                                 gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
  title_->SetLineHeight(kSidebarMetrics.tab_title_line_height);
}

VerticalTabTextView::~VerticalTabTextView() = default;

void VerticalTabTextView::SetTitle(const std::u16string& title) {
  title_->SetText(title);
}

void VerticalTabTextView::SetHostname(const std::u16string&) {
  // Hostname stays on the hover card so the tab row can fade a single title.
}

void VerticalTabTextView::SetColors(SkColor title_color, SkColor) {
  title_->SetEnabledColor(title_color);
}

void VerticalTabTextView::SetAgentActivity(AgentTabActivity activity) {
  if (agent_activity_ == activity) {
    return;
  }
  agent_activity_ = activity;
  InvalidateLayout();
  SchedulePaint();
}

void VerticalTabTextView::Layout(PassKey) {
  const int indicator_slot =
      agent_activity_ == AgentTabActivity::kNone
          ? 0
          : kSidebarMetrics.tab_agent_indicator_slot_width;
  title_->SetBoundsRect(GetMirroredRect(
      gfx::Rect(0, 0, std::max(0, width() - indicator_slot), height())));
}

void VerticalTabTextView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
  if (agent_activity_ == AgentTabActivity::kNone) {
    return;
  }

  const int size = kSidebarMetrics.tab_agent_indicator_size;
  if (width() < kSidebarMetrics.tab_agent_indicator_slot_width) {
    return;
  }
  const gfx::Rect bounds = GetMirroredRect(gfx::Rect(
      width() - kSidebarMetrics.tab_agent_indicator_trailing_inset - size,
      (height() - size) / 2, size, size));
  if (GetColorProvider())
    PaintAgentActivityBadge(canvas, bounds, agent_activity_,
                            *GetColorProvider());
}

void PaintAgentActivityBadge(gfx::Canvas* canvas,
                             const gfx::Rect& bounds,
                             AgentTabActivity activity,
                             const ui::ColorProvider& color_provider) {
  if (activity == AgentTabActivity::kNone)
    return;
  const int size = bounds.width();
  const SkColor surface = ResolveShellContrastBackground(color_provider);
  const SkColor state_color =
      color_utils::BlendForMinContrast(activity == AgentTabActivity::kWorking
                                           ? kAgentWorkingColor
                                           : kAgentNeedsInputColor,
                                       surface, std::nullopt, 4.5)
          .color;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  if (activity == AgentTabActivity::kWorking) {
    flags.setColor(SkColorSetA(state_color, 48));
    canvas->DrawCircle(bounds.CenterPoint(), size / 2.0f, flags);
    flags.setColor(state_color);
    canvas->DrawCircle(bounds.CenterPoint(), 3.0f, flags);
    return;
  }

  flags.setColor(state_color);
  canvas->DrawRoundRect(gfx::RectF(bounds), size / 2.0f, flags);
  flags.setColor(color_utils::GetColorWithMaxContrast(state_color));
  canvas->DrawRect(
      gfx::Rect(bounds.CenterPoint().x() - 1, bounds.y() + 3, 2, 4), flags);
  canvas->DrawCircle(gfx::PointF(bounds.CenterPoint().x(), bounds.bottom() - 3),
                     1.0f, flags);
}

gfx::Size VerticalTabTextView::CalculatePreferredSize(
    const views::SizeBounds&) const {
  return title_->GetPreferredSize();
}

BEGIN_METADATA(VerticalTabTextView)
END_METADATA

}  // namespace yee
