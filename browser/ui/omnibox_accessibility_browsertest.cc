// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/metrics/user_action_tester.h"
#include "base/test/run_until.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/projects/project_navigation_controller.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/browser/omnibox_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/test/ui_controls.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include <oleacc.h>
#include <uiautomation.h>
#include <wrl/client.h>

#include "base/test/test_future.h"
#include "base/threading/thread.h"
#include "base/win/scoped_bstr.h"
#include "base/win/scoped_variant.h"
#include "content/public/test/scoped_accessibility_mode_override.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/aura/test/ui_controls_aurawin.h"
#include "ui/views/win/hwnd_util.h"
#endif

namespace yee {
namespace {

#if BUILDFLAG(IS_WIN)
struct WindowAccessibleUrls {
  HRESULT result = S_OK;
  std::vector<std::u16string> addresses;
  std::vector<std::u16string> documents;
};

// 주소창 포인터 없이 실제 HWND의 Control View에서 URL을 읽는다.
WindowAccessibleUrls ReadWindowAccessibleUrls(HWND hwnd) {
  WindowAccessibleUrls urls;
  auto fail = [&](HRESULT result) {
    urls.result = result;
    return urls;
  };
  Microsoft::WRL::ComPtr<IUIAutomation> automation;
  HRESULT result =
      CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&automation));
  if (FAILED(result)) {
    return fail(result);
  }
  Microsoft::WRL::ComPtr<IUIAutomationElement> root;
  result = automation->ElementFromHandle(hwnd, &root);
  if (FAILED(result) || !root) {
    return fail(FAILED(result) ? result : E_POINTER);
  }
  Microsoft::WRL::ComPtr<IUIAutomationCondition> edit;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> document;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> types;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> controls;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
  if (FAILED(result = automation->CreatePropertyCondition(
                 UIA_ControlTypePropertyId,
                 base::win::ScopedVariant(UIA_EditControlTypeId), &edit)) ||
      FAILED(result = automation->CreatePropertyCondition(
                 UIA_ControlTypePropertyId,
                 base::win::ScopedVariant(UIA_DocumentControlTypeId),
                 &document)) ||
      FAILED(result = automation->CreateOrCondition(edit.Get(), document.Get(),
                                                    &types)) ||
      FAILED(result = automation->get_ControlViewCondition(&controls)) ||
      FAILED(result = automation->CreateAndCondition(
                 controls.Get(), types.Get(), &condition))) {
    return fail(result);
  }
  Microsoft::WRL::ComPtr<IUIAutomationElementArray> elements;
  result = root->FindAll(TreeScope_Descendants, condition.Get(), &elements);
  if (FAILED(result) || !elements) {
    return fail(FAILED(result) ? result : E_POINTER);
  }
  int count = 0;
  if (FAILED(result = elements->get_Length(&count))) {
    return fail(result);
  }
  for (int index = 0; index < count; ++index) {
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(result = elements->GetElement(index, &element)) || !element) {
      return fail(FAILED(result) ? result : E_POINTER);
    }
    BOOL offscreen = TRUE;
    CONTROLTYPEID type = 0;
    base::win::ScopedBstr name;
    base::win::ScopedBstr class_name;
    if (FAILED(result = element->get_CurrentIsOffscreen(&offscreen)) ||
        FAILED(result = element->get_CurrentControlType(&type)) ||
        FAILED(result = element->get_CurrentName(name.Receive())) ||
        FAILED(result = element->get_CurrentClassName(class_name.Receive()))) {
      return fail(result);
    }
    const bool address =
        type == UIA_EditControlTypeId && class_name.Get() &&
        std::wstring_view(class_name.Get()) == L"OmniboxViewViews";
    const bool page =
        type == UIA_DocumentControlTypeId && name.Get() &&
        std::wstring_view(name.Get()) == L"Address accessibility fixture";
    if (offscreen || (!address && !page)) {
      continue;
    }
    Microsoft::WRL::ComPtr<IUIAutomationValuePattern> pattern;
    base::win::ScopedBstr value;
    result = element->GetCurrentPatternAs(UIA_ValuePatternId,
                                          IID_PPV_ARGS(&pattern));
    if (FAILED(result) || !pattern) {
      return fail(FAILED(result) ? result : E_POINTER);
    }
    if (FAILED(result = pattern->get_CurrentValue(value.Receive()))) {
      return fail(result);
    }
    if (!value.Get()) {
      return fail(E_UNEXPECTED);
    }
    (address ? urls.addresses : urls.documents)
        .push_back(base::WideToUTF16(value.Get()));
  }
  std::ranges::sort(urls.documents);
  return urls;
}
#endif

// 장식 레이어를 끈 경우와 비교하되 이를 무패치 Chromium으로 간주하지 않는다.
class OmniboxAccessibilityBrowserTest
    : public InProcessBrowserTest,
      public testing::WithParamInterface<std::tuple<bool, bool>> {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    if (!UsesShell()) {
      command_line->AppendSwitch("disable-yee-shell-scaffold");
    }
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    if (UsesHttps()) {
      https_server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    }
    Server()->RegisterRequestHandler(base::BindRepeating(
        [](const net::test_server::HttpRequest&)
            -> std::unique_ptr<net::test_server::HttpResponse> {
          auto response =
              std::make_unique<net::test_server::BasicHttpResponse>();
          response->set_content_type("text/html");
          // 제목이 같아도 각 탭의 실제 URL을 구분해야 한다.
          response->set_content(
              "<!doctype html><title>Address accessibility fixture</title>"
              "<body style='background:#142129;color:white'>Fixture</body>");
          return response;
        }));
    ASSERT_TRUE(Server()->Start());
#if BUILDFLAG(IS_WIN)
    accessibility_mode_ =
        std::make_unique<content::ScopedAccessibilityModeOverride>(
            ui::kAXModeComplete);
    // UIA는 MTA에서 읽고, 브라우저 UI 스레드는 provider 요청을 처리한다.
    accessibility_thread_.init_com_with_mta(true);
    ASSERT_TRUE(accessibility_thread_.Start());
    aura::test::EnableUIControlsAuraWin();
#else
    ui_controls::EnableUIControls();
#endif
    ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  }

  void TearDownOnMainThread() override {
#if BUILDFLAG(IS_WIN)
    accessibility_thread_.Stop();
    accessibility_mode_.reset();
#endif
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  bool UsesShell() const { return std::get<0>(GetParam()); }
  bool UsesHttps() const { return std::get<1>(GetParam()); }

  net::EmbeddedTestServer* Server() {
    return UsesHttps() ? &https_server_ : embedded_test_server();
  }

  BrowserView* View() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  OmniboxViewViews* Omnibox() {
    return View()->GetLocationBarView()->omnibox_view();
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  GURL Url(const char* host = "a.test") {
    return Server()->GetURL(host, "/address?fixture=1#section");
  }

  std::u16string EditableUrl(const GURL& url) {
    // native 편집 값은 HTTP scheme을 생략하지만 HTTPS scheme은 복원한다.
    const std::string http_prefix = "http://";
    return base::UTF8ToUTF16(url.SchemeIs("http")
                                 ? url.spec().substr(http_prefix.size())
                                 : url.spec());
  }

  void FocusAddress() {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_L,
                                                /*control=*/!BUILDFLAG(IS_MAC),
                                                /*shift=*/false,
                                                /*alt=*/false,
                                                /*command=*/BUILDFLAG(IS_MAC)));
    ASSERT_TRUE(base::test::RunUntil([&]() { return Omnibox()->HasFocus(); }));
    ASSERT_TRUE(Omnibox()->IsSelectAll());
  }

  void CheckAccessibleAddress(const std::string& phase,
                              const std::u16string& expected,
                              const std::vector<GURL>& visible_documents = {}) {
    SCOPED_TRACE(phase);
    ASSERT_TRUE(Omnibox()->IsDrawn());
    ASSERT_FALSE(Omnibox()->GetBoundsInScreen().IsEmpty());
    ui::AXNodeData node;
    Omnibox()->GetViewAccessibility().GetAccessibleNodeData(&node);
    EXPECT_EQ(ax::mojom::Role::kTextField, node.role);
    EXPECT_FALSE(node.IsIgnored());
    EXPECT_FALSE(node.HasState(ax::mojom::State::kInvisible));
    EXPECT_FALSE(
        node.GetStringAttribute(ax::mojom::StringAttribute::kName).empty());
    EXPECT_EQ(expected,
              node.GetString16Attribute(ax::mojom::StringAttribute::kValue));
    EXPECT_EQ(expected, Omnibox()->GetText());
    RecordProperty(phase + "_accessible_value",
                   node.GetStringAttribute(ax::mojom::StringAttribute::kValue));
    RecordProperty(phase + "_visible_url", Contents()->GetVisibleURL().spec());
#if BUILDFLAG(IS_WIN)
    // Views 캐시만이 아니라 Windows 클라이언트가 읽는 provider도 검사한다.
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider;
    ASSERT_TRUE(Omnibox()->GetNativeViewAccessible());
    ASSERT_HRESULT_SUCCEEDED(
        Omnibox()->GetNativeViewAccessible()->QueryInterface(
            IID_PPV_ARGS(&provider)));
    ASSERT_TRUE(provider);
    base::win::ScopedVariant type;
    ASSERT_HRESULT_SUCCEEDED(
        provider->GetPropertyValue(UIA_ControlTypePropertyId, type.Receive()));
    ASSERT_EQ(VT_I4, type.type());
    EXPECT_EQ(UIA_EditControlTypeId, type.ptr()->lVal);
    Microsoft::WRL::ComPtr<IUnknown> pattern;
    ASSERT_HRESULT_SUCCEEDED(
        provider->GetPatternProvider(UIA_ValuePatternId, &pattern));
    ASSERT_TRUE(pattern);
    Microsoft::WRL::ComPtr<IValueProvider> value_provider;
    ASSERT_HRESULT_SUCCEEDED(pattern.As(&value_provider));
    base::win::ScopedBstr value;
    ASSERT_HRESULT_SUCCEEDED(value_provider->get_Value(value.Receive()));
    ASSERT_TRUE(value.Get());
    EXPECT_EQ(expected, base::WideToUTF16(value.Get()));
    BOOL read_only = TRUE;
    ASSERT_HRESULT_SUCCEEDED(value_provider->get_IsReadOnly(&read_only));
    EXPECT_FALSE(read_only);

    std::vector<std::u16string> expected_documents;
    for (const auto& url : visible_documents.empty()
                               ? std::vector<GURL>{Contents()->GetVisibleURL()}
                               : visible_documents) {
      expected_documents.push_back(base::UTF8ToUTF16(url.spec()));
    }
    std::ranges::sort(expected_documents);
    const HWND hwnd = views::HWNDForWidget(View()->GetWidget());
    ASSERT_TRUE(hwnd);
    WindowAccessibleUrls window_urls;
    const bool exposed = base::test::RunUntil([&]() {
      base::test::TestFuture<WindowAccessibleUrls> future;
      accessibility_thread_.task_runner()->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&ReadWindowAccessibleUrls, hwnd),
          future.GetCallback());
      window_urls = future.Take();
      return SUCCEEDED(window_urls.result) &&
             window_urls.addresses == std::vector<std::u16string>{expected} &&
             window_urls.documents == expected_documents;
    });
    EXPECT_HRESULT_SUCCEEDED(window_urls.result);
    EXPECT_EQ(std::vector<std::u16string>{expected}, window_urls.addresses);
    EXPECT_EQ(expected_documents, window_urls.documents);
    ASSERT_TRUE(exposed);
    RecordProperty(phase + "_window_omnibox_count",
                   static_cast<int>(window_urls.addresses.size()));
    for (size_t index = 0; index < window_urls.documents.size(); ++index) {
      RecordProperty(phase + "_window_document_" + std::to_string(index),
                     base::UTF16ToUTF8(window_urls.documents[index]));
    }
#endif
  }

 private:
  net::EmbeddedTestServer https_server_{net::EmbeddedTestServer::TYPE_HTTPS};
#if BUILDFLAG(IS_WIN)
  std::unique_ptr<content::ScopedAccessibilityModeOverride> accessibility_mode_;
  base::Thread accessibility_thread_{"Yee window accessibility"};
#endif
};

// 대기·편집 상태의 native URL 규칙과 Ctrl+L의 전체 선택을 보존한다.
IN_PROC_BROWSER_TEST_P(OmniboxAccessibilityBrowserTest,
                       RestAndCtrlLKeepNativeAccessibleUrl) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url()));
  Contents()->Focus();
  ASSERT_TRUE(base::test::RunUntil([&]() { return !Omnibox()->HasFocus(); }));
  if (UsesShell()) {
    auto* resting =
        View()->GetLocationBarView()->GetCompactShellRestingTextForTesting();
    ASSERT_TRUE(resting);
    ASSERT_TRUE(base::test::RunUntil([&]() { return resting->GetVisible(); }));
    EXPECT_TRUE(resting->GetViewAccessibility().GetIsIgnored());
    EXPECT_FALSE(resting->GetCanProcessEventsWithinSubtree());
    for (views::View* child : resting->children()) {
      EXPECT_TRUE(child->GetViewAccessibility().GetIsIgnored());
    }
  }
  // fixture의 대기 상태 기대값을 문서 URL에서 독립적으로 만든다.
  const std::string full_url = Url().spec();
  const std::string scheme_prefix = UsesHttps() ? "https://" : "http://";
  ASSERT_TRUE(full_url.starts_with(scheme_prefix));
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress(
      "rest", base::UTF8ToUTF16(full_url.substr(scheme_prefix.size()))));
  auto* prefs = browser()->GetProfile()->GetPrefs();
  prefs->SetBoolean(omnibox::kPreventUrlElisionsInOmnibox, true);
  if (UsesShell()) {
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return !View()
                  ->GetLocationBarView()
                  ->GetCompactShellRestingTextForTesting()
                  ->GetVisible();
    }));
  }
  ASSERT_NO_FATAL_FAILURE(
      CheckAccessibleAddress("full_url_pref", base::UTF8ToUTF16(full_url)));
  prefs->SetBoolean(omnibox::kPreventUrlElisionsInOmnibox, false);
  if (UsesShell()) {
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return View()
          ->GetLocationBarView()
          ->GetCompactShellRestingTextForTesting()
          ->GetVisible();
    }));
  }
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress(
      "restored_pref",
      base::UTF8ToUTF16(full_url.substr(scheme_prefix.size()))));
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress("ctrl_l", EditableUrl(Url())));
}

// 동일 제목의 탭을 전환해도 주소와 접근성 노드의 소유권은 active 탭을 따른다.
IN_PROC_BROWSER_TEST_P(OmniboxAccessibilityBrowserTest,
                       TabSwitchExposesActiveUrl) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url()));
  const auto first_id = Omnibox()->GetViewAccessibility().GetUniqueId();
  chrome::AddTabAt(browser(), Url("b.test"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  for (int index : {0, 1, 0}) {
    browser()->tab_strip_model()->ActivateTabAt(index);
    ASSERT_NO_FATAL_FAILURE(FocusAddress());
    const GURL expected = index == 0 ? Url() : Url("b.test");
    EXPECT_EQ(expected, Contents()->GetVisibleURL());
    EXPECT_EQ(first_id, Omnibox()->GetViewAccessibility().GetUniqueId());
    ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress(
        "tab_" + std::to_string(index), EditableUrl(expected)));
  }
}

// 분할의 재부착과 해제에서도 하나의 native 주소창과 현재 URL을 유지한다.
IN_PROC_BROWSER_TEST_P(OmniboxAccessibilityBrowserTest,
                       SplitRehostingKeepsNativeAccessibleUrl) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url()));
  auto* native_omnibox = Omnibox();
  const auto native_id = native_omnibox->GetViewAccessibility().GetUniqueId();
  auto* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), Url("b.test"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  model->ActivateTabAt(0);
  const auto split =
      model->AddToNewSplit({1}, split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
  for (int index : {0, 1, 0}) {
    model->ActivateTabAt(index);
    View()->GetWidget()->LayoutRootViewIfNecessary();
    ASSERT_NO_FATAL_FAILURE(FocusAddress());
    const GURL expected = index == 0 ? Url() : Url("b.test");
    EXPECT_EQ(expected, Contents()->GetVisibleURL());
    EXPECT_EQ(native_omnibox, Omnibox());
    EXPECT_EQ(native_id, Omnibox()->GetViewAccessibility().GetUniqueId());
    ASSERT_NO_FATAL_FAILURE(
        CheckAccessibleAddress("split_" + std::to_string(index),
                               EditableUrl(expected), {Url(), Url("b.test")}));
  }
  model->RemoveSplit(split);
  View()->GetWidget()->LayoutRootViewIfNecessary();
  EXPECT_EQ(View()->toolbar(), View()->GetLocationBarView()->parent());
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  EXPECT_EQ(native_omnibox, Omnibox());
  EXPECT_EQ(Url(), Contents()->GetVisibleURL());
  ASSERT_NO_FATAL_FAILURE(
      CheckAccessibleAddress("unsplit", EditableUrl(Url())));
}

// 편집 중인 문자열은 현재 문서의 URL로 위장하지 않으며 취소할 수 있다.
IN_PROC_BROWSER_TEST_P(OmniboxAccessibilityBrowserTest,
                       EditableDraftDoesNotChangeDocumentUrl) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url()));
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  const std::u16string draft = u"https://uncommitted.yee-fixture.test/draft";
  auto* controller = View()->GetLocationBarView()->GetOmniboxController();
  Omnibox()->SetUserText(draft, /*update_popup=*/true);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return controller->IsPopupOpen(); }));
  EXPECT_TRUE(controller->edit_model()->user_input_in_progress());
  RecordProperty("draft_popup_open", controller->IsPopupOpen());
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress("draft", draft));
  EXPECT_EQ(Url(), Contents()->GetVisibleURL());
  EXPECT_EQ(Url(), Contents()->GetLastCommittedURL());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE, false,
                                              false, false, false));
  // 첫 Escape는 제안만 닫고 초안을 유지한다. 다음 Escape가 편집을 취소한다.
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !controller->IsPopupOpen(); }));
  EXPECT_TRUE(controller->edit_model()->user_input_in_progress());
  RecordProperty("after_first_escape_popup_open", controller->IsPopupOpen());
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress("popup_closed_draft", draft));
  EXPECT_EQ(Url(), Contents()->GetVisibleURL());
  EXPECT_EQ(Url(), Contents()->GetLastCommittedURL());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE, false,
                                              false, false, false));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !controller->edit_model()->user_input_in_progress(); }));
  EXPECT_EQ(Url(), Contents()->GetVisibleURL());
  EXPECT_EQ(Url(), Contents()->GetLastCommittedURL());
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  ASSERT_NO_FATAL_FAILURE(
      CheckAccessibleAddress("cancelled", EditableUrl(Url())));
}

INSTANTIATE_TEST_SUITE_P(
    Presentation,
    OmniboxAccessibilityBrowserTest,
    testing::Combine(testing::Bool(), testing::Bool()),
    [](const testing::TestParamInfo<std::tuple<bool, bool>>& info) {
      return std::string(std::get<0>(info.param) ? "YeeShell"
                                                 : "NativeToolbar") +
             (std::get<1>(info.param) ? "Https" : "Http");
    });

class OmniboxNavigationBrowserTest : public OmniboxAccessibilityBrowserTest {};

// 주소 편집 뒤 실제 Enter와 뒤로·앞으로 입력이 문서·주소를 함께 갱신한다.
IN_PROC_BROWSER_TEST_P(OmniboxNavigationBrowserTest,
                       EnterBackAndForwardKeepCommittedUrl) {
#if BUILDFLAG(IS_MAC)
  GTEST_SKIP() << "이 사례는 Windows/Linux의 Alt+화살표 입력을 검사합니다";
#else
  base::HistogramTester shortcuts;
  base::UserActionTester actions;
  auto* const original_contents = Contents();
#if BUILDFLAG(IS_WIN)
  ui_test_utils::ScopedNativeInputTrace native_input(View()->GetWidget());
#endif
  // 입력 도달·명령 전달·내비게이션 이력을 구분하되 입력과 대기는 바꾸지 않는다.
  const auto record_history = [&](const std::string& phase) {
    auto& navigation = Contents()->GetController();
    RecordProperty(phase + "_entry_count", navigation.GetEntryCount());
    RecordProperty(phase + "_entry_index", navigation.GetCurrentEntryIndex());
    RecordProperty(phase + "_can_back", navigation.CanGoBack());
    RecordProperty(phase + "_can_forward", navigation.CanGoForward());
    RecordProperty(phase + "_back_enabled",
                   chrome::IsCommandEnabled(browser(), IDC_BACK));
    RecordProperty(phase + "_forward_enabled",
                   chrome::IsCommandEnabled(browser(), IDC_FORWARD));
    RecordProperty(
        phase + "_hub",
        projects::ProjectNavigationController::IsHubShowing(browser()));
    RecordProperty(phase + "_same_contents", Contents() == original_contents);
    RecordProperty(phase + "_tab_count", browser()->tab_strip_model()->count());
    RecordProperty(phase + "_active_tab",
                   browser()->tab_strip_model()->active_index());
    RecordProperty(phase + "_window_active", View()->GetWidget()->IsActive());
    RecordProperty(phase + "_omnibox_focus", Omnibox()->HasFocus());
    RecordProperty(
        phase + "_popup",
        View()->GetLocationBarView()->GetOmniboxController()->IsPopupOpen());
    RecordProperty(phase + "_loading", Contents()->IsLoading());
    RecordProperty(phase + "_committed_url",
                   Contents()->GetLastCommittedURL().spec());
    RecordProperty(phase + "_pending_url",
                   navigation.GetPendingEntry()
                       ? navigation.GetPendingEntry()->GetURL().spec()
                       : std::string());
    RecordProperty(phase + "_back_shortcuts",
                   shortcuts.GetBucketCount(
                       "Browser.Shortcuts.TriggeredCommandId", IDC_BACK));
    RecordProperty(phase + "_back_actions", actions.GetActionCount("Back"));
    for (int index = 0; index < navigation.GetEntryCount(); ++index) {
      RecordProperty(phase + "_entry_" + std::to_string(index),
                     navigation.GetEntryAtIndex(index)->GetURL().spec());
    }
#if BUILDFLAG(IS_WIN)
    native_input.Snapshot(phase, Omnibox()->GetBoundsInScreen().CenterPoint());
    RecordProperty("navigation_native_input", native_input.ToJson());
#endif
  };
  const GURL first = Url();
  const GURL second = Server()->GetURL("a.test", "/history-next?step=2");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first));
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  Omnibox()->SetUserText(base::UTF8ToUTF16(second.spec()),
                         /*update_popup=*/true);
  content::TestNavigationObserver entered(Contents());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RETURN, false,
                                              false, false, false));
  entered.Wait();
  ASSERT_EQ(second, Contents()->GetLastCommittedURL());
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  ASSERT_NO_FATAL_FAILURE(
      CheckAccessibleAddress("entered", EditableUrl(second)));

  record_history("before_back");
  content::TestNavigationObserver went_back(Contents());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_LEFT, false,
                                              false, true, false));
  record_history("after_back_key");
  went_back.Wait();
  record_history("after_back_wait");
  ASSERT_EQ(first, Contents()->GetLastCommittedURL());
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  ASSERT_NO_FATAL_FAILURE(CheckAccessibleAddress("back", EditableUrl(first)));

  record_history("before_forward");
  content::TestNavigationObserver went_forward(Contents());
  ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_RIGHT, false,
                                              false, true, false));
  record_history("after_forward_key");
  went_forward.Wait();
  record_history("after_forward_wait");
  ASSERT_EQ(second, Contents()->GetLastCommittedURL());
  ASSERT_NO_FATAL_FAILURE(FocusAddress());
  ASSERT_NO_FATAL_FAILURE(
      CheckAccessibleAddress("forward", EditableUrl(second)));
#endif
}

INSTANTIATE_TEST_SUITE_P(
    YeeShell,
    OmniboxNavigationBrowserTest,
    testing::Values(std::make_tuple(true, false), std::make_tuple(true, true)),
    [](const testing::TestParamInfo<std::tuple<bool, bool>>& info) {
      return std::get<1>(info.param) ? "Https" : "Http";
    });

}  // namespace
}  // namespace yee
