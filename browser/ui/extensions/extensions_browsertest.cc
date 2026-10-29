// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include <string>

#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/developer_private/developer_private_api.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_management_test_util.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/policy/policy_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/crx_file/crx_verifier.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/yee_branding/internal_urls.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/manifest.mojom.h"
#include "extensions/test/test_extension_dir.h"

namespace yee {
namespace {
class ExtensionsBrowserTest : public extensions::ExtensionBrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    ExtensionBrowserTest::SetUpInProcessBrowserTestFixture();
    policy_provider_.SetDefaultReturns(
        /*is_initialization_complete_return=*/true,
        /*is_first_policy_load_complete_return=*/true);
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
        &policy_provider_);
  }

  void SetUpOnMainThread() override {
    ExtensionBrowserTest::SetUpOnMainThread();
    // 브라우저 테스트에서는 이 keyed API가 기본적으로 null이다. 네이티브 이벤트
    // 라우터가 동작하도록 초기화하며, 실제 제품에서는 자동으로 생성된다.
    ASSERT_TRUE(extensions::DeveloperPrivateAPI::Get(browser()->GetProfile()));
    first_dir_.WriteManifest(
        base::DictValue()
            .Set("manifest_version", 3)
            .Set("name", "Yee review alpha")
            .Set("description", "Review extension with site permissions")
            .Set("version", "1.0")
            .Set("host_permissions", base::ListValue().Append("<all_urls>")));
    second_dir_.WriteManifest(R"({
      "manifest_version": 3,
      "name": "Yee review beta",
      "version": "2.0"
    })");
    const auto* first = LoadExtension(first_dir_.UnpackedPath());
    ASSERT_TRUE(first);
    first_id_ = first->id();
    const auto* second = LoadExtension(second_dir_.UnpackedPath());
    ASSERT_TRUE(second);
    second_id_ = second->id();
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  bool WaitFor(const std::string& condition) {
    const auto result = content::EvalJs(Contents(), std::string(R"JS(
      (async () => {
        for (let attempt = 0; attempt < 250; ++attempt) {
          if (
    )JS") + condition + R"JS(
          ) return true;
          await new Promise(resolve => setTimeout(resolve, 20));
        }
        return false;
      })()
    )JS");
    EXPECT_TRUE(result.is_bool()) << result;
    return result.is_bool() && result.ExtractBool();
  }

  bool NavigateToList() {
    return ui_test_utils::NavigateToURL(browser(), branding::ExtensionsURL()) &&
           WaitFor(
               "!!document.querySelector('[data-extension-list]') && "
               "!!document.querySelector('[data-extension-id=\"" +
               first_id_ +
               "\"]') && !!document.querySelector('[data-extension-id=\"" +
               second_id_ + "\"]')");
  }

  bool SetSearch(const std::string& query) {
    return content::ExecJs(Contents(), content::JsReplace(R"JS(
      (() => {
        const search = document.querySelector('input[type=search]');
        search.value = $1;
        search.dispatchEvent(new Event('input', {bubbles: true}));
      })();
    )JS",
                                                          query));
  }

  std::string Row(const std::string& id) {
    return "document.querySelector('[data-extension-id=\"" + id + "\"]')";
  }

  content::EvalJsResult PageState(const std::string& id) {
    return content::EvalJs(Contents(), content::JsReplace(R"JS(
      (async () => {
        let info;
        try {info = await chrome.developerPrivate.getExtensionInfo($1);}
        catch (error) {info = String(error);}
        let items;
        try {
          items = (await chrome.developerPrivate.getExtensionsInfo({
            includeDisabled: true, includeTerminated: true
          })).map(({id, name, state, location, userMayModify, mustRemainInstalled}) =>
              ({id, name, state, location, userMayModify, mustRemainInstalled}));
        } catch (error) {items = String(error);}
        return JSON.stringify({
          origin: location.origin,
          info,
          items,
          changes: window.extensionChanges,
          switches: [...document.querySelectorAll('[role=switch]')].map(toggle => ({
            label: toggle.getAttribute('aria-label'),
            checked: toggle.getAttribute('aria-checked'), disabled: toggle.disabled
          })),
          notice: document.querySelector('[data-extension-notice]')?.textContent
        });
      })()
    )JS",
                                                          id));
  }

  extensions::TestExtensionDir first_dir_;
  extensions::TestExtensionDir second_dir_;
  std::string first_id_;
  std::string second_id_;
  testing::NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;
};

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest,
                       NativeListSearchAndEnabledEvents) {
  ASSERT_TRUE(NavigateToList());
  EXPECT_EQ("chrome://yee-extensions",
            content::EvalJs(Contents(), "location.origin"));
  EXPECT_EQ(true, content::EvalJs(Contents(),
                                  content::JsReplace(R"JS(
    (async () => {
      const items = await chrome.developerPrivate.getExtensionsInfo({
        includeDisabled: true, includeTerminated: true});
      const first = items.find(item => item.id === $1);
      const second = items.find(item => item.id === $2);
      return first?.name === 'Yee review alpha' && first.state === 'ENABLED' &&
             second?.name === 'Yee review beta' && second.version === '2.0';
    })()
  )JS",
                                                     first_id_, second_id_)));

  ASSERT_TRUE(SetSearch("review alpha"));
  ASSERT_TRUE(WaitFor(Row(first_id_) + " && !" + Row(second_id_)));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      Row(first_id_) + ".querySelector('[data-extension-enable]').click()"));
  ASSERT_TRUE(WaitFor(Row(first_id_) +
                      ".querySelector('[data-extension-enable]')"
                      ".getAttribute('aria-checked') === 'false' && !" +
                      Row(first_id_) +
                      ".querySelector('[data-extension-enable]').disabled"))
      << PageState(first_id_);
  EXPECT_TRUE(extension_registry()->disabled_extensions().Contains(first_id_));
  EXPECT_EQ("DISABLED",
            content::EvalJs(
                Contents(),
                content::JsReplace("chrome.developerPrivate.getExtensionInfo($"
                                   "1).then(info => info.state)",
                                   first_id_)));

  // 네이티브 변경은 실제 이벤트 라우터를 거쳐 페이지에 전달되어야 한다.
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    window.extensionChanges = [];
    chrome.developerPrivate.onItemStateChanged.addListener(
        event => window.extensionChanges.push(event.item_id));
  )JS"));
  extension_registrar()->EnableExtension(first_id_);
  ASSERT_TRUE(WaitFor(Row(first_id_) +
                      ".querySelector('[data-extension-enable]')"
                      ".getAttribute('aria-checked') === 'true'"))
      << PageState(first_id_);
  ASSERT_TRUE(WaitFor("window.extensionChanges.includes('" + first_id_ + "')"));
  EXPECT_TRUE(extension_registry()->enabled_extensions().Contains(first_id_));

  ASSERT_TRUE(SetSearch("no matching Yee review extension"));
  ASSERT_TRUE(WaitFor("!" + Row(first_id_) + " && !" + Row(second_id_)));
  ASSERT_TRUE(SetSearch("review"));
  ASSERT_TRUE(WaitFor(Row(first_id_) + " && " + Row(second_id_)));
  ASSERT_TRUE(
      WaitFor("new URL(location.href).searchParams.get('q') === 'review'"));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      Row(first_id_) + ".querySelector('[data-extension-name]').click()"));
  const std::string first_details =
      "document.querySelector('[data-extension-details]')?.dataset.extensionId "
      "=== '" +
      first_id_ +
      "' && !document.querySelector('[data-extension-list]') && "
      "new URL(location.href).searchParams.get('id') === '" +
      first_id_ +
      "' && "
      "new URL(location.href).searchParams.get('q') === 'review'";
  ASSERT_TRUE(WaitFor(first_details)) << PageState(first_id_);
  EXPECT_EQ("Yee review alpha",
            content::EvalJs(Contents(),
                            "document.querySelector('[data-extension-details] "
                            "[data-extension-name]').textContent"));

  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('[data-extension-details] .back').click()"));
  const std::string filtered_list =
      "!!document.querySelector('[data-extension-list]') && " + Row(first_id_) +
      " && " + Row(second_id_) +
      " && !document.querySelector('[data-extension-details]') && "
      "!new URL(location.href).searchParams.has('id') && "
      "new URL(location.href).searchParams.get('q') === 'review' && "
      "document.querySelector('input[type=search]')?.value === 'review'";
  ASSERT_TRUE(WaitFor(filtered_list)) << PageState(first_id_);

  ASSERT_TRUE(Contents()->GetController().CanGoBack());
  Contents()->GetController().GoBack();
  ASSERT_TRUE(WaitFor(first_details)) << PageState(first_id_);
  ASSERT_TRUE(Contents()->GetController().CanGoForward());
  Contents()->GetController().GoForward();
  ASSERT_TRUE(WaitFor(filtered_list)) << PageState(first_id_);

  // 기록에서 복원한 목록의 다른 행을 열면 새 선택에 맞는 정보와 조작을 표시해야
  // 한다.
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      Row(second_id_) + ".querySelector('[data-extension-name]').click()"));
  ASSERT_TRUE(WaitFor(
      "document.querySelector('[data-extension-details]')?.dataset.extensionId "
      "=== '" +
      second_id_ + "' && new URL(location.href).searchParams.get('id') === '" +
      second_id_ +
      "' && new URL(location.href).searchParams.get('q') === 'review'"))
      << PageState(second_id_);
  EXPECT_EQ("Yee review beta",
            content::EvalJs(Contents(),
                            "document.querySelector('[data-extension-details] "
                            "[data-extension-name]').textContent"));
  EXPECT_EQ(true,
            content::EvalJs(Contents(),
                            "document.querySelector('[data-extension-details]')"
                            ".textContent.includes('2.0')"));
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://extensions/")));
  EXPECT_EQ(true,
            content::EvalJs(Contents(),
                            "!!document.querySelector('extensions-manager')"));
  EXPECT_EQ("chrome://extensions",
            content::EvalJs(Contents(), "location.origin"));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest, DetailsAndNativeSitePermissions) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(branding::ExtensionsURL().spec() + "?id=" + first_id_)));
  ASSERT_TRUE(WaitFor("!!document.querySelector('[data-extension-details]')"));
  EXPECT_EQ(true,
            content::EvalJs(Contents(),
                            "document.querySelector('[data-extension-details]')"
                            ".textContent.includes('Yee review alpha')"));
  EXPECT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    (async () => {
      await chrome.developerPrivate.updateExtensionConfiguration({
        extensionId: $1, hostAccess: 'ON_CLICK'});
      const info = await chrome.developerPrivate.getExtensionInfo($1);
      return info.permissions.runtimeHostPermissions.hostAccess === 'ON_CLICK';
    })()
  )JS",
                                                                 first_id_)));
  const auto* extension = extension_registry()->GetExtensionById(
      first_id_, extensions::ExtensionRegistry::ENABLED);
  ASSERT_TRUE(extension);
  auto* permissions =
      extensions::PermissionsManager::Get(browser()->GetProfile());
  EXPECT_TRUE(permissions->HasWithheldHostPermissions(*extension));
  ASSERT_TRUE(
      WaitFor("document.querySelector('[data-extension-site-access]')"
              "?.value === 'ON_CLICK'"));

  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (() => {
      const access = document.querySelector('[data-extension-site-access]');
      access.value = 'ON_ALL_SITES';
      access.dispatchEvent(new Event('change', {bubbles: true}));
    })();
  )JS"));
  ASSERT_TRUE(WaitFor(
      "(await chrome.developerPrivate.getExtensionInfo('" + first_id_ +
      "')).permissions.runtimeHostPermissions.hostAccess === 'ON_ALL_SITES'"));
  ASSERT_TRUE(
      WaitFor("document.querySelector('[data-extension-site-access]')"
              "?.value === 'ON_ALL_SITES'"));
  ASSERT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    (async () => {
      const info = await chrome.developerPrivate.getExtensionInfo($1);
      const permissions = info.permissions.runtimeHostPermissions;
      window.allSitesPermissions = JSON.stringify(permissions);
      return permissions.hasAllHosts && permissions.hostAccess === 'ON_ALL_SITES' &&
          permissions.hosts.some(host => host.granted);
    })()
  )JS",
                                                                 first_id_)));
  EXPECT_FALSE(permissions->HasWithheldHostPermissions(*extension));
  EXPECT_TRUE(permissions->HasBroadGrantedHostPermissions(*extension));

  // 전체 사이트의 granted 항목이 있어도 특정 사이트로 전환하려면 입력이
  // 필요하다.
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (() => {
      const access = document.querySelector('[data-extension-site-access]');
      access.value = 'ON_SPECIFIC_SITES';
      access.dispatchEvent(new Event('change', {bubbles: true}));
    })();
  )JS"));
  ASSERT_TRUE(
      WaitFor("!!document.querySelector('[role=dialog] #extension-site') && "
              "document.querySelector('[data-extension-site-access]')"
              "?.value === 'ON_ALL_SITES'"))
      << PageState(first_id_);
  EXPECT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    chrome.developerPrivate.getExtensionInfo($1).then(info =>
        JSON.stringify(info.permissions.runtimeHostPermissions) === window.allSitesPermissions)
  )JS",
                                                                 first_id_)));
  ASSERT_TRUE(content::ExecJs(Contents(),
                              "document.querySelector('[role=dialog] form "
                              "button[type=button]').click()"));
  ASSERT_TRUE(
      WaitFor("!document.querySelector('[role=dialog]') && "
              "document.querySelector('[data-extension-site-access]')"
              "?.value === 'ON_ALL_SITES'"));
  EXPECT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    chrome.developerPrivate.getExtensionInfo($1).then(info =>
        JSON.stringify(info.permissions.runtimeHostPermissions) === window.allSitesPermissions)
  )JS",
                                                                 first_id_)));
  EXPECT_FALSE(permissions->HasWithheldHostPermissions(*extension));

  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    (() => {
      const access = document.querySelector('[data-extension-site-access]');
      access.value = 'ON_SPECIFIC_SITES';
      access.dispatchEvent(new Event('change', {bubbles: true}));
    })();
  )JS"));
  ASSERT_TRUE(
      WaitFor("!!document.querySelector('[role=dialog] #extension-site')"));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    const site = document.querySelector('#extension-site');
    site.value = 'https://alpha.example.test';
    site.dispatchEvent(new Event('input', {bubbles: true}));
  )JS"));
  ASSERT_TRUE(
      WaitFor("!document.querySelector('[role=dialog] button[type=submit]')"
              ".disabled"));
  ASSERT_TRUE(content::ExecJs(
      Contents(),
      "document.querySelector('[role=dialog] button[type=submit]').click()"));
  ASSERT_TRUE(WaitFor("(await chrome.developerPrivate.getExtensionInfo('" +
                      first_id_ +
                      "')).permissions.runtimeHostPermissions.hostAccess === "
                      "'ON_SPECIFIC_SITES' && "
                      "document.querySelector('[data-extension-site-access]')"
                      "?.value === 'ON_SPECIFIC_SITES' && "
                      "!document.querySelector('[role=dialog]')"))
      << PageState(first_id_);
  EXPECT_TRUE(permissions->HasWithheldHostPermissions(*extension));
  EXPECT_FALSE(permissions->HasBroadGrantedHostPermissions(*extension));
  EXPECT_TRUE(permissions->HasGrantedHostPermission(
      *extension, GURL("https://alpha.example.test/")));
  EXPECT_FALSE(permissions->HasGrantedHostPermission(
      *extension, GURL("https://beta.example.test/")));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest,
                       FiniteRequestedSitesRequireWithholdingBeforeEditing) {
  extensions::TestExtensionDir finite_dir;
  finite_dir.WriteManifest(R"({
    "manifest_version": 3,
    "name": "Yee review finite sites",
    "version": "1.0",
    "host_permissions": [
      "https://alpha.example.test/*",
      "https://beta.example.test/*"
    ]
  })");
  const auto* extension = LoadExtension(finite_dir.UnpackedPath());
  ASSERT_TRUE(extension);
  const std::string id = extension->id();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(branding::ExtensionsURL().spec() + "?id=" + id)));
  ASSERT_TRUE(WaitFor(
      "!!document.querySelector('[data-extension-all-requested-sites]') && "
      "document.querySelectorAll('.site-access li.host [role=switch]')"
      ".length === 2"));
  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    window.extensionChanges = [];
    chrome.developerPrivate.onItemStateChanged.addListener(
        event => window.extensionChanges.push(event.item_id));
  )JS"));
  EXPECT_EQ(true, content::EvalJs(Contents(), content::JsReplace(R"JS(
    (async () => {
      const info = await chrome.developerPrivate.getExtensionInfo($1);
      const permissions = info.permissions.runtimeHostPermissions;
      const master = document.querySelector('[data-extension-all-requested-sites]');
      const hosts = [...document.querySelectorAll('.site-access li.host [role=switch]')];
      return !permissions.hasAllHosts && permissions.hostAccess === 'ON_ALL_SITES' &&
          permissions.hosts.length === 2 && permissions.hosts.every(host => host.granted) &&
          master.getAttribute('aria-checked') === 'true' &&
          hosts.every(toggle => toggle.disabled && toggle.getAttribute('aria-checked') === 'true');
    })()
  )JS",
                                                                 id)));

  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    document.querySelector('[data-extension-all-requested-sites]').click();
  )JS"));
  // 보류 후 허용된 사이트가 없으면 Chromium은 ON_CLICK을 반환한다.
  // 요청한 사이트를 하나 허용한 뒤에 ON_SPECIFIC_SITES가 된다.
  ASSERT_TRUE(WaitFor(
      "(await chrome.developerPrivate.getExtensionInfo('" + id +
      "')).permissions.runtimeHostPermissions.hostAccess === 'ON_CLICK' && "
      "document.querySelector('[data-extension-all-requested-sites]')"
      ".getAttribute('aria-checked') === 'false' && "
      "[...document.querySelectorAll('.site-access li.host [role=switch]')]"
      ".every(toggle => !toggle.disabled && "
      "toggle.getAttribute('aria-checked') === 'false')"));
  auto* permissions =
      extensions::PermissionsManager::Get(browser()->GetProfile());
  EXPECT_TRUE(permissions->HasWithheldHostPermissions(*extension));
  const GURL alpha("https://alpha.example.test/");
  const GURL beta("https://beta.example.test/");
  EXPECT_FALSE(permissions->HasGrantedHostPermission(*extension, alpha));
  EXPECT_FALSE(permissions->HasGrantedHostPermission(*extension, beta));

  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    window.alphaSiteToggle = () =>
        [...document.querySelectorAll('.site-access li.host')]
            .find(row => row.querySelector('span').textContent === 'https://alpha.example.test/*')
            .querySelector('[role=switch]');
    window.alphaSiteToggle().click();
  )JS"));
  ASSERT_TRUE(WaitFor(
      "(await chrome.developerPrivate.getExtensionInfo('" + id +
      "')).permissions.runtimeHostPermissions.hostAccess === "
      "'ON_SPECIFIC_SITES' && "
      "window.alphaSiteToggle().getAttribute('aria-checked') === 'true' && "
      "!window.alphaSiteToggle().disabled"));
  EXPECT_TRUE(permissions->HasWithheldHostPermissions(*extension));
  EXPECT_TRUE(permissions->HasGrantedHostPermission(*extension, alpha));
  EXPECT_FALSE(permissions->HasGrantedHostPermission(*extension, beta));

  ASSERT_TRUE(content::ExecJs(Contents(), "window.alphaSiteToggle().click()"));
  ASSERT_TRUE(WaitFor(
      "(await chrome.developerPrivate.getExtensionInfo('" + id +
      "')).permissions.runtimeHostPermissions.hostAccess === 'ON_CLICK' && "
      "window.alphaSiteToggle().getAttribute('aria-checked') === 'false' && "
      "!window.alphaSiteToggle().disabled"))
      << PageState(id);
  EXPECT_FALSE(permissions->HasGrantedHostPermission(*extension, alpha));

  ASSERT_TRUE(content::ExecJs(Contents(), R"JS(
    document.querySelector('[data-extension-all-requested-sites]').click();
  )JS"));
  ASSERT_TRUE(WaitFor(
      "(await chrome.developerPrivate.getExtensionInfo('" + id +
      "')).permissions.runtimeHostPermissions.hostAccess === 'ON_ALL_SITES' && "
      "document.querySelector('[data-extension-all-requested-sites]')"
      ".getAttribute('aria-checked') === 'true' && "
      "[...document.querySelectorAll('.site-access li.host [role=switch]')]"
      ".every(toggle => toggle.disabled && "
      "toggle.getAttribute('aria-checked') === 'true')"));
  EXPECT_FALSE(permissions->HasWithheldHostPermissions(*extension));
  EXPECT_TRUE(permissions->HasGrantedHostPermission(*extension, alpha));
  EXPECT_TRUE(permissions->HasGrantedHostPermission(*extension, beta));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest,
                       PolicyInstallationCannotBeModified) {
  // 네이티브 정책 브라우저 테스트와 같은 관리 기기 환경을 사용한다.
  // 관리되지 않는 macOS 기기에서는 스토어 외부 강제 설치가 비활성화된다.
  policy::ScopedDomainEnterpriseManagement domain_management;
  extensions::TestExtensionDir managed_dir;
  managed_dir.WriteManifest(R"({
    "manifest_version": 3,
    "name": "Yee review managed",
    "version": "1.0"
  })");
  const base::FilePath crx_path = managed_dir.Pack();
  std::string managed_id;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_EQ(crx_file::VerifierResult::OK_FULL,
              crx_file::Verify(crx_path, crx_file::VerifierFormat::CRX3,
                               /*required_key_hashes=*/{},
                               /*required_file_hash=*/{},
                               /*public_key=*/nullptr, &managed_id,
                               /*compressed_verified_contents=*/nullptr));
  }
  {
    // 서명된 CRX 설치 전에 실제 강제 설치 정책에 소유권을 등록한다.
    // 설치 출처는 정책 URL 제공자와 일치해야 한다.
    extensions::ExtensionManagementPolicyUpdater policy(&policy_provider_);
    policy.SetIndividualExtensionAutoInstalled(
        managed_id, "https://example.test/update.xml", /*forced=*/true);
  }
  const auto* managed = InstallExtensionWithSourceAndFlags(
      crx_path, 1, extensions::mojom::ManifestLocation::kExternalPolicyDownload,
      extensions::Extension::NO_FLAGS);
  ASSERT_TRUE(managed);
  ASSERT_EQ(managed_id, managed->id());
  ASSERT_EQ(extensions::mojom::ManifestLocation::kExternalPolicyDownload,
            managed->location());
  ASSERT_TRUE(NavigateToList());
  SCOPED_TRACE(testing::Message()
               << "Managed registry enabled="
               << extension_registry()->enabled_extensions().Contains(
                      managed_id)
               << ", disabled="
               << extension_registry()->disabled_extensions().Contains(
                      managed_id)
               << ", installed="
               << !!extension_registry()->GetInstalledExtension(managed_id));
  SCOPED_TRACE(PageState(managed_id));
  ASSERT_TRUE(WaitFor("!!" + Row(managed_id))) << PageState(managed_id);
  EXPECT_EQ(false,
            content::EvalJs(
                Contents(),
                content::JsReplace("chrome.developerPrivate.getExtensionInfo($"
                                   "1).then(info => info.userMayModify)",
                                   managed_id)));
  EXPECT_EQ(true,
            content::EvalJs(
                Contents(),
                content::JsReplace("chrome.developerPrivate.getExtensionInfo($"
                                   "1).then(info => info.mustRemainInstalled)",
                                   managed_id)));
  EXPECT_EQ(true,
            content::EvalJs(
                Contents(),
                content::JsReplace("chrome.developerPrivate.getExtensionInfo($"
                                   "1).then(info => !!info.controlledInfo)",
                                   managed_id)));
  const std::string policy_error =
      "Extension " + managed_id + " cannot be modified by user.";
  EXPECT_EQ(policy_error,
            content::EvalJs(Contents(), content::JsReplace(R"JS(
    chrome.management.setEnabled($1, false).then(
        () => 'unexpected-success', error => String(error.message ?? error))
  )JS",
                                                           managed_id)));
  EXPECT_EQ(policy_error,
            content::EvalJs(Contents(), content::JsReplace(R"JS(
    chrome.management.uninstall($1, {showConfirmDialog: true}).then(
        () => 'unexpected-success', error => String(error.message ?? error))
  )JS",
                                                           managed_id)));
  EXPECT_EQ("ENABLED",
            content::EvalJs(
                Contents(),
                content::JsReplace("chrome.developerPrivate.getExtensionInfo($"
                                   "1).then(info => info.state)",
                                   managed_id)));
  EXPECT_EQ(true,
            content::EvalJs(
                Contents(),
                Row(managed_id) +
                    ".querySelector('[data-extension-enable]').disabled"));
  EXPECT_TRUE(extension_registry()->enabled_extensions().Contains(managed_id));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest,
                       NativeRemovalConfirmationAndEvent) {
  ASSERT_TRUE(NavigateToList());
  {
    extensions::ScopedTestDialogAutoConfirm cancel(
        extensions::ScopedTestDialogAutoConfirm::CANCEL);
    EXPECT_EQ(true,
              content::EvalJs(Contents(), content::JsReplace(R"JS(
      (async () => {
        try {await chrome.management.uninstall($1, {showConfirmDialog: true});}
        catch {return true;}
        return false;
      })()
    )JS",
                                                             second_id_)));
  }
  EXPECT_TRUE(extension_registry()->enabled_extensions().Contains(second_id_));
  EXPECT_EQ(true, content::EvalJs(Contents(), "!!" + Row(second_id_)));
  {
    extensions::ScopedTestDialogAutoConfirm accept(
        extensions::ScopedTestDialogAutoConfirm::ACCEPT);
    ASSERT_TRUE(content::ExecJs(
        Contents(),
        content::JsReplace(
            "chrome.management.uninstall($1, {showConfirmDialog: true})",
            second_id_)));
  }
  ASSERT_TRUE(WaitFor("!" + Row(second_id_)));
  EXPECT_FALSE(extension_registry()->GetExtensionById(
      second_id_, extensions::ExtensionRegistry::EVERYTHING));
  EXPECT_EQ(true, content::EvalJs(Contents(), "!!" + Row(first_id_)));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest, GuestKeepsNativeRestriction) {
  BrowserWindowInterface* guest = CreateGuestBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(guest, branding::ExtensionsURL()));
  auto* contents = guest->tab_strip_model()->GetActiveWebContents();
  EXPECT_EQ(true, content::EvalJs(
                      contents,
                      "!!document.querySelector('h1') && "
                      "document.querySelector('h1').textContent.length > 0"));
  EXPECT_EQ(false,
            content::EvalJs(
                contents, "!!document.querySelector('[data-extension-list]')"));
  EXPECT_EQ(false,
            content::EvalJs(contents,
                            "document.title.includes('yee-extensions') || "
                            "document.querySelector('h1').textContent.includes("
                            "'yee-extensions')"));
}

IN_PROC_BROWSER_TEST_F(ExtensionsBrowserTest, PrivateAPIsKeepNativeSemantics) {
  BrowserWindowInterface* private_browser =
      CreateIncognitoBrowser(browser()->GetProfile());
  auto* private_contents =
      private_browser->tab_strip_model()->GetActiveWebContents();
  const GURL private_url = private_contents->GetLastCommittedURL();
  NavigateParams native_params(private_browser, GURL("chrome://extensions/"),
                               ui::PAGE_TRANSITION_TYPED);
  ui_test_utils::NavigateToURL(&native_params);
  ASSERT_TRUE(native_params.browser);
  ASSERT_EQ(browser()->GetProfile(), native_params.browser->GetProfile());
  auto* native_contents = native_params.navigated_or_inserted_contents.get();
  ASSERT_TRUE(native_contents);
  ASSERT_EQ(GURL("chrome://extensions/"),
            native_contents->GetLastCommittedURL());
  EXPECT_EQ(private_url, private_contents->GetLastCommittedURL());
  // Chromium은 비공개 창의 확장 프로그램 탐색을 원래 프로필로 옮긴다.
  // 제품 경로도 같은 프로필 경계를 유지해야 한다.
  const std::string snapshot = content::JsReplace(R"JS(
    (async () => {
      const items = await chrome.developerPrivate.getExtensionsInfo({
        includeDisabled: true, includeTerminated: true});
      return JSON.stringify({
        profile: await chrome.developerPrivate.getProfileConfiguration(),
        items: items.filter(item => item.id === $1 || item.id === $2)
            .map(({id, state, userMayModify}) => ({id, state, userMayModify}))
            .sort((a, b) => a.id.localeCompare(b.id))
      });
    })()
  )JS",
                                                  first_id_, second_id_);
  const auto native_result = content::EvalJs(native_contents, snapshot);
  ASSERT_TRUE(native_result.is_string()) << native_result;
  const std::string native = native_result.ExtractString();
  NavigateParams product_params(private_browser, branding::ExtensionsURL(),
                                ui::PAGE_TRANSITION_TYPED);
  ui_test_utils::NavigateToURL(&product_params);
  ASSERT_TRUE(product_params.browser);
  ASSERT_EQ(browser()->GetProfile(), product_params.browser->GetProfile());
  auto* product_contents = product_params.navigated_or_inserted_contents.get();
  ASSERT_TRUE(product_contents);
  EXPECT_EQ(branding::ExtensionsURL(), product_contents->GetLastCommittedURL());
  EXPECT_EQ(private_url, private_contents->GetLastCommittedURL());
  EXPECT_EQ(native, content::EvalJs(product_contents, snapshot));
  EXPECT_EQ("chrome://yee-extensions",
            content::EvalJs(product_contents, "location.origin"));
}
}  // namespace
}  // namespace yee
