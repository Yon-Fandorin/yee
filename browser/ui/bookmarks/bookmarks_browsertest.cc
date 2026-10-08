// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/bookmarks/managed/managed_bookmark_service.h"
#include "components/bookmarks/test/bookmark_test_helpers.h"
#include "components/prefs/pref_service.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace yee {
namespace {
class BookmarksBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    bookmarks::test::WaitForBookmarkModelToLoad(Model());
    const auto* bar = Model()->bookmark_bar_node();
    bar_id_ = base::NumberToString(bar->id());
    const auto* folder = Model()->AddFolder(bar, 0, u"Review folder");
    folder_id_ = base::NumberToString(folder->id());
    const auto* bookmark =
        Model()->AddURL(bar, 1, u"Review bookmark",
                        embedded_test_server()->GetURL("/title1.html"));
    bookmark_id_ = base::NumberToString(bookmark->id());
  }

  bookmarks::BookmarkModel* Model() {
    return BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  bool WaitFor(const std::string& condition) {
    return content::EvalJs(Contents(), std::string(R"JS(
      (async () => {
        for (let attempt = 0; attempt < 250; ++attempt) {
          if (
    )JS") + condition + R"JS(
          ) return true;
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        return false;
      })()
    )JS")
        .ExtractBool();
  }

  bool WaitForRows(int count) {
    return WaitFor(
        "document.querySelector('[data-bookmark-list]') && "
        "document.querySelector('[data-bookmark-list]').closest('section')"
        ".getAttribute('aria-busy') === 'false' && "
        "document.querySelectorAll('[data-bookmark-id]').length === " +
        base::NumberToString(count));
  }

  content::EvalJsResult PageState() {
    return content::EvalJs(Contents(), R"JS(
      JSON.stringify({
        url: location.href,
        folder: document.querySelector('.breadcrumb')?.textContent,
        rows: [...document.querySelectorAll('[data-bookmark-id]')].map(row => row.dataset.bookmarkId),
        error: document.querySelector('.feedback')?.textContent,
        body: document.body.textContent
      })
    )JS");
  }

  bool ChooseAction(const std::string& id, const std::string& key) {
    if (!content::ExecJs(Contents(), content::JsReplace(R"JS(
      (() => {
        const id = $1;
        document.querySelector('[data-bookmark-id="' + CSS.escape(id) + '"] button[aria-haspopup=menu]').click();
      })();
    )JS",
                                                        id))) {
      return false;
    }
    if (!WaitFor(
            "!!document.querySelector('[data-slot=dropdown-menu-content]')")) {
      return false;
    }
    return content::EvalJs(Contents(), content::JsReplace(R"JS(
      (async () => {
        const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
        const item = [...document.querySelectorAll('[data-slot=dropdown-menu-item]')]
            .find(item => item.textContent.trim() === loadTimeData.getString($1));
        if (!item) return false;
        item.click();
        return true;
      })()
    )JS",
                                                          key))
        .ExtractBool();
  }

  std::string bar_id_;
  std::string folder_id_;
  std::string bookmark_id_;
};

IN_PROC_BROWSER_TEST_F(BookmarksBrowserTest,
                       NativeListSearchEditMoveDeleteUndo) {
  const GURL branded(std::string(branding::InternalURLScheme()) +
                     "://bookmarks/");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branded));
  EXPECT_EQ(branding::BookmarksURL(), Contents()->GetLastCommittedURL());
  ASSERT_TRUE(WaitForRows(2));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    const search = document.querySelector('input[type=search]');
    search.value = 'Review bookmark';
    search.dispatchEvent(new Event('input', {bubbles: true}));
  )JS"));
  ASSERT_TRUE(WaitForRows(1));
  ASSERT_TRUE(ChooseAction(bookmark_id_, "bookmarksEdit"));
  ASSERT_TRUE(WaitFor("!!document.querySelector('#bookmark-name')"));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    const name = document.querySelector('#bookmark-name');
    name.value = 'Review bookmark edited';
    name.dispatchEvent(new Event('input', {bubbles: true}));
    name.closest('form').requestSubmit();
  )JS"));
  ASSERT_TRUE(
      WaitFor("!document.querySelector('#bookmark-name') && "
              "document.querySelector('.item-title')?.textContent.trim() === "
              "'Review bookmark edited'"));
  ASSERT_TRUE(ChooseAction(bookmark_id_, "bookmarksMove"));
  ASSERT_TRUE(WaitFor("!!document.querySelector('#bookmark-destination')"));
  ASSERT_TRUE(content::ExecJs(Contents(), content::JsReplace(R"JS(
    const destination = document.querySelector('#bookmark-destination');
    destination.value = $1;
    destination.dispatchEvent(new Event('change', {bubbles: true}));
    destination.closest('form').requestSubmit();
  )JS",
                                                             folder_id_)));
  ASSERT_TRUE(WaitFor("!document.querySelector('#bookmark-destination')"));
  ASSERT_EQ(1u, Model()->bookmark_bar_node()->children()[0]->children().size());
  ASSERT_TRUE(ChooseAction(bookmark_id_, "bookmarksDelete"));
  ASSERT_TRUE(
      WaitFor("!!document.querySelector('[data-slot=alert-dialog-content]')"));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      [...document.querySelectorAll('[data-slot=alert-dialog-content] button')]
          .find(button => button.textContent.trim() === loadTimeData.getString('bookmarksDelete')).click();
    })()
  )JS"));
  ASSERT_TRUE(WaitForRows(0));
  ASSERT_TRUE(content::ExecJs(
      Contents(), "document.querySelector('.feedback button').click()"));
  ASSERT_TRUE(WaitForRows(1));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://bookmarks/")));
  EXPECT_EQ(true, content::EvalJs(Contents(),
                                  "!!document.querySelector('bookmarks-app')"));
}

IN_PROC_BROWSER_TEST_F(BookmarksBrowserTest, EditPolicyAppliesToRendererAPIs) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), branding::BookmarksURL()));
  ASSERT_TRUE(WaitForRows(2));
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kEditBookmarksEnabled, false);
  ASSERT_TRUE(WaitFor("!!document.querySelector('.policy')"));
  EXPECT_EQ(0, content::EvalJs(
                   Contents(),
                   "document.querySelectorAll('[data-slot=checkbox]').length"));
  EXPECT_EQ(true, content::EvalJs(
                      Contents(),
                      content::JsReplace(R"JS(
    (async () => {
      const operations = [
        () => chrome.bookmarks.create({parentId: $1, title: 'Forbidden'}),
        () => chrome.bookmarks.update($2, {title: 'Forbidden'}),
        () => chrome.bookmarks.move($2, {parentId: $3}),
        () => chrome.bookmarkManagerPrivate.removeTrees([$2])
      ];
      for (const operation of operations) {
        try { await operation(); return false; } catch {}
      }
      return true;
    })()
  )JS",
                                         bar_id_, bookmark_id_, folder_id_)));
  EXPECT_EQ(2u, Model()->bookmark_bar_node()->children().size());
  EXPECT_EQ(u"Review bookmark",
            Model()->bookmark_bar_node()->children()[1]->GetTitle());
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      bookmarks::prefs::kEditBookmarksEnabled, true);
  ASSERT_TRUE(WaitFor("!document.querySelector('.policy')"));
}

IN_PROC_BROWSER_TEST_F(BookmarksBrowserTest, OpensThroughNativeBookmarkAPI) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), branding::BookmarksURL()));
  ASSERT_TRUE(WaitForRows(2));
  ui_test_utils::TabAddedWaiter added(browser());
  ASSERT_TRUE(content::ExecJs(Contents(), content::JsReplace(R"JS(
    (() => {
      const id = $1;
      document.querySelector('[data-bookmark-id="' + CSS.escape(id) + '"] .item-title').click();
    })();
  )JS",
                                                             bookmark_id_)));
  added.Wait();
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_EQ(embedded_test_server()->GetURL("/title1.html"),
            Contents()->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(BookmarksBrowserTest, RestoresNativeFolderAddress) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(branding::BookmarksURL().spec() + "?id=" + folder_id_)));
  ASSERT_TRUE(WaitForRows(0));
  EXPECT_EQ("Review folder",
            content::EvalJs(Contents(),
                            "document.querySelector('.breadcrumb "
                            "[aria-current=page]').textContent.trim()"));
  ASSERT_TRUE(content::ExecJs(
      Contents(), "document.querySelector('.breadcrumb button').click()"));
  ASSERT_TRUE(WaitForRows(2));
  EXPECT_EQ(bar_id_,
            content::EvalJs(Contents(),
                            "new URL(location.href).searchParams.get('id')"));
  ASSERT_TRUE(content::ExecJs(Contents(), "history.back()"));
  ASSERT_TRUE(WaitForRows(0));
}

IN_PROC_BROWSER_TEST_F(BookmarksBrowserTest, ManagedNodesStayReadOnly) {
  base::ListValue list;
  base::DictValue item;
  item.Set("name", "Managed review bookmark");
  item.Set("url", "https://example.com/");
  list.Append(std::move(item));
  browser()->GetProfile()->GetPrefs()->Set(bookmarks::prefs::kManagedBookmarks,
                                           base::Value(std::move(list)));
  const auto* managed =
      ManagedBookmarkServiceFactory::GetForProfile(browser()->GetProfile())
          ->managed_node();
  ASSERT_EQ(1u, managed->children().size());
  const std::string id = base::NumberToString(managed->children()[0]->id());
  const GURL folder_url(branding::BookmarksURL().spec() +
                        "?id=" + base::NumberToString(managed->id()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), folder_url));
  ASSERT_TRUE(WaitForRows(1)) << PageState();
  EXPECT_EQ(0, content::EvalJs(
                   Contents(),
                   "document.querySelectorAll('[data-slot=checkbox]').length"));
  EXPECT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    (async () => {
      try { await chrome.bookmarks.update($1, {title: 'Forbidden'}); return false; } catch {}
      try { await chrome.bookmarkManagerPrivate.removeTrees([$1]); return false; } catch {}
      return true;
    })()
  )JS",
                                                                 id)));
  EXPECT_EQ(1u, managed->children().size());
}
}  // namespace
}  // namespace yee
