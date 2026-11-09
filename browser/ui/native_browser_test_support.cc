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
#if !BUILDFLAG(IS_WIN)
  ui_test_utils::BrowserActivationWaiter waiter(browser);
#endif
  const ui::BaseWindow* const browser_window = browser->GetWindow();
  if (!browser_window) {
    return false;
  }
  const auto window = browser_window->GetNativeWindow();
  if (!window || !ShowAndFocusNativeWindow(window)) {
    return false;
  }
#if !BUILDFLAG(IS_WIN)
  waiter.WaitForActivation();
#endif
  return true;
}
#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_WIN)
bool ShowAndFocusNativeWindow(gfx::NativeWindow window) {
  return ui_test_utils::ShowAndFocusNativeWindow(window);
}
#endif

bool SendKeyPressSync(const BrowserWindowInterface* browser,
                      ui::KeyboardCode key,
                      bool control,
                      bool shift,
                      bool alt,
                      bool command) {
  const auto* window = browser->GetWindow();
  return window && SendKeyPressToWindowSync(window->GetNativeWindow(), key,
                                            control, shift, alt, command);
}

#if !BUILDFLAG(IS_WIN)
bool SendKeyPressToWindowSync(gfx::NativeWindow window,
                              ui::KeyboardCode key,
                              bool control,
                              bool shift,
                              bool alt,
                              bool command) {
  return window && ui_test_utils::SendKeyPressToWindowSync(window, key, control,
                                                           shift, alt, command);
}
#endif
}  // namespace yee::test
