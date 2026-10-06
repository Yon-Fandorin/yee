// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/yee_branding/internal_urls.h"

#include <optional>
#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/startup/url_util.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/omnibox/browser/omnibox_text_util.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::branding {
namespace {

class InternalURLsBrowserTest : public InProcessBrowserTest {
 protected:
  GURL Branded(const char* host) {
    return GURL(std::string(InternalURLScheme()) + "://" + host + "/");
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  std::u16string Address() {
    return browser()->GetFeatures().location_bar_model()->GetFormattedFullURL();
  }
};

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       InputNavigationAndNativeAddress) {
  const GURL url = Branded("version");
  EXPECT_TRUE(ProfileIOData::IsHandledURL(url));
  ChromeAutocompleteSchemeClassifier classifier(browser()->GetProfile());
  EXPECT_EQ(metrics::OmniboxInputType::URL,
            classifier.GetInputTypeForScheme(InternalURLScheme()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  EXPECT_EQ(GURL("chrome://version/"), Contents()->GetLastCommittedURL());
  EXPECT_EQ(GURL("chrome://version/"), Contents()->GetVisibleURL());
  EXPECT_EQ("chrome://version", content::EvalJs(Contents(), "location.origin"));
  EXPECT_EQ(base::UTF8ToUTF16(std::string(InternalURLScheme()) + "://version"),
            Address());
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       ExternalLaunchUsesCanonicalPolicy) {
  for (const char* suffix :
       {"version/", "settings/", "settings/content-blocking",
        "chromium-settings/resetProfileSettings"}) {
    const GURL branded(std::string(InternalURLScheme()) + "://" + suffix);
    const GURL canonical = CanonicalInternalURL(branded);
    EXPECT_EQ(startup::ValidateLaunchUrlWebUnsafe(canonical),
              startup::ValidateLaunchUrlWebUnsafe(branded));
    EXPECT_FALSE(startup::ValidateLaunchUrlWebSafe(branded));
  }
  EXPECT_FALSE(startup::ValidateLaunchUrlWebUnsafe(Branded("version")));
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest, NativeCopyBookmarkAndSession) {
  const GURL url = Branded("version");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  std::u16string text = Address();
  GURL copied_url;
  bool write_url = false;
  omnibox::AdjustTextForCopy(0, &text, false, false, std::nullopt,
                             Contents()->GetVisibleURL(), nullptr,
                             metrics::OmniboxEventProto::OTHER, GURL(),
                             &copied_url, &write_url);
  EXPECT_TRUE(write_url);
  EXPECT_EQ(url, copied_url);
  EXPECT_EQ(base::UTF8ToUTF16(url.spec()), text);

  // The about rewrite can retain a branded virtual URL. Persist the canonical
  // page even for that path, so a later brand build can restore it.
  content::NavigationEntry* entry =
      Contents()->GetController().GetLastCommittedEntry();
  entry->SetVirtualURL(url);
  entry->SetOriginalRequestURL(url);
  EXPECT_EQ(GURL("chrome://version/"), chrome::GetURLToBookmark(Contents()));
  const auto saved =
      sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(0,
                                                                        entry);
  EXPECT_EQ(GURL("chrome://version/"), saved.virtual_url());
  EXPECT_EQ(GURL("chrome://version/"), saved.original_request_url());
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       ReloadAndHistoryKeepBrandDisplay) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Branded("version")));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://settings/")));
  EXPECT_EQ(base::UTF8ToUTF16(std::string(InternalURLScheme()) +
                              "://chromium-settings"),
            Address());
  content::TestNavigationObserver back(Contents());
  Contents()->GetController().GoBack();
  back.Wait();
  EXPECT_EQ(GURL("chrome://version/"), Contents()->GetLastCommittedURL());
  EXPECT_EQ(base::UTF8ToUTF16(std::string(InternalURLScheme()) + "://version"),
            Address());
  content::TestNavigationObserver reload(Contents());
  Contents()->GetController().Reload(content::ReloadType::NORMAL, false);
  reload.Wait();
  EXPECT_EQ(GURL("chrome://version/"), Contents()->GetLastCommittedURL());
  EXPECT_EQ(base::UTF8ToUTF16(std::string(InternalURLScheme()) + "://version"),
            Address());
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       ProductAndNativeSettingsRoutes) {
  const GURL branded(std::string(InternalURLScheme()) +
                     "://settings/content-blocking");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), branded));
  EXPECT_EQ(GURL("chrome://yee-settings/content-blocking"),
            Contents()->GetLastCommittedURL());
  EXPECT_EQ(base::UTF8ToUTF16(branded.spec()), Address());
  EXPECT_EQ(true, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {loadTimeData} = await import('chrome://resources/js/load_time_data.js');
      for (let attempt = 0; attempt < 250; ++attempt) {
        const title = document.getElementById('blocking-title');
        if (title) {
          return title.textContent === loadTimeData.getString('blockingTitle') &&
              document.documentElement.lang === loadTimeData.getString('language') &&
              document.documentElement.dir === loadTimeData.getString('textdirection');
        }
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      return false;
    })()
  )JS"));
  EXPECT_EQ(false, content::EvalJs(
                       Contents(),
                       "document.getElementById('content-blocking').hidden"));
  const GURL native(std::string(InternalURLScheme()) +
                    "://chromium-settings/content");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), native));
  EXPECT_EQ(GURL("chrome://settings/content"),
            Contents()->GetLastCommittedURL());
  EXPECT_EQ(base::UTF8ToUTF16(native.spec()), Address());
  EXPECT_EQ(true, content::EvalJs(Contents(),
                                  "!!document.querySelector('settings-ui')"));
  chrome::ShowSettings(browser());
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  EXPECT_EQ(SettingsURL(), Contents()->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       SettingsEditsExistingSitePolicy) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), SettingsURL()));
  EXPECT_EQ("example.test", content::EvalJs(Contents(), R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      const state = await sendWithPromise('setContentBlockingException',
                                          'https://example.test/path', false);
      return state.exceptions[0];
    })()
  )JS"));
  auto* service =
      content_blocking::ContentBlockingServiceFactory::GetForProfile(
          browser()->GetProfile());
  ASSERT_TRUE(service);
  EXPECT_FALSE(service->EnabledForSite(GURL("https://example.test/")));
  EXPECT_TRUE(service->EnabledForSite(GURL("https://sub.example.test/")));
  EXPECT_EQ("invalid-site", content::EvalJs(Contents(), R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      try {await sendWithPromise('setContentBlockingException', 'file:///tmp/a', false);}
      catch (error) {return error;}
      return 'unexpected-success';
    })()
  )JS"));
  service->SetEnabledForSite(GURL("https://example.test/"), true);
  EXPECT_EQ(0, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      return (await sendWithPromise('getContentBlockingState')).exceptions.length;
    })()
  )JS"));
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       PrivateSettingsCannotUpdateLists) {
  Browser* private_browser = CreateIncognitoBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser, SettingsURL()));
  auto* contents = private_browser->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      const state = await sendWithPromise('getContentBlockingState');
      return state.privateProfile && !state.updatesAvailable;
    })()
  )JS"));
  EXPECT_EQ("updates-unavailable", content::EvalJs(contents, R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      try {await sendWithPromise('checkContentBlockingLists');}
      catch (error) {return error;}
      return 'unexpected-success';
    })()
  )JS"));
}

}  // namespace
}  // namespace yee::branding
