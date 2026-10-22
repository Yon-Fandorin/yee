// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_CUSTOMIZATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_CUSTOMIZATION_H_

namespace content {
class WebUIDataSource;
class WebContents;
}  // namespace content

namespace yee {
// 고정 탐색 행을 쓰는 compact shell에서는 Chromium의 고정 도구 모음 편집기를
// 제공하지 않는다. 기본 shell로 돌아간 경우에만 이 편집기를 지원한다.
bool SupportsToolbarCustomization();
void ConfigureCustomizationDataSource(content::WebUIDataSource& source);
struct CustomizationTabLayout {
  bool vertical;
  bool enabled;
};
CustomizationTabLayout GetCustomizationTabLayout(
    content::WebContents& contents);
void SetCustomizationTabLayout(content::WebContents& contents, bool vertical);
}  // namespace yee
#endif
