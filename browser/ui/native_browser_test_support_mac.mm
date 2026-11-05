// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/native_browser_test_support.h"

#include <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/app/chrome_command_ids.h"
#import "ui/base/test/windowed_nsnotification_observer.h"

@interface NSApplication (YeeNativeFocus)
- (void)_handleActivatedEvent:(id)arg1;
@end

namespace yee::test {
namespace {

bool IsWindowStateTraceEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      "yee-mac-window-state-trace");
}

void TraceWindowState(const char* event, NSWindow* changed) {
  if (!IsWindowStateTraceEnabled()) {
    return;
  }
  NSWindow* key = NSApp.keyWindow;
  NSWindow* main = NSApp.mainWindow;
  NSRunningApplication* front =
      NSWorkspace.sharedWorkspace.frontmostApplication;
  LOG(ERROR) << "YEE_MAC_WINDOW_STATE event=" << event
             << " changed=" << changed.windowNumber << " class="
             << (changed ? NSStringFromClass(changed.class).UTF8String : "none")
             << " app_active=" << NSApp.active << " key=" << key.windowNumber
             << " main=" << main.windowNumber
             << " front_pid=" << front.processIdentifier
             << " visible=" << changed.visible
             << " active_space=" << changed.onActiveSpace
             << " occlusion=" << changed.occlusionState;
}

void InstallWindowStateTrace() {
  static bool installed = false;
  if (installed || !IsWindowStateTraceEnabled()) {
    return;
  }
  installed = true;
  // 진단을 명시한 검사 프로세스에서만 알림을 관찰하며 창을 붙잡지 않는다.
  for (NSString* name in @[
         NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification,
         NSWindowWillEnterFullScreenNotification,
         NSWindowDidEnterFullScreenNotification,
         NSWindowWillExitFullScreenNotification,
         NSWindowDidExitFullScreenNotification, NSWindowWillCloseNotification,
         NSWindowDidChangeOcclusionStateNotification,
         NSApplicationDidBecomeActiveNotification,
         NSApplicationDidResignActiveNotification
       ]) {
    [NSNotificationCenter.defaultCenter
        addObserverForName:name
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* notification) {
                  NSWindow* window =
                      [notification.object isKindOfClass:NSWindow.class]
                          ? notification.object
                          : nil;
                  TraceWindowState(notification.name.UTF8String, window);
                }];
  }
  for (NSString* name in @[
         NSWorkspaceDidActivateApplicationNotification,
         NSWorkspaceActiveSpaceDidChangeNotification
       ]) {
    [NSWorkspace.sharedWorkspace.notificationCenter
        addObserverForName:name
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* notification) {
                  TraceWindowState(notification.name.UTF8String,
                                   NSApp.keyWindow);
                }];
  }
  // 입력 이벤트는 그대로 돌려주고 좌표·modifier만 기록한다.
  [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                           NSEventMaskLeftMouseDragged |
                                           NSEventMaskLeftMouseUp |
                                           NSEventMaskFlagsChanged
                                   handler:^NSEvent*(NSEvent* event) {
                                     LOG(ERROR)
                                         << "YEE_MAC_INPUT type=" << event.type
                                         << " window="
                                         << event.window.windowNumber
                                         << " x=" << event.locationInWindow.x
                                         << " y=" << event.locationInWindow.y
                                         << " flags=" << event.modifierFlags;
                                     return event;
                                   }];
}

}  // namespace

bool ShowAndFocusNativeWindow(gfx::NativeWindow native_window) {
  NSWindow* window = native_window.GetNativeNSWindow();
  if (!window || ![NSApp.windows containsObject:window])
    return false;
  InstallWindowStateTrace();
  TraceWindowState("focus_request", window);
  // Make sure an unbundled program can get the input focus.
  ProcessSerialNumber psn = {0, kCurrentProcess};
  TransformProcessType(&psn, kProcessTransformToForegroundApplication);
  // We used to call [NSApp activateIgnoringOtherApps:YES] but this
  // would not reliably activate the app, causing the window to never
  // become key. This bit of private API appears to be the secret
  // incantation that gets us what we want. See https://crbug.com/40184452.
  [NSApplication.sharedApplication _handleActivatedEvent:nil];

  WindowedNSNotificationObserver* async_waiter;
  if (!window.keyWindow) {
    // Only wait when expecting a change to actually occur.
    async_waiter = [[WindowedNSNotificationObserver alloc]
        initForNotification:NSWindowDidBecomeKeyNotification
                     object:window];
  }
  [window makeKeyAndOrderFront:nil];

  // Wait until |window| becomes key window, then make sure the shortcuts for
  // "Close Window" and "Close Tab" are updated.
  // This is because normal AppKit menu updating does not get invoked when
  // events are sent via ui_test_utils::SendKeyPressSync.
  BOOL notification_observed = [async_waiter wait];
  NSMenu* file_menu = [[NSApp.mainMenu itemWithTag:IDC_FILE_MENU] submenu];
  [file_menu.delegate menuNeedsUpdate:file_menu];

  TraceWindowState("focus_request_complete", window);
  return !async_waiter || notification_observed;
}
}  // namespace yee::test
