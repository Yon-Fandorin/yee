// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/site_controls.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_coordinator.h"
#include "chrome/browser/ui/views/location_bar/zoom_bubble_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "chrome/browser/ui/views/page_info/page_info_view_factory.h"
#include "chrome/browser/ui/views/page_info/permission_toggle_row_view.h"
#include "chrome/browser/ui/views/passwords/password_change/password_change_toast.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/prerender_test_util.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/test/ui_controls.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/events/event.h"
#include "ui/events/test/test_event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/test/ink_drop_impl_test_api.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/test_support/app_menu_test_accessor.h"
#include "components/permissions/fake_usb_chooser_controller.h"
#include "ui/aura/test/ui_controls_aurawin.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/snapshot/snapshot_aura.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "ui/views/widget/any_widget_observer.h"
#endif

namespace yee {
namespace {

template <typename T>
T* FindView(views::View* root) {
  if (!root) {
    return nullptr;
  }
  if (auto* result = views::AsViewClass<T>(root)) {
    return result;
  }
  for (views::View* child : root->children()) {
    if (auto* result = FindView<T>(child)) {
      return result;
    }
  }
  return nullptr;
}

class SiteControlsBrowserTest : public InProcessBrowserTest {
 public:
  void TearDownOnMainThread() override {
    applied_.Stop();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  yee::test::AppliedTestContext<yee::test::AppliedBrowserObservers> applied_;
  yee::test::AppliedCondition VerticalIdle() {
    auto* view = BrowserView::GetBrowserViewForBrowser(browser());
    auto owner = applied_.source().ObserveBrowserAnimation(
        *view, *BrowserAnimationController::From(browser()),
        TabStripAnimations::kVerticalTabStrip,
        TabStripAnimations::kTabStripWidth, 1);
    return applied_.Condition(
        owner, yee::test::AnimationMatches{.group_id = 1, .running = false});
  }
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("yee-content-blocking-test-rules");
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](std::shared_ptr<std::atomic<int>> requests,
           const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          if (request.relative_url == "/fixture" ||
              request.relative_url == "/prerender-fixture") {
            response->set_content_type("text/html");
            response->set_content(R"(
              <!doctype html><title>Site Controls regression</title>
              <script>window.early = !!window.__yeeFilterFixture;</script>
              <div id="ad" class="yee-ad">advertisement</div>
              <script>
                window.blocked = fetch('http://yee-block.test:' + location.port
                  + '/blocked').then(() => false, () => true);
              </script>)");
          } else if (request.relative_url == "/worker") {
            response->set_content_type("text/html");
            response->set_content(
                "<!doctype html><title>Worker regression</title>");
          } else if (request.relative_url == "/dedicated-worker.js") {
            response->set_content_type("text/javascript");
            response->set_content(R"(
              onmessage = async event => {
                const blocked = await fetch(event.data).then(() => false, () => true);
                postMessage(blocked);
              };)");
          } else if (request.relative_url == "/shared-worker.js") {
            response->set_content_type("text/javascript");
            response->set_content(R"(
              onconnect = event => {
                const port = event.ports[0];
                port.onmessage = async message => {
                  const blocked = await fetch(message.data).then(() => false, () => true);
                  port.postMessage(blocked);
                };
                port.start();
              };)");
          } else if (request.relative_url == "/service-worker.js") {
            response->set_content_type("text/javascript");
            response->set_content(R"(
              self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
              self.addEventListener('activate', event => event.waitUntil(clients.claim()));
              self.addEventListener('message', event => {
                event.waitUntil((async () => {
                  const {url, prime} = event.data;
                  const cache = await caches.open('yee-regression');
                  try {
                    const response = await fetch(url);
                    if (prime) await cache.put(url, response);
                    event.ports[0].postMessage(false);
                  } catch {
                    event.ports[0].postMessage(true);
                  }
                })());
              });
              self.addEventListener('fetch', event => {
                if (new URL(event.request.url).hostname === 'yee-block.test') {
                  event.respondWith((async () => {
                    const cached = await caches.match(event.request);
                    return cached || fetch(event.request);
                  })());
                }
              });)");
          } else {
            response->AddCustomHeader("Access-Control-Allow-Origin", "*");
            if (request.relative_url == "/cache-ad") {
              ++*requests;
              response->AddCustomHeader("Cache-Control",
                                        "public, max-age=3600");
            }
            response->set_content("allowed");
          }
          return response;
        },
        cached_ad_requests_));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  GURL FixtureURL(const std::string& host = "yee-fixture.test") {
    return embedded_test_server()->GetURL(host, "/fixture");
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  content_blocking::ContentBlockingService* Service() {
    return content_blocking::ContentBlockingServiceFactory::GetForProfile(
        browser()->GetProfile());
  }

  void CheckPage(bool protection_enabled,
                 content::WebContents* contents = nullptr) {
    if (!contents) {
      contents = Contents();
    }
    EXPECT_EQ(protection_enabled, content::EvalJs(contents, "window.early"));
    EXPECT_EQ(protection_enabled, content::EvalJs(contents, "window.blocked"));
    EXPECT_EQ(protection_enabled ? "none" : "block",
              content::EvalJs(contents,
                              "getComputedStyle(document.getElementById('ad'))"
                              ".display"));
  }

  views::Widget* OpenControls(BrowserWindowInterface* owner = nullptr) {
    if (!owner) {
      owner = browser();
    }
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(owner);
    auto* button = Button(owner);
    if (!button) {
      ADD_FAILURE() << "Protection 버튼이 없습니다";
      return nullptr;
    }
    views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
    for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
             browser_view->GetWidget()->GetNativeView())) {
      if (FindView<views::ToggleButton>(widget->GetContentsView())) {
        return widget;
      }
    }
    return nullptr;
  }

  void ToggleAndWaitForReload(BrowserWindowInterface* owner = nullptr) {
    if (!owner) {
      owner = browser();
    }
    auto* contents = owner->tab_strip_model()->GetActiveWebContents();
    auto* widget = OpenControls(owner);
    ASSERT_TRUE(widget);
    auto* toggle = FindView<views::ToggleButton>(widget->GetContentsView());
    ASSERT_TRUE(toggle);
    content::TestNavigationObserver reload(contents);
    const yee::test::AppliedConditions closed{applied_.Widget(
        *widget, {.closed = true}, yee::test::AppliedMatch::kWidgetClosed)};
    ASSERT_TRUE(applied_.Run(closed, [&] {
      views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
    }));
    reload.Wait();
    EXPECT_TRUE(reload.last_navigation_succeeded());
    EXPECT_FALSE(IsSiteControlsBubbleShowing());
  }

  SiteControlsButton* Button(BrowserWindowInterface* owner = nullptr) {
    return FindView<SiteControlsButton>(
        BrowserView::GetBrowserViewForBrowser(owner ? owner : browser())
            ->GetLocationBarView());
  }

  GURL WorkerURL() {
    return embedded_test_server()->GetURL("localhost", "/worker");
  }

  GURL AdURL(const std::string& path = "/worker-ad") {
    return embedded_test_server()->GetURL("yee-block.test", path);
  }

  void StartWorker(bool shared, content::WebContents* contents = nullptr) {
    ASSERT_TRUE(content::ExecJs(contents ? contents : Contents(), shared ? R"(
          window.worker = new SharedWorker('/shared-worker.js');
          window.workerPort = worker.port; workerPort.start();
        )"
                                                                         : R"(
          window.worker = new Worker('/dedicated-worker.js');
          window.workerPort = worker;
        )"));
  }

  content::EvalJsResult WorkerFetch(content::WebContents* contents = nullptr) {
    return content::EvalJs(contents ? contents : Contents(),
                           content::JsReplace(R"(
          Promise.race([
            new Promise(resolve => {
              workerPort.onmessage = e => resolve(e.data);
              workerPort.postMessage($1);
            }),
            new Promise(resolve => setTimeout(() => resolve('timeout'), 10000))
          ])
        )",
                                              AdURL()));
  }

  void StartServiceWorker(content::WebContents* contents = nullptr) {
    ASSERT_TRUE(content::ExecJs(contents ? contents : Contents(), R"(
      (async () => {
        const controlled = navigator.serviceWorker.controller ? Promise.resolve() :
          new Promise(resolve => navigator.serviceWorker.addEventListener(
            'controllerchange', resolve, {once: true}));
        await navigator.serviceWorker.register('/service-worker.js');
        await navigator.serviceWorker.ready;
        await controlled;
      })()
    )"));
  }

  content::EvalJsResult ServiceWorkerFetch(
      bool prime = false,
      content::WebContents* contents = nullptr) {
    return content::EvalJs(contents ? contents : Contents(),
                           content::JsReplace(R"(
      Promise.race([
        new Promise(resolve => {
          const channel = new MessageChannel();
          channel.port1.onmessage = e => resolve(e.data);
          navigator.serviceWorker.controller.postMessage({url: $1, prime: $2},
            [channel.port2]);
        }),
        new Promise(resolve => setTimeout(() => resolve('timeout'), 10000))
      ])
    )",
                                              AdURL("/cache-ad"), prime));
  }

  const std::shared_ptr<std::atomic<int>> cached_ad_requests_ =
      std::make_shared<std::atomic<int>>(0);
  content::test::PrerenderTestHelper prerender_helper_{
      base::BindRepeating(&SiteControlsBrowserTest::Contents,
                          base::Unretained(this))};
};

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       ToggleReloadsNetworkAndDocumentPolicy) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  CheckPage(true);
  ToggleAndWaitForReload();
  EXPECT_FALSE(Service()->EnabledForSite(FixtureURL()));
  CheckPage(false);
  ToggleAndWaitForReload();
  EXPECT_TRUE(Service()->EnabledForSite(FixtureURL()));
  CheckPage(true);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, PRE_PersistsHostException) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  ToggleAndWaitForReload();
  ASSERT_FALSE(Service()->EnabledForSite(FixtureURL()));
  CheckPage(false);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, PersistsHostException) {
  EXPECT_FALSE(Service()->EnabledForSite(FixtureURL()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  CheckPage(false);
  ToggleAndWaitForReload();
  CheckPage(true);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       ProtectionButtonClosesOnSecondNativeClick) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  CheckPage(true);
#if BUILDFLAG(IS_WIN)
  aura::test::EnableUIControlsAuraWin();
#else
  ui_controls::EnableUIControls();
#endif
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
#if BUILDFLAG(IS_WIN)
  yee::test::ScopedNativeInputTrace native_trace(view->GetWidget());
  const auto record_input = [&](std::string_view phase,
                                const gfx::Point& intended) {
    native_trace.Snapshot(phase, intended);
    RecordProperty("YeeProtectionNativeInput", native_trace.ToJson());
  };
#else
  const auto record_input = [](std::string_view, const gfx::Point&) {};
#endif
  for (bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    bool observed_unready_layout = false;
    const auto layout_ready_now = [&] {
      auto* current = Button();
      const bool ready =
          !BrowserAnimationController::From(browser())->IsAnimating(
              TabStripAnimations::kVerticalTabStrip) &&
          current && current->IsDrawn() &&
          !current->GetLocalBounds().IsEmpty() &&
          current->GetVisibleBounds().Contains(
              current->GetLocalBounds().CenterPoint());
      observed_unready_layout |= !ready;
      return ready;
    };
    const yee::test::AppliedConditions layout_conditions{
        VerticalIdle(),
        applied_.View(*Button(), {.drawn = true},
                      yee::test::AppliedMatch::kViewDrawn),
        applied_.View(*Button(), {.bounds = gfx::Rect(0, 0, 1, 1)},
                      yee::test::AppliedMatch::kViewNonEmpty),
        applied_.View(*Button(), {},
                      yee::test::AppliedMatch::kViewCenterVisible)};
    std::vector<yee::test::AppliedSignalKey> keys;
    for (const auto& condition : layout_conditions)
      keys.push_back({condition.owner,
                      yee::test::GetAppliedConditionKind(condition.expected)});
    auto observation = applied_.source().Subscribe(
        keys,
        base::BindLambdaForTesting([&](const yee::test::AppliedSignalSample&) {
          observed_unready_layout |= !layout_ready_now();
        }));
    ASSERT_TRUE(applied_.Run(layout_conditions,
                             [&] { state->SetVerticalTabsEnabled(vertical); }));
    const bool layout_ready = layout_ready_now();
    observation = {};
    RecordProperty(vertical ? "YeeVerticalProtectionObservedUnreadyLayout"
                            : "YeeHorizontalProtectionObservedUnreadyLayout",
                   observed_unready_layout ? 1 : 0);
    ASSERT_TRUE(layout_ready);
    ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
    view->GetWidget()->LayoutRootViewIfNecessary();
    auto* button = Button();
    ASSERT_TRUE(button);
    RecordProperty(vertical ? "YeeVerticalProtectionButtonBounds"
                            : "YeeHorizontalProtectionButtonBounds",
                   button->GetBoundsInScreen().ToString());
    const auto native_window = view->GetWidget()->GetNativeWindow();
    int click = 0;
    for (bool expected_open : {true, false, true, false}) {
      SCOPED_TRACE(++click);
      SCOPED_TRACE(expected_open);
      const std::string phase =
          (vertical ? "vertical-" : "horizontal-") + std::to_string(click);
      auto* root = button->GetWidget()->GetRootView();
      const auto intended = button->GetBoundsInScreen().CenterPoint();
      auto point = intended;
      views::View::ConvertPointFromScreen(root, &point);
      auto* target = root->GetEventHandlerForPoint(point);
      ASSERT_TRUE(target && (target == button || button->Contains(target)));
      record_input(phase + "-before-move", intended);
      const bool moved =
          ui_test_utils::SendMouseMoveSync(intended, native_window);
      record_input(phase + "-after-move", intended);
      ASSERT_TRUE(moved);
      ASSERT_TRUE(button->IsMouseHovered())
          << "실제 포인터가 Protection 버튼에 도착하지 않았습니다";
      auto popup = applied_.Condition(
          applied_.source().ObserveUiState(
              *button,
              yee::test::AppliedBrowserObservers::UiProbe::kSiteControlsPopup),
          yee::test::ViewStateMatches{.visible = expected_open},
          yee::test::AppliedMatch::kViewVisible,
          yee::test::AppliedFreshness::kNewApply);
      ASSERT_TRUE(applied_.Run({popup}, [&] {
        const bool clicked = ui_test_utils::SendMouseEventsSync(
            ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
            native_window);
        record_input(phase + "-after-click", intended);
        ASSERT_TRUE(clicked);
      }));
      // 기존 metric 이름을 유지하되 실제 popup 적용 뒤 시점을 기록한다.
      record_input(phase + "-after-idle", intended);
      RecordProperty("YeeProtectionInputPhase", phase);
      RecordProperty("YeeProtectionCurrentButtonBounds",
                     button->GetBoundsInScreen().ToString());
      ASSERT_EQ(expected_open, IsSiteControlsBubbleShowing());
    }
  }
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PageInfoAndProtectionUseSeparatePopups) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  const auto capture_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-site-controls-capture-dir");
  const auto capture = [&](views::Widget* widget, const std::string& name) {
    if (capture_dir.empty()) {
      return;
    }
    widget->LayoutRootViewIfNecessary();
    auto* compositor = widget->GetCompositor();
    compositor->ScheduleFullRedraw();
    ASSERT_TRUE(applied_.WaitForPresentedFrame(*widget));
#if BUILDFLAG(IS_WIN)
    // DirectComposition 팝업은 PrintWindow로 캡처하면 비어 있을 수 있다.
    // Windows에서는 실제 합성 프레임을 캡처한다.
    base::test::TestFuture<gfx::Image> snapshot;
    ui::GrabWindowSnapshotAura(
        widget->GetNativeWindow(),
        gfx::Rect(widget->GetWindowBoundsInScreen().size()),
        snapshot.GetCallback());
    auto png = snapshot.Take().As1xPNGBytes();
#else
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
    ui::GrabWindowSnapshotAsPNG(
        widget->GetNativeWindow(),
        gfx::Rect(widget->GetWindowBoundsInScreen().size()),
        snapshot.GetCallback());
    auto png = snapshot.Take();
#endif
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(base::WriteFile(capture_dir.AppendASCII(name),
                                static_cast<base::span<const uint8_t>>(*png)));
  };
  for (bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    auto* protection = OpenControls();
    ASSERT_TRUE(protection);
    EXPECT_FALSE(FindView<views::TabbedPane>(protection->GetContentsView()));
    EXPECT_FALSE(FindView<views::MdTextButton>(protection->GetContentsView()));
    EXPECT_EQ(PageInfoBubbleView::BUBBLE_NONE,
              PageInfoBubbleView::GetShownBubbleType());
    capture(protection,
            vertical ? "protection-sidebar.png" : "protection-horizontal.png");

    auto* identity = view->GetLocationBarView()->location_icon_view();
    ASSERT_TRUE(identity);
    EXPECT_FALSE(identity->IsBubbleShowing());
    ui::test::TestEvent event;
    ASSERT_TRUE(identity->ShowBubble(event));
    EXPECT_TRUE(identity->IsBubbleShowing());
    EXPECT_FALSE(IsSiteControlsBubbleShowing());
    ASSERT_EQ(PageInfoBubbleView::BUBBLE_PAGE_INFO,
              PageInfoBubbleView::GetShownBubbleType());
    auto* page_info = PageInfoBubbleView::GetPageInfoBubbleForTesting();
    ASSERT_TRUE(page_info);
    EXPECT_FALSE(FindView<views::TabbedPane>(page_info));
    EXPECT_FALSE(FindView<views::ToggleButton>(page_info));
    EXPECT_TRUE(page_info->GetViewByID(
        PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SITE_SETTINGS));
    capture(page_info->GetWidget(),
            vertical ? "page-info-sidebar.png" : "page-info-horizontal.png");
    page_info->GetWidget()->CloseNow();
  }
}

void ExpectPopupIconHover(
    views::Button* button,
    ui::ColorId surface_color = ui::kColorBubbleBackground) {
  ASSERT_TRUE(button && button->IsDrawn() && button->GetEnabled());
  auto* const image_button = views::AsViewClass<views::ImageButton>(button);
  ASSERT_TRUE(image_button);
  const gfx::Rect native_clip = gfx::ToEnclosingRect(
      gfx::SkRectToRectF(views::GetHighlightPath(button).getBounds()));
  ASSERT_FALSE(native_clip.IsEmpty());
  auto* const host = views::InkDrop::Get(button);
  auto* const ink_drop = host->GetInkDrop();
  ink_drop->SetHovered(true);
  ASSERT_TRUE(ink_drop->IsHighlightFadingInOrVisible());
  views::test::InkDropImplTestApi ink_drop_api(
      static_cast<views::InkDropImpl*>(ink_drop));
  const auto* const layer = ink_drop_api.GetRootLayer();
  ASSERT_TRUE(layer && ink_drop_api.highlight());
  EXPECT_EQ(native_clip, layer->clip_rect());
  EXPECT_EQ(gfx::RoundedCornersF(kSidebarMetrics.shell_control_corner_radius),
            layer->rounded_corner_radii());

  const SkColor surface =
      button->GetColorProvider()->GetColor(surface_color);
  const float opacity = ink_drop_api.highlight()->layer()->GetTargetOpacity();
  ASSERT_GT(opacity, 0.0f);
  const SkColor hover_surface =
      color_utils::AlphaBlend(host->GetBaseColor(), surface, opacity);
  for (const auto state :
       {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED}) {
    SCOPED_TRACE(state);
    const gfx::ImageSkia glyph = image_button->GetImage(state);
    ASSERT_FALSE(glyph.isNull());
    const SkBitmap& pixels = glyph.GetRepresentation(1.0f).GetBitmap();
    float contrast = std::numeric_limits<float>::infinity();
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        const SkColor pixel = pixels.getColor(x, y);
        if (SkColorGetA(pixel) == SK_AlphaOPAQUE) {
          contrast = std::min(
              contrast,
              color_utils::GetContrastRatio(
                  pixel, state == views::Button::STATE_HOVERED ? hover_surface
                                                               : surface));
        }
      }
    }
    ASSERT_TRUE(std::isfinite(contrast));
    EXPECT_GE(contrast, color_utils::kMinimumVisibleContrastRatio);
  }
  ink_drop->SetHovered(false);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PermissionDetailsUseShellHoverCorners) {
  const GURL url = FixtureURL("localhost");
  const auto permission = content_settings::GeolocationContentSettingsType();
  const PermissionSetting blocked =
      permission == ContentSettingsType::GEOLOCATION_WITH_OPTIONS
          ? PermissionSetting{GeolocationSetting{PermissionOption::kDenied,
                                                 PermissionOption::kDenied}}
          : PermissionSetting{CONTENT_SETTING_BLOCK};
  HostContentSettingsMapFactory::GetForProfile(browser()->GetProfile())
      ->SetPermissionSettingDefaultScope(url, url, permission, blocked);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  auto* const location_bar =
      BrowserView::GetBrowserViewForBrowser(browser())->GetLocationBarView();
  for (const auto scheme : {ThemeService::BrowserColorScheme::kLight,
                            ThemeService::BrowserColorScheme::kDark}) {
    SCOPED_TRACE(static_cast<int>(scheme));
    theme->SetBrowserColorScheme(scheme);
    ASSERT_TRUE(location_bar->ShowPageInfoDialog());
    auto* const page_info = PageInfoBubbleView::GetPageInfoBubbleForTesting();
    ASSERT_TRUE(page_info);
    auto* const widget = page_info->GetWidget();
    widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(scheme == ThemeService::BrowserColorScheme::kDark
                  ? ui::ColorProviderKey::ColorMode::kDark
                  : ui::ColorProviderKey::ColorMode::kLight,
              widget->GetColorProviderKeyForTesting().color_mode);
    const auto close_button = [&] {
      return views::Button::AsButton(page_info->GetViewByID(
          PageInfoViewFactory::VIEW_ID_PAGE_INFO_CLOSE_BUTTON));
    };
    ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(close_button()));
    auto* const details = views::Button::AsButton(
        views::ElementTrackerViews::GetInstance()->GetUniqueView(
            PermissionToggleRowView::kSubpageButtonElementId,
            views::ElementTrackerViews::GetContextForView(page_info)));
    ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(details));
    views::test::ButtonTestApi(details).NotifyDefaultMouseClick();
    widget->LayoutRootViewIfNecessary();
    auto* const back = views::Button::AsButton(page_info->GetViewByID(
        PageInfoViewFactory::VIEW_ID_PAGE_INFO_BACK_BUTTON));
    ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(back));
    ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(close_button()));
    views::test::ButtonTestApi(back).NotifyDefaultMouseClick();
    EXPECT_FALSE(page_info->GetViewByID(
        PageInfoViewFactory::VIEW_ID_PAGE_INFO_BACK_BUTTON));
    widget->CloseNow();
  }
  theme->SetBrowserColorScheme(original_scheme);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       ZoomPopupUsesShellHoverCornersWithThemeColor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  const auto original_color = theme->GetUserColor();
  theme->SetUserColor(SK_ColorRED);
  auto* const browser_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  auto* const coordinator = ZoomBubbleCoordinator::From(browser());
  ASSERT_TRUE(coordinator);
  for (const auto scheme : {ThemeService::BrowserColorScheme::kLight,
                            ThemeService::BrowserColorScheme::kDark}) {
    SCOPED_TRACE(static_cast<int>(scheme));
    theme->SetBrowserColorScheme(scheme);
    coordinator->Show(Contents(), LocationBarBubbleDelegateView::USER_GESTURE);
    auto* const bubble = coordinator->bubble();
    ASSERT_TRUE(bubble && coordinator->IsShowing());
    auto* const widget = bubble->GetWidget();
    widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(widget->GetColorProvider(), browser_widget->GetColorProvider());
    EXPECT_EQ(browser_widget->GetColorProviderKeyForTesting().user_color,
              SK_ColorRED);
    EXPECT_EQ(scheme == ThemeService::BrowserColorScheme::kDark
                  ? ui::ColorProviderKey::ColorMode::kDark
                  : ui::ColorProviderKey::ColorMode::kLight,
              widget->GetColorProviderKeyForTesting().color_mode);
    ASSERT_NO_FATAL_FAILURE(
        ExpectPopupIconHover(bubble->GetZoomInButtonForTesting()));
    widget->CloseNow();
  }
  theme->SetUserColor(original_color);
  theme->SetBrowserColorScheme(original_scheme);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PasswordChangeToastCloseKeepsInversePalette) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  auto* const anchor =
      BrowserView::GetBrowserViewForBrowser(browser())->GetLocationBarView();
  for (const auto scheme : {ThemeService::BrowserColorScheme::kLight,
                            ThemeService::BrowserColorScheme::kDark}) {
    SCOPED_TRACE(static_cast<int>(scheme));
    theme->SetBrowserColorScheme(scheme);
    views::BubbleDialogDelegate bubble(anchor, views::BubbleBorder::TOP_RIGHT);
    bubble.SetButtons(0);
    bubble.SetBackgroundColor(ui::kColorToastBackgroundProminent);
    auto* const toast = bubble.SetContentsView(
        std::make_unique<PasswordChangeToast>(PasswordChangeToast::ToastOptions(
            u"Password changed", base::DoNothing())));
    auto widget = views::BubbleDialogDelegate::CreateBubble(&bubble);
    widget->Show();
    widget->LayoutRootViewIfNecessary();
    EXPECT_EQ(scheme == ThemeService::BrowserColorScheme::kDark
                  ? ui::ColorProviderKey::ColorMode::kDark
                  : ui::ColorProviderKey::ColorMode::kLight,
              widget->GetColorProviderKeyForTesting().color_mode);
    ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(
        toast->close_button(), ui::kColorToastBackgroundProminent));
  }
  theme->SetBrowserColorScheme(original_scheme);
}

#if BUILDFLAG(IS_WIN)
void ExpectMenuZoomHighlight(views::Button& button, bool mouse_hover) {
  ASSERT_EQ(views::Button::STATE_HOVERED, button.GetState());
  ASSERT_EQ(mouse_hover, button.IsMouseHovered());
  auto* const widget = button.GetWidget();
  ASSERT_TRUE(widget && widget->IsVisible());
  const gfx::Rect crop = gfx::ToEnclosingRect(views::View::ConvertRectToTarget(
      &button, widget->GetRootView(), gfx::RectF(button.GetLocalBounds())));
  base::test::TestFuture<gfx::Image> snapshot;
  // 다시 그리기를 요청하면 마우스 진입 시 필요한 무효화가 빠져도 통과한다.
  ui::GrabWindowSnapshotAndScaleAura(widget->GetNativeWindow(), crop,
                                     button.size(), snapshot.GetCallback());
  ASSERT_TRUE(snapshot.Wait());
  const SkBitmap bitmap = snapshot.Take().AsBitmap();
  ASSERT_EQ(button.size(), gfx::Size(bitmap.width(), bitmap.height()));
  const SkColor selected = button.GetColorProvider()->GetColor(
      ui::kColorMenuButtonBackgroundSelected);
  constexpr gfx::Size kHighlightSize(28, 28);
  gfx::Rect bounds = button.GetLocalBounds();
  bounds.ClampToCenteredSize(kHighlightSize);
  ASSERT_EQ(kHighlightSize, bounds.size());
  const int x = bounds.x();
  const int y = bounds.y();
  ASSERT_EQ(selected, bitmap.getColor(bounds.CenterPoint().x(), y + 1));
  EXPECT_NE(selected, bitmap.getColor(x, y));
  // 이 픽셀은 둥근 사각형 안쪽이며 기존 원 밖이다.
  EXPECT_EQ(mouse_hover, bitmap.getColor(x + 3, y + 3) == selected);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       AppMenuZoomDistinguishesMouseAndKeyboardHighlight) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  yee::test::InstallOwnedWindowsUIControls();
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  base::ScopedClosureRunner restore_scheme(base::BindOnce(
      &ThemeService::SetBrowserColorScheme, base::Unretained(theme),
      theme->GetBrowserColorScheme()));
  auto* const browser_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();

  for (const auto scheme : {ThemeService::BrowserColorScheme::kLight,
                            ThemeService::BrowserColorScheme::kDark}) {
    SCOPED_TRACE(static_cast<int>(scheme));
    theme->SetBrowserColorScheme(scheme);
    ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        browser_widget->GetClientAreaBoundsInScreen().CenterPoint(),
        browser_widget->GetNativeWindow()));

    AppMenuTestAccessor menu(browser());
    menu.ShowMenu();
    base::ScopedClosureRunner close_menu(base::BindOnce(
        &AppMenuTestAccessor::CloseMenu, base::Unretained(&menu)));
    auto* const root = menu.GetRootMenuItemView();
    ASSERT_TRUE(root);
    auto* const controller = root->GetMenuController();
    ASSERT_TRUE(controller);
    auto* const zoom = AppMenu::GetZoomAppMenuViewForTest(root);
    ASSERT_TRUE(zoom && zoom->GetWidget());
    auto* const menu_widget = zoom->GetWidget();
    EXPECT_EQ(scheme == ThemeService::BrowserColorScheme::kDark
                  ? ui::ColorProviderKey::ColorMode::kDark
                  : ui::ColorProviderKey::ColorMode::kLight,
              menu_widget->GetColorProviderKeyForTesting().color_mode);
    std::array<views::Button*, 3> buttons{};
    for (views::View* child : zoom->children()) {
      if (auto* const button = views::Button::AsButton(child)) {
        const int tag = button->tag();
        if (tag >= 0 && static_cast<size_t>(tag) < buttons.size()) {
          buttons[tag] = button;
        }
      }
    }
    ASSERT_TRUE(std::ranges::all_of(buttons, [](const views::Button* button) {
      return button && button->GetEnabled();
    }));
    auto* const zoom_item =
        views::AsViewClass<views::MenuItemView>(zoom->parent());
    auto* const zoom_label = FindView<views::Label>(zoom);
    ASSERT_TRUE(zoom_item && zoom_label && zoom_label->IsDrawn());
    ASSERT_TRUE(applied_.WaitForPresentedFrame(*menu_widget));
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        zoom_label->GetBoundsInScreen().CenterPoint(),
        menu_widget->GetNativeWindow()));
    ASSERT_TRUE(zoom_item->IsSelected());
    ASSERT_TRUE(std::ranges::none_of(buttons, [](const views::Button* button) {
      return button->IsHotTracked();
    }));

    const auto present_after = [&](auto dispatch) {
      return applied_.Run({applied_.Frame(*menu_widget, /*redraw=*/false)},
                          std::move(dispatch), yee::test::AppliedDomain::kFrame);
    };
    for (auto* const button : buttons) {
      SCOPED_TRACE(button->tag());
      ASSERT_TRUE(present_after([&] {
        ui::KeyEvent key(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE);
        controller->OnWillDispatchKeyEvent(&key);
      }));
      ASSERT_TRUE(button->IsHotTracked());
      ASSERT_NO_FATAL_FAILURE(ExpectMenuZoomHighlight(*button, false));
      bool moved = false;
      const auto presented = present_after([&] {
        moved = ui_test_utils::SendMouseMoveSync(
            button->GetBoundsInScreen().CenterPoint(),
            menu_widget->GetNativeWindow());
      });
      ASSERT_TRUE(moved);
      ASSERT_TRUE(presented);
      ASSERT_NO_FATAL_FAILURE(ExpectMenuZoomHighlight(*button, true));
    }
  }
}

void ExpectWindowsHighContrastIconHover(views::Button* button) {
  ASSERT_TRUE(button && button->IsDrawn() && button->GetEnabled());
  const auto* const provider = button->GetColorProvider();
  auto* const host = views::InkDrop::Get(button);
  EXPECT_EQ(provider->GetColor(ui::kColorNativeHighlight), host->GetBaseColor());
  auto rich_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* const ink_drop = host->GetInkDrop();
  views::test::InkDropImplTestApi ink_drop_api(
      static_cast<views::InkDropImpl*>(ink_drop));
  button->SetState(views::Button::STATE_HOVERED);
  ink_drop->SetHovered(true);
  ASSERT_TRUE(ink_drop_api.highlight());
  EXPECT_FLOAT_EQ(1.0f, ink_drop_api.highlight()->layer()->opacity());
  const SkColor glyph_color =
      provider->GetColor(ui::kColorNativeHighlightText);
  const SkBitmap painted = views::test::PaintViewToBitmap(button);
  bool paints_highlight_glyph = false;
  for (int y = 0; y < painted.height(); ++y) {
    for (int x = 0; x < painted.width(); ++x) {
      paints_highlight_glyph |= painted.getColor(x, y) == glyph_color;
    }
  }
  EXPECT_TRUE(paints_highlight_glyph);
  button->SetState(views::Button::STATE_NORMAL);
  ink_drop->SetHovered(false);
  EXPECT_FALSE(ink_drop_api.highlight());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PopupIconsUseReadableWindowsHighContrastPair) {
  // 테스트 제공자가 사라지면 운영체제의 실제 테마 설정을 다시 읽는다.
  base::ScopedClosureRunner restore_native_theme(base::BindOnce([] {
    auto* const native_theme = ui::NativeTheme::GetInstanceForNativeUi();
    native_theme->BeginObservingOsSettingChanges();
    native_theme->NotifyOnNativeThemeUpdated();
  }));
  ui::MockOsSettingsProvider settings;
  settings.SetPreferredContrast(ui::NativeTheme::PreferredContrast::kMore);
  settings.SetForcedColorsActive(true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(view->GetLocationBarView()->ShowPageInfoDialog());
  auto* const page_info = PageInfoBubbleView::GetPageInfoBubbleForTesting();
  ASSERT_TRUE(page_info);
  auto* const widget = page_info->GetWidget();
  widget->LayoutRootViewIfNecessary();
  EXPECT_EQ(view->GetWidget()->GetColorProviderKeyForTesting().contrast_mode,
            ui::ColorProviderKey::ContrastMode::kHigh);
  auto* const close = views::Button::AsButton(page_info->GetViewByID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_CLOSE_BUTTON));
  ASSERT_NO_FATAL_FAILURE(ExpectWindowsHighContrastIconHover(close));
  ASSERT_NO_FATAL_FAILURE(ExpectPopupIconHover(close));
  widget->CloseNow();

  views::NamedWidgetShownWaiter waiter(views::test::AnyWidgetTestPasskey{},
                                      "ChooserBubbleUiViewDelegate");
  base::ScopedClosureRunner close_chooser(chrome::ShowDeviceChooserDialog(
      Contents()->GetPrimaryMainFrame(),
      std::make_unique<FakeUsbChooserController>(1)));
  auto* const chooser = waiter.WaitIfNeededAndGet();
  ASSERT_TRUE(chooser);
  chooser->LayoutRootViewIfNecessary();
  auto* const dialog = chooser->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(dialog);
  ASSERT_NO_FATAL_FAILURE(ExpectWindowsHighContrastIconHover(
      FindView<views::ImageButton>(dialog->GetExtraView())));
}
#endif

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PageInfoOpensNativeSiteSettings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  ASSERT_TRUE(BrowserView::GetBrowserViewForBrowser(browser())
                  ->GetLocationBarView()
                  ->ShowPageInfoDialog());
  auto* page_info = PageInfoBubbleView::GetPageInfoBubbleForTesting();
  ASSERT_TRUE(page_info);
  auto* settings = views::Button::AsButton(page_info->GetViewByID(
      PageInfoViewFactory::VIEW_ID_PAGE_INFO_LINK_OR_BUTTON_SITE_SETTINGS));
  ASSERT_TRUE(settings);
  content::WebContentsAddedObserver new_tab;
  views::test::ButtonTestApi(settings).NotifyDefaultMouseClick();
  auto* settings_contents = new_tab.GetWebContents();
  ASSERT_TRUE(settings_contents);
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  ASSERT_TRUE(content::WaitForLoadStop(settings_contents));
  EXPECT_TRUE(settings_contents->GetVisibleURL().spec().starts_with(
      "chrome://settings/content/siteDetails?site="));
  EXPECT_NE(std::string::npos,
            settings_contents->GetVisibleURL().spec().find("yee-fixture.test"));
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, ButtonOutlivesDestroyedTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  SiteControlsButton detached_button(browser());
  detached_button.Update(Contents(), false);
  ASSERT_TRUE(detached_button.GetVisible());
  auto* popup = OpenControls();
  ASSERT_TRUE(popup);
  content::WebContentsDestroyedWatcher destroyed(Contents());
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup, {.closed = true},
                       yee::test::AppliedMatch::kWidgetClosed)},
      [&] {
        browser()->tab_strip_model()->CloseWebContentsAt(0, 0);
        destroyed.Wait();
      },
      yee::test::AppliedDomain::kWidget));
  EXPECT_EQ(nullptr, detached_button.web_contents());
  EXPECT_FALSE(detached_button.GetVisible());
  detached_button.SetSurfacePresentation(
      ResolveBrowserSurfacePresentation(SK_ColorWHITE, 1, 1, 1));
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, WindowClosesWithControlsOpen) {
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, FixtureURL()));
  ASSERT_TRUE(OpenControls(second));
  CloseBrowserSynchronously(second);
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       SwitchingVisibleSplitPaneClosesControls) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* first = Contents();
  chrome::AddTabAt(browser(), FixtureURL("other.yee-fixture.test"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  auto* second = Contents();
  auto* model = browser()->tab_strip_model();
  model->ActivateTabAt(0);
  ASSERT_EQ(first, model->GetWebContentsAt(0));
  ASSERT_EQ(second, model->GetWebContentsAt(1));
  auto* panes =
      BrowserView::GetBrowserViewForBrowser(browser())->multi_contents_view();
  const uint64_t first_handle =
      model->GetTabAtIndex(0)->GetHandle().raw_value();
  const uint64_t second_handle =
      model->GetTabAtIndex(1)->GetHandle().raw_value();
  yee::test::AppliedConditions split_ready;
  for (auto* contents : {first, second}) {
    const uint64_t handle = contents == first ? first_handle : second_handle;
    const uint64_t peer = contents == first ? second_handle : first_handle;
    auto host = applied_.Condition(
        applied_.source().ExpectPageHosts(*browser(), *panes, handle, handle,
                                          true, peer),
        yee::test::PageHostsApplied{.pane_id = handle, .tab_handle = handle},
        yee::test::AppliedMatch::kPageIdentity,
        yee::test::AppliedFreshness::kNewApply);
    host.coordinates = yee::test::GetAppliedViewCoordinates(
        *panes, yee::test::AppliedCoordinates::Space::kParent);
    split_ready.push_back(std::move(host));
    split_ready.push_back(applied_.Condition(
        applied_.source().ObserveWebContentsVisibility(*contents),
        yee::test::ViewStateMatches{.visible = true},
        yee::test::AppliedMatch::kViewVisible));
  }
  // 인덱스 하나를 넘기면 활성 탭 0을 pivot으로 탭 1과 묶는다.
  ASSERT_TRUE(applied_.Run(std::move(split_ready), [&] {
    model->AddToNewSplit({1}, split_tabs::SplitTabVisualData(),
                         split_tabs::SplitTabCreatedSource::kToolbarButton);
  }));
  ASSERT_TRUE(model->GetTabAtIndex(0)->IsVisible());
  ASSERT_TRUE(model->GetTabAtIndex(1)->IsVisible());
  ASSERT_EQ(first, Contents());
  auto* popup = OpenControls();
  ASSERT_TRUE(popup);
  // 창 비활성화의 자동 닫힘과 분리해 탭 소유권의 닫힘을 검사한다.
  auto* delegate = popup->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(delegate);
  delegate->set_close_on_deactivate(false);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup, {.closed = true},
                       yee::test::AppliedMatch::kWidgetClosed)},
      [&] { chrome::SelectNextTab(browser()); },
      yee::test::AppliedDomain::kWidget));
  EXPECT_EQ(second, Contents());
  EXPECT_TRUE(model->GetTabAtIndex(0)->IsVisible());
  EXPECT_TRUE(model->GetTabAtIndex(1)->IsVisible());
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       MovingOwnerTabToOtherWindowClosesControls) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* owner = Contents();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  BrowserWindowInterface* destination = CreateBrowser(browser()->GetProfile());
  auto* popup = OpenControls();
  ASSERT_TRUE(popup);
  auto* delegate = popup->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(delegate);
  delegate->set_close_on_deactivate(false);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup, {.closed = true},
                       yee::test::AppliedMatch::kWidgetClosed)},
      [&] { chrome::MoveTabsToExistingWindow(browser(), destination, {0}); },
      yee::test::AppliedDomain::kWidget));
  EXPECT_EQ(-1, browser()->tab_strip_model()->GetIndexOfWebContents(owner));
  EXPECT_EQ(owner, destination->GetTabStripModel()->GetActiveWebContents());
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
  CloseBrowserSynchronously(destination);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, RapidTabSwitchKeepsBadgeOwner) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  CheckPage(true);
  auto* first = Contents();
  chrome::AddTabAt(browser(), FixtureURL("other.yee-fixture.test"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  CheckPage(true);
  auto* second = Contents();
  EXPECT_EQ(true,
            content::EvalJs(
                second,
                "fetch('http://yee-block.test:' + location.port + '/second')"
                ".then(() => false, () => true)"));
  // renderer의 차단 결과 IPC/IO 알림을 비UI 모델에 전달한다.
  base::RunLoop().RunUntilIdle();
  auto* first_helper =
      content_blocking::ContentBlockingTabHelper::FromWebContents(first);
  auto* second_helper =
      content_blocking::ContentBlockingTabHelper::FromWebContents(second);
  ASSERT_NE(first_helper->blocked_count(), second_helper->blocked_count());
  for (int i = 0; i < 30; ++i) {
    browser()->tab_strip_model()->ActivateTabAt(i % 2);
    auto* button = Button();
    ASSERT_TRUE(button);
    EXPECT_EQ(i % 2 ? second : first, button->web_contents());
    auto* badge = FindView<views::Label>(button);
    ASSERT_TRUE(badge);
    EXPECT_TRUE(badge->GetVisible());
    EXPECT_EQ(i % 2 ? u"2" : u"1", badge->GetText());
    // 탭 전환에서 건수와 페이지 기반 전경색이 같은 탭을 따라야 한다.
    const SkColor surface = i % 2 ? SK_ColorWHITE : SK_ColorBLACK;
    auto* source =
        BrowserSurfaceColorController::FromWebContents(button->web_contents());
    ASSERT_TRUE(source);
    source->SetPageSurfaceColorForTesting(surface);
    button->GetWidget()->LayoutRootViewIfNecessary();
    EXPECT_TRUE(button->GetLocalBounds().Contains(badge->bounds()));
    EXPECT_GE(badge->x(), button->GetMinimumImageSize().width());
    EXPECT_GE(badge->height(), badge->font_list().GetHeight());
    gfx::Canvas canvas(badge->size(), 1.f, false);
    badge->background()->Paint(&canvas, badge);
    const auto center = badge->GetLocalBounds().CenterPoint();
    EXPECT_GE(color_utils::GetContrastRatio(
                  badge->GetEnabledColor(),
                  canvas.GetBitmap().getColor(center.x(), center.y())),
              color_utils::kMinimumReadableContrastRatio);
  }
  auto* popup = OpenControls();
  ASSERT_TRUE(popup);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup, {.closed = true},
                       yee::test::AppliedMatch::kWidgetClosed)},
      [&] { browser()->tab_strip_model()->ActivateTabAt(0); },
      yee::test::AppliedDomain::kWidget));
  EXPECT_FALSE(IsSiteControlsBubbleShowing());

  const auto capture_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-header-capture-dir");
  if (capture_dir.empty()) {
    return;
  }
  // 선택적 캡처만 실제 native Header와 차단 helper를 사용한다.
  // 일반 실행은 스크린샷 없이 탭별 건수 소유권을 검사한다.
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* theme = ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  auto* state = tabs::VerticalTabStripStateController::From(browser());
  const auto original_scheme = theme->GetBrowserColorScheme();
  while (first_helper->blocked_count() < 10) {
    first_helper->RecordBlockedRequest();
  }
  const auto capture = [&](const std::string& name) {
    view->GetWidget()->LayoutRootViewIfNecessary();
    auto* compositor = view->GetWidget()->GetCompositor();
    compositor->ScheduleFullRedraw();
    ASSERT_TRUE(applied_.WaitForPresentedFrame(*view->GetWidget()));
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
    ui::GrabWindowSnapshotAsPNG(
        view->GetWidget()->GetNativeWindow(),
        gfx::Rect(view->GetWidget()->GetWindowBoundsInScreen().size()),
        snapshot.GetCallback());
    auto png = snapshot.Take();
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(base::WriteFile(capture_dir.AppendASCII(name),
                                static_cast<base::span<const uint8_t>>(*png)));
  };
  for (bool dark : {false, true}) {
    theme->SetBrowserColorScheme(
        dark ? ThemeService::BrowserColorScheme::kDark
             : ThemeService::BrowserColorScheme::kLight);
    const SkColor surface = dark ? SK_ColorBLACK : SK_ColorWHITE;
    ASSERT_TRUE(
        content::ExecJs(first, dark ? "document.body.style.background='black';"
                                      "document.body.style.color='white'"
                                    : "document.body.style.background='white';"
                                      "document.body.style.color='black'"));
    for (bool vertical : {false, true}) {
      state->SetVerticalTabsEnabled(vertical);
      auto* source = BrowserSurfaceColorController::FromWebContents(first);
      ASSERT_TRUE(source);
      source->SetPageSurfaceColorForTesting(surface);
      view->GetWidget()->GetFocusManager()->ClearFocus();
      const std::string name = std::string(dark ? "dark" : "light") +
                               (vertical ? "-sidebar.png" : "-horizontal.png");
      capture(name);
    }
  }
  // 장식용 숫자 위의 native hover·클릭도 부모 버튼에 전달돼야 한다.
  // 명시적으로 요청한 시각 검사에서만 실제 입력을 활성화한다.
#if BUILDFLAG(IS_WIN)
  aura::test::EnableUIControlsAuraWin();
#else
  ui_controls::EnableUIControls();
#endif
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  auto* button = Button();
  auto* badge = FindView<views::Label>(button);
  const auto native_window = view->GetWidget()->GetNativeWindow();
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      view->GetLocationBarView()->GetBoundsInScreen().CenterPoint(),
      native_window));
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*button, {.hovered = true},
                     yee::test::AppliedMatch::kViewHovered)},
      [&] {
        ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
            badge->GetBoundsInScreen().CenterPoint(), native_window));
      }));
  ASSERT_EQ(views::Button::STATE_HOVERED, button->GetState());
  capture("dark-sidebar-hover.png");
  ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP, native_window));
  EXPECT_TRUE(IsSiteControlsBubbleShowing());
  for (views::Widget* widget :
       views::Widget::GetAllOwnedWidgets(view->GetWidget()->GetNativeView())) {
    if (FindView<views::ToggleButton>(widget->GetContentsView())) {
      widget->CloseNow();
    }
  }
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      view->GetLocationBarView()->GetBoundsInScreen().CenterPoint(),
      native_window));
  auto* focus = view->GetWidget()->GetFocusManager();
  focus->SetKeyboardAccessible(true);
  focus->SetFocusedViewWithReason(
      button, views::FocusManager::FocusChangeReason::kFocusTraversal);
  EXPECT_TRUE(views::FocusRing::Get(button)->ShouldPaintForTesting());
  capture("dark-sidebar-focus.png");
  focus->ClearFocus();
  // 시스템 마우스 버튼을 누른 채 두지 않고 native 눌림 상태를 캡처한다.
  button->SetState(views::Button::STATE_PRESSED);
  capture("dark-sidebar-pressed.png");
  button->SetState(views::Button::STATE_NORMAL);
  Service()->SetEnabledForSite(first->GetVisibleURL(), false);
  EXPECT_FALSE(badge->GetVisible());
  capture("dark-sidebar-protection-off.png");
  Service()->SetEnabledForSite(first->GetVisibleURL(), true);
  while (first_helper->blocked_count() < 100) {
    first_helper->RecordBlockedRequest();
  }
  EXPECT_EQ(u"99+", badge->GetText());
  capture("dark-sidebar-99plus.png");
  EXPECT_TRUE(button->GetLocalBounds().Contains(badge->bounds()));
  theme->SetBrowserColorScheme(original_scheme);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       SplitPanesKeepExactSiteException) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* first = Contents();
  chrome::AddTabAt(browser(), FixtureURL("other.yee-fixture.test"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  auto* second = Contents();
  browser()->tab_strip_model()->ActivateTabAt(0);
  browser()->tab_strip_model()->AddToNewSplit(
      {1}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_TRUE(Button());
  EXPECT_EQ(first, Button()->web_contents());
  ToggleAndWaitForReload();
  CheckPage(false, first);
  CheckPage(true, second);
  EXPECT_TRUE(Service()->EnabledForSite(second->GetVisibleURL()));
  browser()->tab_strip_model()->ActivateTabAt(1);
  EXPECT_EQ(second, Button()->web_contents());
  EXPECT_TRUE(FindView<views::Label>(Button())->GetVisible());
  ASSERT_TRUE(BrowserView::GetBrowserViewForBrowser(browser())
                  ->GetLocationBarView()
                  ->ShowPageInfoDialog());
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
  EXPECT_EQ(PageInfoBubbleView::BUBBLE_PAGE_INFO,
            PageInfoBubbleView::GetShownBubbleType());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, MultipleWindowsKeepExactSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, FixtureURL("other.yee-fixture.test")));
  ToggleAndWaitForReload(second);
  CheckPage(false, second->tab_strip_model()->GetActiveWebContents());
  CheckPage(true);
  EXPECT_TRUE(Service()->EnabledForSite(FixtureURL()));
  EXPECT_FALSE(Service()->EnabledForSite(FixtureURL("other.yee-fixture.test")));
  CloseBrowserSynchronously(second);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       SiteChangeUpdatesOtherWindowBadge) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  BrowserWindowInterface* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, FixtureURL()));
  CheckPage(true, second->tab_strip_model()->GetActiveWebContents());
  ASSERT_TRUE(Button(second));
  auto* badge = FindView<views::Label>(Button(second));
  ASSERT_TRUE(badge);
  ASSERT_TRUE(badge->GetVisible());
  ToggleAndWaitForReload();
  EXPECT_FALSE(badge->GetVisible());
  CloseBrowserSynchronously(second);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       IncognitoExceptionStaysPrivate) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  BrowserWindowInterface* incognito =
      CreateIncognitoBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, FixtureURL()));
  ToggleAndWaitForReload(incognito);
  CheckPage(false, incognito->tab_strip_model()->GetActiveWebContents());
  EXPECT_TRUE(Service()->EnabledForSite(FixtureURL()));
  EXPECT_TRUE(browser()
                  ->GetProfile()
                  ->GetPrefs()
                  ->GetList(content_blocking::kDisabledSitesPref)
                  .empty());
  CloseBrowserSynchronously(incognito);
  BrowserWindowInterface* fresh =
      CreateIncognitoBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(fresh, FixtureURL()));
  CheckPage(true, fresh->tab_strip_model()->GetActiveWebContents());
  CloseBrowserSynchronously(fresh);
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       DedicatedWorkerReadsLivePolicy) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  StartWorker(false);
  EXPECT_EQ(false, WorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), true);
  EXPECT_EQ(true, WorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ(false, WorkerFetch());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       SharedWorkerMultipleClientsReadLivePolicy) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  StartWorker(true);
  auto* first = Contents();
  chrome::AddTabAt(browser(), WorkerURL(), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  StartWorker(true);
  EXPECT_EQ(false, WorkerFetch(first));
  EXPECT_EQ(false, WorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), true);
  EXPECT_EQ(true, WorkerFetch(first));
  EXPECT_EQ(true, WorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ(false, WorkerFetch(first));
  EXPECT_EQ(false, WorkerFetch());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       ServiceWorkerMultipleClientsReadLivePolicy) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  StartServiceWorker();
  auto* first = Contents();
  chrome::AddTabAt(browser(), WorkerURL(), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  StartServiceWorker();
  EXPECT_EQ(false, ServiceWorkerFetch(false, first));
  EXPECT_EQ(false, ServiceWorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), true);
  EXPECT_EQ(true, ServiceWorkerFetch(false, first));
  EXPECT_EQ(true, ServiceWorkerFetch());
  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ(false, ServiceWorkerFetch(false, first));
  EXPECT_EQ(false, ServiceWorkerFetch());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       HttpCacheCannotBypassReenabledProtection) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  const auto fetch = content::JsReplace(
      "fetch($1, {cache: 'force-cache'}).then(r => r.text(), () => 'blocked')",
      AdURL("/cache-ad"));
  EXPECT_EQ("allowed", content::EvalJs(Contents(), fetch));
  EXPECT_EQ(1, cached_ad_requests_->load());
  Service()->SetEnabledForSite(WorkerURL(), true);
  EXPECT_EQ("blocked", content::EvalJs(Contents(), fetch));
  EXPECT_EQ(1, cached_ad_requests_->load());
  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ("allowed", content::EvalJs(Contents(), fetch));
  EXPECT_EQ(1, cached_ad_requests_->load());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       ServiceWorkerCacheCannotBypassReenabledProtection) {
  const GURL cached_url = AdURL("/cache-ad");
  // HTTP 캐시를 우회해 서비스워커의 CacheStorage 응답만 확인한다.
  const auto fetch = content::JsReplace(
      "fetch($1, {cache: 'no-store'}).then(r => r.text(), () => 'blocked')",
      cached_url);
  const auto cached_response = content::JsReplace(
      "caches.match($1).then(r => r ? r.text() : 'missing')", cached_url);

  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  StartServiceWorker();
  ASSERT_EQ(false, ServiceWorkerFetch(true));
  ASSERT_EQ(1, cached_ad_requests_->load());
  ASSERT_EQ("allowed", content::EvalJs(Contents(), cached_response));
  ASSERT_EQ("allowed", content::EvalJs(Contents(), fetch));
  ASSERT_EQ(1, cached_ad_requests_->load());

  Service()->SetEnabledForSite(WorkerURL(), true);
  // 캐시를 삭제하지 않아도 응답을 반환하기 전 현재 정책으로 차단해야 한다.
  EXPECT_EQ("blocked", content::EvalJs(Contents(), fetch));
  EXPECT_EQ("allowed", content::EvalJs(Contents(), cached_response));
  EXPECT_EQ(1, cached_ad_requests_->load());

  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ("allowed", content::EvalJs(Contents(), fetch));
  EXPECT_EQ(1, cached_ad_requests_->load());

  // 캐시를 비우면 같은 요청이 서버에 도달하는지 대조한다.
  ASSERT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"(
    caches.open('yee-regression').then(cache => cache.delete($1))
  )",
                                                                 cached_url)));
  EXPECT_EQ("allowed", content::EvalJs(Contents(), fetch));
  EXPECT_EQ(2, cached_ad_requests_->load());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       LinkPrefetchReadsSiteException) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  const auto prefetch = [](const GURL& url) {
    return content::JsReplace(R"(
      Promise.race([
        new Promise(resolve => {
          const link = document.createElement('link');
          link.rel = 'prefetch'; link.href = $1;
          link.onload = () => resolve(false);
          link.onerror = () => resolve(true);
          document.head.append(link);
        }),
        new Promise(resolve => setTimeout(() => resolve('timeout'), 10000))
      ])
    )",
                              url);
  };
  EXPECT_EQ(true, content::EvalJs(Contents(), prefetch(AdURL("/prefetch-on"))));
  Service()->SetEnabledForSite(WorkerURL(), false);
  EXPECT_EQ(false,
            content::EvalJs(Contents(), prefetch(AdURL("/prefetch-off"))));
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       PrerenderDoesNotCountAgainstPrimaryPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  auto* helper =
      content_blocking::ContentBlockingTabHelper::FromWebContents(Contents());
  ASSERT_TRUE(helper);
  ASSERT_EQ(0u, helper->blocked_count());
  const GURL target =
      embedded_test_server()->GetURL("localhost", "/prerender-fixture");
  const auto id = prerender_helper_.AddPrerender(target);
  auto* frame = prerender_helper_.GetPrerenderedMainFrameHost(id);
  ASSERT_TRUE(frame);
  EXPECT_EQ(true, content::EvalJs(frame, "window.blocked",
                                  content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  // renderer의 차단 결과 IPC/IO 알림을 비UI 모델에 전달한다.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, helper->blocked_count());
  prerender_helper_.NavigatePrimaryPage(target);
  EXPECT_EQ(frame, Contents()->GetPrimaryMainFrame());
  EXPECT_EQ(0u, helper->blocked_count());
  EXPECT_EQ(true, content::EvalJs(
                      Contents(),
                      content::JsReplace(
                          "fetch($1).then(() => false, () => true)", AdURL())));
  // renderer의 차단 결과 IPC/IO 알림을 비UI 모델에 전달한다.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, helper->blocked_count());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, PrerenderReadsSiteException) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  const GURL target =
      embedded_test_server()->GetURL("localhost", "/prerender-fixture");
  const auto id = prerender_helper_.AddPrerender(target);
  auto* frame = prerender_helper_.GetPrerenderedMainFrameHost(id);
  ASSERT_TRUE(frame);
  EXPECT_EQ(false, content::EvalJs(frame, "window.blocked",
                                   content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  prerender_helper_.NavigatePrimaryPage(target);
  EXPECT_EQ(frame, Contents()->GetPrimaryMainFrame());
  EXPECT_FALSE(Service()->EnabledForSite(target));
  EXPECT_EQ(false, content::EvalJs(Contents(), "window.blocked"));
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest,
                       BackForwardCacheRestoresDocument) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  CheckPage(true);
  ASSERT_TRUE(content::ExecJs(Contents(), "window.cacheMarker = 17"));
  content::RenderFrameHostWrapper original(Contents()->GetPrimaryMainFrame());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  ASSERT_TRUE(original);
  EXPECT_EQ(content::RenderFrameHost::LifecycleState::kInBackForwardCache,
            original->GetLifecycleState());
  content::TestNavigationObserver restored(Contents());
  Contents()->GetController().GoBack();
  restored.Wait();
  EXPECT_EQ(original.get(), Contents()->GetPrimaryMainFrame());
  EXPECT_EQ(17, content::EvalJs(Contents(), "window.cacheMarker"));
  CheckPage(true);
  auto* helper =
      content_blocking::ContentBlockingTabHelper::FromWebContents(Contents());
  ASSERT_TRUE(helper);
  EXPECT_EQ(0u, helper->blocked_count());
  EXPECT_EQ(true, content::EvalJs(
                      Contents(),
                      content::JsReplace(
                          "fetch($1).then(() => false, () => true)", AdURL())));
  // renderer의 차단 결과 IPC/IO 알림을 비UI 모델에 전달한다.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, helper->blocked_count());
}

}  // namespace
}  // namespace yee
