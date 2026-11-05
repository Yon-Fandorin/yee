// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_FRAME_APPLIED_TEST_SUPPORT_MAC_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_FRAME_APPLIED_TEST_SUPPORT_MAC_H_

#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"

namespace yee::test {

// 두 Mac fixture가 소유하며 BrowserView보다 먼저 해제한다.
class NativeFrameAppliedObserver {
 public:
  NativeFrameAppliedObserver(AppliedBrowserObservers& source,
                             BrowserView& view);
  ~NativeFrameAppliedObserver();
  NativeFrameAppliedObserver(const NativeFrameAppliedObserver&) = delete;
  NativeFrameAppliedObserver& operator=(const NativeFrameAppliedObserver&) =
      delete;
  AppliedOwnerToken owner() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_FRAME_APPLIED_TEST_SUPPORT_MAC_H_
