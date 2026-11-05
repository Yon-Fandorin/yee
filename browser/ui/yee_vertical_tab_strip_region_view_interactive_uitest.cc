// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"

#include "base/test/scoped_run_loop_timeout.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_interactive_test_mixin.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "content/public/test/browser_test.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/widget_activation_waiter.h"

#if BUILDFLAG(IS_WIN)
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/multiprocess_test.h"
#include "base/win/object_watcher.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/scoped_handle.h"
#include "testing/multiprocess_func_list.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/accessibility/platform/ax_platform_node_win.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"

namespace {
using namespace yee::test;

class HandleSignalWaiter : public base::win::ObjectWatcher::Delegate {
 public:
  bool Wait(HANDLE handle) {
    if (!watcher_.StartWatchingOnce(handle, this))
      return false;
    loop_.Run();
    return signaled_;
  }
  void OnObjectSignaled(HANDLE) override {
    signaled_ = true;
    loop_.Quit();
  }

 private:
  base::win::ObjectWatcher watcher_;
  base::RunLoop loop_;
  bool signaled_ = false;
};

std::wstring AccessibilityEventName(std::string_view suffix,
                                    std::string_view parent) {
  return base::UTF8ToWide("Local\\YeeFooterAx" + std::string(suffix) +
                          std::string(parent));
}
}  // namespace

// 외부 UIA 조회와 참조 반환은 브라우저의 STA와 분리한 MTA에서 처리한다.
MULTIPROCESS_TEST_MAIN(YeeFooterAccessibilityClient) {
  std::thread client([]() {
    base::win::ScopedCOMInitializer com(base::win::ScopedCOMInitializer::kMTA);
    CHECK(com.Succeeded());
    const auto& command = *base::CommandLine::ForCurrentProcess();
    const std::string parent = command.GetSwitchValueASCII("yee-ax-parent");
    base::win::ScopedHandle ready(
        ::OpenEvent(EVENT_MODIFY_STATE, FALSE,
                    AccessibilityEventName("Ready", parent).c_str()));
    base::win::ScopedHandle done(::OpenEvent(
        SYNCHRONIZE, FALSE, AccessibilityEventName("Done", parent).c_str()));
    CHECK(ready.is_valid());
    CHECK(done.is_valid());
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    CHECK(SUCCEEDED(::CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&automation))));
    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
    CHECK(SUCCEEDED(automation->CreateTrueCondition(&condition)));
    std::vector<Microsoft::WRL::ComPtr<IUIAutomationElementArray>> trees;
    for (const char* flag : {"yee-ax-browser", "yee-ax-popup"}) {
      uint64_t handle = 0;
      CHECK(base::StringToUint64(command.GetSwitchValueASCII(flag), &handle));
      Microsoft::WRL::ComPtr<IUIAutomationElement> root;
      CHECK(SUCCEEDED(automation->ElementFromHandle(
          reinterpret_cast<HWND>(handle), &root)));
      Microsoft::WRL::ComPtr<IUIAutomationElementArray> tree;
      CHECK(
          SUCCEEDED(root->FindAll(TreeScope_Subtree, condition.Get(), &tree)));
      int length = 0;
      CHECK(SUCCEEDED(tree->get_Length(&length)));
      CHECK_GT(length, 1);
      trees.push_back(std::move(tree));
    }
    CHECK(::SetEvent(ready.get()));
    CHECK_EQ(::WaitForSingleObject(done.get(), 15000), WAIT_OBJECT_0);
    Microsoft::WRL::ComPtr<IUIAutomationElement> closed_popup;
    CHECK(SUCCEEDED(trees.back()->GetElement(0, &closed_popup)));
    CONTROLTYPEID control_type = 0;
    CHECK(FAILED(closed_popup->get_CurrentControlType(&control_type)));
  });
  client.join();
  return 0;
}
#endif

class YeeVerticalTabStripRegionViewInteractiveUiTest
    : public VerticalTabsInteractiveTestMixin<InteractiveBrowserTest> {
 public:
  YeeVerticalTabStripRegionViewInteractiveUiTest() = default;

#if BUILDFLAG(IS_WIN)
  void StartAccessibilityClient(views::Widget& owner, views::Widget& popup) {
    const std::string parent = base::NumberToString(::GetCurrentProcessId());
    ax_ready_.Set(::CreateEvent(
        nullptr, TRUE, FALSE, AccessibilityEventName("Ready", parent).c_str()));
    ax_done_.Set(::CreateEvent(nullptr, TRUE, FALSE,
                               AccessibilityEventName("Done", parent).c_str()));
    ASSERT_TRUE(ax_ready_.is_valid());
    ASSERT_TRUE(ax_done_.is_valid());
    auto command = base::GetMultiProcessTestChildBaseCommandLine();
    command.AppendSwitchASCII("yee-ax-parent", parent);
    command.AppendSwitchASCII(
        "yee-ax-browser",
        base::NumberToString(reinterpret_cast<uintptr_t>(
            owner.GetNativeWindow()->GetHost()->GetAcceleratedWidget())));
    command.AppendSwitchASCII(
        "yee-ax-popup",
        base::NumberToString(reinterpret_cast<uintptr_t>(
            popup.GetNativeWindow()->GetHost()->GetAcceleratedWidget())));
    base::LaunchOptions options;
    options.start_hidden = true;
    ax_client_ = base::SpawnMultiProcessTestChild(
        "YeeFooterAccessibilityClient", command, options);
    ASSERT_TRUE(ax_client_.IsValid());
    HandleSignalWaiter ready;
    ASSERT_TRUE(ready.Wait(ax_ready_.get()));
  }

  void TearDownOnMainThread() override {
    if (ax_client_.IsValid()) {
      base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
      // 외부 참조 반환은 브라우저 STA의 메시지 처리가 필요하다. 창·UI 스레드
      // 종료 전에 클라이언트를 정리하고, 지연된 COM Release까지 처리한다.
      EXPECT_TRUE(::SetEvent(ax_done_.get()));
      int exit_code = -1;
      HandleSignalWaiter exited;
      if (exited.Wait(ax_client_.Handle())) {
        ASSERT_TRUE(
            ax_client_.WaitForExitWithTimeout(base::TimeDelta(), &exit_code));
        EXPECT_EQ(exit_code, 0);
      } else {
        ax_client_.Terminate(1, true);
        ADD_FAILURE() << "접근성 클라이언트가 종료되지 않았습니다.";
      }
    }
    applied_.Stop();
    VerticalTabsInteractiveTestMixin<
        InteractiveBrowserTest>::TearDownOnMainThread();
  }

  void PostRunTestOnMainThread() override {
    VerticalTabsInteractiveTestMixin<
        InteractiveBrowserTest>::PostRunTestOnMainThread();
    if (ax_client_.IsValid()) {
      base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
      // 창 종료 후 남은 UIA/COM 참조 반환을 기다린다.
      ui::AXPlatform::GetInstance().DisableActiveUiaProvider();
      // 클라이언트의 COM apartment 종료와 UiaDisconnectAllProviders의 반환은
      // 참조 해제 완료 경계다. 이후 남은 ghost는 재시도로 숨기지 않는다.
      EXPECT_EQ(0u, ui::AXPlatformNodeWin::GetCounts().ghost_nodes);
    }
  }

#endif

 protected:
  yee::test::AppliedTestContext<yee::test::AppliedBrowserObservers> applied_;

 private:
#if BUILDFLAG(IS_WIN)
  base::win::ScopedHandle ax_ready_;
  base::win::ScopedHandle ax_done_;
  base::Process ax_client_;
#endif
};

#if BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_F(YeeVerticalTabStripRegionViewInteractiveUiTest,
                       YeeFooterPointerExitKeepsPopupAndEscapeRestoresFocus) {
  ASSERT_TRUE(yee::IsShellEnabled());
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(10));
  auto* const browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  gfx::Rect bounds(50, 50, 1000, 700);
  bounds.AdjustToFit(display::Screen::Get()->GetPrimaryDisplay().work_area());
  browser_view->GetWidget()->SetBounds(bounds);
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  tabs::VerticalTabStripStateController::From(browser())->RequestCollapse(
      false);
  RunScheduledLayouts();
  views::View* const footer = region->sidebar_footer_for_testing();
  ASSERT_TRUE(footer);
  ASSERT_TRUE(footer->GetVisible());
  const auto menu_owner = applied_.source().ObserveUiState(
      *footer, AppliedBrowserObservers::UiProbe::kFooterMenu);
  const auto menu_open = applied_.Condition(
      menu_owner, ViewStateMatches{.visible = true}, AppliedMatch::kFooterMenu);

  // 새로 활성화되는 bubble을 기존 interaction Widget에 묶지 않고 클릭한다.
  const gfx::Point center = footer->GetLocalBounds().CenterPoint();
  ui::MouseEvent press(ui::EventType::kMousePressed, center, center,
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, center, center,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  ASSERT_TRUE(applied_.Run({menu_open}, [&] {
    footer->OnMouseEvent(&press);
    footer->OnMouseEvent(&release);
  }));

  ASSERT_NO_FATAL_FAILURE(StartAccessibilityClient(
      *browser_view->GetWidget(),
      *yee::GetSidebarFooterMenuForTesting(*footer)->GetWidget()));

  // 실제 OS 포인터가 부모 창과 trigger에 재진입해도 같은 팝업이 활성 상태다.
  for (const gfx::Point point :
       {browser_view->GetBoundsInScreen().CenterPoint(),
        footer->GetBoundsInScreen().CenterPoint(),
        browser_view->GetBoundsInScreen().CenterPoint()}) {
    SCOPED_TRACE(point.ToString());
    auto* popup = yee::GetSidebarFooterMenuForTesting(*footer)->GetWidget();
    ASSERT_TRUE(applied_.Run({menu_open, applied_.Frame(*popup)}, [&] {
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(point));
    }));
  }

  views::View* const menu = yee::GetSidebarFooterMenuForTesting(*footer);
  ASSERT_TRUE(menu);
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(menu_owner, ViewStateMatches{.visible = false},
                          AppliedMatch::kFooterMenu),
       applied_.Focus(*footer)},
      [&] {
        EXPECT_TRUE(menu->GetFocusManager()->ProcessAccelerator(
            ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
      }));
  EXPECT_EQ(nullptr, yee::GetSidebarFooterMenuForTesting(*footer));
  EXPECT_EQ(views::FocusManager::FocusChangeReason::kFocusTraversal,
            footer->GetFocusManager()->focus_change_reason());
}

#endif
