// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_WINDOW_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_WINDOW_TEST_SUPPORT_H_

#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"

namespace views {
class Widget;
}

namespace yee::test {

// headless를 native 활성·Space·가림의 증거로 해석하지 않는다.
class NativeWindowObservation {
 public:
  NativeWindowObservation(views::Widget& widget,
                          base::RepeatingClosure changed);
  ~NativeWindowObservation();
  NativeWindowObservation(const NativeWindowObservation&) = delete;
  NativeWindowObservation& operator=(const NativeWindowObservation&) = delete;
  bool supported() const;
  std::optional<NativeWindowMatches> Read() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_WINDOW_TEST_SUPPORT_H_
