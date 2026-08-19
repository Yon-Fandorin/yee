// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/vertical_tab_text_view.h"

#include "chrome/browser/ui/views/tabs/tab/tab_title.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/views/layout/flex_layout.h"

namespace yee {

VerticalTabTextView::VerticalTabTextView() {
  SetCanProcessEventsWithinSubtree(false);
  auto* text_layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  text_layout->SetOrientation(views::LayoutOrientation::kVertical)
      .SetMainAxisAlignment(views::LayoutAlignment::kCenter)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStretch);

  title_ = AddChildView(std::make_unique<TabTitle>());
  title_->SetElideBehavior(gfx::FADE_TAIL);
  title_->SetSubpixelRenderingEnabled(false);
  title_->SetFontList(title_->font_list().Derive(
      kSidebarMetrics.tab_title_font_delta, gfx::Font::NORMAL,
      gfx::Font::Weight::MEDIUM));
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

BEGIN_METADATA(VerticalTabTextView)
END_METADATA

}  // namespace yee
