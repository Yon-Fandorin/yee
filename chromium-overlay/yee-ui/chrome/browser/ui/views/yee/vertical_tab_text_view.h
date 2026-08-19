// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_TAB_TEXT_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_TAB_TEXT_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class TabTitle;

namespace yee {

// Owns the vertical tab's single-line title presentation. Long titles fade
// on the trailing edge. Hostname/URL belong on the hover card, not the row.
// TabView still computes the displayed title and color and keeps tab model,
// selection, drag, and accessibility ownership.
class VerticalTabTextView : public views::View {
  METADATA_HEADER(VerticalTabTextView, views::View)

 public:
  VerticalTabTextView();
  VerticalTabTextView(const VerticalTabTextView&) = delete;
  VerticalTabTextView& operator=(const VerticalTabTextView&) = delete;
  ~VerticalTabTextView() override;

  void SetTitle(const std::u16string& title);
  void SetHostname(const std::u16string& hostname);
  void SetColors(SkColor title_color, SkColor subtitle_color);

  TabTitle* title() { return title_; }
  const TabTitle* title() const { return title_; }

 private:
  raw_ptr<TabTitle> title_ = nullptr;
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_TAB_TEXT_VIEW_H_
