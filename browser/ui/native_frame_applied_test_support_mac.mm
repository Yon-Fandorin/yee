// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/native_frame_applied_test_support_mac.h"

#import <AppKit/AppKit.h>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/glass_frame_service.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller_mac.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "ui/display/screen.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/view_tracker.h"

namespace yee::test {

class NativeFrameAppliedObserver::Impl {
 public:
  Impl(AppliedViewsObservers& source, BrowserView& view)
      : source(source), view(&view) {
    source.ObserveView(view);
    source.ObserveWidget(*view.GetWidget());
    source.ObserveNativeWindow(*view.GetWidget());
    if (auto* overlay = view.overlay_widget()) {
      source.ObserveWidget(*overlay);
      source.ObserveView(*overlay->GetRootView());
    }
    // 외부 Toolbar는 원래 Widget의 placeholder NSWindow와 다른 창으로
    // 이동한다. AppKit 알림에서는 값만 다시 읽고 창을 만들지 않는다.
    notifications = [[NSMutableArray alloc] init];
    const auto changed = base::BindRepeating(&Impl::Changed, weak.GetWeakPtr());
    for (NSNotificationName name in @[
           NSWindowDidMoveNotification, NSWindowDidResizeNotification,
           NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification,
           NSWindowDidChangeOcclusionStateNotification,
           NSWindowDidEnterFullScreenNotification,
           NSWindowDidExitFullScreenNotification
         ]) {
      [notifications addObject:[NSNotificationCenter.defaultCenter
                                   addObserverForName:name
                                               object:nil
                                                queue:nil
                                           usingBlock:^(NSNotification*) {
                                             changed.Run();
                                           }]];
    }
    if (auto* service = GlassFrameService::GetInstance()) {
      glass = service->RegisterGlassFrameEligibilityChangedCallback(
          view.browser(), base::BindRepeating([](base::RepeatingClosure changed,
                                                 bool) { changed.Run(); },
                                              changed));
    }
  }
  ~Impl() {
    for (id notification in notifications)
      [NSNotificationCenter.defaultCenter removeObserver:notification];
    source->OwnerDestroyed(token);
  }
  void Changed() { source->Notify(); }
  std::optional<AppliedSnapshot> Read() {
    auto* browser = static_cast<BrowserView*>(view.view());
    if (!browser || !browser->GetWidget())
      return std::nullopt;
    NSWindow* window =
        browser->GetWidget()->GetNativeWindow().GetNativeNSWindow();
    NativeFrameMatches value{.opaque = window.opaque};
    if (auto* service = GlassFrameService::GetInstance())
      value.glass_eligible =
          service->IsBrowserWindowEligible(browser->browser());
    auto* immersive = static_cast<ImmersiveModeControllerMac*>(
        ImmersiveModeController::From(browser->browser()));
    if (immersive) {
      value.toolbar_revealed = immersive->IsRevealed();
      value.menu_bar_reveal = immersive->menu_bar_reveal_amount_for_testing();
    }
    auto* overlay = browser->overlay_widget();
    auto* top = browser->top_container();
    if (overlay && top && top->parent()) {
      NSView* moved = (__bridge NSView*)overlay->GetNativeWindowProperty(
          views::NativeWidgetMacNSWindowHost::kMovedContentNSView);
      value.toolbar_bounds = top->parent()->GetBoundsInScreen();
      value.toolbar_host_ready =
          window.visible && window.onActiveSpace && moved && moved.window &&
          !NSIsEmptyRect(moved.frame) && !value.toolbar_bounds.IsEmpty();
      value.toolbar_on_active_space = moved && moved.window.onActiveSpace;
      value.toolbar_fully_visible =
          moved && NSContainsRect(moved.visibleRect, moved.bounds);
      value.toolbar_offscreen =
          moved && NSIsEmptyRect(moved.visibleRect) &&
          !value.toolbar_bounds.Intersects(
              display::Screen::Get()
                  ->GetDisplayNearestWindow(
                      browser->GetWidget()->GetNativeWindow())
                  .bounds());
    }
    return AppliedSnapshot{.owner = token,
                           .value = value,
                           .coordinates = GetAppliedViewCoordinates(*browser)};
  }
  raw_ref<AppliedViewsObservers> source;
  views::ViewTracker view;
  AppliedOwnerToken token = AppliedOwnerToken::Create();
  NSMutableArray* __strong notifications = nil;
  base::CallbackListSubscription glass;
  base::WeakPtrFactory<Impl> weak{this};
};

NativeFrameAppliedObserver::NativeFrameAppliedObserver(
    AppliedBrowserObservers& source,
    BrowserView& view)
    : impl_(std::make_unique<Impl>(source.views(), view)) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("headless"))
    return;
  source.views().TieToView(impl_->token, view);
  source.views().RegisterSignal(
      impl_->token, AppliedConditionKind::kNativeFrameMatches,
      base::BindRepeating(
          [](base::WeakPtr<Impl> self) -> std::optional<AppliedSnapshot> {
            return self ? self->Read() : std::nullopt;
          },
          impl_->weak.GetWeakPtr()));
}
NativeFrameAppliedObserver::~NativeFrameAppliedObserver() = default;
AppliedOwnerToken NativeFrameAppliedObserver::owner() const {
  return impl_->token;
}

}  // namespace yee::test
