// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/immersive/immersive_mode_controller.h"

#import <Cocoa/Cocoa.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#import "base/mac/mac_util.h"

#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/side_panel_animations.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller_mac.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/browser/ui/views/yee/native_frame_applied_test_support_mac.h"
#include "chrome/browser/ui/views/yee/page_viewport_geometry.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "ui/base/cocoa/nswindow_test_util.h"
#include "ui/base/hit_test.h"
#include "ui/base/test/ui_controls.h"
#include "ui/base/window_open_disposition.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#import "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/native_widget_mac.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

using namespace yee::test;

class ScopedAlwaysShowToolbar {
 public:
  ScopedAlwaysShowToolbar(BrowserWindowInterface* browser, bool always_show) {
    prefs_ = browser->GetProfile()->GetPrefs();
    original_ = prefs_->GetBoolean(prefs::kShowFullscreenToolbar);
    prefs_->SetBoolean(prefs::kShowFullscreenToolbar, always_show);
  }
  ~ScopedAlwaysShowToolbar() {
    prefs_->SetBoolean(prefs::kShowFullscreenToolbar, original_);
  }

 private:
  raw_ptr<PrefService> prefs_;
  bool original_;
};

class YeeImmersiveModeControllerMacInteractiveTest
    : public InProcessBrowserTest {
 public:
  YeeImmersiveModeControllerMacInteractiveTest() = default;

  YeeImmersiveModeControllerMacInteractiveTest(
      const YeeImmersiveModeControllerMacInteractiveTest&) = delete;
  YeeImmersiveModeControllerMacInteractiveTest& operator=(
      const YeeImmersiveModeControllerMacInteractiveTest&) = delete;

  NSView* GetMovedContentViewForWidget(views::Widget* overlay_widget) {
    return (__bridge NSView*)overlay_widget->GetNativeWindowProperty(
        views::NativeWidgetMacNSWindowHost::kMovedContentNSView);
  }

  // 본문은 macOS 전체화면 캡처의 잘림을 피하고 실제 합성 프레임을 저장한다.
  void CaptureWidgetIfRequested(views::Widget& widget,
                                std::string_view name,
                                SkColor* shell_color = nullptr) {
    const auto directory =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "yee-sidebar-capture-dir");
    if (directory.empty() && !shell_color) {
      return;
    }
    auto* const compositor = widget.GetCompositor();
    base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
    auto request = std::make_unique<viz::CopyOutputRequest>(
        viz::CopyOutputRequest::ResultFormat::RGBA,
        viz::CopyOutputRequest::ResultDestination::kSystemMemory,
        readback.GetCallback());
    request->set_area(gfx::Rect(compositor->size()));
    compositor->root_layer()->RequestCopyOfOutput(std::move(request));
    compositor->ScheduleFullRedraw();
    ASSERT_TRUE(applied_.WaitForPresentedFrame(widget));
    auto result = readback.Take();
    ASSERT_TRUE(result && !result->IsEmpty());
    auto pixels = result->ScopedAccessSkBitmap();
    if (shell_color) {
      // 두 native 창의 조작 없는 leading 여백을 실제 합성 결과로 비교한다.
      const auto& bitmap = pixels.GetOutScopedBitmap();
      int x = static_cast<int>(yee::kSidebarMetrics.content_gutter *
                               compositor->device_scale_factor());
      ASSERT_LT(x, bitmap.width());
      if (base::i18n::IsRTL()) {
        x = bitmap.width() - 1 - x;
      }
      *shell_color = bitmap.getColor(x, bitmap.height() / 2);
    }
    if (directory.empty()) {
      return;
    }
    const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(
        pixels.GetOutScopedBitmap(), /*discard_transparency=*/false);
    ASSERT_TRUE(png);
    ASSERT_TRUE(base::WriteFile(
        directory.AppendASCII(std::string(name) + ".png"), *png));
  }

  void TearDownOnMainThread() override {
    frame_.reset();
    applied_.Stop();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  BrowserView& view() {
    return *BrowserView::GetBrowserViewForBrowser(browser());
  }
  AppliedCondition Frame(NativeFrameMatches expected, AppliedMatch match) {
    if (!frame_)
      frame_ = std::make_unique<NativeFrameAppliedObserver>(applied_.source(),
                                                            view());
    return applied_.Condition(frame_->owner(), expected, match);
  }
  testing::AssertionResult Await(AppliedConditions conditions) {
    return applied_.Run(
        std::move(conditions), [] {}, AppliedDomain::kLayout,
        TestTimeouts::action_timeout(), true);
  }
  AppliedCondition VerticalIdle() {
    return applied_.Condition(
        applied_.source().ObserveBrowserAnimation(
            view(), *BrowserAnimationController::From(browser()),
            TabStripAnimations::kVerticalTabStrip,
            TabStripAnimations::kTabStripWidth, 1),
        AnimationMatches{.group_id = 1, .running = false});
  }
  AppliedCondition Revealed(bool revealed) {
    return Frame({.toolbar_revealed = revealed},
                 AppliedMatch::kNativeToolbarRevealed);
  }
  testing::AssertionResult ActivateBrowserAndWaitForSpace() {
    auto& widget = *view().GetWidget();
    return applied_.Run(
        {applied_.Widget(widget, {}, AppliedMatch::kWidgetReady),
         applied_.Condition(applied_.views().ObserveNativeWindow(widget),
                            NativeWindowMatches{true, true, true,
                                                widget.IsFullscreen(), true})},
        [&] { ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser())); });
  }
  testing::AssertionResult ToggleFullscreen(bool fullscreen) {
    return applied_.Run(
        {applied_.Condition(
            applied_.views().ObserveNativeWindow(*view().GetWidget()),
            NativeWindowMatches{.fullscreen = fullscreen},
            AppliedMatch::kNativeFullscreen)},
        [&] { ui_test_utils::ToggleFullscreenModeAndWait(browser()); });
  }
  testing::AssertionResult RendererReady(content::WebContents& contents,
                                         views::View& viewport) {
    const double scale =
        viewport.GetWidget()->GetCompositor()->device_scale_factor();
    return Await({applied_.source().ExpectRendererViewport(
        contents, gfx::ScaleToCeiledSize(viewport.size(), scale), scale, false,
        true)});
  }

 protected:
  AppliedTestContext<AppliedBrowserObservers> applied_;
  std::unique_ptr<NativeFrameAppliedObserver> frame_;

 public:
  // Convenience function to get the NSWindow from the browser window.
  NSWindow* browser_window() {
    return browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  }
};

IN_PROC_BROWSER_TEST_F(YeeImmersiveModeControllerMacInteractiveTest,
                       ContentFullscreenChildren) {
  base::AutoReset<bool> enable_animation_for_test =
      FindBarHost::SetEnableAnimationsForTesting(false);

  // Enter browser fullscreen.
  ASSERT_TRUE(ToggleFullscreen(true));

  // Open the find bar
  views::NamedWidgetShownWaiter shown_waiter(
      views::test::AnyWidgetTestPasskey{}, "FindBarHost");
  chrome::Find(browser());
  views::Widget* find_bar = shown_waiter.WaitIfNeededAndGet();

  // The find bar should be a child of the overlay widget.
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  EXPECT_EQ(browser_view->overlay_widget(), find_bar->parent());

  // Enter content fullscreen. The find bar should move to become a child of the
  // browser widget.
  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  tab->GetDelegate()->EnterFullscreenModeForTab(tab->GetPrimaryMainFrame(), {});
  EXPECT_EQ(browser_view->GetWidget(), find_bar->parent());

  // Leave content fullscreen (back to browser fullscreen), the find bar should
  // move back to the overlay widget.
  tab->GetDelegate()->ExitFullscreenModeForTab(tab);
  EXPECT_EQ(browser_view->overlay_widget(), find_bar->parent());

  chrome::CloseFind(browser());
}

IN_PROC_BROWSER_TEST_F(YeeImmersiveModeControllerMacInteractiveTest,
                       HandlesOverlayWidgetDestruction) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* overlay_widget = browser_view->overlay_widget();

  // Transition to fullscreen.
  ASSERT_TRUE(ActivateBrowserAndWaitForSpace());
  ASSERT_TRUE(ToggleFullscreen(true));
  ASSERT_FALSE(testing::Test::HasFailure());
  FullscreenController* fullscreen_controller =
      ExclusiveAccessManager::From(browser())->fullscreen_controller();
  ASSERT_TRUE(fullscreen_controller->IsFullscreenForBrowser());

  // Simulate a synchronous destruction of the overlay widget. This should not
  // crash.
  overlay_widget->CloseNow();
  auto* sidebar = browser_view->vertical_tab_strip_region_view_for_testing();
  if (auto* header = sidebar->sidebar_header_actions()) {
    EXPECT_EQ(sidebar, header->parent());
  }

  // Transition out of fullscreen.
  ASSERT_TRUE(ToggleFullscreen(false));
  ASSERT_FALSE(testing::Test::HasFailure());
  EXPECT_FALSE(fullscreen_controller->IsFullscreenForBrowser());
}

IN_PROC_BROWSER_TEST_F(YeeImmersiveModeControllerMacInteractiveTest,
                       ToggleFullscreenWithVerticalTabstrip) {
  gfx::ScopedAnimationDurationScaleMode no_animation(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  ScopedAlwaysShowToolbar scoped_always_show(browser(), false);
  base::ScopedClosureRunner restore_pointer;
  const auto move_system_pointer = [](CGPoint point) {
    // AppKit이 읽는 NSEvent 위치 mock도 실제 포인터와 맞춘다.
    CHECK(ui_controls::SendMouseMove(static_cast<int>(point.x),
                                     static_cast<int>(point.y)));
    CHECK_EQ(kCGErrorSuccess, CGWarpMouseCursorPosition(point));
    base::apple::ScopedCFTypeRef<CGEventRef> event(CGEventCreateMouseEvent(
        nullptr, kCGEventMouseMoved, point, kCGMouseButtonLeft));
    CHECK(event);
    CGEventPost(kCGHIDEventTap, event.get());
  };
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  const auto capture_url =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          "yee-sidebar-capture-url");
  // 실제 입력은 commit된 페이지의 native renderer를 대상으로 한다.
  const GURL page_url(
      capture_url.empty()
          ? "data:text/html,<!doctype html><title>Yee fullscreen</title>"
            "<body tabindex='0' style='margin:0;min-height:100vh'>"
            "Fullscreen layout verification</body>"
          : capture_url);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url));
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  RunScheduledLayouts();
  views::Widget* overlay_widget = browser_view->overlay_widget();
  ui::Layer* const header_layer_parent =
      browser_view->top_container()->layer()
          ? browser_view->top_container()->layer()->parent()
          : nullptr;

  NSView* overlay_widget_content_view =
      overlay_widget->GetNativeWindow().GetNativeNSWindow().contentView;
  NSWindow* overlay_widget_window = [overlay_widget_content_view window];

  EXPECT_EQ(GetMovedContentViewForWidget(overlay_widget), nullptr);
  ASSERT_TRUE(ActivateBrowserAndWaitForSpace());
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  ASSERT_NO_FATAL_FAILURE(
      CaptureWidgetIfRequested(*browser_view->GetWidget(), "windowed-pinned"));
  ASSERT_TRUE(ToggleFullscreen(true));
  ASSERT_FALSE(testing::Test::HasFailure());
  // Browser 상태와 별도로 AppKit의 실제 Space 전환 완료를 기다린다.
  ASSERT_TRUE(applied_.WaitForPresentedFrame(*browser_view->GetWidget()));

  FullscreenController* fullscreen_controller =
      ExclusiveAccessManager::From(browser())->fullscreen_controller();

  ASSERT_TRUE(fullscreen_controller->IsFullscreenForBrowser());
  ASSERT_EQ(GetMovedContentViewForWidget(overlay_widget),
            overlay_widget_content_view);

  EXPECT_NE(overlay_widget_window.contentView, overlay_widget_content_view);

  if (browser_view->UsesYeeBrowserSurfaceGeometry()) {
    auto* const sidebar =
        tabs::VerticalTabStripStateController::From(browser());
    auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
        browser_view->GetBrowserViewLayoutForTesting());
    auto* const page = browser()->tab_strip_model()->GetActiveWebContents();
    auto* const region =
        browser_view->vertical_tab_strip_region_view_for_testing();
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        browser_view->multi_contents_view()->GetBoundsInScreen().CenterPoint(),
        browser_view->GetWidget()->GetNativeWindow()));
    // 실제 AppKit 재배치 후 숨김/고정 Toolbar와 두 Sidebar 상태를 확인한다.
    // 상태 머신만 수동 변경하는 검사로는 외부 창의 레이아웃을 검증할 수 없다.
    for (const bool always_show : {false, true, false}) {
      const auto set_toolbar_pref = [&] {
        browser()->GetProfile()->GetPrefs()->SetBoolean(
            prefs::kShowFullscreenToolbar, always_show);
      };
      if (always_show)
        ASSERT_TRUE(applied_.Run({Revealed(true)}, set_toolbar_pref));
      else
        set_toolbar_pref();
      for (const bool collapsed : {false, true}) {
        SCOPED_TRACE(testing::Message() << "툴바 고정=" << always_show
                                        << ", Sidebar 접힘=" << collapsed);
        ASSERT_TRUE(applied_.Run(
            {applied_.Condition(
                 applied_.source().ObserveUiState(
                     *browser_view,
                     AppliedBrowserObservers::UiProbe::kSidebarCollapsed),
                 ViewStateMatches{.visible = collapsed},
                 AppliedMatch::kViewVisible),
             VerticalIdle()},
            [&] { sidebar->RequestCollapse(collapsed); }));
        if (always_show) {
          ASSERT_TRUE(Await({applied_.View(*browser_view->GetLocationBarView(),
                                           {}, AppliedMatch::kViewFullyVisible),
                             Frame({.toolbar_on_active_space = true},
                                   AppliedMatch::kNativeToolbarActiveSpace)}));
        }
        RunScheduledLayouts();
        ASSERT_TRUE(layout->last_native_layout_for_testing());
        const auto& snapshot = *layout->last_native_layout_for_testing();
        EXPECT_EQ(yee::SurfaceGeometryTransitionPhase::kStable,
                  snapshot.surface_transition_phase);
        EXPECT_FALSE(snapshot.target_content_size);
        EXPECT_EQ(!always_show, snapshot.header_bounds.IsEmpty());
        EXPECT_EQ(always_show ? 0 : yee::kSidebarMetrics.content_gutter,
                  browser_view->multi_contents_view()->y());
        if (always_show) {
          EXPECT_FLOAT_EQ(0.0f, browser_view->multi_contents_view()
                                    ->GetBackgroundRadii()
                                    .upper_left());
          auto* fill = overlay_widget->GetContentsView()->GetViewByID(
              yee::kBrowserSurfaceFillViewId);
          ASSERT_TRUE(fill);
          const gfx::Rect toolbar = fill->GetBoundsInScreen();
          const gfx::Rect body =
              browser_view->multi_contents_view()->GetBoundsInScreen();
          EXPECT_EQ(body.x(), toolbar.x());
          EXPECT_EQ(body.width(), toolbar.width());
          EXPECT_EQ(body.y(),
                    toolbar.y() +
                        yee::kSidebarMetrics.browser_surface_header_height());
          if (!collapsed) {
            auto* create = static_cast<ToolbarButton*>(
                region->sidebar_header_actions()->GetViewByID(
                    yee::kSidebarHeaderCreateViewId));
            ASSERT_TRUE(create);
            EXPECT_NEAR(create->GetBoundsInScreen().CenterPoint().y(),
                        browser_view->GetLocationBarView()
                            ->GetBoundsInScreen()
                            .CenterPoint()
                            .y(),
                        1);
            ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
                *browser_view->GetWidget(), "fullscreen-pinned"));
            ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
                *overlay_widget, "fullscreen-pinned-header"));
            // Sidebar Header와 Toolbar가 같은 native 행에서 실제 입력을 받는다.
            ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
                create->GetBoundsInScreen().CenterPoint()));
            const auto menu = applied_.source().ObserveUiState(
                *create, AppliedBrowserObservers::UiProbe::kToolbarMenu);
            ASSERT_TRUE(applied_.Run(
                {applied_.Condition(menu, ViewStateMatches{.visible = true},
                                    AppliedMatch::kViewVisible)},
                [&] {
                  ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
                      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
                }));
            ASSERT_TRUE(applied_.Run(
                {applied_.Condition(menu, ViewStateMatches{.visible = false},
                                    AppliedMatch::kViewVisible)},
                [&] {
                  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
                      browser(), ui::VKEY_ESCAPE, false, false, false, false));
                }));
            ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(body.CenterPoint()));

            // Side Panel을 함께 열어도 외부 Toolbar가 현재 본문 폭을 따른다.
            auto* const panel = SidePanelUI::From(browser());
            ASSERT_TRUE(panel);
            const auto panel_owner = applied_.source().ObserveUiState(
                *browser_view,
                AppliedBrowserObservers::UiProbe::kSidePanelEntry);
            ASSERT_TRUE(applied_.Run(
                {applied_.Condition(
                    panel_owner,
                    PresentationMatches{.presentation_id =
                                            static_cast<uint64_t>(
                                                SidePanelEntryId::kBookmarks) +
                                            1},
                    AppliedMatch::kPresentationRevision)},
                [&] {
                  panel->Show(SidePanelEntryId::kBookmarks, std::nullopt, true);
                }));
            RunScheduledLayouts();
            const gfx::Rect panel_header = fill->GetBoundsInScreen();
            const gfx::Rect panel_body =
                browser_view->multi_contents_view()->GetBoundsInScreen();
            EXPECT_EQ(panel_body.x(), panel_header.x());
            EXPECT_EQ(panel_body.width(), panel_header.width());
            // Side Panel의 native shadow가 본문 앞에 1 DIP를 배분할 수 있다.
            // 카드의 Header 끝은 본문 창의 원점이며 그 native 배분을 유지한다.
            EXPECT_EQ(browser_view->GetBoundsInScreen().y(),
                      panel_header.y() +
                          yee::kSidebarMetrics.browser_surface_header_height());
            EXPECT_EQ(panel_body.y(),
                      browser_view->GetBoundsInScreen().y() +
                          layout->last_native_layout_for_testing()
                              ->body.notice_flow_allocation.y());
            EXPECT_EQ(1, layout->last_native_layout_for_testing()
                             ->top_container_calculation_count);
            ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
                *browser_view->GetWidget(), "fullscreen-side-panel"));
            ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
                *overlay_widget, "fullscreen-side-panel-header"));
            ASSERT_TRUE(applied_.Run(
                {applied_.Condition(panel_owner,
                                    PresentationMatches{.presentation_id = 0},
                                    AppliedMatch::kPresentationRevision)},
                [&] {
                  panel->Close(SidePanelEntryHideReason::kSidePanelClosed,
                               true);
                }));
            RunScheduledLayouts();
          }
        }

        // 앱 내부 NSEvent만으로는 macOS 메뉴막대가 열리지 않는다.
        // 실제 시스템 포인터를 사용하고 검사 후 원위치로 돌려놓는다.
        ASSERT_TRUE(ActivateBrowserAndWaitForSpace());
        page->Focus();
        ASSERT_TRUE(CGPreflightPostEventAccess())
            << "시스템 메뉴막대 검사는 실행 호스트의 손쉬운 사용 이벤트 "
               "전송 권한이 필요합니다. tools/dev/README.md를 확인하세요.";
        // 후속 검사까지 끝난 뒤 복구해야 원래의 상단 포인터가 메뉴를
        // 다시 열어 같은 case의 상태를 바꾸지 않는다.
        if (!restore_pointer) {
          base::apple::ScopedCFTypeRef<CGEventRef> pointer(
              CGEventCreate(nullptr));
          ASSERT_TRUE(pointer);
          restore_pointer.ReplaceClosure(base::BindOnce(
              move_system_pointer, CGEventGetLocation(pointer.get())));
        }
        const gfx::Point content_center = browser_view->multi_contents_view()
                                              ->GetBoundsInScreen()
                                              .CenterPoint();
        move_system_pointer(
            CGPointMake(content_center.x(), content_center.y()));
        auto* const immersive = static_cast<ImmersiveModeControllerMac*>(
            ImmersiveModeController::From(browser()));
        const gfx::Rect display_bounds =
            display::Screen::Get()
                ->GetDisplayNearestWindow(
                    browser_view->GetWidget()->GetNativeWindow())
                .bounds();
        auto* const external_top = browser_view->top_container();
        auto* const external_root = external_top->parent();
        const auto toolbar_mode = Frame({.toolbar_revealed = always_show},
                                        AppliedMatch::kNativeToolbarMode);
        ASSERT_TRUE(Await({toolbar_mode}));
        EXPECT_EQ(overlay_widget_content_view,
                  GetMovedContentViewForWidget(overlay_widget));
        // pref 전환의 native 창 원점과 실제 표시 프레임까지 기다린다.
        ASSERT_TRUE(applied_.WaitForPresentedFrame(*browser_view->GetWidget()));
        RunScheduledLayouts();
        const int hidden_menu_y = external_root->GetBoundsInScreen().y();
        const gfx::Rect hidden_sidebar = region->GetBoundsInScreen();
        const gfx::Rect hidden_contents =
            browser_view->multi_contents_view()->bounds();
        const gfx::Rect hidden_page = browser_view->multi_contents_view()
                                          ->GetActiveContentsContainerView()
                                          ->contents_view()
                                          ->bounds();
        auto* const footer = region->sidebar_footer_for_testing();
        ASSERT_TRUE(footer);
        const gfx::Rect hidden_footer = footer->GetBoundsInScreen();
        // 노치 화면도 native reveal 값과 Toolbar 표시를 함께 관측한다.
        ASSERT_TRUE(applied_.Run({Frame({}, AppliedMatch::kNativeMenuRevealed)},
                                 [&] {
                                   move_system_pointer(
                                       CGPointMake(display_bounds.x() + 20,
                                                   display_bounds.y()));
                                 }))
            << "display=" << display_bounds.ToString()
            << ", hidden menu y=" << hidden_menu_y << ", revealed root="
            << external_root->GetBoundsInScreen().ToString();
        RunScheduledLayouts();
        {
          SCOPED_TRACE(
              testing::Message()
              << "top=" << external_top->bounds().ToString()
              << ", root=" << external_root->GetBoundsInScreen().ToString()
              << ", body=" << browser_view->GetBoundsInScreen().ToString());
          EXPECT_TRUE(
              external_root->GetLocalBounds().Contains(external_top->bounds()));
          auto* const location = browser_view->GetLocationBarView();
          EXPECT_EQ(location->GetLocalBounds(), location->GetVisibleBounds());
          auto* fill =
              external_root->GetViewByID(yee::kBrowserSurfaceFillViewId);
          ASSERT_TRUE(fill);
          gfx::Rect header_fill = fill->GetBoundsInScreen();
          header_fill.set_height(
              yee::kSidebarMetrics.browser_surface_header_height());
          EXPECT_TRUE(external_root->GetBoundsInScreen().Contains(header_fill));
          // 다른 native 창의 가림은 GetVisibleBounds에 나타나지 않는다.
          EXPECT_GE(region->GetBoundsInScreen().y(),
                    external_root->GetBoundsInScreen().bottom());
          EXPECT_EQ(hidden_sidebar.bottom(),
                    region->GetBoundsInScreen().bottom());
          if (!collapsed) {
            EXPECT_EQ(hidden_footer, footer->GetBoundsInScreen());
          }
          EXPECT_EQ(hidden_contents,
                    browser_view->multi_contents_view()->bounds());
          EXPECT_EQ(hidden_page, browser_view->multi_contents_view()
                                     ->GetActiveContentsContainerView()
                                     ->contents_view()
                                     ->bounds());
        }
        std::string capture_prefix =
            always_show ? "fullscreen-fixed-menu" : "fullscreen-autohide-menu";
        if (collapsed) {
          capture_prefix += "-collapsed";
        }
        SkColor header_shell = SK_ColorTRANSPARENT;
        SkColor body_shell = SK_ColorTRANSPARENT;
        ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
            *overlay_widget, capture_prefix + "-header",
            collapsed ? nullptr : &header_shell));
        ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(
            *browser_view->GetWidget(), capture_prefix + "-body",
            collapsed ? nullptr : &body_shell));
        if (!collapsed) {
          EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(header_shell));
          EXPECT_EQ(body_shell, header_shell);
        }
        AppliedConditions hidden_conditions{toolbar_mode};
        if (always_show)
          hidden_conditions.push_back(
              Frame({.toolbar_bounds = gfx::Rect(0, hidden_menu_y, 0, 0)},
                    AppliedMatch::kNativeToolbarY));
        const auto menu_hidden_again = applied_.Run(hidden_conditions, [&] {
          move_system_pointer(
              CGPointMake(content_center.x(), content_center.y()));
        });
        if (!menu_hidden_again) {
          CaptureWidgetIfRequested(*overlay_widget,
                                   capture_prefix + "-hide-failed-header");
          CaptureWidgetIfRequested(*browser_view->GetWidget(),
                                   capture_prefix + "-hide-failed-body");
        }
        ASSERT_TRUE(menu_hidden_again)
            << "menu reveal=" << immersive->menu_bar_reveal_amount_for_testing()
            << ", toolbar revealed=" << immersive->IsRevealed()
            << ", hidden y=" << hidden_menu_y
            << ", display=" << display_bounds.ToString()
            << ", browser visible=" << browser_window().visible
            << ", browser active space=" << browser_window().onActiveSpace
            << ", root=" << external_root->GetBoundsInScreen().ToString()
            << ", moved view frame="
            << NSStringFromRect(overlay_widget_content_view.frame).UTF8String
            << ", moved view visible="
            << NSStringFromRect(overlay_widget_content_view.visibleRect)
                   .UTF8String
            << ", native window="
            << NSStringFromRect(overlay_widget_content_view.window.frame)
                   .UTF8String
            << ", active space="
            << overlay_widget_content_view.window.onActiveSpace;
        RunScheduledLayouts();
        EXPECT_EQ(hidden_sidebar, region->GetBoundsInScreen());
        EXPECT_EQ(hidden_contents,
                  browser_view->multi_contents_view()->bounds());
        EXPECT_EQ(hidden_page, browser_view->multi_contents_view()
                                   ->GetActiveContentsContainerView()
                                   ->contents_view()
                                   ->bounds());
        if (!collapsed) {
          EXPECT_EQ(hidden_footer, footer->GetBoundsInScreen());
        }
        EXPECT_EQ(overlay_widget, browser_view->top_container()->GetWidget());
        auto* const background = browser_view->top_container()
                                     ->background()
                                     ->AsA<CustomCornersBackground>();
        ASSERT_TRUE(background);
        EXPECT_FALSE(background->is_visible());
        EXPECT_FLOAT_EQ(1.0f, background->primary_color().opacity);
        EXPECT_TRUE(browser_view->toolbar()->GetVisible());
        EXPECT_FALSE(browser_view->toolbar()->bounds().IsEmpty());
        auto* const location = browser_view->GetLocationBarView();
        EXPECT_TRUE(location->GetVisible());
        EXPECT_EQ(location->GetLocalBounds(), location->GetVisibleBounds());
        const auto viewport = browser_view->multi_contents_view()
                                  ->GetActiveContentsContainerView()
                                  ->GetPageViewportGeometry()
                                  .current_viewport_in_container;
        EXPECT_EQ(browser_view->multi_contents_view()->width(),
                  viewport.width());
        ASSERT_TRUE(RendererReady(*page, *browser_view->multi_contents_view()
                                              ->GetActiveContentsContainerView()
                                              ->contents_view()));
        ASSERT_TRUE(
            content::EvalJs(page, content::JsReplace(
                                      "innerWidth === $1 && innerHeight === $2",
                                      viewport.width(), viewport.height()))
                .ExtractBool())
            << "expected=" << viewport.ToString() << ", current="
            << browser_view->multi_contents_view()
                   ->GetActiveContentsContainerView()
                   ->GetPageViewportGeometry()
                   .current_viewport_in_container.ToString()
            << ", renderer="
            << content::EvalJs(page,
                               "JSON.stringify([innerWidth, innerHeight])");
      }
      if (always_show) {
        ASSERT_NO_FATAL_FAILURE(
            CaptureWidgetIfRequested(*overlay_widget, "fullscreen-header"));
      }
    }

    // 메뉴/Side Panel의 편집 focus와 팝업을 실제 페이지 클릭으로 닫는다.
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        browser_view->multi_contents_view()->GetBoundsInScreen().CenterPoint(),
        browser_view->GetWidget()->GetNativeWindow()));
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(applied_.source().ObserveOmnibox(
                                *browser_view->GetLocationBarView()),
                            ViewStateMatches{.popup_open = false},
                            AppliedMatch::kOmniboxPopup),
         applied_.Focus(*browser_view->GetLocationBarView(), false),
         Revealed(false)},
        [&] {
          ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
              ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
              browser_view->GetWidget()->GetNativeWindow()));
        }));
    // 실제 전체화면의 가장자리 입력도 같은 Flyout을 열고 본문을 밀지 않는다.
    sidebar->SetExpandOnHoverEnabled(true);
    RunScheduledLayouts();
    const gfx::Rect page_bounds =
        browser_view->multi_contents_view()->GetBoundsInScreen();
    // 메뉴 검사와 같은 실제 포인터를 사용해 native tracking도 일치시킨다.
    ASSERT_TRUE(ActivateBrowserAndWaitForSpace());
    const gfx::Point edge_center = region->GetBoundsInScreen().CenterPoint();
    move_system_pointer(CGPointMake(page_bounds.CenterPoint().x(),
                                    page_bounds.CenterPoint().y()));
    {
      auto expected = region->bounds();
      expected.set_width(region->uncollapsed_width());
      const auto expanded = applied_.Run(
          {applied_.Condition(
               applied_.source().ObserveUiState(
                   *region, AppliedBrowserObservers::UiProbe::kSidebarHover),
               ViewStateMatches{.expanded_on_hover = true},
               AppliedMatch::kSidebarHover),
           applied_.View(*region, {.bounds = expected},
                         AppliedMatch::kViewWidth),
           VerticalIdle()},
          [&] {
            move_system_pointer(CGPointMake(edge_center.x(), edge_center.y()));
          });
      base::apple::ScopedCFTypeRef<CGEventRef> pointer(CGEventCreate(nullptr));
      ASSERT_TRUE(pointer);
      const CGPoint physical_pointer = CGEventGetLocation(pointer.get());
      auto* const root = browser_view->GetWidget()->GetRootView();
      gfx::Point edge_in_root = edge_center;
      views::View::ConvertPointFromScreen(root, &edge_in_root);
      auto* const handler = root->GetEventHandlerForPoint(edge_in_root);
      ASSERT_TRUE(expanded)
          << "physical pointer=" << physical_pointer.x << ","
          << physical_pointer.y << ", mocked pointer="
          << display::Screen::Get()->GetCursorScreenPoint().ToString()
          << ", handler=" << (handler ? handler->GetClassName() : "none")
          << ", region owns handler=" << (handler && region->Contains(handler))
          << ", visible bounds=" << region->GetVisibleBounds().ToString()
          << ", collapsed=" << sidebar->IsCollapsed()
          << ", bounds=" << region->GetBoundsInScreen().ToString()
          << ", mouse_inside=" << region->IsMouseHovered()
          << ", active=" << browser_view->GetWidget()->IsActive()
          << ", paint_active="
          << browser_view->GetWidget()->ShouldPaintAsActive() << ", popup="
          << browser_view->GetLocationBarView()
                 ->GetOmniboxController()
                 ->IsPopupOpen()
          << ", enabled=" << sidebar->IsExpandOnHoverEnabled();
    }
    RunScheduledLayouts();
    EXPECT_EQ(yee::kSidebarMetrics.sidebar_flyout_inset, region->y());
    EXPECT_EQ(page_bounds,
              browser_view->multi_contents_view()->GetBoundsInScreen());
    for (const int id :
         {yee::kSidebarHeaderCreateViewId, yee::kSidebarHeaderAgentViewId,
          yee::kSidebarFooterViewId}) {
      const views::View* const control = region->GetViewByID(id);
      ASSERT_TRUE(control);
      EXPECT_TRUE(control->GetVisible());
      EXPECT_EQ(control->GetLocalBounds(), control->GetVisibleBounds());
    }
    ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(*browser_view->GetWidget(),
                                                     "fullscreen-flyout"));
    // 고정 전환의 첫 프레임과 완료 직전에도 목록이 위로 뛰지 않아야 한다.
    {
      gfx::ScopedAnimationDurationScaleMode normal_animation(
          gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
      const auto render_mode =
          gfx::AnimationTestApi::SetRichAnimationRenderMode(
              gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
      auto* const controller = BrowserAnimationController::From(browser());
      auto clock = base::MakeRefCounted<gfx::AnimationContainer>();
      controller->SetAnimationContainerForTesting(
          TabStripAnimations::kVerticalTabStrip, clock.get());
      gfx::AnimationContainerTestApi time(clock.get());
      auto* const header = region->sidebar_header_actions();
      const int tab_top =
          browser_view->tab_strip_view()->GetBoundsInScreen().y() -
          region->GetBoundsInScreen().y();
      sidebar->RequestCollapse(false);
      RunScheduledLayouts();
      EXPECT_FALSE(region->is_expanded_on_hover());
      EXPECT_EQ(region, header->parent());
      EXPECT_EQ(tab_top,
                browser_view->tab_strip_view()->GetBoundsInScreen().y() -
                    region->GetBoundsInScreen().y());
      const base::TimeDelta duration =
          controller->GetMotionDuration(TabStripAnimations::kVerticalTabStrip);
      ASSERT_GT(duration, base::Milliseconds(1));
      time.IncrementTime(duration - base::Milliseconds(1));
      RunScheduledLayouts();
      EXPECT_TRUE(
          controller->IsAnimating(TabStripAnimations::kVerticalTabStrip));
      EXPECT_EQ(region, header->parent());
      EXPECT_TRUE(header->GetVisible());
      EXPECT_LE(header->height(), 1);
      const int tab_before_end =
          browser_view->tab_strip_view()->GetBoundsInScreen().y();
      time.IncrementTime(base::Seconds(2));
      RunScheduledLayouts();
      EXPECT_EQ(browser_view->top_container(), header->parent());
      EXPECT_TRUE(header->GetVisible());
      EXPECT_NEAR(tab_before_end,
                  browser_view->tab_strip_view()->GetBoundsInScreen().y(), 1);
      move_system_pointer(CGPointMake(page_bounds.CenterPoint().x(),
                                      page_bounds.CenterPoint().y()));
    }
    sidebar->RequestCollapse(false);
    RunScheduledLayouts();

    // 항상 표시 설정을 유지해도 분할은 빈 native 상단 행을 예약하지 않는다.
    const gfx::Rect fullscreen_body = browser_view->GetBoundsInScreen();
    ASSERT_TRUE(applied_.Run(
        {Revealed(true),
         applied_.View(*browser_view, {.screen_bounds = fullscreen_body},
                       AppliedMatch::kViewScreenBoundsDifferent)},
        [&] {
          browser()->GetProfile()->GetPrefs()->SetBoolean(
              prefs::kShowFullscreenToolbar, true);
        }));
    const gfx::Rect fullscreen_body_with_toolbar =
        browser_view->GetBoundsInScreen();

    // 분할에서는 주소줄이 본문의 각 Header로 이동하고 외부 Toolbar는 숨는다.
    ASSERT_TRUE(applied_.Run(
        {applied_.View(*browser_view, {.screen_bounds = fullscreen_body},
                       AppliedMatch::kViewScreenBounds)},
        [&] {
          chrome::NewSplitTab(
              browser(), split_tabs::SplitTabLayout::kSideBySide,
              split_tabs::SplitTabCreatedSource::kToolbarButton);
        }));
    EXPECT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
        prefs::kShowFullscreenToolbar));
    RunScheduledLayouts();
    auto* const inactive =
        browser_view->multi_contents_view()->GetInactiveContentsContainerView();
    ASSERT_TRUE(inactive);
    ASSERT_EQ(page, inactive->contents_view()->web_contents());
    // 활성화를 거치지 않은 기존 pane도 새 viewport를 renderer에 전달해야 한다.
    {
      ASSERT_TRUE(RendererReady(*page, *inactive->contents_view()));
      const auto current =
          inactive->GetPageViewportGeometry().current_viewport_in_container;
      ASSERT_TRUE(
          content::EvalJs(page, content::JsReplace(
                                    "innerWidth === $1 && innerHeight === $2",
                                    current.width(), current.height()))
              .ExtractBool())
          << "pane="
          << inactive->GetPageViewportGeometry()
                 .current_viewport_in_container.ToString()
          << ", view=" << inactive->contents_view()->bounds().ToString()
          << ", visibility=" << static_cast<int>(page->GetVisibility())
          << ", renderer="
          << content::EvalJs(page, "JSON.stringify([innerWidth,innerHeight,"
                                   "document.visibilityState])");
    }
    if (!capture_url.empty()) {
      ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(capture_url)));
    }
    RunScheduledLayouts();
    auto* const contents = browser_view->multi_contents_view();
    ASSERT_TRUE(contents->IsInSplitView());
    EXPECT_EQ(yee::kSidebarMetrics.content_gutter, contents->y());
    EXPECT_FALSE(browser_view->toolbar()->GetVisible());
    auto* const location = browser_view->GetLocationBarView();
    EXPECT_EQ(browser_view->GetWidget(), location->GetWidget());
    EXPECT_TRUE(location->GetVisible());
    EXPECT_EQ(location->GetLocalBounds(), location->GetVisibleBounds());
    // 본문으로 옮겨진 주소창은 실제 키 입력으로 팝업을 열 수 있어야 한다.
    ASSERT_TRUE(applied_.Run({applied_.Focus(*location)}, [&] {
      ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_L, false,
                                                  false, false, true));
    }));
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(applied_.source().ObserveOmnibox(*location),
                            ViewStateMatches{.popup_open = true},
                            AppliedMatch::kOmniboxPopup)},
        [&] {
          ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
              browser(), ui::VKEY_A, false, false, false, false));
        }));
    ASSERT_NO_FATAL_FAILURE(CaptureWidgetIfRequested(*browser_view->GetWidget(),
                                                     "fullscreen-split"));
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                                false, false, false, false));
    // 분할 중 설정 전환은 같은 본문을 유지하고, 해제하면 원래 설정을 따른다.
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kShowFullscreenToolbar, false);
    RunScheduledLayouts();
    EXPECT_EQ(fullscreen_body, browser_view->GetBoundsInScreen());
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kShowFullscreenToolbar, true);
    ASSERT_TRUE(applied_.Run(
        {applied_.View(*browser_view,
                       {.screen_bounds = fullscreen_body_with_toolbar},
                       AppliedMatch::kViewScreenBounds)},
        [&] { contents->OnExitSplit(); }));
    EXPECT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
        prefs::kShowFullscreenToolbar));
  }

  ASSERT_TRUE(ToggleFullscreen(false));
  ASSERT_FALSE(testing::Test::HasFailure());

  EXPECT_FALSE(fullscreen_controller->IsFullscreenForBrowser());
  EXPECT_EQ(GetMovedContentViewForWidget(overlay_widget), nullptr);
  EXPECT_EQ(overlay_widget_window.contentView, overlay_widget_content_view);
  if (browser_view->UsesYeeBrowserSurfaceGeometry()) {
    RunScheduledLayouts();
    EXPECT_EQ(browser_view, browser_view->top_container()->parent());
    ASSERT_TRUE(browser_view->top_container()->layer());
    EXPECT_EQ(header_layer_parent,
              browser_view->top_container()->layer()->parent());
    ASSERT_TRUE(browser_view->GetViewByID(yee::kCombinedSurfaceOutlineViewId));
    EXPECT_EQ(browser_view,
              browser_view->GetViewByID(yee::kCombinedSurfaceOutlineViewId)
                  ->parent());
    EXPECT_TRUE(browser_view->toolbar()->GetVisible());
    EXPECT_EQ(yee::kSidebarMetrics.titlebar_height,
              browser_view->multi_contents_view()->y());
  }
}
