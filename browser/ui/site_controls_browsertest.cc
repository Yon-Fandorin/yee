// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/site_controls.h"

#include <atomic>
#include <memory>
#include <string>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
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
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

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

  views::Widget* OpenControls(SiteControlsSection section,
                              Browser* owner = nullptr) {
    if (!owner) {
      owner = browser();
    }
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(owner);
    EXPECT_TRUE(ShowSiteControlsBubble(
        browser_view->GetLocationBarView(), owner,
        owner->tab_strip_model()->GetActiveWebContents(), section));
    for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
             browser_view->GetWidget()->GetNativeView())) {
      if (FindView<views::TabbedPane>(widget->GetContentsView())) {
        return widget;
      }
    }
    return nullptr;
  }

  void ToggleAndWaitForReload(Browser* owner = nullptr) {
    if (!owner) {
      owner = browser();
    }
    auto* contents = owner->tab_strip_model()->GetActiveWebContents();
    auto* widget = OpenControls(SiteControlsSection::kProtection, owner);
    ASSERT_TRUE(widget);
    auto* toggle = FindView<views::ToggleButton>(widget->GetContentsView());
    ASSERT_TRUE(toggle);
    content::TestNavigationObserver reload(contents);
    views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
    reload.Wait();
    EXPECT_TRUE(reload.last_navigation_succeeded());
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(IsSiteControlsBubbleShowing());
  }

  SiteControlsButton* Button(Browser* owner = nullptr) {
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

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, PageInfoTabsAndSiteSettings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  auto* widget = OpenControls(SiteControlsSection::kPageInfo);
  ASSERT_TRUE(widget);
  auto* tabs = FindView<views::TabbedPane>(widget->GetContentsView());
  ASSERT_TRUE(tabs);
  EXPECT_EQ(1u, tabs->GetSelectedTabIndex());
  tabs->SelectTabAt(0, false);
  EXPECT_EQ(0u, tabs->GetSelectedTabIndex());
  tabs->SelectTabAt(1, false);
  auto* settings =
      FindView<views::MdTextButton>(tabs->GetTabContentsForTesting(1));
  ASSERT_TRUE(settings);
  views::test::ButtonTestApi(settings).NotifyDefaultMouseClick();
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_TRUE(Contents()->GetVisibleURL().spec().starts_with(
      "chrome://settings/content/siteDetails?site="));
  EXPECT_NE(std::string::npos,
            Contents()->GetVisibleURL().spec().find("yee-fixture.test"));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, ButtonOutlivesDestroyedTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  SiteControlsButton detached_button(browser());
  detached_button.Update(Contents(), false);
  ASSERT_TRUE(detached_button.GetVisible());
  ASSERT_TRUE(OpenControls(SiteControlsSection::kProtection));
  content::WebContentsDestroyedWatcher destroyed(Contents());
  browser()->tab_strip_model()->CloseWebContentsAt(0, 0);
  destroyed.Wait();
  EXPECT_EQ(nullptr, detached_button.web_contents());
  EXPECT_FALSE(detached_button.GetVisible());
  detached_button.SetSurfacePresentation(
      ResolveBrowserSurfacePresentation(SK_ColorWHITE, 1, 1, 1));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, WindowClosesWithControlsOpen) {
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, FixtureURL()));
  auto* view = BrowserView::GetBrowserViewForBrowser(second);
  ASSERT_TRUE(
      ShowSiteControlsBubble(view->GetLocationBarView(), second,
                             second->tab_strip_model()->GetActiveWebContents(),
                             SiteControlsSection::kProtection));
  CloseBrowserSynchronously(second);
  EXPECT_FALSE(IsSiteControlsBubbleShowing());
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
  ASSERT_TRUE(OpenControls(SiteControlsSection::kProtection));
  browser()->tab_strip_model()->ActivateTabAt(0);
  base::RunLoop().RunUntilIdle();
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
    ASSERT_TRUE(
        ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
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
  ui_controls::EnableUIControls();
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  auto* button = Button();
  auto* badge = FindView<views::Label>(button);
  const auto native_window = view->GetWidget()->GetNativeWindow();
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      view->GetLocationBarView()->GetBoundsInScreen().CenterPoint(),
      native_window));
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      badge->GetBoundsInScreen().CenterPoint(), native_window));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return button->GetState() == views::Button::STATE_HOVERED; }));
  capture("dark-sidebar-hover.png");
  ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
      ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP, native_window));
  EXPECT_TRUE(IsSiteControlsBubbleShowing());
  for (views::Widget* widget :
       views::Widget::GetAllOwnedWidgets(view->GetWidget()->GetNativeView())) {
    if (FindView<views::TabbedPane>(widget->GetContentsView())) {
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
  EXPECT_TRUE(IsSiteControlsBubbleShowing());
}

IN_PROC_BROWSER_TEST_F(SiteControlsBrowserTest, MultipleWindowsKeepExactSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), FixtureURL()));
  Browser* second = CreateBrowser(browser()->GetProfile());
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
  Browser* second = CreateBrowser(browser()->GetProfile());
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
  Browser* incognito = CreateIncognitoBrowser(browser()->GetProfile());
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
  Browser* fresh = CreateIncognitoBrowser(browser()->GetProfile());
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
                       ServiceWorkerCachedResponseIsOutsideNetworkFilter) {
  Service()->SetEnabledForSite(WorkerURL(), false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), WorkerURL()));
  StartServiceWorker();
  ASSERT_EQ(false, ServiceWorkerFetch(true));
  ASSERT_EQ(1, cached_ad_requests_->load());
  Service()->SetEnabledForSite(WorkerURL(), true);
  // CacheStorage delivery never starts an HTTP URLLoaderFactory request.
  // This boundary is recorded explicitly; it is not evidence of ad blocking.
  EXPECT_EQ("allowed", content::EvalJs(
                           Contents(),
                           content::JsReplace(
                               "fetch($1).then(r => r.text(), () => 'blocked')",
                               AdURL("/cache-ad"))));
  EXPECT_EQ(1, cached_ad_requests_->load());
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
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0u, helper->blocked_count());
  prerender_helper_.NavigatePrimaryPage(target);
  EXPECT_EQ(frame, Contents()->GetPrimaryMainFrame());
  EXPECT_EQ(0u, helper->blocked_count());
  EXPECT_EQ(true, content::EvalJs(
                      Contents(),
                      content::JsReplace(
                          "fetch($1).then(() => false, () => true)", AdURL())));
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
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, helper->blocked_count());
}

}  // namespace
}  // namespace yee
