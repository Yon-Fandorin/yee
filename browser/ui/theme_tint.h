// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_THEME_TINT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_THEME_TINT_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

namespace content {
class WebContents;
}

namespace yee {
// 페이지와 같은 ColorProvider에서 프로필 강조색을 읽는다. 기본·회색조 테마는
// Yee의 중성 조작색을 유지한다.
std::optional<SkColor> ResolveThemeAccent(content::WebContents& contents);
}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_THEME_TINT_H_
