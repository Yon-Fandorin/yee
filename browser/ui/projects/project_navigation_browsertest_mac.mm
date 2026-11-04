// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#import <Cocoa/Cocoa.h>

#include <utility>

#include "chrome/browser/ui/views/yee/projects/project_navigation_test_util.h"
#include "ui/base/models/menu_model.h"
#include "ui/menus/cocoa/menu_controller.h"

namespace yee::projects {

bool RunWithNativeContextMenuForTesting(base::OnceClosure show_menu,
                                        std::optional<int> command_id) {
  __block bool opened = false;
  __block bool executed = false;
  auto* center = NSNotificationCenter.defaultCenter;
  id observer = [center
      addObserverForName:NSMenuDidBeginTrackingNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                opened = true;
                NSMenu* menu = notification.object;
                // Tracking 시작 통지는 메뉴 준비가 끝나기 전에 올 수 있다.
                [NSRunLoop.currentRunLoop
                    performInModes:@[
                      NSEventTrackingRunLoopMode, NSDefaultRunLoopMode
                    ]
                             block:^{
                               if (command_id) {
                                 for (NSInteger i = 0; i < menu.numberOfItems;
                                      ++i) {
                                   NSMenuItem* item = [menu itemAtIndex:i];
                                   if (![item.target
                                           isKindOfClass:MenuControllerCocoa
                                                             .class]) {
                                     continue;
                                   }
                                   auto* model = [(MenuControllerCocoa*)
                                                      item.target model];
                                   if (model &&
                                       model->GetCommandIdAt(item.tag) ==
                                           *command_id &&
                                       model->IsEnabledAt(item.tag)) {
                                     executed = true;
                                     [menu performActionForItemAtIndex:i];
                                     break;
                                   }
                                 }
                               }
                               [menu cancelTracking];
                             }];
              }];
  std::move(show_menu).Run();
  [center removeObserver:observer];
  return opened && (!command_id || executed);
}

}  // namespace yee::projects
