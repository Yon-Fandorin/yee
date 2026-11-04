// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/native_window_test_support.h"

#import <AppKit/AppKit.h>

#include "base/command_line.h"
#include "ui/views/widget/widget.h"

namespace yee::test {

class NativeWindowObservation::Impl {
 public:
  Impl(views::Widget& widget, base::RepeatingClosure changed)
      : changed_(std::move(changed)) {
    if (base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
      return;
    }
    window_ = widget.GetNativeWindow().GetNativeNSWindow();
    if (!window_) {
      return;
    }
    tokens_ = [[NSMutableArray alloc] init];
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name in @[
           NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification,
           NSWindowDidChangeOcclusionStateNotification,
           NSWindowWillEnterFullScreenNotification,
           NSWindowWillExitFullScreenNotification,
           NSWindowDidEnterFullScreenNotification,
           NSWindowDidExitFullScreenNotification, NSWindowDidResizeNotification,
           NSWindowDidMoveNotification, NSWindowWillCloseNotification
         ]) {
      [tokens_
          addObject:
              [center
                  addObserverForName:name
                              object:window_
                               queue:nil
                          usingBlock:^(NSNotification* notification) {
                            if ([notification.name
                                    isEqual:
                                        NSWindowWillEnterFullScreenNotification] ||
                                [notification.name
                                    isEqual:
                                        NSWindowWillExitFullScreenNotification]) {
                              transitioning_ = true;
                            } else if (
                                [notification.name
                                    isEqual:
                                        NSWindowDidEnterFullScreenNotification] ||
                                [notification.name
                                    isEqual:
                                        NSWindowDidExitFullScreenNotification]) {
                              transitioning_ = false;
                            }
                            changed_.Run();
                          }]];
    }
    workspace_token_ = [NSWorkspace.sharedWorkspace.notificationCenter
        addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification*) {
                  changed_.Run();
                }];
  }
  ~Impl() {
    for (id token in tokens_) {
      [NSNotificationCenter.defaultCenter removeObserver:token];
    }
    if (workspace_token_) {
      [NSWorkspace.sharedWorkspace.notificationCenter
          removeObserver:workspace_token_];
    }
  }
  std::optional<NativeWindowMatches> Read() const {
    NSWindow* window = window_;
    if (!window) {
      return std::nullopt;
    }
    return NativeWindowMatches{
        .active = window.keyWindow,
        .on_active_space = window.onActiveSpace,
        .unoccluded =
            (window.occlusionState & NSWindowOcclusionStateVisible) != 0,
        .fullscreen = (window.styleMask & NSWindowStyleMaskFullScreen) != 0,
        .fullscreen_transition_complete = !transitioning_};
  }

 private:
  NSWindow* __weak window_ = nil;
  NSMutableArray* __strong tokens_ = nil;
  id __strong workspace_token_ = nil;
  bool transitioning_ = false;
  base::RepeatingClosure changed_;
};

NativeWindowObservation::NativeWindowObservation(views::Widget& widget,
                                                 base::RepeatingClosure changed)
    : impl_(std::make_unique<Impl>(widget, std::move(changed))) {}
NativeWindowObservation::~NativeWindowObservation() = default;
bool NativeWindowObservation::supported() const {
  return impl_->Read().has_value();
}
std::optional<NativeWindowMatches> NativeWindowObservation::Read() const {
  return impl_->Read();
}

}  // namespace yee::test
