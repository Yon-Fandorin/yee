// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_

namespace content {
class WebContents;
}

namespace yee {
// 신뢰된 WebUI 팝업 문서가 준비된 뒤, 결과를 보이기 전에 Views와 같은
// 제안 목록 여백과 곡률을 한 번 적용한다.
void ApplyOmniboxPopupStyle(content::WebContents* contents);
}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_
