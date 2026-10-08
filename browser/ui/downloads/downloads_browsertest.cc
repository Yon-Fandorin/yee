// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_branding/internal_urls.h"

#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/downloads/downloads_ui.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/history/core/common/pref_names.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/download_test_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"

namespace yee {
namespace {
class DownloadsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest& request)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          if (request.relative_url != "/review.txt") {
            return nullptr;
          }
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_code(net::HTTP_OK);
          response->set_content_type("application/octet-stream");
          response->set_content("Download fixture\n");
          return response;
        }));
    ASSERT_TRUE(embedded_test_server()->Start());
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    path_ = directory_.GetPath().AppendASCII("review.txt");
    browser()->GetProfile()->GetPrefs()->SetFilePath(
        prefs::kDownloadDefaultDirectory, directory_.GetPath());
    auto* manager = browser()->GetProfile()->GetDownloadManager();
    content::DownloadTestObserverTerminal waiter(
        manager, 1, content::DownloadTestObserver::ON_DANGEROUS_DOWNLOAD_FAIL);
    auto parameters = std::make_unique<download::DownloadUrlParameters>(
        embedded_test_server()->GetURL("/review.txt"),
        TRAFFIC_ANNOTATION_FOR_TESTS);
    parameters->set_prompt(false);
    manager->DownloadUrl(std::move(parameters));
    waiter.WaitForFinished();
    ASSERT_EQ(1u,
              waiter.NumDownloadsSeenInState(download::DownloadItem::COMPLETE));
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    ASSERT_EQ(1u, items.size());
    ASSERT_TRUE(DownloadItemModel(items[0]).ShouldShowInUi());
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  bool WaitForRows(int count) {
    return content::EvalJs(Contents(), content::JsReplace(R"JS(
      (async () => {
        for (let attempt = 0; attempt < 250; ++attempt) {
          const list = document.querySelector('[data-download-list]');
          if (list && list.closest('section').getAttribute('aria-busy') === 'false' &&
              list.querySelectorAll('[data-download-id]').length === $1) return true;
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        return false;
      })()
    )JS",
                                                          count))
        .ExtractBool();
  }

  base::ScopedTempDir directory_;
  base::FilePath path_;
};

IN_PROC_BROWSER_TEST_F(DownloadsBrowserTest, NativeListSearchRemoveAndUndo) {
  const GURL branded(std::string(branding::InternalURLScheme()) +
                     "://downloads/");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branded));
  EXPECT_EQ(branding::DownloadsURL(), Contents()->GetLastCommittedURL());
  EXPECT_TRUE(Contents()->GetWebUI()->GetController()->GetAs<::DownloadsUI>());
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ(
      "review.txt",
      content::EvalJs(Contents(),
                      "document.querySelector('.file-name').textContent"));
  EXPECT_EQ(embedded_test_server()->GetURL("/review.txt").spec(),
            content::EvalJs(Contents(),
                            "document.querySelector('.metadata a').href"));
  EXPECT_EQ(true, content::EvalJs(Contents(), R"JS(
    (() => {
      const input = document.querySelector('input[type=search]');
      input.value = 'missing-result';
      input.dispatchEvent(new Event('input', {bubbles: true}));
      return true;
    })()
  )JS"));
  ASSERT_TRUE(WaitForRows(0));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    const input = document.querySelector('input[type=search]');
    input.value = '"review"';
    input.dispatchEvent(new Event('input', {bubbles: true}));
  )JS"));
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ(true, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      return [...document.querySelectorAll('header button')].find(
          button => button.textContent === loadTimeData.getString('downloadsClear')).disabled;
    })()
  )JS"));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      document.querySelector(`[aria-label="${loadTimeData.getString('downloadsRemove')}"]`).click();
    })()
  )JS"));
  ASSERT_TRUE(WaitForRows(0));
  ASSERT_TRUE(content::ExecJs(
      Contents(), "document.querySelector('.feedback button').click()"));
  ASSERT_TRUE(WaitForRows(1));
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_TRUE(base::PathExists(path_));
  }
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://downloads/")));
  EXPECT_EQ(
      u"chrome://downloads",
      browser()->GetFeatures().location_bar_model()->GetFormattedFullURL());
  EXPECT_EQ(true,
            content::EvalJs(Contents(),
                            "!!document.querySelector('downloads-manager')"));
  chrome::ShowDownloads(browser());
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_EQ(branding::DownloadsURL(), Contents()->GetLastCommittedURL());
  ASSERT_TRUE(WaitForRows(1));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      [...document.querySelectorAll('header button')].find(
          button => button.textContent === loadTimeData.getString('downloadsClear')).click();
    })()
  )JS"));
  ASSERT_TRUE(WaitForRows(0));
  ASSERT_TRUE(content::ExecJs(
      Contents(), "document.querySelector('.feedback button').click()"));
  ASSERT_TRUE(WaitForRows(1));
}

IN_PROC_BROWSER_TEST_F(DownloadsBrowserTest, PreservesHistoryDeletionPolicy) {
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kAllowDeletingBrowserHistory, false);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), branding::DownloadsURL()));
  ASSERT_TRUE(WaitForRows(1));
  EXPECT_EQ(false, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      return !!document.querySelector(`[aria-label="${loadTimeData.getString('downloadsRemove')}"]`);
    })()
  )JS"));
  // A compromised renderer still cannot bypass the native history policy.
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (async () => {
      const native = await import('/downloads.mojom-webui.js');
      const handler = new native.PageHandlerRemote();
      const router = new native.PageCallbackRouter();
      native.PageHandlerFactory.getRemote().createPageHandler(
          router.$.bindNewPipeAndPassRemote(), handler.$.bindNewPipeAndPassReceiver());
      handler.remove(document.querySelector('[data-download-id]').dataset.downloadId);
      handler.clearAll();
      await handler.isEligibleForEsbPromo();
      handler.$.close();
      router.$.close();
    })()
  )JS"));
  content::DownloadManager::DownloadVector items;
  browser()->GetProfile()->GetDownloadManager()->GetAllDownloads(&items);
  ASSERT_EQ(1u, items.size());
  EXPECT_TRUE(DownloadItemModel(items[0]).ShouldShowInUi());
}
}  // namespace
}  // namespace yee
