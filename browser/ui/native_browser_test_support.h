// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_BROWSER_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_BROWSER_TEST_SUPPORT_H_

#include "ui/gfx/native_ui_types.h"

class BrowserWindowInterface;
namespace yee::test {
// 테스트 소유 창의 활성 완료를 기다린다. focus 거부를 다른 창 조작으로 우회하지
// 않는다.
[[nodiscard]] bool ShowAndFocusNativeWindow(gfx::NativeWindow window);
[[nodiscard]] bool BringBrowserWindowToFront(
    const BrowserWindowInterface* browser);
}  // namespace yee::test
#endif
