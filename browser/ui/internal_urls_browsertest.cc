// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/yee_branding/internal_urls.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
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
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::branding {
namespace {

class InternalURLsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*.example.test", "127.0.0.1");
  }

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

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       SettingsDomainImportIsPreviewedAndMerged) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), SettingsURL()));
  EXPECT_EQ(true, content::EvalJs(Contents(), R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      await sendWithPromise('addBlockedDomain', 'ADS.example.test.', false);
      try {
        await sendWithPromise('addBlockedDomain', 'ads.example.test', true);
        return false;
      } catch (error) {if (error !== 'duplicate-domain') return false;}
      await sendWithPromise('setBlockedDomain', 'ads.example.test', true);
      await sendWithPromise('setBlockedDomain', 'ads.example.test', false);
      try {
        await sendWithPromise('setBlockedDomain', 'missing.example.test', true);
        return false;
      } catch (error) {if (error !== 'domain-missing') return false;}
      const csv = 'domain,include_subdomains\nnew.example.test,true\nADS.example.test,true\n' +
                  'new.example.test,false\nhttps://invalid.example,true\n';
      const preview = await sendWithPromise('previewBlockedDomains', 'csv', csv);
      const before = await sendWithPromise('getContentBlockingState');
      if (preview.additions !== 1 || preview.duplicates !== 2 || preview.invalid !== 1 ||
          before.blockedDomains.length !== 1) return false;
      if (await sendWithPromise('importBlockedDomains', 'csv', csv) !== 1) return false;
      const after = await sendWithPromise('getContentBlockingState');
      if (after.blockedDomains.length !== 2 || after.blockedDomains[0].includeSubdomains) return false;
      try {await sendWithPromise('setBlockedDomain', 'https://wrong.example', true);}
      catch (error) {if (error !== 'invalid-domain') return false;}
      await sendWithPromise('removeBlockedDomain', 'new.example.test');
      return (await sendWithPromise('getContentBlockingState')).blockedDomains.length === 1;
    })()
  )JS"));
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       PrivateDomainChangesStayInSession) {
  auto* regular =
      content_blocking::ContentBlockingServiceFactory::GetForProfile(
          browser()->GetProfile());
  ASSERT_TRUE(regular->SetBlockedDomain("regular.example.test", false));
  Browser* private_browser = CreateIncognitoBrowser(browser()->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser, SettingsURL()));
  auto* contents = private_browser->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const {sendWithPromise} = await import('chrome://resources/js/cr.js');
      await sendWithPromise('addBlockedDomain', 'private.example.test', true);
      await sendWithPromise('setBlockedDomain', 'regular.example.test', true);
      const state = await sendWithPromise('getContentBlockingState');
      return state.privateProfile && state.blockedDomains.length === 2 &&
             state.blockedDomains.every(rule => rule.includeSubdomains);
    })()
  )JS"));
  EXPECT_EQ((std::vector<content_blocking::BlockedDomain>{
                {"regular.example.test", false}}),
            regular->BlockedDomains());
}

IN_PROC_BROWSER_TEST_F(InternalURLsBrowserTest,
                       DomainsBlockLiveRequestsAndNavigations) {
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/domain-resource") {
          return nullptr;
        }
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content("domain-response");
        response->set_content_type("text/plain");
        response->AddCustomHeader("Access-Control-Allow-Origin", "*");
        response->AddCustomHeader("Cache-Control", "no-store");
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL publisher =
      embedded_test_server()->GetURL("publisher.example.test", "/title1.html");
  const GURL blocked =
      embedded_test_server()->GetURL("ads.example.test", "/domain-resource");
  const GURL child = embedded_test_server()->GetURL("child.ads.example.test",
                                                    "/domain-resource");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), publisher));
  auto fetch = [&](const GURL& url) {
    return content::EvalJs(
        Contents(), content::JsReplace("fetch($1, {cache: 'no-store'}).then(r "
                                       "=> r.text()).catch(() => 'blocked')",
                                       url.spec()));
  };
  auto* service =
      content_blocking::ContentBlockingServiceFactory::GetForProfile(
          browser()->GetProfile());
  EXPECT_EQ("domain-response", fetch(blocked));
  ASSERT_TRUE(service->SetBlockedDomain("ads.example.test", false));
  EXPECT_EQ("blocked", fetch(blocked));
  EXPECT_EQ("domain-response", fetch(child));
  ASSERT_TRUE(service->SetBlockedDomain("ads.example.test", true));
  EXPECT_EQ("blocked", fetch(child));
  service->SetEnabledForSite(publisher, false);
  EXPECT_EQ("domain-response", fetch(blocked));
  service->SetEnabledForSite(publisher, true);
  EXPECT_EQ("blocked", fetch(blocked));
  content::TestNavigationObserver blocked_navigation(Contents());
  EXPECT_FALSE(content::NavigateToURL(Contents(), blocked));
  blocked_navigation.Wait();
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT,
            blocked_navigation.last_net_error_code());
  service->RemoveBlockedDomain("ads.example.test");
  EXPECT_TRUE(content::NavigateToURL(Contents(), blocked));
  EXPECT_EQ("domain-response",
            content::EvalJs(Contents(), "document.body.textContent"));
}

}  // namespace
}  // namespace yee::branding
