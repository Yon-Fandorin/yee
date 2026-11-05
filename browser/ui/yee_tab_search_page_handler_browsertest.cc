// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/tab_search/tab_search_page_handler.h"

#include <stdint.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/test_future.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/timer/mock_timer.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/sessions/chrome_tab_restore_service_client.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/tab_group_sync/tab_group_sync_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_ui_controller/browser_ui_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/tab_group_sync_service_initialized_observer.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_group_deletion_dialog_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/metrics_reporter/metrics_reporter.h"
#include "chrome/browser/ui/webui/metrics_reporter/mock_metrics_reporter.h"
#include "chrome/browser/ui/webui/tab_search/tab_search.mojom-forward.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_prefs.h"
#include "chrome/browser/ui/webui/tab_search/tab_search_ui.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/browser/vr/vr_tab_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/tab_restore_service_impl.h"
#include "components/split_tabs/split_tab_id.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/base_window.h"
#include "ui/base/unowned_user_data/unowned_user_data_host.h"
#include "ui/base/unowned_user_data/user_data_factory.h"
#include "ui/gfx/color_utils.h"

using testing::_;
namespace {
constexpr char kTabName1[] = "Tab 1";
class MockPage : public tab_search::mojom::Page {
 public:
  MockPage() = default;
  ~MockPage() override = default;

  mojo::PendingRemote<tab_search::mojom::Page> BindAndGetRemote() {
    DCHECK(!receiver_.is_bound());
    return receiver_.BindNewPipeAndPassRemote();
  }
  mojo::Receiver<tab_search::mojom::Page> receiver_{this};

  MOCK_METHOD(void, HostWindowChanged, ());
  MOCK_METHOD(void, TabsChanged, (tab_search::mojom::ProfileDataPtr));
  MOCK_METHOD(void, TabUpdated, (tab_search::mojom::TabUpdateInfoPtr));
  MOCK_METHOD(void, TabsRemoved, (tab_search::mojom::TabsRemovedInfoPtr));
  MOCK_METHOD(void, TabUnsplit, ());
};
class TestTabSearchPageHandler : public TabSearchPageHandler {
 public:
  TestTabSearchPageHandler(mojo::PendingRemote<tab_search::mojom::Page> page,
                           content::WebUI* web_ui,
                           TabSearchUI* webui_controller)
      : TabSearchPageHandler(
            mojo::PendingReceiver<tab_search::mojom::PageHandler>(),
            std::move(page),
            web_ui,
            webui_controller,
            &metrics_reporter_) {
    auto timer = std::make_unique<base::MockRetainingOneShotTimer>();
    mock_debounce_timer_ = timer.get();
    SetTimerForTesting(std::move(timer));
  }

  base::MockRetainingOneShotTimer* mock_debounce_timer() {
    return mock_debounce_timer_;
  }

 private:
  raw_ptr<base::MockRetainingOneShotTimer> mock_debounce_timer_ = nullptr;
  testing::NiceMock<MockMetricsReporter> metrics_reporter_;
};

class YeeTabSearchPageHandlerTest : public InProcessBrowserTest,
                                    public testing::WithParamInterface<bool> {
 public:
  YeeTabSearchPageHandlerTest() {
    // Parametrize tests on kTabSearchPerformanceImprovements to verify
    // behavior in both enabled and disabled experiment arms.
    feature_list_.InitWithFeatureState(tabs::kTabSearchPerformanceImprovements,
                                       GetParam());
    webui_omnibox_feature_list_.InitWithFeatures(
        /*enabled_features=*/{},
        /*disabled_features=*/
        // TODO(crbug.com/452061489): Fix tests that fail when the WebUI Omnibox
        // is enabled and then remove these two Features.
        {omnibox::internal::kWebUIOmniboxPopup,
         omnibox::internal::kWebUIOmniboxAimPopup});
  }
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    tab_url1_ = embedded_test_server()->GetURL("/title1.html?1");
    tab_url2_ = embedded_test_server()->GetURL("/title1.html?2");
    browser1()->GetWindow()->Activate();
    BrowserUiController::From(browser1())
        ->set_update_ui_immediately_for_testing();
    web_contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(profile1()));
    web_ui_.set_web_contents(web_contents_.get());
    webui::SetBrowserWindowInterface(web_contents_.get(), browser1());
    webui_controller_ = std::make_unique<TabSearchUI>(web_ui());
    handler_ = std::make_unique<TestTabSearchPageHandler>(
        page_.BindAndGetRemote(), web_ui(), webui_controller_.get());
    EXPECT_CALL(page_, HostWindowChanged()).Times(testing::AnyNumber());
    EXPECT_CALL(page_, TabUpdated(_)).Times(testing::AnyNumber());
    EXPECT_CALL(page_, TabsChanged(_)).Times(testing::AnyNumber());
    EXPECT_CALL(page_, TabsRemoved(_)).Times(testing::AnyNumber());
    EXPECT_CALL(page_, TabUnsplit()).Times(testing::AnyNumber());
    tab_groups::TabGroupSyncServiceInitializedObserver observer(
        tab_groups::TabGroupSyncServiceFactory::GetForProfile(profile1()));
    observer.Wait();
    ASSERT_TRUE(content::WaitForLoadStop(
        browser1()->tab_strip_model()->GetActiveWebContents()));
    BrowserUiController::From(browser1())->ProcessPendingUIUpdates();
    page_.receiver_.FlushForTesting();
  }
  void TearDownOnMainThread() override {
    handler_.reset();
    webui_controller_.reset();
    web_contents_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  content::TestWebUI* web_ui() { return &web_ui_; }
  Profile* profile1() { return browser()->GetProfile(); }
  BrowserWindowInterface* browser1() { return browser(); }
  TestTabSearchPageHandler* handler() { return handler_.get(); }
  void AddTabWithTitle(BrowserWindowInterface* browser,
                       const GURL& url,
                       const std::string& title) {
    chrome::AddTabAt(browser, url, 0, true);
    content::WebContents* web_contents =
        browser->GetTabStripModel()->GetActiveWebContents();
    content::WaitForLoadStop(web_contents);
    content::TitleWatcher title_watcher(web_contents, base::UTF8ToUTF16(title));
    ASSERT_TRUE(content::ExecJs(
        web_contents,
        base::StringPrintf("document.title = '%s';", title.c_str())));
    ASSERT_EQ(base::UTF8ToUTF16(title), title_watcher.WaitAndGetTitle());
    BrowserUiController::From(browser)->ProcessPendingUIUpdates();
    page_.receiver_.FlushForTesting();
  }

 protected:
  testing::StrictMock<MockPage> page_;
  std::unique_ptr<content::WebContents> web_contents_;
  GURL tab_url1_, tab_url2_;
  content::TestWebUI web_ui_;
  base::test::ScopedFeatureList feature_list_, webui_omnibox_feature_list_;
  std::unique_ptr<TestTabSearchPageHandler> handler_;
  std::unique_ptr<TabSearchUI> webui_controller_;
};
INSTANTIATE_TEST_SUITE_P(All, YeeTabSearchPageHandlerTest, testing::Bool());

IN_PROC_BROWSER_TEST_P(YeeTabSearchPageHandlerTest, RemoveSplit_NTP) {
  EXPECT_CALL(page_, HostWindowChanged()).Times(testing::AnyNumber());
  EXPECT_CALL(page_, TabsChanged(_)).Times(testing::AnyNumber());
  EXPECT_CALL(page_, TabUpdated(_)).Times(testing::AnyNumber());
  EXPECT_CALL(page_, TabsRemoved(_)).Times(testing::AnyNumber());

  AddTabWithTitle(browser1(), tab_url1_, kTabName1);

  webui::SetBrowserWindowInterface(web_contents_.get(), nullptr);
  browser1()->tab_strip_model()->AppendWebContents(std::move(web_contents_),
                                                   true);

  TabStripModel* tab_strip_model = browser1()->tab_strip_model();
  const split_tabs::SplitTabId split_id = tab_strip_model->AddToNewSplit(
      {0}, split_tabs::SplitTabVisualData(),
      split_tabs::SplitTabCreatedSource::kToolbarButton);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser1(), GURL(chrome::kChromeUISplitViewNewTabPageURL)));

  // 분할 새 탭이 아직 현재 문서인 동안 다음 이동의 표시 URL로 판정하면
  // 비동기 응답이 false가 되어 사용자가 입력한 이동을 새 탭으로 덮어쓴다.
  auto* const split_contents = tab_strip_model->GetActiveWebContents();
  const GURL split_url(chrome::kChromeUISplitViewNewTabPageURL);
  ASSERT_EQ(split_url, split_contents->GetLastCommittedURL());
  content::TestNavigationManager pending_navigation(split_contents, tab_url2_);
  split_contents->GetController().LoadURL(
      tab_url2_, content::Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_TRUE(pending_navigation.WaitForRequestStart());
  EXPECT_EQ(tab_url2_, split_contents->GetVisibleURL());
  EXPECT_EQ(split_url, split_contents->GetLastCommittedURL());
  base::test::TestFuture<bool> is_split;
  handler()->GetIsSplit(is_split.GetCallback());
  EXPECT_TRUE(is_split.Get());
  // 실패한 baseline도 멈춘 이동을 취소한 뒤 원래 분할 해제 검사를 계속한다.
  split_contents->Stop();
  EXPECT_TRUE(pending_navigation.WaitForNavigationFinished());
  EXPECT_FALSE(pending_navigation.was_committed());
  EXPECT_EQ(split_url, split_contents->GetLastCommittedURL());

  base::test::TestFuture<void> tab_unsplit;
  EXPECT_CALL(page_, TabUnsplit())
      .WillOnce(base::test::RunOnceClosure(tab_unsplit.GetCallback()));
  tab_strip_model->RemoveSplit(split_id);
  ASSERT_TRUE(tab_unsplit.Wait());
}

}  // namespace
