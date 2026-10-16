// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SIDE_PANEL_COLORS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SIDE_PANEL_COLORS_H_

namespace ui {
class ColorProvider;
struct ColorProviderKey;
}  // namespace ui

namespace yee {
// Native Side Panel과 WebUI 본문은 같은 바탕을 쓰고, 조작 요소에 테마 강조색을 남긴다.
void AddSidePanelColorMixer(ui::ColorProvider& provider,
                            const ui::ColorProviderKey& key);
}  // namespace yee
#endif
