// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_PRESENTATION_H_

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"

namespace views {
class View;
}
namespace ui {
class ColorProvider;
}

namespace yee {

// 호스트가 제공하는 잠금을 팝업 수명 동안 보유한다. 팝업은 Chromium의
// 사이드바 상태나 잠금 구현을 알 필요가 없다.
using SidebarPopupLockCallback =
    base::RepeatingCallback<base::ScopedClosureRunner()>;

struct SidebarFlyoutShadow {
  int elevation;
  double light_key;
  double light_ambient;
  double dark_key;
  double dark_ambient;
};

inline constexpr SidebarFlyoutShadow kSidebarFlyoutShadow{.elevation = 8,
                                                          .light_key = 0.12,
                                                          .light_ambient = 0.06,
                                                          .dark_key = 0.32,
                                                          .dark_ambient = 0.14};

struct SidebarFlyoutPresentation {
  float surface_opacity;
  float shadow_opacity;
  int corner_radius;
  ui::ColorId outline_color;
  float outline_opacity;
};

struct SidebarFlyoutGeometry {
  int leading_offset = 0;
  int top_inset = 0;
  int header_inset = 0;
  // 패널 바로 위의 이동 통로만 포함하고 창 버튼 영역은 제외한다.
  int hit_test_top_inset = 0;
  // native 레이아웃의 기본 하단 gutter에 추가하는 여백이다.
  int bottom_inset = 0;
};

// 창 버튼 아래에서 열며 페이지 폭과 native caption 기준선은 유지한다.
SidebarFlyoutGeometry ResolveSidebarFlyoutGeometry(double pinned,
                                                   double hover,
                                                   bool toolbar_visible);

SkColor ResolveSidebarFlyoutColor(const ui::ColorProvider& colors, bool active);

// 테마나 창 활성화 상태가 바뀌면 열린 flyout에도 현재 색을 반영한다.
SkColor ResolveSidebarSurfaceColor(const views::View& view);

// Hover에서 고정으로 전환할 때 두 트랙이 서로 교차하므로 합산한다.
double ResolveSidebarReveal(double pinned, double hover);

SidebarFlyoutPresentation ResolveSidebarFlyoutPresentation(double reveal);

// 숨긴 가장자리 영역은 Header 높이를 유지하며, 고정과 hover 펼침 상태는
// 동일한 Header 조작과 Footer를 표시한다.
void UpdateSidebarExpandedPresentation(views::View* header,
                                       views::View* footer,
                                       bool expanded);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_PRESENTATION_H_
