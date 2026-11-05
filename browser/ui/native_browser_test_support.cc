// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/native_browser_test_support.h"

#include "build/build_config.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "ui/base/base_window.h"

namespace yee::test {
bool BringBrowserWindowToFront(const BrowserWindowInterface* browser) {
  ui_test_utils::BrowserActivationWaiter waiter(browser);
  const ui::BaseWindow* const browser_window = browser->GetWindow();
  if (!browser_window)
    return false;
  const auto window = browser_window->GetNativeWindow();
  if (!window || !ShowAndFocusNativeWindow(window))
    return false;
  waiter.WaitForActivation();
  return true;
}
#if !BUILDFLAG(IS_MAC)
bool ShowAndFocusNativeWindow(gfx::NativeWindow window) {
  // Windows 입력·창 보존 구현은 다음 Windows checkpoint까지 그대로 사용한다.
  return ui_test_utils::ShowAndFocusNativeWindow(window);
}
#endif
}  // namespace yee::test
