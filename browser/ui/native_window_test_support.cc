// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/native_window_test_support.h"

#include "build/build_config.h"

#if !BUILDFLAG(IS_MAC)
namespace yee::test {

// 해당 플랫폼의 native backend를 연결하기 전까지 Begin에서 명시적으로 거부한다.
class NativeWindowObservation::Impl {};
NativeWindowObservation::NativeWindowObservation(views::Widget&,
                                                 base::RepeatingClosure)
    : impl_(std::make_unique<Impl>()) {}
NativeWindowObservation::~NativeWindowObservation() = default;
bool NativeWindowObservation::supported() const {
  return false;
}
std::optional<NativeWindowMatches> NativeWindowObservation::Read() const {
  return std::nullopt;
}

}  // namespace yee::test
#endif
