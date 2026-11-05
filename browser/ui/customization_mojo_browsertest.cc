// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/customization.h"

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_command_line.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/yee/webui/customization/tab_layout.mojom.h"
#include "chrome/browser/ui/webui/side_panel/customize_chrome/customize_chrome_ui.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/browser/back_forward_cache.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller_interface_binder.h"
#include "content/public/test/back_forward_cache_util.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/web_contents_tester.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "url/url_constants.h"

namespace {
class CustomizationMojoBrowserTest : public InProcessBrowserTest,
                                     public yee::mojom::TabLayoutPage {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL(chrome::kChromeUICustomizeChromeSidePanelURL)));
    contents_ = browser()->GetTabStripModel()->GetActiveWebContents();
    Connect();
  }
  void TearDownOnMainThread() override {
    handler_.reset();
    page_.reset();
    contents_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }
  void Connect() {
    handler_.reset();
    page_.reset();
    updates_ = 0;
    ASSERT_TRUE(contents_->GetWebUI());
    auto* ui =
        contents_->GetWebUI()->GetController()->GetAs<CustomizeChromeUI>();
    ASSERT_TRUE(ui);
    ui->BindInterface(contents_->GetPrimaryMainFrame(),
                      handler_.BindNewPipeAndPassReceiver());
    Observe();
  }
  void Observe() {
    handler_->Observe(page_.BindNewPipeAndPassRemote());
    Flush();
    ASSERT_GT(updates_, 0);
  }
  void Flush() {
    handler_.FlushForTesting();
    page_.FlushForTesting();
  }
  void Set(bool vertical) {
    handler_->SetTabLayout(vertical);
    Flush();
  }
  void SetTabLayout(bool vertical, bool enabled) override {
    vertical_ = vertical;
    enabled_ = enabled;
    ++updates_;
  }
  tabs::VerticalTabStripStateController* state() {
    return tabs::VerticalTabStripStateController::From(browser());
  }
  raw_ptr<content::WebContents> contents_ = nullptr;
  mojo::Remote<yee::mojom::TabLayoutHandler> handler_;
  mojo::Receiver<yee::mojom::TabLayoutPage> page_{this};
  bool vertical_ = false;
  bool enabled_ = false;
  int updates_ = 0;
};

IN_PROC_BROWSER_TEST_F(CustomizationMojoBrowserTest,
                       RoundTripsAndTracksExternalPref) {
  ASSERT_TRUE(state());
  EXPECT_EQ(state()->ShouldDisplayVerticalTabs(), vertical_);
  EXPECT_TRUE(enabled_);
  Set(false);
  EXPECT_FALSE(state()->ShouldDisplayVerticalTabs());
  EXPECT_FALSE(vertical_);
  browser()->GetProfile()->GetPrefs()->SetBoolean(prefs::kVerticalTabsEnabled,
                                                  true);
  Flush();
  EXPECT_TRUE(state()->ShouldDisplayVerticalTabs());
  EXPECT_TRUE(vertical_);
  Set(false);
  EXPECT_FALSE(state()->ShouldDisplayVerticalTabs());
  EXPECT_FALSE(vertical_);
}

IN_PROC_BROWSER_TEST_F(CustomizationMojoBrowserTest,
                       ManagedWritesRemainDisabled) {
  ASSERT_TRUE(state());
  const bool initial = state()->ShouldDisplayVerticalTabs();
  handler_.reset();
  page_.reset();
  updates_ = 0;
  TestingProfile managed;
  managed.GetTestingPrefService()->SetManagedPref(prefs::kVerticalTabsEnabled,
                                                  base::Value(initial));
  auto contents =
      content::WebContentsTester::CreateTestWebContents(&managed, nullptr);
  webui::SetBrowserWindowInterface(contents.get(), browser());
  // binder의 controller 권한 검사는 별도 사례에서 검사한다. 여기서는 managed
  // profile을 가진 문서에 같은 서비스 구현을 연결해 쓰기 거부를 확인한다.
  yee::BindCustomizationTabLayout(*contents->GetPrimaryMainFrame(),
                                  handler_.BindNewPipeAndPassReceiver());
  Observe();
  Set(!initial);
  EXPECT_FALSE(enabled_);
  EXPECT_EQ(initial, vertical_);
  EXPECT_EQ(initial, state()->ShouldDisplayVerticalTabs());
  handler_.reset();
  page_.reset();
}

IN_PROC_BROWSER_TEST_F(CustomizationMojoBrowserTest,
                       OnlyCustomizationControllerBindsCapability) {
  auto contents = content::WebContentsTester::CreateTestWebContents(
      browser()->GetProfile(), nullptr);
  mojo::Remote<yee::mojom::TabLayoutHandler> rejected;
  auto receiver = rejected.BindNewPipeAndPassReceiver();
  EXPECT_FALSE((content::internal::SafeDownCastAndBindInterface<
                yee::mojom::TabLayoutHandler, CustomizeChromeUI>(
      contents->GetPrimaryMainFrame(), receiver)));
  EXPECT_TRUE(receiver.is_valid());
  EXPECT_TRUE(handler_.is_connected());
}

IN_PROC_BROWSER_TEST_F(CustomizationMojoBrowserTest,
                       DisconnectAndNavigationDestroyBindings) {
  // handler pipe, 상태 observer pipe, 문서 수명은 각각 서비스를 종료한다.
  base::test::TestFuture<void> observer_disconnected;
  page_.set_disconnect_handler(observer_disconnected.GetCallback());
  handler_.reset();
  ASSERT_TRUE(observer_disconnected.Wait());
  Connect();
  base::test::TestFuture<void> handler_disconnected;
  handler_.set_disconnect_handler(handler_disconnected.GetCallback());
  page_.reset();
  ASSERT_TRUE(handler_disconnected.Wait());
  Connect();
  base::test::TestFuture<void> document_disconnected;
  handler_.set_disconnect_handler(document_disconnected.GetCallback());
  content::DisableBackForwardCacheForTesting(
      contents_, content::BackForwardCache::TEST_REQUIRES_NO_CACHING);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL)));
  ASSERT_TRUE(document_disconnected.Wait());
  EXPECT_FALSE(handler_.is_connected());
  const int updates = updates_;
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kVerticalTabsEnabled, !state()->ShouldDisplayVerticalTabs());
  EXPECT_EQ(updates, updates_);
}

IN_PROC_BROWSER_TEST_F(CustomizationMojoBrowserTest,
                       BrowserAndShellAvailabilityGateWrites) {
  ASSERT_TRUE(state());
  const bool initial = state()->ShouldDisplayVerticalTabs();
  handler_.reset();
  page_.reset();
  updates_ = 0;
  // 일반 탭의 TabInterface 추적과 분리해 direct browser context를 검사한다.
  auto contents = content::WebContentsTester::CreateTestWebContents(
      browser()->GetProfile(), nullptr);
  webui::SetBrowserWindowInterface(contents.get(), browser());
  yee::BindCustomizationTabLayout(*contents->GetPrimaryMainFrame(),
                                  handler_.BindNewPipeAndPassReceiver());
  Observe();
  webui::SetBrowserWindowInterface(contents.get(), nullptr);
  Flush();
  EXPECT_FALSE(enabled_);
  Set(!initial);
  EXPECT_EQ(initial, state()->ShouldDisplayVerticalTabs());
  webui::SetBrowserWindowInterface(contents.get(), browser());
  Flush();
  EXPECT_TRUE(enabled_);
  base::test::ScopedCommandLine command;
  command.GetProcessCommandLine()->AppendSwitch("disable-yee-shell-scaffold");
  Set(!initial);
  EXPECT_EQ(initial, state()->ShouldDisplayVerticalTabs());
  EXPECT_FALSE(enabled_);
  handler_.reset();
  page_.reset();
}
}  // namespace
