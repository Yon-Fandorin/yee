// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/native_browser_test_support.h"

#include <windows.h>

#include <oleacc.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/base_switches.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/test/test_switches.h"
#include "base/time/time.h"
#include "base/values.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_variant.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "content/public/common/content_switches.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/test/ui_controls_aura.h"
#include "ui/base/test/ui_controls_internal_win.h"
#include "ui/base/win/event_creation_utils.h"
#include "ui/base/win/shell.h"
#include "ui/display/win/screen_win.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/events/keycodes/keyboard_code_conversion_win.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/win/hwnd_util.h"

namespace yee::test {
namespace {

bool IsOwnedWindow(HWND hwnd) {
  DWORD pid = 0;
  ::GetWindowThreadProcessId(hwnd, &pid);
  return hwnd && pid == ::GetCurrentProcessId();
}

HWND OwnedWindowHandle(gfx::NativeWindow window) {
  if (!window || !window->GetHost()) {
    return nullptr;
  }
  const HWND hwnd = window->GetHost()->GetAcceleratedWidget();
  return IsOwnedWindow(hwnd) ? hwnd : nullptr;
}

BOOL CALLBACK FindTaskList(HWND child, LPARAM result) {
  std::array<wchar_t, 64> class_name{};
  const int length = ::GetClassNameW(child, class_name.data(), class_name.size());
  if (std::wstring_view(class_name.data(), length) != L"MSTaskListWClass") {
    return TRUE;
  }
  *reinterpret_cast<HWND*>(result) = child;
  return FALSE;
}

class TaskbarEntryWatch {
 public:
  TaskbarEntryWatch(HWND tray, DWORD shell_pid, base::RepeatingCallback<LONG()> find)
      : tray_(tray), find_(std::move(find)) {
    CHECK(!active_);
    hook_ = ::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE,
                              nullptr, &OnChanged, shell_pid, 0,
                              WINEVENT_OUTOFCONTEXT);
    if (hook_) {
      active_ = this;
    }
  }

  ~TaskbarEntryWatch() {
    weak_.InvalidateWeakPtrs();
    if (hook_) {
      active_ = nullptr;
      ::UnhookWinEvent(hook_);
    }
  }

  LONG Wait() {
    const LONG ready = find_.Run();
    if (ready != 0 || !hook_) {
      return ready;
    }
    // Explorer가 창 그룹을 등록한 뒤 내보내는 접근성 알림을 기다린다.
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(3));
    return found_.Wait(base::RunLoop::Type::kNestableTasksAllowed)
               ? found_.Get()
               : 0;
  }

 private:
  static void CALLBACK OnChanged(HWINEVENTHOOK hook,
                                  DWORD,
                                  HWND hwnd,
                                  LONG,
                                  LONG,
                                  DWORD,
                                  DWORD) {
    if (active_ && active_->hook_ == hook &&
        ::GetAncestor(hwnd, GA_ROOT) == active_->tray_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&TaskbarEntryWatch::Refresh,
                                    active_->weak_.GetWeakPtr()));
    }
  }

  void Refresh() {
    if (!found_.IsReady()) {
      const LONG entry = find_.Run();
      if (entry != 0 && !found_.IsReady()) {
        found_.SetValue(entry);
      }
    }
  }

  inline static thread_local TaskbarEntryWatch* active_ = nullptr;
  const HWND tray_;
  const base::RepeatingCallback<LONG()> find_;
  HWINEVENTHOOK hook_ = nullptr;
  base::test::TestFuture<LONG> found_;
  base::WeakPtrFactory<TaskbarEntryWatch> weak_{this};
};

bool UsesExtendedScanCode(ui::KeyboardCode key) {
  switch (key) {
    case ui::VKEY_INSERT:
    case ui::VKEY_DELETE:
    case ui::VKEY_HOME:
    case ui::VKEY_END:
    case ui::VKEY_NEXT:
    case ui::VKEY_PRIOR:
    case ui::VKEY_LEFT:
    case ui::VKEY_RIGHT:
    case ui::VKEY_UP:
    case ui::VKEY_DOWN:
    case ui::VKEY_NUMLOCK:
      return true;
    default:
      return false;
  }
}

WORD MessageKey(WORD key) {
  switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
      return VK_CONTROL;
    case VK_LSHIFT:
    case VK_RSHIFT:
      return VK_SHIFT;
    case VK_LMENU:
    case VK_RMENU:
      return VK_MENU;
    default:
      return key;
  }
}

// popup이 Escape로 사라져도 입력 전에 검증한 owner 창까지의 release만 받는다.
std::set<HWND> InputOwners(HWND hwnd) {
  std::set<HWND> owners;
  for (HWND owner = hwnd; IsOwnedWindow(owner);
       owner = ::GetWindow(owner, GW_OWNER)) {
    if (!owners.insert(owner).second) {
      break;
    }
    const HWND root = ::GetAncestor(owner, GA_ROOT);
    if (IsOwnedWindow(root)) {
      owners.insert(root);
    }
  }
  return owners;
}

bool IsInputOwner(HWND hwnd, const std::set<HWND>& owners) {
  if (!IsOwnedWindow(hwnd)) {
    return false;
  }
  for (HWND root = ::GetAncestor(hwnd, GA_ROOT); IsOwnedWindow(root);
       root = ::GetWindow(root, GW_OWNER)) {
    if (owners.contains(root)) {
      return true;
    }
    const HWND next = ::GetWindow(root, GW_OWNER);
    if (next == root) {
      break;
    }
  }
  return false;
}

std::optional<gfx::Point> OwnedMouseButtonPoint(HWND hwnd,
                                              bool pressed,
                                              bool allow_owned_capture = false) {
  if (!hwnd || base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
    return std::nullopt;
  }
  const auto owners = InputOwners(hwnd);
  POINT cursor = {};
  if (!IsInputOwner(::GetForegroundWindow(), owners) ||
      !::GetCursorPos(&cursor)) {
    return std::nullopt;
  }
  const HWND capture = ::GetCapture();
  if (capture && ((pressed && !allow_owned_capture) ||
                  !IsInputOwner(capture, owners))) {
    return std::nullopt;
  }
  // 다른 스레드의 창 위에서는 버튼을 누른 동안만 capture로 입력된다.
  const bool held_capture =
      capture && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000);
  if (!held_capture && !IsInputOwner(::WindowFromPoint(cursor), owners)) {
    return std::nullopt;
  }
  return gfx::Point(cursor);
}

HWND OwnedForegroundWindow() {
  const HWND hwnd = ::GetForegroundWindow();
  return IsOwnedWindow(hwnd) &&
                 !base::CommandLine::ForCurrentProcess()->HasSwitch("headless")
             ? hwnd
             : nullptr;
}

bool IsOwnedScreenPoint(const gfx::Point& point) {
  return IsOwnedWindow(::WindowFromPoint({point.x(), point.y()}));
}

class OwnedWindowsUIControls : public ui_controls::UIControlsAura {
 public:
  bool SendKeyEvents(gfx::NativeWindow window,
                     ui::KeyboardCode key,
                     int event_types,
                     int accelerators) override {
    return SendKeyEventsNotifyWhenDone(window, key, event_types, {},
                                       accelerators, ui_controls::kKeyRelease);
  }

  bool SendKeyEventsNotifyWhenDone(
      gfx::NativeWindow window,
      ui::KeyboardCode key,
      int event_types,
      base::OnceClosure done,
      int accelerators,
      ui_controls::KeyEventType wait_for) override {
    const HWND hwnd = OwnedWindowHandle(window);
    if (!hwnd || !OwnedForegroundWindow() ||
        event_types != (ui_controls::kKeyPress | ui_controls::kKeyRelease) ||
        (accelerators & ui_controls::kCommand) ||
        ((accelerators & ui_controls::kControl) && key == ui::VKEY_ESCAPE)) {
      return false;
    }
    // Chromium이 topmost 메뉴에 직접 전달하는 경로도 소유 창만 허용한다.
    const HWND menu = ::FindWindowW(L"#32768", nullptr);
    if (menu && menu == ::GetTopWindow(nullptr) && !IsOwnedWindow(menu)) {
      return false;
    }
    return ui_controls::internal::SendKeyPressReleaseImpl(
        hwnd, key, accelerators, wait_for, std::move(done));
  }

  bool SendMouseMove(int x, int y) override {
    return SendMouseMoveNotifyWhenDone(x, y, {});
  }

  bool SendMouseMoveNotifyWhenDone(int x,
                                   int y,
                                   base::OnceClosure done) override {
    const HWND foreground = OwnedForegroundWindow();
    auto* screen = display::win::GetScreenWin();
    if (!foreground || !screen) {
      return false;
    }
    // Chromium은 같은 위치의 move를 전달하기 위해 한 DIP 옆으로 보낸다.
    return IsOwnedScreenPoint(screen->DIPToScreenPoint({x, y})) &&
           IsOwnedScreenPoint(screen->DIPToScreenPoint({x + 1, y})) &&
           ::GetForegroundWindow() == foreground &&
           ui_controls::internal::SendMouseMoveImpl(x, y, std::move(done));
  }

  bool SendMouseEvents(ui_controls::MouseButton button,
                       int state,
                       int accelerators) override {
    return SendMouseEventsNotifyWhenDone(button, state, {}, accelerators);
  }

  bool SendMouseEventsNotifyWhenDone(ui_controls::MouseButton button,
                                     int state,
                                     base::OnceClosure done,
                                     int accelerators) override {
    // Aura의 Windows 버튼 경로는 window hint 없이 현재 포인터에 보낸다.
    return OwnedMouseButtonPoint(OwnedForegroundWindow(),
                                 (state & ui_controls::DOWN) != 0,
                                 /*allow_owned_capture=*/true) &&
           ui_controls::internal::SendMouseEventsImpl(
               button, state, std::move(done), accelerators);
  }

  bool SendMouseClick(ui_controls::MouseButton button) override {
    return SendMouseEvents(button, ui_controls::DOWN | ui_controls::UP,
                           ui_controls::kNoAccelerator);
  }

  bool SendTouchEvents(int action, int count, int x, int y) override {
    const HWND foreground = OwnedForegroundWindow();
    if (!foreground || count < 1 || count > 16) {
      return false;
    }
    for (int index = 0; index < count; ++index) {
      const int touch_x = x + 10 * index;
      if (!IsOwnedScreenPoint({touch_x, y}) ||
          ((action & (ui_controls::kTouchMove | ui_controls::kTouchRelease)) &&
           !IsOwnedScreenPoint({touch_x + 30, y + 10}))) {
        return false;
      }
    }
    return ::GetForegroundWindow() == foreground &&
           ui_controls::internal::SendTouchEventsImpl(action, count, x, y);
  }
};

class NativeInputCompletion {
 public:
  enum class Source { kMouse, kKeyboard };

  NativeInputCompletion(HWND target,
                        Source source,
                        base::RepeatingCallback<bool(const MSG&)> matches,
                        base::TimeTicks deadline = {})
      : owners_(InputOwners(target)),
        matches_(std::move(matches)),
        deadline_(deadline.is_null()
                      ? base::TimeTicks::Now() + TestTimeouts::action_timeout()
                      : deadline) {
    CHECK(!active_);
    static std::atomic<ULONG_PTR> next_token{1};
    token_ = next_token.fetch_add(1, std::memory_order_relaxed);
    CHECK_NE(token_, 0u);
    const bool mouse = source == Source::kMouse;
    hook_ = ::SetWindowsHookEx(mouse ? WH_MOUSE : WH_GETMESSAGE,
                               mouse ? &MouseHook : &KeyboardMessageHook,
                               nullptr,
                               ::GetCurrentThreadId());
    if (hook_) {
      active_ = this;
    }
  }

  ~NativeInputCompletion() {
    weak_.InvalidateWeakPtrs();
    if (hook_) {
      ::UnhookWindowsHookEx(hook_);
      CHECK_EQ(active_, this);
      active_ = nullptr;
    }
  }

  bool ready() const { return hook_ && !owners_.empty(); }
  ULONG_PTR token() const { return token_; }
  bool OwnsForeground() const {
    return IsInputOwner(::GetForegroundWindow(), owners_);
  }
  bool OwnsPoint(const gfx::Point& point) const {
    return IsInputOwner(::WindowFromPoint({point.x(), point.y()}), owners_);
  }
  bool OwnsWindow(HWND hwnd) const { return IsInputOwner(hwnd, owners_); }
  bool HasTime() const { return base::TimeTicks::Now() < deadline_; }
  bool Wait() {
    const auto remaining = deadline_ - base::TimeTicks::Now();
    if (remaining <= base::TimeDelta()) {
      return false;
    }
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, remaining);
    return completed_.Wait(base::RunLoop::Type::kNestableTasksAllowed);
  }

 private:
  static LRESULT CALLBACK MouseHook(int code, WPARAM message, LPARAM event) {
    const LRESULT next = ::CallNextHookEx(nullptr, code, message, event);
    // 다른 훅이 차단한 이벤트는 입력 완료로 세지 않는다.
    if (next == 0 && code == HC_ACTION && active_) {
      const auto& mouse = *reinterpret_cast<const MOUSEHOOKSTRUCT*>(event);
      // 마우스 메시지의 토큰과 대상 창을 같은 이벤트에서 읽는다.
      const MSG observed{.hwnd = mouse.hwnd,
                         .message = static_cast<UINT>(message),
                         .pt = mouse.pt};
      active_->OnMessage(observed, mouse.dwExtraInfo);
    }
    return next;
  }

  static LRESULT CALLBACK KeyboardMessageHook(int code,
                                              WPARAM removed,
                                              LPARAM message) {
    if (code == HC_ACTION && removed == PM_REMOVE && active_) {
      active_->OnMessage(*reinterpret_cast<const MSG*>(message),
                         static_cast<ULONG_PTR>(::GetMessageExtraInfo()));
    }
    return ::CallNextHookEx(nullptr, code, removed, message);
  }

  void OnMessage(const MSG& message, ULONG_PTR extra_info) {
    if (completion_posted_ || extra_info != token_ ||
        !IsInputOwner(message.hwnd, owners_) || !matches_.Run(message)) {
      return;
    }
    completion_posted_ = true;
    // 현재 native 입력 메시지가 처리된 뒤 완료한다.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&NativeInputCompletion::Complete, weak_.GetWeakPtr()));
  }

  void Complete() { completed_.SetValue(); }

  inline static thread_local NativeInputCompletion* active_ = nullptr;
  const std::set<HWND> owners_;
  const base::RepeatingCallback<bool(const MSG&)> matches_;
  const base::TimeTicks deadline_;
  bool completion_posted_ = false;
  ULONG_PTR token_ = 0;
  HHOOK hook_ = nullptr;
  base::test::TestFuture<void> completed_;
  base::WeakPtrFactory<NativeInputCompletion> weak_{this};
};

INPUT KeyboardInput(ui::KeyboardCode key, bool key_up, ULONG_PTR token) {
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = ui::WindowsKeyCodeForKeyboardCode(key);
  UINT scan = ::MapVirtualKey(input.ki.wVk, MAPVK_VK_TO_VSC);
  const bool extended = UsesExtendedScanCode(key);
  if (extended) {
    scan |= KF_EXTENDED;
  }
  // 레이아웃 의존 키는 upstream처럼 가상 키로 보낸다.
  if (input.ki.wVk == ::MapVirtualKey(scan, MAPVK_VSC_TO_VK)) {
    input.ki.wScan = static_cast<WORD>(scan);
    input.ki.dwFlags |= KEYEVENTF_SCANCODE;
  }
  if (extended) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  if (key_up) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  input.ki.dwExtraInfo = token;
  return input;
}

}  // namespace

void InstallOwnedWindowsUIControls() {
  ui_controls::InstallUIControlsAura(new OwnedWindowsUIControls());
}

void QueueOwnedDragUnblockNudge() {
  const HWND foreground = OwnedForegroundWindow();
  const auto cursor = OwnedMouseButtonPoint(foreground, /*pressed=*/false);
  if (!cursor || !IsOwnedScreenPoint(*cursor)) {
    return;
  }
  gfx::Point adjacent = *cursor;
  adjacent.Offset(0, 1);
  if (!IsOwnedScreenPoint(adjacent)) {
    adjacent.Offset(0, -2);
  }
  constexpr int kFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;
  constexpr auto kNudge = ui::MouseEventExtraInfo::kDragUnblockNudge;
  if (IsOwnedScreenPoint(adjacent) && ::GetForegroundWindow() == foreground &&
      ui::SendMouseEvent(adjacent, kFlags, kNudge) &&
      IsOwnedScreenPoint(*cursor) && ::GetForegroundWindow() == foreground) {
    ui::SendMouseEvent(*cursor, kFlags, kNudge);
  }
}

ScopedLauncherForegroundPermission::ScopedLauncherForegroundPermission() {
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (!command_line.HasSwitch("yee-test-foreground-handoff") ||
      command_line.HasSwitch(switches::kProcessType) ||
      command_line.HasSwitch(switches::kTestChildProcess) ||
      command_line.HasSwitch(switches::kLaunchAsBrowser) ||
      command_line.HasSwitch("headless")) {
    return;
  }
  if (!command_line.HasSwitch(switches::kSingleProcessTests) ||
      ::GetFileType(::GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_PIPE) {
    ready_ = false;
    LOG(ERROR) << "Foreground handoff requires a piped single-process test.";
    return;
  }
  const base::ProcessId parent =
      base::GetParentProcessId(base::GetCurrentProcessHandle());
  launcher_ = base::Process::OpenWithAccess(
      parent, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION);
  const base::Time parent_started =
      launcher_.IsValid() ? launcher_.CreationTime() : base::Time();
  const base::Time test_started = base::Process::Current().CreationTime();
  // 실행기 종료 뒤 재사용된 부모 PID에는 권한을 주지 않는다.
  ready_ = !parent_started.is_null() && parent_started < test_started &&
           launcher_.IsRunning() && std::getchar() == 'Y' &&
           launcher_.IsRunning();
  if (!ready_) {
    LOG(ERROR) << "Missing foreground test launcher startup signal.";
  }
}

ScopedLauncherForegroundPermission::~ScopedLauncherForegroundPermission() {
  if (ready_ && launcher_.IsValid() && launcher_.IsRunning()) {
    const bool granted = ::AllowSetForegroundWindow(launcher_.Pid());
    LOG(INFO) << "Foreground permission returned to test launcher: " << granted;
  }
}

bool ShowAndFocusNativeWindow(gfx::NativeWindow window) {
  const HWND hwnd = OwnedWindowHandle(window);
  if (!hwnd || base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
    return false;
  }
  auto* widget = views::Widget::GetWidgetForNativeWindow(window);
  if (!widget) {
    return false;
  }

  const bool already_active = widget->IsVisible() && widget->IsActive();
  if (already_active && ::GetForegroundWindow() == hwnd) {
    return true;
  }
  const auto weak_widget = widget->GetWeakPtr();
  AppliedTestContext<AppliedViewsObservers> applied;
  const auto result = applied.Run(
      {applied.Widget(*widget, {.visible = true}, AppliedMatch::kWidgetVisible),
       applied.Widget(*widget, {.active = true}, AppliedMatch::kWidgetActive)},
      [&] {
        if (!weak_widget) {
          return;
        }
        weak_widget->Show();
        if (!weak_widget) {
          return;
        }
        ::ShowWindow(hwnd, SW_SHOW);
        if (!weak_widget) {
          return;
        }
        weak_widget->Activate();
        if (weak_widget && IsOwnedWindow(hwnd)) {
          ::SetForegroundWindow(hwnd);
        }
      },
      AppliedDomain::kWidget, TestTimeouts::action_timeout(), already_active);
  if (!result) {
    LOG(ERROR) << result.message();
  }
  if (::GetForegroundWindow() != hwnd) {
    LOG(ERROR) << "Windows did not activate the test-owned foreground window.";
    return false;
  }
  return static_cast<bool>(result);
}

NativeMouseButtonResult SendLeftMouseButtonSync(gfx::NativeWindow window,
                                                bool pressed,
                                                base::TimeTicks deadline) {
  const HWND hwnd = OwnedWindowHandle(window);
  const auto point = OwnedMouseButtonPoint(hwnd, pressed);
  if (!point) {
    return {};
  }
  NativeInputCompletion button(
      hwnd, NativeInputCompletion::Source::kMouse,
      base::BindLambdaForTesting([pressed](const MSG& message) {
        if (pressed) {
          return message.message == WM_LBUTTONDOWN ||
                 message.message == WM_NCLBUTTONDOWN ||
                 message.message == WM_LBUTTONDBLCLK ||
                 message.message == WM_NCLBUTTONDBLCLK;
        }
        return message.message == WM_LBUTTONUP ||
               message.message == WM_NCLBUTTONUP;
      }),
      deadline);
  if (!button.ready() || !button.HasTime()) {
    return {};
  }
  const auto token = static_cast<ui::MouseEventExtraInfo>(
      base::checked_cast<int>(button.token()));
  const int flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  const bool sent =
      ui::SendMouseEvent(*point, flags | MOUSEEVENTF_VIRTUALDESK, token);
  const bool completed = sent && button.Wait();
  if (sent && !completed) {
    LOG(ERROR) << "Native mouse " << (pressed ? "press" : "release")
               << " was not acknowledged at " << point->ToString();
  }
  return {.sent = sent, .completed = completed};
}

namespace {

bool SendMouseClickSync(HWND hwnd, bool right, bool allow_owned_capture) {
  if (!OwnedMouseButtonPoint(hwnd, true, allow_owned_capture)) {
    return false;
  }
  NativeInputCompletion click(
      hwnd, NativeInputCompletion::Source::kMouse,
      base::BindLambdaForTesting([right](const MSG& message) {
        return message.message == (right ? WM_RBUTTONUP : WM_LBUTTONUP) ||
               message.message == (right ? WM_NCRBUTTONUP : WM_NCLBUTTONUP);
      }));
  if (!click.ready() || !click.HasTime() ||
      !OwnedMouseButtonPoint(hwnd, true, allow_owned_capture)) {
    return false;
  }
  INPUT down = {};
  down.type = INPUT_MOUSE;
  down.mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
  down.mi.dwExtraInfo = click.token();
  INPUT up = down;
  up.mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
  // 한 번에 넣어 누름과 해제 사이에 외부 입력이 끼어들지 않게 한다.
  INPUT events[] = {down, up};
  const UINT sent = ::SendInput(2, events, sizeof(INPUT));
  if (sent == 1 && OwnedMouseButtonPoint(hwnd, false)) {
    // 일부만 전송됐으면 소유 창 안에서 이 요청의 누름을 해제한다.
    ::SendInput(1, &up, sizeof(INPUT));
  }
  return sent == 2 && click.Wait();
}

}  // namespace

bool SendLeftMouseClickSync(gfx::NativeWindow window) {
  return SendMouseClickSync(OwnedWindowHandle(window), /*right=*/false,
                            /*allow_owned_capture=*/false);
}

bool SendContextMenuClickSync(views::View& target,
                              NativeContextMenuAction action) {
  auto* const widget = target.GetWidget();
  if (!widget || !target.IsDrawn()) {
    return false;
  }
  views::ViewTracker tracker(&target);
  const auto bounds = target.GetBoundsInScreen();
  const auto window = widget->GetNativeWindow();
  if (bounds.IsEmpty() || !SendMouseMoveSync(bounds.CenterPoint(), window)) {
    return false;
  }
  auto* const current = tracker.view();
  if (!current || current->GetWidget() != widget || !current->IsDrawn() ||
      !current->GetBoundsInScreen().Contains(
          display::Screen::Get()->GetCursorScreenPoint())) {
    return false;
  }
  const bool open = action == NativeContextMenuAction::kOpen;
  // 메뉴를 선택할 때는 같은 소유 체인의 메뉴가 잡은 capture도 허용한다.
  return SendMouseClickSync(OwnedWindowHandle(widget->GetNativeWindow()),
                            /*right=*/open, /*allow_owned_capture=*/!open);
}

bool SendLeftMouseButtonRelease(gfx::NativeWindow window) {
  const auto point = OwnedMouseButtonPoint(OwnedWindowHandle(window), false);
  return point && ui::SendMouseEvent(
                      *point, MOUSEEVENTF_LEFTUP | MOUSEEVENTF_VIRTUALDESK);
}

bool NameTaskbarTestWindow(gfx::NativeWindow window,
                           std::wstring_view title_marker) {
  const HWND hwnd = OwnedWindowHandle(window);
  const auto& command_line = *base::CommandLine::ForCurrentProcess();
  if (!hwnd || title_marker.empty() ||
      !command_line.HasSwitch("yee-test-foreground-handoff")) {
    return false;
  }
  const std::wstring label(title_marker);
  // Explorer는 개별 창 제목과 작업 표시줄의 앱 그룹 이름을 따로 관리한다.
  // 창에만 속성을 두고 마지막에 고유 앱 ID를 설정해 작업 표시줄을 갱신한다.
  ui::win::SetRelaunchDetailsForWindow(command_line.GetCommandLineString(),
                                       label, hwnd);
  ui::win::SetAppIdForWindow(L"Yee.Automation." + label, hwnd);
  return true;
}

bool RestoreFromTaskbar(gfx::NativeWindow window,
                        std::wstring_view title_marker) {
  const HWND target = OwnedWindowHandle(window);
  if (!target || title_marker.empty() || !::IsIconic(target)) {
    return false;
  }
  std::array<wchar_t, 512> title{};
  const int title_length =
      ::GetWindowTextW(target, title.data(), title.size());
  if (std::wstring_view(title.data(), title_length).find(title_marker) ==
      std::wstring_view::npos) {
    LOG(ERROR) << "Test window title does not contain its taskbar marker.";
    return false;
  }

  const HWND tray = ::FindWindowW(L"Shell_TrayWnd", nullptr);
  DWORD shell_pid = 0;
  DWORD tray_pid = 0;
  ::GetWindowThreadProcessId(::GetShellWindow(), &shell_pid);
  ::GetWindowThreadProcessId(tray, &tray_pid);
  if (!shell_pid || tray_pid != shell_pid) {
    LOG(ERROR) << "Windows taskbar does not belong to the shell.";
    return false;
  }
  HWND task_list = nullptr;
  ::EnumChildWindows(tray, FindTaskList, reinterpret_cast<LPARAM>(&task_list));
  Microsoft::WRL::ComPtr<IAccessible> accessible;
  if (!task_list || FAILED(::AccessibleObjectFromWindow(
                        task_list, OBJID_CLIENT, IID_PPV_ARGS(&accessible)))) {
    LOG(ERROR) << "Windows task list accessibility is unavailable.";
    return false;
  }
  const std::wstring group_prefix = std::wstring(title_marker) + L" - ";
  const auto matches = [&](VARIANT child) {
    base::win::ScopedBstr name;
    if (FAILED(accessible->get_accName(child, name.Receive())) || !name.Get()) {
      return false;
    }
    const std::wstring_view text(name.Get(), name.Length());
    // Explorer는 그룹 이름 뒤에 실행 중인 창 수를 현지화해 붙인다.
    if (text != title_marker && !text.starts_with(group_prefix)) {
      return false;
    }
    base::win::ScopedVariant role;
    base::win::ScopedVariant state;
    LONG x = 0, y = 0, width = 0, height = 0;
    return SUCCEEDED(accessible->get_accRole(child, role.Receive())) &&
           role.type() == VT_I4 && role.ptr()->lVal == ROLE_SYSTEM_PUSHBUTTON &&
           SUCCEEDED(accessible->get_accState(child, state.Receive())) &&
           state.type() == VT_I4 &&
           !(state.ptr()->lVal & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN |
                                  STATE_SYSTEM_UNAVAILABLE)) &&
           SUCCEEDED(accessible->accLocation(&x, &y, &width, &height, child)) &&
           width > 0 && height > 0;
  };
  const auto find = base::BindLambdaForTesting([&]() -> LONG {
    LONG count = 0;
    if (FAILED(accessible->get_accChildCount(&count))) {
      return -1;
    }
    LONG match = 0;
    for (LONG id = 1; id <= count; ++id) {
      VARIANT child{};
      child.vt = VT_I4;
      child.lVal = id;
      if (matches(child)) {
        if (match != 0) {
          return -1;
        }
        match = id;
      }
    }
    return match;
  });
  {
    TaskbarEntryWatch watch(tray, shell_pid, find);
    if (watch.Wait() <= 0) {
      LOG(ERROR) << "No unique visible taskbar button for the test window.";
      return false;
    }
  }
  VARIANT match{};
  match.vt = VT_I4;
  match.lVal = find.Run();
  if (match.lVal <= 0 || !matches(match) || !::IsIconic(target)) {
    LOG(ERROR) << "No current taskbar entry for the minimized test window.";
    return false;
  }
  return SUCCEEDED(accessible->accDoDefaultAction(match));
}

bool SendMouseMoveSync(const gfx::Point& screen_dip,
                       gfx::NativeWindow window,
                       base::TimeTicks deadline) {
  const HWND capture = ::GetCapture();
  const HWND hwnd = OwnedWindowHandle(window);
  auto* screen = display::win::GetScreenWin();
  if (!IsOwnedWindow(hwnd) || !screen ||
      base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
    return false;
  }
  gfx::Point target = screen->DIPToScreenPoint(screen_dip);
  const auto read_dip = [screen](const gfx::Point& pixel) {
    return gfx::ToFlooredPoint(screen->ScreenToDIPPoint(gfx::PointF(pixel)));
  };
  const gfx::Point rounded_dip = read_dip(target);
  if (rounded_dip != screen_dip) {
    // GetCursorScreenPoint의 내림 방식으로도 요청 DIP가 되는 픽셀을 고른다.
    target.Offset(std::clamp(screen_dip.x() - rounded_dip.x(), -1, 1),
                  std::clamp(screen_dip.y() - rounded_dip.y(), -1, 1));
    if (read_dip(target) != screen_dip) {
      return false;
    }
  }
  NativeInputCompletion moved(
      hwnd, NativeInputCompletion::Source::kMouse,
      base::BindLambdaForTesting([target](const MSG& message) {
        return (message.message == WM_MOUSEMOVE ||
                message.message == WM_NCMOUSEMOVE) &&
               std::abs(message.pt.x - target.x()) <= 1 &&
               std::abs(message.pt.y - target.y()) <= 1;
      }),
      deadline);
  const auto owns_destination = [&](const gfx::Point& point) {
    return ::GetCapture() == capture && moved.OwnsForeground() &&
           (!capture || moved.OwnsWindow(capture)) && moved.OwnsPoint(point);
  };
  POINT cursor = {};
  if (!moved.ready() || !owns_destination(target) || !::GetCursorPos(&cursor)) {
    return false;
  }
  constexpr int flags =
      MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE_NOCOALESCE;
  if (cursor.x == target.x() && cursor.y == target.y()) {
    // 같은 점이면 소유 창 안에서 한 픽셀 움직인 뒤 정확한 목표로 돌아온다.
    gfx::Point adjacent = target;
    adjacent.Offset(1, 0);
    if (!owns_destination(adjacent)) {
      adjacent.Offset(-2, 0);
    }
    if (!moved.HasTime() || !owns_destination(adjacent) ||
        !ui::SendMouseEvent(adjacent, flags)) {
      return false;
    }
  }
  const auto token = static_cast<ui::MouseEventExtraInfo>(
      base::checked_cast<int>(moved.token()));
  if (!moved.HasTime() || !owns_destination(target) ||
      !ui::SendMouseEvent(target, flags, token)) {
    return false;
  }
  if (!moved.Wait()) {
    LOG(ERROR) << "Native mouse move was not acknowledged at "
               << target.ToString();
    return false;
  }
  return display::Screen::Get()->GetCursorScreenPoint() == screen_dip;
}

bool SendKeyPressToWindowSync(gfx::NativeWindow window,
                              ui::KeyboardCode key,
                              bool control,
                              bool shift,
                              bool alt,
                              bool command) {
  const HWND hwnd = OwnedWindowHandle(window);
  if (!hwnd || command || (control && key == ui::VKEY_ESCAPE) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
    return false;
  }
  const auto owners = InputOwners(hwnd);
  if (!IsInputOwner(::GetForegroundWindow(), owners) &&
      !ShowAndFocusNativeWindow(window)) {
    return false;
  }

  std::vector<ui::KeyboardCode> modifiers;
  if (control) {
    modifiers.push_back(ui::VKEY_CONTROL);
  }
  if (alt) {
    modifiers.push_back(ui::VKEY_LMENU);
  }
  if (shift) {
    modifiers.push_back(ui::VKEY_SHIFT);
  }
  std::vector<WORD> releases{
      MessageKey(ui::WindowsKeyCodeForKeyboardCode(key))};
  for (auto modifier : modifiers) {
    releases.push_back(MessageKey(ui::WindowsKeyCodeForKeyboardCode(modifier)));
  }

  size_t release_index = 0;
  NativeInputCompletion released(
      hwnd, NativeInputCompletion::Source::kKeyboard,
      base::BindLambdaForTesting(
          [&releases, &release_index](const MSG& message) {
            if ((message.message != WM_KEYUP &&
                 message.message != WM_SYSKEYUP) ||
                MessageKey(static_cast<WORD>(message.wParam)) !=
                    releases[release_index]) {
              return false;
            }
            return ++release_index == releases.size();
          }));
  if (!released.ready() || !released.OwnsForeground()) {
    return false;
  }
  std::vector<INPUT> inputs;
  for (auto modifier : modifiers) {
    inputs.push_back(KeyboardInput(modifier, false, released.token()));
  }
  inputs.push_back(KeyboardInput(key, false, released.token()));
  inputs.push_back(KeyboardInput(key, true, released.token()));
  for (auto modifier : modifiers) {
    inputs.push_back(KeyboardInput(modifier, true, released.token()));
  }

  if (!released.OwnsForeground()) {
    return false;
  }
  const UINT sent = ::SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                                sizeof(INPUT));
  if (sent != inputs.size()) {
    LOG(ERROR) << "Partial keyboard input: sent=" << sent
               << " expected=" << inputs.size();
    if (sent && released.OwnsForeground()) {
      // 일부만 전송됐으면 이 요청의 키를 해제한 뒤 실패한다.
      std::vector<INPUT> cleanup{KeyboardInput(key, true, released.token())};
      for (auto modifier : modifiers) {
        cleanup.push_back(KeyboardInput(modifier, true, released.token()));
      }
      if (::SendInput(static_cast<UINT>(cleanup.size()), cleanup.data(),
                      sizeof(INPUT)) != cleanup.size()) {
        LOG(ERROR) << "Keyboard release cleanup failed.";
      }
    }
    return false;
  }
  return released.Wait();
}

bool SendTextToWindowSync(gfx::NativeWindow window, std::u16string_view text) {
  const HWND hwnd = OwnedWindowHandle(window);
  if (!hwnd || text.empty() ||
      base::CommandLine::ForCurrentProcess()->HasSwitch("headless")) {
    return false;
  }
  for (char16_t character : text) {
    if (::GetWindowThreadProcessId(hwnd, nullptr) != ::GetCurrentThreadId() ||
        ::GetForegroundWindow() != hwnd || ::GetFocus() != hwnd) {
      return false;
    }
    // 창 전환 중에도 다른 앱의 키보드 큐에 시험 문자를 보내지 않는다.
    ::SendMessageW(hwnd, WM_CHAR, character, /*repeat_count=*/1);
  }
  return true;
}

class ScopedNativeInputTrace::Observer final : public ui::EventObserver {
 public:
  explicit Observer(views::Widget* widget)
      : expected_window_(widget ? views::HWNDForWidget(widget) : nullptr),
        monitor_(views::EventMonitor::CreateApplicationMonitor(
            this,
            widget ? widget->GetNativeWindow() : nullptr,
            {ui::EventType::kMouseMoved, ui::EventType::kMouseDragged,
             ui::EventType::kMousePressed, ui::EventType::kMouseReleased,
             ui::EventType::kKeyPressed, ui::EventType::kKeyReleased})) {}
  ~Observer() override = default;

  void OnEvent(const ui::Event& event) override {
    auto& record = events_[event_count_ % kEventCapacity];
    ++event_count_;
    record.name = event.GetName();
    record.flags = event.flags();
    record.timestamp = event.time_stamp();
    record.observed = base::TimeTicks::Now();
    record.located = event.IsLocatedEvent();
    if (record.located) {
      record.screen_dip = event.AsLocatedEvent()->root_location_f();
    }
    record.has_native = event.HasNativeEvent();
    if (record.has_native) {
      record.message = event.native_event();
      record.native_pid = 0;
      record.native_thread =
          ::GetWindowThreadProcessId(record.message.hwnd, &record.native_pid);
      record.extra_info = ::GetMessageExtraInfo();
    }
  }

  void Snapshot(std::string_view phase, const gfx::Point& intended) {
    base::DictValue value;
    value.Set("phase", phase);
    value.Set("observed_us", Micros(base::TimeTicks::Now()));
    value.Set("events_seen", event_count_);
    value.Set("intended_screen_dip", intended.ToString());
    value.Set("expected_window", Window(expected_window_));
    value.Set("foreground_window", Window(::GetForegroundWindow()));
    value.Set("calling_thread_capture", Window(::GetCapture()));
    const DWORD target_thread =
        ::GetWindowThreadProcessId(expected_window_, nullptr);
    GUITHREADINFO gui = {};
    gui.cbSize = sizeof(gui);
    const bool gui_ok =
        target_thread && ::GetGUIThreadInfo(target_thread, &gui);
    value.Set("target_thread_gui_query_ok", gui_ok);
    if (gui_ok) {
      value.Set("target_thread_capture", Window(gui.hwndCapture));
      value.Set("target_thread_focus", Window(gui.hwndFocus));
      value.Set("target_thread_active", Window(gui.hwndActive));
    }
    auto* screen = display::win::GetScreenWin();
    value.Set("screen_win_available", screen != nullptr);
    if (screen) {
      const auto physical = screen->DIPToScreenPoint(intended);
      value.Set("intended_screen_physical", physical.ToString());
      value.Set("window_at_intended_point",
                Window(::WindowFromPoint({physical.x(), physical.y()})));
    }
    POINT cursor = {};
    const bool cursor_ok = ::GetCursorPos(&cursor);
    value.Set("cursor_query_ok", cursor_ok);
    if (cursor_ok) {
      value.Set("cursor_screen_physical",
                gfx::Point(cursor.x, cursor.y).ToString());
      value.Set("window_at_cursor", Window(::WindowFromPoint(cursor)));
      if (screen) {
        value.Set("cursor_screen_dip",
                  screen->ScreenToDIPPoint(gfx::PointF(cursor.x, cursor.y))
                      .ToString());
      }
    }
    value.Set("expected_window_dpi",
              static_cast<int>(::GetDpiForWindow(expected_window_)));
    if (snapshots_.size() == kSnapshotCapacity) {
      snapshots_.pop_front();
      ++dropped_snapshots_;
    }
    snapshots_.push_back(std::move(value));
  }

  std::string ToJson() const {
    base::DictValue root;
    root.Set("observation_boundary", "application_pre_target");
    root.Set("process_id", static_cast<int>(::GetCurrentProcessId()));
    root.Set("force_device_scale_factor",
             base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                 "force-device-scale-factor"));
    root.Set("force_ui_direction",
             base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                 "force-ui-direction"));
    root.Set("dropped_events", std::max(0, event_count_ - kEventCapacity));
    root.Set("dropped_snapshots", dropped_snapshots_);
    base::ListValue events;
    for (int sequence = std::max(0, event_count_ - kEventCapacity);
         sequence < event_count_; ++sequence) {
      const auto& record = events_[sequence % kEventCapacity];
      base::DictValue value;
      value.Set("sequence", sequence + 1);
      value.Set("name", record.name);
      value.Set("flags", record.flags);
      value.Set("event_us", Micros(record.timestamp));
      value.Set("observed_us", Micros(record.observed));
      if (record.located) {
        value.Set("screen_dip", record.screen_dip.ToString());
      }
      value.Set("has_native_message", record.has_native);
      if (record.has_native) {
        value.Set("native_hwnd", Address(record.message.hwnd));
        value.Set("native_pid", static_cast<int>(record.native_pid));
        value.Set("native_thread", static_cast<int>(record.native_thread));
        value.Set("native_message", static_cast<int>(record.message.message));
        value.Set("native_wparam", base::NumberToString(record.message.wParam));
        value.Set("native_lparam", base::NumberToString(record.message.lParam));
        value.Set("native_time_ms", base::NumberToString(record.message.time));
        value.Set(
            "native_screen_point",
            gfx::Point(record.message.pt.x, record.message.pt.y).ToString());
        value.Set("current_message_extra_info",
                  base::NumberToString(record.extra_info));
      }
      events.Append(std::move(value));
    }
    base::ListValue snapshots;
    for (const auto& snapshot : snapshots_) {
      snapshots.Append(snapshot.Clone());
    }
    root.Set("events", std::move(events));
    root.Set("snapshots", std::move(snapshots));
    std::string json;
    base::JSONWriter::Write(root, &json);
    return json;
  }

 private:
  static constexpr int kEventCapacity = 64;
  static constexpr size_t kSnapshotCapacity = 32;
  struct EventRecord {
    std::string name;
    int flags = 0;
    base::TimeTicks timestamp;
    base::TimeTicks observed;
    bool located = false;
    gfx::PointF screen_dip;
    bool has_native = false;
    CHROME_MSG message = {};
    DWORD native_pid = 0;
    DWORD native_thread = 0;
    LPARAM extra_info = 0;
  };

  static std::string Micros(base::TimeTicks time) {
    return base::NumberToString(time.since_origin().InMicroseconds());
  }
  static std::string Address(HWND window) {
    return base::StringPrintf("%p", static_cast<void*>(window));
  }
  static base::DictValue Window(HWND window) {
    DWORD pid = 0;
    const DWORD thread = ::GetWindowThreadProcessId(window, &pid);
    return base::DictValue()
        .Set("hwnd", Address(window))
        .Set("root_hwnd", Address(::GetAncestor(window, GA_ROOT)))
        .Set("owner_hwnd", Address(::GetWindow(window, GW_OWNER)))
        .Set("valid", !!::IsWindow(window))
        .Set("pid", static_cast<int>(pid))
        .Set("thread", static_cast<int>(thread));
  }

  const HWND expected_window_;
  std::array<EventRecord, kEventCapacity> events_;
  int event_count_ = 0;
  std::deque<base::DictValue> snapshots_;
  int dropped_snapshots_ = 0;
  std::unique_ptr<views::EventMonitor> monitor_;
};

ScopedNativeInputTrace::ScopedNativeInputTrace(views::Widget* widget)
    : observer_(std::make_unique<Observer>(widget)) {}
ScopedNativeInputTrace::~ScopedNativeInputTrace() = default;
void ScopedNativeInputTrace::Snapshot(std::string_view phase,
                                      const gfx::Point& intended_screen_dip) {
  observer_->Snapshot(phase, intended_screen_dip);
}
std::string ScopedNativeInputTrace::ToJson() const {
  return observer_->ToJson();
}

}  // namespace yee::test
