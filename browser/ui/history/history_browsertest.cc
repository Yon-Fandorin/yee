// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "components/yee_branding/internal_urls.h"

#include <string>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/yee/internal_page_test_support.h"
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
#include "content/public/test/test_devtools_protocol_client.h"
#include "ui/gfx/color_utils.h"

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
    return content::EvalJs(Contents(), test::WaitForInternalPageScript(
                                           content::JsReplace(R"JS(
          (() => {
            const list = document.querySelector('[data-history-list]');
            return !!list &&
                list.closest('section').getAttribute('aria-busy') === 'false' &&
                list.querySelectorAll('[data-history-id]').length === $1;
          })()
        )JS",
                                                              count)))
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

  void ExpectDeleteButtonContrast() {
    content::TestDevToolsProtocolClient devtools;
    devtools.AttachToWebContents(Contents());
    base::ScopedClosureRunner detach(base::BindOnce(
        &content::TestDevToolsProtocolClient::DetachProtocolClient,
        base::Unretained(&devtools)));
    ASSERT_TRUE(devtools.SendCommandSync("DOM.enable"));
    ASSERT_TRUE(devtools.SendCommandSync("CSS.enable"));
    const auto* document = devtools.SendCommandSync("DOM.getDocument");
    ASSERT_TRUE(document);
    const auto* button = devtools.SendCommandSync(
        "DOM.querySelector",
        base::DictValue()
            .Set("nodeId", document->FindIntByDottedPath("root.nodeId").value())
            .Set("selector", "[data-slot=alert-dialog-action]"));
    ASSERT_TRUE(button);
    const int button_id = button->FindInt("nodeId").value_or(0);
    ASSERT_GT(button_id, 0);

    // 실제 :hover 스타일을 적용하고, 색 전환이 끝난 뒤 최종 대비를 읽는다.
    for (const char* scheme : {"light", "dark"}) {
      SCOPED_TRACE(scheme);
      ASSERT_TRUE(devtools.SendCommandSync(
          "Emulation.setEmulatedMedia",
          base::DictValue().Set("features", base::ListValue().Append(
              base::DictValue().Set("name", "prefers-color-scheme")
                  .Set("value", scheme)))));
      ASSERT_TRUE(devtools.SendCommandSync(
          "CSS.forcePseudoState",
          base::DictValue().Set("nodeId", button_id).Set(
              "forcedPseudoClasses", base::ListValue().Append("hover"))));
      const auto colors = content::EvalJs(Contents(), R"JS(
        (async () => {
          const button = document.querySelector('[data-slot=alert-dialog-action]');
          const dialog = button.closest('[role=alertdialog]');
          const style = getComputedStyle(button);
          const dialogStyle = getComputedStyle(dialog);
          // 계산된 스타일을 읽어 미디어·hover 변경이 만든 전환을 시작한다.
          style.color;
          dialogStyle.backgroundColor;
          await Promise.allSettled([button, dialog].flatMap(node =>
              node.getAnimations().map(animation => animation.finished)));
          const canvas = document.createElement('canvas').getContext('2d',
              {willReadFrequently: true});
          const composite = (color) => {
            canvas.fillStyle = color;
            canvas.fillRect(0, 0, 1, 1);
            const [r, g, b] = canvas.getImageData(0, 0, 1, 1).data;
            return (r << 16) | (g << 8) | b;
          };
          composite(dialogStyle.backgroundColor);
          return [composite(style.backgroundColor), composite(style.color)];
        })()
      )JS");
      ASSERT_TRUE(colors.is_list()) << colors;
      const auto& pixels = colors.ExtractList();
      ASSERT_EQ(pixels.size(), 2u);
      const SkColor background = SkColorSetA(pixels[0].GetInt(), SK_AlphaOPAQUE);
      const SkColor foreground = SkColorSetA(pixels[1].GetInt(), SK_AlphaOPAQUE);
      EXPECT_GE(color_utils::GetContrastRatio(foreground, background),
                color_utils::kMinimumReadableContrastRatio);
    }
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
  EXPECT_EQ(true,
            content::EvalJs(Contents(), test::WaitForInternalPageScript(R"JS(
    (() => {
      const icons = [...document.querySelectorAll('.site-icon img')];
      return icons.length === 2 &&
          icons.every(icon => icon.complete && icon.naturalWidth > 0);
    })()
  )JS")));
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
  ASSERT_NO_FATAL_FAILURE(ExpectDeleteButtonContrast());
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
  EXPECT_EQ(u"chrome://history",
            LocationBarModel::From(browser())->GetFormattedFullURL());
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
