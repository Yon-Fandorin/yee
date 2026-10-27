// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/projects/project_navigation_test_util.h"

#import <Cocoa/Cocoa.h>

#include <utility>

namespace yee::projects {

bool RunWithNativeContextMenuAutoCloseForTesting(base::OnceClosure show_menu) {
  __block bool opened = false;
  auto* center = NSNotificationCenter.defaultCenter;
  id observer = [center
      addObserverForName:NSMenuDidBeginTrackingNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                opened = true;
                NSMenu* menu = notification.object;
                // Tracking 시작 통지는 메뉴 준비가 끝나기 전에 올 수 있다.
                [NSRunLoop.currentRunLoop performInModes:@[
                  NSEventTrackingRunLoopMode, NSDefaultRunLoopMode
                ]
                                                   block:^{
                                                     [menu cancelTracking];
                                                   }];
              }];
  std::move(show_menu).Run();
  [center removeObserver:observer];
  return opened;
}

}  // namespace yee::projects
