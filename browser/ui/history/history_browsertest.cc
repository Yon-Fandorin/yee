// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_branding/internal_urls.h"

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/history/history_ui.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/common/pref_names.h"
#include "components/history/core/test/history_service_test_util.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"

namespace yee {
namespace {
class HistoryBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    service_ = HistoryServiceFactory::GetForProfile(
        browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
    ASSERT_TRUE(service_);
    visit_time_ =
        base::Time::Now().LocalMidnight() - base::Days(1) + base::Hours(12);
    AddVisit("alpha", "Alpha review", base::Minutes(5));
    AddVisit("alpha", "Alpha review", base::Minutes(4));
    AddVisit("beta", "Beta review", base::Minutes(1));
    history::BlockUntilHistoryProcessesPendingRequests(service_);
  }

  void AddVisit(const std::string& path,
                const std::string& title,
                base::TimeDelta age) {
    const GURL url("https://history.example.test/" + path);
    service_->AddPage(url, visit_time_ - age, history::SOURCE_BROWSED);
    service_->SetPageTitle(url, base::UTF8ToUTF16(title));
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  bool WaitForRows(int count) {
    return content::EvalJs(Contents(), content::JsReplace(R"JS(
      (async () => {
        for (let attempt = 0; attempt < 250; ++attempt) {
          const list = document.querySelector('[data-history-list]');
          if (list && list.closest('section').getAttribute('aria-busy') === 'false' &&
              list.querySelectorAll('[data-history-id]').length === $1) return true;
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        return false;
      })()
    )JS",
                                                          count))
        .ExtractBool();
  }

  void Search(const std::string& query) {
    ASSERT_TRUE(content::ExecJs(Contents(), content::JsReplace(R"JS(
      (() => {
        const input = document.querySelector('input[type=search]');
        input.value = $1;
        input.dispatchEvent(new Event('input', {bubbles: true}));
      })();
    )JS",
                                                               query)));
  }

  raw_ptr<history::HistoryService> service_ = nullptr;
  base::Time visit_time_;
};

IN_PROC_BROWSER_TEST_F(HistoryBrowserTest,
                       NativeSearchDeletionAndOriginalPage) {
  const GURL branded(std::string(branding::InternalURLScheme()) +
                     "://history/");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branded));
  EXPECT_EQ(branding::HistoryURL(), Contents()->GetLastCommittedURL());
  EXPECT_TRUE(Contents()->GetWebUI()->GetController()->GetAs<::HistoryUI>());
  ASSERT_TRUE(WaitForRows(2));
  EXPECT_EQ(true, content::EvalJs(Contents(), R"JS(
    (async () => {
      for (let attempt = 0; attempt < 250; ++attempt) {
        const icons = [...document.querySelectorAll('.site-icon img')];
        if (icons.length === 2 && icons.every(icon => icon.complete && icon.naturalWidth > 0)) return true;
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      return false;
    })()
  )JS"));
  Search("Alpha review");
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ("Alpha review",
            content::EvalJs(
                Contents(),
                "document.querySelector('.history-row .title').textContent"));
  EXPECT_EQ("Alpha review",
            content::EvalJs(Contents(),
                            "new URL(location.href).searchParams.get('q')"));
  Search("missing-result");
  ASSERT_TRUE(WaitForRows(0));
  Search("host:history.example.test");
  ASSERT_TRUE(WaitForRows(2));
  Search("Alpha review");
  ASSERT_TRUE(WaitForRows(1));
  ASSERT_TRUE(content::ExecJs(Contents(),
                              "document.querySelector('.history-row "
                              "> button:not([role=checkbox])').click()"));
  EXPECT_EQ(true,
            content::EvalJs(Contents(),
                            "!!document.querySelector('[role=alertdialog]')"));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('[data-slot=alert-dialog-cancel]').click()"));
  ASSERT_TRUE(WaitForRows(1));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('.history-row [data-slot=checkbox]').click()"));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('.selection [data-slot=button]').click()"));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('[data-slot=alert-dialog-action]').click()"));
  ASSERT_TRUE(WaitForRows(0));
  Search("");
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ("Beta review",
            content::EvalJs(
                Contents(),
                "document.querySelector('.history-row .title').textContent"));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://history/")));
  EXPECT_EQ(
      u"chrome://history",
      browser()->GetFeatures().location_bar_model()->GetFormattedFullURL());
  EXPECT_EQ(true, content::EvalJs(Contents(),
                                  "!!document.querySelector('history-app')"));
  EXPECT_EQ(1, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {PageHandler} = await import('chrome://resources/cr_components/history/history.mojom-webui.js');
      const handler = PageHandler.getRemote();
      const {results} = await handler.queryHistory('', 150, null, true, true);
      handler.$.close();
      return results.value.length;
    })()
  )JS"));
  chrome::ShowHistory(browser());
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_EQ(branding::HistoryURL(), Contents()->GetLastCommittedURL());
  ASSERT_TRUE(WaitForRows(1));
  chrome::ShowHistorySubPage(browser(), "syncedTabs");
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_EQ(GURL("chrome://history/syncedTabs"),
            Contents()->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(HistoryBrowserTest, PreservesDeletionPolicy) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kAllowDeletingBrowserHistory, false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branding::HistoryURL()));
  ASSERT_TRUE(WaitForRows(2));
  EXPECT_EQ(false,
            content::EvalJs(Contents(),
                            "!!document.querySelector('.history-row button')"));
  EXPECT_EQ(2, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {PageHandler} = await import('chrome://resources/cr_components/history/history.mojom-webui.js');
      const handler = PageHandler.getRemote();
      const before = await handler.queryHistory('', 150, null, true, true);
      const visits = before.results.value.flatMap(item => Object.entries(item.allTimestamps).map(([url, timestamps]) => ({url, timestamps})));
      await handler.removeVisits(visits);
      const after = await handler.queryHistory('', 150, null, true, true);
      handler.$.close();
      return after.results.value.length;
    })()
  )JS"));
}

IN_PROC_BROWSER_TEST_F(HistoryBrowserTest, LoadsContinuationAndLatestSearch) {
  for (int i = 0; i < 155; ++i) {
    AddVisit("page-" + std::to_string(i), "Extra visit " + std::to_string(i),
             base::Minutes(10 + i));
  }
  history::BlockUntilHistoryProcessesPendingRequests(service_);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branding::HistoryURL()));
  ASSERT_TRUE(WaitForRows(150));
  ASSERT_TRUE(content::ExecJs(
      Contents(), "document.querySelector('.load-more button').click()"));
  ASSERT_TRUE(WaitForRows(157));
  Search("missing-result");
  Search("Beta review");
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ("Beta review",
            content::EvalJs(
                Contents(),
                "document.querySelector('.history-row .title').textContent"));
}
}  // namespace
}  // namespace yee
