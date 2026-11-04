// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/background_browser_test_support.h"

#import <AppKit/AppKit.h>

#include "base/apple/scoped_objc_class_swizzler.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/no_destructor.h"

@interface YeeBackgroundBrowserApplication : NSObject
- (void)activateIgnoringOtherApps:(BOOL)flag;
- (void)activate;
@end

@implementation YeeBackgroundBrowserApplication

// native 창의 생성과 headless 논리 focus는 유지하고 앱 활성화만 막는다.
- (void)activateIgnoringOtherApps:(BOOL)flag {
}

- (void)activate {
}

@end

namespace yee::test {

void SuppressBackgroundBrowserActivation() {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  CHECK(command_line.GetSwitchValueASCII("yee-test-tier") == "background");
  CHECK(command_line.HasSwitch("headless"));

  // Widget::Show()의 bridge는 headless NSWindow 구현을 부르기 전에
  // NSApp에 activateIgnoringOtherApps:를 보낸다. 정책 변경만으로는 막지 못한다.
  static base::NoDestructor<base::apple::ScopedObjCClassSwizzler> legacy(
      [NSApplication class], [YeeBackgroundBrowserApplication class],
      @selector(activateIgnoringOtherApps:));
  if (@available(macOS 14.0, *)) {
    static base::NoDestructor<base::apple::ScopedObjCClassSwizzler> current(
        [NSApplication class], [YeeBackgroundBrowserApplication class],
        @selector(activate));
  }
}

}  // namespace yee::test
