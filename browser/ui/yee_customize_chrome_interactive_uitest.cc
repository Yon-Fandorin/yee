// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/side_panel/side_panel_action_callback.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/animations/side_panel_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/webui/test_support/webui_interactive_test_mixin.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/policy/core/common/management/scoped_management_service_override_for_testing.h"
#include "components/prefs/pref_service.h"
#include "components/search/ntp_features.h"
#include "components/themes/ntp_background_data.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "extensions/browser/install_verifier.h"
#include "extensions/test/test_extension_dir.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/actions/actions.h"
#include "ui/base/test/ui_controls.h"
#if BUILDFLAG(IS_WIN)
#include "ui/aura/window.h"
#endif
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/interaction/element_tracker_views.h"

namespace {
using namespace yee::test;
class YeeCustomizeChromeInteractiveTest
    : public WebUiInteractiveTestMixin<InteractiveBrowserTest> {
 public:
  void SetUp() override {
    scoped_feature_list_.InitWithFeatures(
        {ntp_features::kNtpFooter},
        {ntp_features::kNtpBackgroundImageErrorDetection});
    InteractiveBrowserTest::SetUp();
  }

  void TearDownOnMainThread() override {
    applied_.Stop();
    WebUiInteractiveTestMixin<InteractiveBrowserTest>::TearDownOnMainThread();
  }

 protected:
  AppliedTestContext<AppliedBrowserObservers> applied_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(YeeCustomizeChromeInteractiveTest,
                       YeeTabLayoutSelectPopupFollowsItsHost) {
  base::test::ScopedRunLoopTimeout test_timeout(FROM_HERE, base::Seconds(45));
  auto* const browser_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  const auto original_z_order = browser_widget->GetZOrderLevel();
  browser_widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(embedded_test_server()->Start());
  // 패널 등록에 필요한 새 탐색을 로컬 페이지로 완료해 NTP 제공자에 의존하지
  // 않는다.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  RunTestSequence(
      Do([&] {
        ASSERT_TRUE(chrome::ExecuteCommandWithContext(
            browser(), IDC_SHOW_CUSTOMIZE_CHROME_SIDE_PANEL,
            actions::ActionInvocationContext::Builder()
                .SetProperty(
                    kSidePanelOpenTriggerKey,
                    static_cast<std::underlying_type_t<SidePanelOpenTrigger>>(
                        SidePanelOpenTrigger::kToolbarButton))
                .Build()));
      }),
      WaitForShow(kCustomizeChromeSidePanelWebViewElementId));
  auto* const view =
      views::ElementTrackerViews::GetInstance()
          ->GetUniqueViewAs<views::WebView>(
              kCustomizeChromeSidePanelWebViewElementId,
              views::ElementTrackerViews::GetContextForWidget(browser_widget));
  ASSERT_TRUE(view);
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*view, {.drawn = true}, AppliedMatch::kViewDrawn)}, [] {},
      AppliedDomain::kLayout, TestTimeouts::action_timeout(), true));
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(15));
  auto* const contents = view->GetWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  base::test::TestFuture<bool> installed;
  const auto document = applied_.source().InstallWebUiObserver(
      *contents->GetPrimaryMainFrame(),
      {.root = "html", .probe = WebUiAppliedSelectors::Probe::kTabLayout},
      installed.GetCallback());
  ASSERT_TRUE(installed.Get());
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(
          document,
          WebUiStateMatches{.document = document, .customization_ready = true},
          AppliedMatch::kWebUiCustomizationReady)},
      [] {}, AppliedDomain::kWebUi, TestTimeouts::action_timeout(), true));
  content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(contents);
  // 패널이 자연스럽게 열린 뒤 창 이동 전후의 같은 기준 좌표를 비교한다.
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(applied_.source().ObserveBrowserAnimation(
                              *BrowserView::GetBrowserViewForBrowser(browser()),
                              *BrowserAnimationController::From(browser()),
                              SidePanelAnimations::kSidePanel,
                              SidePanelAnimations::kPanelWidth, 1),
                          AnimationMatches{.group_id = 1, .running = false})},
      [] {}, AppliedDomain::kAnimation, TestTimeouts::action_timeout(), true));
  browser_widget->LayoutRootViewIfNecessary();
  auto* const host = contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
  const gfx::Point initial_position =
      browser_widget->GetWindowBoundsInScreen().origin();
  // 첫 popup은 창 이동으로 좌표를 미리 갱신하지 않고 두 번 확인한다.
  for (const gfx::Point position :
       {initial_position, initial_position, gfx::Point(40, 40),
        gfx::Point(40, 40), gfx::Point(240, 120), gfx::Point(240, 120)}) {
    auto* const widget =
        BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
    gfx::Rect bounds = widget->GetWindowBoundsInScreen();
    if (bounds.origin() != position) {
      bounds.set_origin(position);
      ASSERT_TRUE(applied_.Run({applied_.Widget(*widget, {.bounds = bounds},
                                                AppliedMatch::kWidgetBounds)},
                               [&] {
                                 widget->SetBounds(bounds);
                                 widget->LayoutRootViewIfNecessary();
                               }));
    }
    SCOPED_TRACE(position.ToString());
    // 표시 위치뿐 아니라 select가 참조하는 native 원점도 확인한다.
    EXPECT_EQ(view->holder()->GetBoundsInScreen(),
              contents->GetRenderWidgetHostView()->GetViewBounds());
    auto* const native_view =
        contents->GetRenderWidgetHostView()->GetNativeView();
    EXPECT_EQ(view->holder()->GetBoundsInScreen(),
              native_view->GetBoundsInScreenWithoutTransform());
    SCOPED_TRACE(
        "native=" + native_view->bounds().ToString() + " untransformed=" +
        native_view->GetBoundsInScreenWithoutTransform().ToString() +
        " parent=" + native_view->parent()->bounds().ToString() +
        " parent_untransformed=" +
        native_view->parent()->GetBoundsInScreenWithoutTransform().ToString());
    const auto rect = content::EvalJs(contents, R"JS(
            (() => {
              const select = document.querySelector('customize-chrome-app')
                .shadowRoot.querySelector('yee-tab-layout')
                .shadowRoot.querySelector('#layout');
              if (select.disabled) throw new Error('Tab layout select disabled');
              select.focus();
              const r = select.getBoundingClientRect();
              return [Math.floor(r.x), Math.floor(r.y),
                      Math.ceil(r.width), Math.ceil(r.height)];
            })();
          )JS")
                          .ExtractList()
                          .Clone();
    const gfx::Rect local(rect[0].GetInt(), rect[1].GetInt(), rect[2].GetInt(),
                          rect[3].GetInt());
    gfx::Rect anchor = local;
    anchor.Offset(
        view->holder()->GetBoundsInScreen().origin().OffsetFromOrigin());
    const auto focus = applied_.source().ObserveRenderWidgetHost(*host);
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(
            focus, FocusMatches{.kind = FocusMatches::Kind::kRendererDocument,
                                .target = focus,
                                .document = focus,
                                .focused = true})},
        [&] { view->RequestFocus(); }));
    content::ShowPopupWidgetWaiter popup_waiter(
        contents, contents->GetPrimaryMainFrame());
    // showPicker도 Blink의 실제 native popup 경로와 화면 좌표 변환을
    // 쓴다.
    ASSERT_TRUE(content::ExecJs(contents, R"JS(
            document.querySelector('customize-chrome-app').shadowRoot
              .querySelector('yee-tab-layout').shadowRoot
              .querySelector('#layout').showPicker();
          )JS"));
    popup_waiter.Wait();
    content::RenderWidgetHost* popup_host = nullptr;
    auto hosts = content::RenderWidgetHost::GetRenderWidgetHosts();
    while (auto* candidate = hosts->GetNextHost()) {
      if (candidate->GetRoutingID() == popup_waiter.last_routing_id() &&
          candidate->GetProcess() == host->GetProcess()) {
        popup_host = candidate;
        break;
      }
    }
    ASSERT_TRUE(popup_host);
    const auto popup_owner =
        applied_.source().ObserveRenderWidgetHost(*popup_host);
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(popup_owner, WidgetStateMatches{.visible = true},
                            AppliedMatch::kWidgetVisible)},
        [] {}, AppliedDomain::kWidget, TestTimeouts::action_timeout(), true));
    ASSERT_TRUE(popup_host->GetView());
    const gfx::Rect popup = popup_host->GetView()->GetViewBounds();
    SCOPED_TRACE("host=" + view->holder()->GetBoundsInScreen().ToString() +
                 " select=" + anchor.ToString() +
                 " request=" + popup_waiter.last_initial_rect().ToString() +
                 " popup=" + popup.ToString());
    EXPECT_GE(anchor.CenterPoint().x(), popup.x());
    EXPECT_LT(anchor.CenterPoint().x(), popup.right());
    EXPECT_LE(popup.y(), anchor.bottom() + anchor.height());
    EXPECT_GE(popup.bottom(), anchor.y() - anchor.height());
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(popup_owner, WidgetStateMatches{.closed = true},
                            AppliedMatch::kWidgetClosed)},
        [&] {
          ASSERT_TRUE(ui_controls::SendKeyPress(widget->GetNativeWindow(),
                                                ui::VKEY_ESCAPE, false, false,
                                                false, false));
        }));
  }
  browser_widget->SetZOrderLevel(original_z_order);
}

}  // namespace
