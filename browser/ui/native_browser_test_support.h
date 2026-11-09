// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_BROWSER_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_BROWSER_TEST_SUPPORT_H_

#include <memory>
#include <string>
#include <string_view>

#include "build/build_config.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/native_ui_types.h"

#if BUILDFLAG(IS_WIN)
#include "base/process/process.h"
#include "base/time/time.h"
#endif

class BrowserWindowInterface;

namespace gfx {
class Point;
}  // namespace gfx
namespace views {
class View;
class Widget;
}  // namespace views

namespace yee::test {
// 테스트 소유 창의 활성 완료를 기다린다. focus 거부를 다른 창 조작으로 우회하지
// 않는다.
[[nodiscard]] bool ShowAndFocusNativeWindow(gfx::NativeWindow window);
[[nodiscard]] bool BringBrowserWindowToFront(
    const BrowserWindowInterface* browser);
// 입력 완료 callback까지 기다리고, 전면화가 실패하면 다른 창에 입력하지 않는다.
[[nodiscard]] bool SendKeyPressSync(const BrowserWindowInterface* browser,
                                    ui::KeyboardCode key,
                                    bool control,
                                    bool shift,
                                    bool alt,
                                    bool command);
[[nodiscard]] bool SendKeyPressToWindowSync(gfx::NativeWindow window,
                                            ui::KeyboardCode key,
                                            bool control,
                                            bool shift,
                                            bool alt,
                                            bool command);

#if BUILDFLAG(IS_WIN)
// Kombucha의 실제 Windows 입력을 검사 프로세스가 소유한 창으로 제한한다.
void InstallOwnedWindowsUIControls();
void QueueOwnedDragUnblockNudge();

// 활성 상태인 테스트 소유 창에 WM_CHAR로 UTF-16 입력을 직접 전달한다.
[[nodiscard]] bool SendTextToWindowSync(gfx::NativeWindow window,
                                        std::u16string_view text);
[[nodiscard]] bool SendMouseMoveSync(const gfx::Point& screen_dip,
                                     gfx::NativeWindow window,
                                     base::TimeTicks deadline = {});
// 입력을 보냈지만 완료 신호를 받지 못한 경우에도 누름을 해제할 수 있다.
struct NativeMouseButtonResult {
  bool sent = false;
  bool completed = false;
};
[[nodiscard]] NativeMouseButtonResult SendLeftMouseButtonSync(
    gfx::NativeWindow window,
    bool pressed,
    base::TimeTicks deadline = {});
[[nodiscard]] bool SendLeftMouseClickSync(gfx::NativeWindow window);
// 메뉴를 열거나 선택하기 직전에 포인터와 테스트 창 소유권을 확인한다.
enum class NativeContextMenuAction { kOpen, kSelect };
[[nodiscard]] bool SendContextMenuClickSync(views::View& target,
                                            NativeContextMenuAction action);
// 예상 밖의 detach에서 진행 중인 입력 대기에 겹치지 않도록 해제만 보낸다.
[[nodiscard]] bool SendLeftMouseButtonRelease(gfx::NativeWindow window);
// 테스트 창의 작업 표시줄 그룹을 구분하고 그 창의 버튼만 활성화한다.
[[nodiscard]] bool NameTaskbarTestWindow(gfx::NativeWindow window,
                                         std::wstring_view title_marker);
[[nodiscard]] bool RestoreFromTaskbar(gfx::NativeWindow window,
                                      std::wstring_view title_marker);

// 직렬 실행기가 시작 신호를 보낸 뒤 테스트를 시작한다. 종료할 때 실제
// 부모 실행기에만 전면화 권한을 전달한다.
class ScopedLauncherForegroundPermission {
 public:
  ScopedLauncherForegroundPermission();
  ~ScopedLauncherForegroundPermission();
  [[nodiscard]] bool ready() const { return ready_; }

 private:
  base::Process launcher_;
  bool ready_ = true;
};

// Yee 테스트 창의 실제 입력과 창·포인터 상태를 제한된 버퍼로 관측한다.
class ScopedNativeInputTrace {
 public:
  explicit ScopedNativeInputTrace(views::Widget* widget);
  ScopedNativeInputTrace(const ScopedNativeInputTrace&) = delete;
  ScopedNativeInputTrace& operator=(const ScopedNativeInputTrace&) = delete;
  ~ScopedNativeInputTrace();

  // 이 조회는 메시지 루프를 돌리지 않는다.
  void Snapshot(std::string_view phase, const gfx::Point& intended_screen_dip);
  std::string ToJson() const;

 private:
  class Observer;
  std::unique_ptr<Observer> observer_;
};
#endif  // BUILDFLAG(IS_WIN)

}  // namespace yee::test
#endif  // CHROME_BROWSER_UI_VIEWS_YEE_NATIVE_BROWSER_TEST_SUPPORT_H_
