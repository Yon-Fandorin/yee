// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_HORIZONTAL_TAB_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_HORIZONTAL_TAB_PRESENTATION_H_

#include "base/functional/callback_forward.h"
#include "chrome/browser/ui/views/yee/browser_surface_layout.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "ui/color/color_id.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace gfx {
class Canvas;
}

namespace ui {
class ColorProvider;
class NativeTheme;
class ThemeProvider;
}  // namespace ui

namespace views {
class View;
}

namespace yee {

// 이미지 테마와 접근성 팔레트에서는 탭·그룹·새 탭 버튼에 native 스타일을 쓴다.
bool ShouldUseNativeHorizontalTabTheme(const ui::NativeTheme& native_theme,
                                       const ui::ThemeProvider* theme);

gfx::Insets ResolveHorizontalTabToolbarInsets(gfx::Insets insets);
// native의 겹침 폭을 제외해도 인접 favicon 사이에 최소 간격을 남긴다.
int ResolveHorizontalTabMinimumInactiveWidth(const views::View& tab,
                                             int native_minimum_width,
                                             int native_overlap);

// 탭 본문의 native 최소 폭에 같은 행의 고정 조작 폭을 한 번만 더한다.
gfx::Size ResolveHorizontalTabStripMinimumSize(const views::View& region,
                                               const views::View& strip,
                                               gfx::Size native_minimum_size);

// 그리기 전에 구조를 Header에 적용한다. 전체화면에서 다른 Widget으로
// 이동한 뒤에도 View가 이 상태를 유지한다.
void ApplyBrowserSurfaceHeaderDecoration(
    views::View& header,
    const ResolvedBrowserSurfaceDecoration& decoration,
    const gfx::Rect& paint_bounds,
    const std::optional<BrowserSurfacePresentation>& presentation);

// 색 갱신은 마지막 구조와 Header clip을 유지하고 부모에서 모드를 추론하지
// 않는다.
void UpdateHorizontalTabHeaderBackground(
    views::View& header,
    const std::optional<BrowserSurfacePresentation>& presentation);
struct HorizontalTabColors {
  SkColor background;
  SkColor foreground;
};

// 선택 탭은 연결된 Header의 페이지색을 따르고, 나머지는 Shell 위의 상태색을
// 쓴다.
HorizontalTabColors ResolveHorizontalTabColors(
    const ui::ColorProvider& color_provider,
    bool active,
    bool selected,
    double hover_progress,
    bool frame_active,
    std::optional<SkColor> group_color = std::nullopt,
    bool split = false,
    std::optional<SkColor> page_surface = std::nullopt);

// 분할 멤버가 공통 구분선의 범위를 canvas 픽셀로 전달한다.
// 불투명한 계산 색으로 어느 멤버를 먼저 그려도 같은 결과를 유지한다.
void PaintHorizontalSplitTabDivider(gfx::Canvas& canvas,
                                    const gfx::RectF& bounds,
                                    const HorizontalTabColors& colors);

// 좁은 분할 탭에서는 파비콘을 우선한다. 닫기 버튼에 키보드 포커스가 있으면
// 계속 표시한다.
bool ShouldShowHorizontalSplitTabCloseButton(int available_width,
                                             int leading_icon_width,
                                             int close_button_width,
                                             bool close_button_focused);

SkColor ResolveHorizontalTabSeparatorColor(
    const ui::ColorProvider& color_provider,
    bool frame_active);

// native 탭은 Widget에 연결된 동안에만 자신의 색 갱신 경로를 등록한다.
void SetHorizontalTabColorRefresh(views::View& tab,
                                  base::RepeatingClosure callback);
void RefreshHorizontalTabColors(views::View& root);
// 새 Widget의 native 탭 상태와 child 배치가 확정된 뒤 초기 색을 한 번 보정한다.
void InitializeHorizontalTabColorsAfterLayout(views::View& root);

// native의 단색 favicon만 탭 전경 역할을 따라 그린다.
void SetHorizontalTabIconForeground(views::View& icon, SkColor foreground);
std::optional<SkColor> GetHorizontalTabIconForeground(const views::View& icon);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_HORIZONTAL_TAB_PRESENTATION_H_
