// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_IMMERSIVE_HEADER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_IMMERSIVE_HEADER_H_

#include <optional>

#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "ui/gfx/geometry/rect.h"

namespace views {
class View;
}  // namespace views

namespace yee {

inline constexpr int kImmersiveHeaderBackdropViewId = 92025;

// native 전체화면의 상단 행에서도 기존 Sidebar Header 한 개를 재사용한다.
void SetSidebarHeaderHost(views::View& sidebar,
                          views::View& header,
                          views::View* external_host,
                          double sidebar_header_reveal = 1.0);

// 두 창에 나뉜 동일한 Browser Surface의 바탕과 윤곽을 이어 그린다.
void UpdateImmersiveHeaderBackdrop(
    views::View& host,
    views::View& shell_source,
    const gfx::Rect& surface_bounds,
    std::optional<BrowserSurfacePresentation> presentation);

// 전체화면 세션이 끝나면 이전 페이지의 임시 바탕을 남기지 않는다.
void ClearImmersiveHeaderBackdrop(views::View& host);

// 색 전환 중에는 레이아웃이나 Header 호스팅을 바꾸지 않고 paint만 갱신한다.
void UpdateImmersiveHeaderPresentation(
    views::View& host,
    std::optional<BrowserSurfacePresentation> presentation);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_IMMERSIVE_HEADER_H_
