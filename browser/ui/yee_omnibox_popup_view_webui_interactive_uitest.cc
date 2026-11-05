// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_webui.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/pending_task.h"
#include "base/task/current_thread.h"
#include "base/task/task_observer.h"

#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "chrome/browser/autocomplete/aim_eligibility_service_factory.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/test/theme_service_changed_waiter.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_next_features.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_state_manager.h"
#include "chrome/browser/ui/omnibox/omnibox_tab_helper.h"
#include "chrome/browser/ui/permission_bubble/permission_prompt.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/omnibox/full_webui_omnibox_frame.h"
#include "chrome/browser/ui/views/omnibox/omnibox_aim_popup_webui_content.h"
#include "chrome/browser/ui/views/omnibox/omnibox_full_popup_webui_content.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_aim_presenter.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_full_presenter.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_presenter.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_presenter_delegate.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_webui_base_content.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_webui_content.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/omnibox/rounded_omnibox_results_frame.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/webui/omnibox_popup/omnibox_popup_handler.h"
#include "chrome/browser/ui/webui/omnibox_popup/omnibox_popup_ui.h"
#include "chrome/browser/ui/webui/searchbox/webui_omnibox_handler.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/permissions/permission_request_manager_test_api.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/omnibox/browser/aim_eligibility_service.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/mock_aim_eligibility_service.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/omnibox/browser/omnibox_triggered_feature_service.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/permissions/embedded_permission_prompt_flow_model.h"
#include "components/permissions/test/mock_permission_request.h"
#if BUILDFLAG(IS_MAC)
#include "components/viz/common/features.h"
#endif
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/interaction/expect_call_in_scope.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/compositor.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/test/widget_activation_waiter.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

#if BUILDFLAG(IS_LINUX)
#include "ui/linux/linux_ui.h"
#include "ui/linux/linux_ui_getter.h"
#endif

// ChromeOS environment doesn't instantiate the NewWebUI<OmniboxPopupUI>
// in the factory's GetWebUIFactoryFunction, so these don't work there yet.
// Also avoid burdening test bots on mobile platforms where webui omnibox
// isn't ready and the platform-specific views implementation is in scope.
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

using namespace yee::test;
class YeeOmniboxPopupViewWebUITest : public InProcessBrowserTest {
 public:
  YeeOmniboxPopupViewWebUITest();
  ~YeeOmniboxPopupViewWebUITest() override;
  YeeOmniboxPopupViewWebUITest(const YeeOmniboxPopupViewWebUITest&) = delete;
  YeeOmniboxPopupViewWebUITest& operator=(const YeeOmniboxPopupViewWebUITest&) =
      delete;

  void CreatePopupForTestQuery();

  LocationBar* location_bar() {
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    return browser_view->GetLocationBar();
  }
  LocationBarView* location_bar_view() {
    auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    return browser_view->toolbar()->location_bar_view();
  }
  OmniboxView* omnibox_view() { return location_bar()->GetOmniboxView(); }
  OmniboxViewViews* omnibox_view_views() {
    return location_bar_view() ? location_bar_view()->omnibox_view() : nullptr;
  }
  OmniboxController* controller() {
    return location_bar()->GetOmniboxController();
  }
  OmniboxEditModel* edit_model() {
    return location_bar()->GetOmniboxController()->edit_model();
  }

  void SetUpInProcessBrowserTestFixture() override;

 private:
  static std::unique_ptr<KeyedService> BuildAimEligibilityService(
      content::BrowserContext* context);

 protected:
  AppliedTestContext<AppliedBrowserObservers> applied_;
  AppliedOwnerToken classic_;
  AppliedOwnerToken aim_;

 public:
  void TearDownOnMainThread() override {
    applied_.Stop();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  AppliedCondition Omnibox(ViewStateMatches expected,
                           AppliedMatch match,
                           bool aim = false) {
    auto& owner = aim ? aim_ : classic_;
    if (owner.is_empty())
      owner = aim ? applied_.source().ObserveOmniboxAim(*location_bar_view())
                  : applied_.source().ObserveOmnibox(*location_bar_view());
    return applied_.Condition(owner, expected, match);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
  base::CallbackListSubscription create_services_subscription_;
};

YeeOmniboxPopupViewWebUITest::YeeOmniboxPopupViewWebUITest() {
  feature_list_.InitWithFeatures(
      {omnibox::internal::kWebUIOmniboxPopup,
       omnibox::internal::kWebUIOmniboxAimPopup,
       omnibox::kOmniboxAimDeferShowUntilVisualStateReady},
      {});
}
YeeOmniboxPopupViewWebUITest::~YeeOmniboxPopupViewWebUITest() = default;

void YeeOmniboxPopupViewWebUITest::CreatePopupForTestQuery() {
  auto* autocomplete_controller = controller()->autocomplete_controller();
  ASSERT_TRUE(applied_.Run({Omnibox({}, AppliedMatch::kOmniboxIdle)},
                           [&] { controller()->StopAutocomplete(true); }));

  // Verify that the popup state manager callback is called when popup opens.
  UNCALLED_MOCK_CALLBACK(
      base::RepeatingCallback<void(OmniboxPopupState, OmniboxPopupState)>,
      popup_callback);
  const auto subscription =
      controller()->popup_state_manager()->AddPopupStateChangedCallback(
          popup_callback.Get());

  ASSERT_TRUE(applied_.Run(
      {Omnibox({.popup_open = true}, AppliedMatch::kOmniboxPopup)}, [&] {
        EXPECT_CALL_IN_SCOPE(
            popup_callback,
            Run(OmniboxPopupState::kNone, OmniboxPopupState::kClassic), {
              edit_model()->SetUserText(u"foo");
              AutocompleteInput input(
                  u"foo", metrics::OmniboxEventProto::BLANK,
                  ChromeAutocompleteSchemeClassifier(browser()->GetProfile()));
              input.set_omit_asynchronous_matches(true);
              autocomplete_controller->Start(input);

              EXPECT_FALSE(autocomplete_controller->result().empty());
              EXPECT_TRUE(controller()->IsPopupOpen());
            });
      }));
}

void YeeOmniboxPopupViewWebUITest::SetUpInProcessBrowserTestFixture() {
  InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
  // 157은 eligible 프로필에서만 presenter를 생성한다. UI 검사는 해당 조건을
  // 준비한다.
  create_services_subscription_ =
      BrowserContextDependencyManager::GetInstance()
          ->RegisterCreateServicesCallbackForTesting(
              base::BindRepeating([](content::BrowserContext* context) {
                AimEligibilityServiceFactory::GetInstance()->SetTestingFactory(
                    context,
                    base::BindRepeating(&YeeOmniboxPopupViewWebUITest::
                                            BuildAimEligibilityService));
              }));
}

std::unique_ptr<KeyedService>
YeeOmniboxPopupViewWebUITest::BuildAimEligibilityService(
    content::BrowserContext* context) {
  auto service = std::make_unique<testing::NiceMock<MockAimEligibilityService>>(
      *Profile::FromBrowserContext(context)->GetPrefs(),
      /*template_url_service=*/nullptr,
      /*url_loader_factory=*/nullptr, /*identity_manager=*/nullptr,
      AimEligibilityService::Configuration{});
  ON_CALL(*service, IsAimEligible()).WillByDefault(testing::Return(true));
  ON_CALL(*service, IsFuseboxEligible()).WillByDefault(testing::Return(true));
  ON_CALL(*service, GetLocaleImpl()).WillByDefault(testing::Return("en-US"));
  return service;
}

class YeeOmniboxPopupViewWebUIFullV2Test : public YeeOmniboxPopupViewWebUITest {
 public:
  YeeOmniboxPopupViewWebUIFullV2Test() {
    // interactive_ui_tests sets `ui_test_utils::BringBrowserWindowToFront()`
    // for the setup function by default, which causes timeouts on Windows bots.
    // Unset it here as this test does not strictly require the window to be in
    // front to verify state isolation.
    set_global_browser_set_up_function(nullptr);
    feature_list_full_v2_.InitWithFeatures(
        {omnibox::internal::kWebUIOmniboxPopup,
         omnibox::internal::kWebUIOmniboxFullPopup},
        {});
  }

 private:
  base::test::ScopedFeatureList feature_list_full_v2_;
};

class YeeOmniboxPopupAimShadowGeometryTest
    : public YeeOmniboxPopupViewWebUIFullV2Test,
      public testing::WithParamInterface<bool> {
 public:
  YeeOmniboxPopupAimShadowGeometryTest() {
    shadow_feature_.InitWithFeatureState(omnibox::kOmniboxAimWebUIShadow,
                                         GetParam());
  }

 private:
  base::test::ScopedFeatureList shadow_feature_;
};

INSTANTIATE_TEST_SUITE_P(Shadow,
                         YeeOmniboxPopupAimShadowGeometryTest,
                         testing::Bool());

IN_PROC_BROWSER_TEST_P(YeeOmniboxPopupAimShadowGeometryTest,
                       YeeAimShadowFitsWidgetAndKeepsHostTransparent) {
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(location_bar()->IsCompactShellMode());
  controller()->StopAutocomplete(/*clear_result=*/true);
  auto* const presenter_delegate = location_bar()->GetPresenterDelegate();
  ASSERT_TRUE(presenter_delegate);
  auto* const presenter = presenter_delegate->GetOmniboxPopupAimPresenter();
  ASSERT_TRUE(presenter);
  ASSERT_EQ(GetParam(), presenter->ShouldDrawShadowInWebUI());
  auto* const state = controller()->popup_state_manager();
  ASSERT_TRUE(applied_.Run(
      {Omnibox({.visible = true}, AppliedMatch::kViewVisible, true)},
      [&] { state->SetPopupState(OmniboxPopupState::kAim); }));
  ASSERT_EQ(OmniboxPopupState::kAim, state->popup_state());
  auto* const content = presenter->GetWebUIContent();
  ASSERT_TRUE(content);
  auto* const web_contents = content->GetWebContents();
  ASSERT_TRUE(web_contents);
  ASSERT_TRUE(content::WaitForLoadStop(web_contents));
  ASSERT_TRUE(content::ExecJs(web_contents, R"JS(
    (async () => {
      await customElements.whenDefined('omnibox-aim-app');
      const app = document.querySelector('omnibox-aim-app');
      await app.updateComplete;
      await app.shadowRoot.querySelector('#composebox').updateComplete;
    })();
  )JS"));
  auto* const popup_widget = presenter->get_widget_for_testing();
  ASSERT_TRUE(popup_widget);
  base::test::TestFuture<bool> dom_installed;
  const auto document = applied_.source().InstallWebUiObserver(
      *web_contents->GetPrimaryMainFrame(),
      {.shadow_hosts = {"omnibox-aim-app", "#composebox"},
       .root = "#composebox",
       .rows = "#inputContainer"},
      dom_installed.GetCallback());
  ASSERT_TRUE(dom_installed.Get());
  const auto popup_bounds =
      applied_.source().ObserveWebUiPopupBounds(document, *content);
  const AppliedConditions popup_geometry{
      applied_.Condition(
          popup_bounds,
          WebUiStateMatches{.document = document, .popup_rows = {1}},
          AppliedMatch::kWebUiRowsFitNativeBounds),
      applied_.Widget(*popup_widget, {.visible = true},
                      AppliedMatch::kWidgetVisible),
      applied_.Widget(*popup_widget, {.opacity = 1},
                      AppliedMatch::kWidgetOpacity),
      applied_.View(*popup_widget->GetRootView(), {.drawn = true},
                    AppliedMatch::kViewDrawn),
      Omnibox({}, AppliedMatch::kOmniboxTransitionComplete, true)};
  // FullV2의 전환 해제는 View 알림을 보내지 않는다. pinned producer의
  // 실제 지연 task 완료만 관측해 flag 해제 뒤 팝업 commit을 한 번 요청한다.
  class PopupTransitionTaskObserver final : public base::TaskObserver {
   public:
    PopupTransitionTaskObserver(LocationBarView& location, views::Widget& popup)
        : location_(&location), popup_(popup.GetWeakPtr()) {
      base::CurrentThread::Get()->AddTaskObserver(this);
    }
    ~PopupTransitionTaskObserver() override {
      base::CurrentThread::Get()->RemoveTaskObserver(this);
    }
    void WillProcessTask(const base::PendingTask&, bool) override {}
    void DidProcessTask(const base::PendingTask& task) override {
      if (notified_ || task.delayed_run_time.is_null() ||
          !task.posted_from.has_source_info() ||
          std::string_view(task.posted_from.function_name()) !=
              "OnPopupStateChanged" ||
          !std::string_view(task.posted_from.file_name())
               .ends_with("location_bar_view.cc"))
        return;
      auto* location = static_cast<LocationBarView*>(location_.view());
      if (!location || location->in_popup_state_transition() || !popup_ ||
          !popup_->GetCompositor())
        return;
      notified_ = true;
      popup_->GetCompositor()->ScheduleFullRedraw();
    }

   private:
    views::ViewTracker location_;
    base::WeakPtr<views::Widget> popup_;
    bool notified_ = false;
  } transition_observer(*location_bar_view(), *popup_widget);
#if BUILDFLAG(IS_MAC)
  {
    // Mac 팝업은 P2 fix10처럼 자신의 commit에서 현재 DOM·native 배치를
    // 확인한다. 표시 전 bounds를 고정하거나 성공 presentation으로 해석하지
    // 않으며, dispatch 뒤 commit과 완료 시점 모두 준비 조건을 만족해야 한다.
    const auto ticket =
        applied_.Begin(popup_geometry, AppliedDomain::kFrame, base::Seconds(5));
    DispatchDone popup_committed;
    std::vector<AppliedSignalKey> geometry_keys;
    for (const auto& condition : popup_geometry)
      geometry_keys.push_back(
          {condition.owner, GetAppliedConditionKind(condition.expected)});
    auto commit_subscription = applied_.views().SubscribeCompositorFrames(
        *popup_widget, std::move(geometry_keys),
        base::BindLambdaForTesting([&](const AppliedSignalSample& sample) {
          if (!popup_committed)
            return;
          for (const auto& condition : popup_geometry) {
            const auto snapshot =
                std::ranges::find_if(sample.snapshots, [&](const auto& value) {
                  return value.owner == condition.owner &&
                         GetAppliedConditionKind(value.value) ==
                             GetAppliedConditionKind(condition.expected);
                });
            if (snapshot == sample.snapshots.end() ||
                snapshot->coordinates != condition.coordinates ||
                !AppliedValueMatches(condition, snapshot->value))
              return;
          }
          std::move(popup_committed).Run(AppliedDispatchStatus::kAccepted);
        }));
    const auto dispatch_error = applied_.session().DispatchAsync(
        ticket, base::BindLambdaForTesting([&](DispatchDone done) {
          popup_committed = std::move(done);
          popup_widget->LayoutRootViewIfNecessary();
          popup_widget->GetCompositor()->ScheduleFullRedraw();
        }));
    ASSERT_FALSE(dispatch_error)
        << (dispatch_error ? dispatch_error->ToString() : "");
    ASSERT_TRUE(applied_.Wait(ticket, popup_geometry));
  }
#else
  auto presented_popup = popup_geometry;
  presented_popup.push_back(applied_.views().ObservePresentedAfterApply(
      *popup_widget->GetRootView(), popup_geometry));
  ASSERT_TRUE(applied_.Run(
      std::move(presented_popup),
      [&] { popup_widget->LayoutRootViewIfNecessary(); }, AppliedDomain::kFrame,
      base::Seconds(5)));
#endif

  // CSS의 실제 padding과 native 크기 산정이 같은 그림자 계약을 사용해야 한다.
  const gfx::Insets shadow_insets =
      RoundedOmniboxResultsFrame::GetShadowInsets(!GetParam());
  EXPECT_EQ(GetParam() ? shadow_insets.width() : 0,
            content::EvalJs(web_contents, R"JS(
    (() => {
      const style = getComputedStyle(document.body);
      return parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
    })();
  )JS")
                .ExtractInt());
  EXPECT_EQ(GetParam() ? shadow_insets.height() : 0,
            content::EvalJs(web_contents, R"JS(
    (() => {
      const style = getComputedStyle(document.body);
      return parseFloat(style.paddingTop) + parseFloat(style.paddingBottom);
    })();
  )JS")
                .ExtractInt());

  constexpr int kContentHeight = 400;
  // WebUI 대기 중 팝업이 닫혔다면 크기 검사 전에 실제 상태로 실패한다.
  ASSERT_EQ(OmniboxPopupState::kAim, state->popup_state());
  presenter->OnContentHeightChanged(kContentHeight +
                                    (GetParam() ? shadow_insets.height() : 0));
  auto* const widget = presenter->get_widget_for_testing();
  ASSERT_TRUE(widget);
  ASSERT_TRUE(widget->IsVisible());
  widget->LayoutRootViewIfNecessary();
  gfx::Rect expected_bounds = location_bar()->BoundsInScreen();
  expected_bounds.Inset(
      -RoundedOmniboxResultsFrame::GetLocationBarAlignmentInsets(true));
  expected_bounds.set_height(kContentHeight);
  expected_bounds.Inset(-shadow_insets);
  EXPECT_EQ(expected_bounds, widget->GetWindowBoundsInScreen());

  auto* const frame =
      static_cast<RoundedOmniboxResultsFrame*>(presenter->GetOuterView());
  ASSERT_TRUE(frame);
  ASSERT_TRUE(frame->GetContents());
  auto* const host = frame->GetContents()->parent();
  ASSERT_TRUE(host);
  EXPECT_EQ(!GetParam(), host->background() != nullptr);
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  const int rebuilds =
      presenter->GetCompactShellProviderRebuildCountForTesting();
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(
          aim_,
          PresentationMatches{.presentation_id =
                                  static_cast<uint64_t>(rebuilds)},
          AppliedMatch::kPresentationRevisionChanged)},
      [&] {
        theme->SetBrowserColorScheme(
            original_scheme == ThemeService::BrowserColorScheme::kDark
                ? ThemeService::BrowserColorScheme::kLight
                : ThemeService::BrowserColorScheme::kDark);
      }));
  presenter->RefreshCompactShellSurface();
  ASSERT_EQ(frame, presenter->GetOuterView());
  ASSERT_TRUE(frame->GetContents());
  auto* const rebuilt_host = frame->GetContents()->parent();
  ASSERT_TRUE(rebuilt_host);
  EXPECT_EQ(!GetParam(), rebuilt_host->background() != nullptr);
  EXPECT_EQ(OmniboxPopupState::kAim, state->popup_state());
  frame->SetCompactShellSurfaceColor(std::nullopt);
  EXPECT_EQ(!GetParam(), rebuilt_host->background() != nullptr);
  presenter->Hide();
  theme->SetBrowserColorScheme(original_scheme);
}

IN_PROC_BROWSER_TEST_F(YeeOmniboxPopupViewWebUITest,
                       HiddenWidgetClearsClassicPopupState) {
  CreatePopupForTestQuery();
  auto* popup_view = static_cast<OmniboxPopupViewWebUI*>(
      location_bar()->GetOmniboxPopupView());
  ASSERT_TRUE(popup_view);
  ASSERT_EQ(controller()->popup_state_manager()->popup_state(),
            OmniboxPopupState::kClassic);

  views::Widget* const popup_widget =
      popup_view->presenter()->get_widget_for_testing();
  ASSERT_TRUE(popup_widget);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup_widget, {.visible = true},
                       AppliedMatch::kWidgetVisible)},
      [] {}, AppliedDomain::kWidget, TestTimeouts::action_timeout(), true));

  // Simulate the window system hiding a child popup without going through
  // UpdatePopupAppearance(), as can happen while restoring multiple windows or
  // when the browser window loses activation on macOS.
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup_widget, {.visible = false},
                       AppliedMatch::kWidgetVisible),
       Omnibox({.popup_open = false}, AppliedMatch::kOmniboxPopup)},
      [&] { popup_widget->Hide(); }));

  EXPECT_FALSE(popup_widget->IsVisible());
  EXPECT_EQ(controller()->popup_state_manager()->popup_state(),
            OmniboxPopupState::kNone);
  EXPECT_FALSE(popup_view->IsOpen());
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(
    YeeOmniboxPopupViewWebUITest,
    MultiWindowActivationRestartsAutocompleteWithoutStaleState) {
  ASSERT_TRUE(omnibox_view_views());
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  // 창 비활성화의 OnBlur 경로를 검사하려면 실제 주소 입력 focus가 필요하다.
  location_bar()->FocusLocation(/*is_user_initiated=*/true,
                                /*clear_focus_if_failed=*/false);
  ASSERT_TRUE(omnibox_view_views()->HasFocus());
  CreatePopupForTestQuery();
  auto* popup_view = static_cast<OmniboxPopupViewWebUI*>(
      location_bar()->GetOmniboxPopupView());
  ASSERT_TRUE(popup_view);

  views::Widget* const popup_widget =
      popup_view->presenter()->get_widget_for_testing();
  ASSERT_TRUE(popup_widget);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup_widget, {.visible = true},
                       AppliedMatch::kWidgetVisible)},
      [] {}, AppliedDomain::kWidget, TestTimeouts::action_timeout(), true));
  ASSERT_EQ(controller()->popup_state_manager()->popup_state(),
            OmniboxPopupState::kClassic);
  ASSERT_TRUE(omnibox_view_views()->HasFocus());
  auto* const first_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();

  BrowserWindowInterface* second_browser = nullptr;
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*first_widget, {.active = false},
                       AppliedMatch::kWidgetActive),
       applied_.Widget(*popup_widget, {.visible = false},
                       AppliedMatch::kWidgetVisible)},
      [&] {
        second_browser = CreateBrowser(browser()->GetProfile());
        ASSERT_TRUE(second_browser);
        ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(second_browser));
      }));
  EXPECT_FALSE(omnibox_view_views()->HasFocus());
  EXPECT_TRUE(controller()->autocomplete_controller()->result().empty());
  EXPECT_EQ(controller()->popup_state_manager()->popup_state(),
            OmniboxPopupState::kNone);
  EXPECT_FALSE(controller()->IsPopupOpen());

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  location_bar()->FocusLocation(/*is_user_initiated=*/true,
                                /*clear_focus_if_failed=*/false);
  edit_model()->SetUserText(u"after window activation");
  AutocompleteInput input(
      u"after window activation", metrics::OmniboxEventProto::BLANK,
      ChromeAutocompleteSchemeClassifier(browser()->GetProfile()));
  input.set_omit_asynchronous_matches(true);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*popup_widget, {.visible = true},
                       AppliedMatch::kWidgetVisible)},
      [&] { controller()->autocomplete_controller()->Start(input); }));
  EXPECT_EQ(controller()->popup_state_manager()->popup_state(),
            OmniboxPopupState::kClassic);
  EXPECT_TRUE(controller()->IsPopupOpen());
}

#endif

IN_PROC_BROWSER_TEST_F(YeeOmniboxPopupViewWebUITest,
                       YeeAimPopupOpensAndDismissesAfterClosedThemeChanges) {
#if BUILDFLAG(IS_WIN)
  auto* const browser_widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  const auto original_z_order = browser_widget->GetZOrderLevel();
  browser_widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
#endif
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(location_bar()->IsCompactShellMode());
  auto* const presenter_delegate = location_bar()->GetPresenterDelegate();
  ASSERT_TRUE(presenter_delegate);
  auto* const presenter = presenter_delegate->GetOmniboxPopupAimPresenter();
  ASSERT_TRUE(presenter);
  controller()->StopAutocomplete(/*clear_result=*/true);
  auto* const state = controller()->popup_state_manager();
  ASSERT_EQ(OmniboxPopupState::kNone, state->popup_state());

  // 초기 예열과 닫힌 상태의 테마 갱신 모두 실제 닫힘으로 오인하지 않아야 한다.
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  for (int close_method : {0, 1, 2}) {
    SCOPED_TRACE(close_method);
    theme->SetBrowserColorScheme(
        close_method == 1 ? ThemeService::BrowserColorScheme::kDark
                          : ThemeService::BrowserColorScheme::kLight);
    ASSERT_TRUE(applied_.WaitForPresentedFrame(
        *BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()));
    ASSERT_TRUE(applied_.Run(
        {Omnibox({.presenter_shown = true}, AppliedMatch::kOmniboxShown, true)},
        [&] { state->SetPopupState(OmniboxPopupState::kAim); }));
    ASSERT_EQ(OmniboxPopupState::kAim, state->popup_state());
    auto* const content =
        static_cast<OmniboxAimPopupWebUIContent*>(presenter->GetWebUIContent());
    ASSERT_TRUE(content::WaitForLoadStop(content->GetWebContents()));
    ASSERT_TRUE(applied_.Run(
        {Omnibox({}, AppliedMatch::kOmniboxTransitionComplete, true)}, [] {},
        AppliedDomain::kWidget, TestTimeouts::action_timeout(), true));
    // IsShown()은 지연 표시 요청 중에도 참이다. 실제 팝업 창이 입력을 받을
    // 수 있을 때까지 기다려 이전 브라우저 창으로 Esc가 전달되지 않게 한다.
    ASSERT_TRUE(applied_.Run(
        {applied_.Widget(*presenter->get_widget_for_testing(),
                         {.visible = true}, AppliedMatch::kWidgetVisible),
         applied_.Widget(*presenter->get_widget_for_testing(), {.active = true},
                         AppliedMatch::kWidgetActive)},
        [] {}, AppliedDomain::kWidget, TestTimeouts::action_timeout(), true));
    ASSERT_TRUE(applied_.Run(
        {Omnibox({.presenter_shown = false}, AppliedMatch::kOmniboxShown,
                 true)},
        [&] {
          if (close_method == 0) {
            ASSERT_TRUE(content::ExecJs(content->GetWebContents(), R"JS(
    (async () => {
      await customElements.whenDefined('omnibox-aim-app');
      const app = document.querySelector('omnibox-aim-app');
      await app.updateComplete;
      const box = app.shadowRoot.querySelector('#composebox');
      await box.updateComplete;
      const input = box.shadowRoot.querySelector('#composeboxInput');
      await input.updateComplete;
      input.shadowRoot.querySelector('#cancelIcon').click();
    })();
      )JS"));
          } else if (close_method == 1) {
            ASSERT_TRUE(ui_test_utils::SendKeyPressToWindowSync(
                presenter->GetWidget()->GetNativeWindow(), ui::VKEY_ESCAPE,
                false, false, false, false));
          } else {
            auto* const browser_view =
                BrowserView::GetBrowserViewForBrowser(browser());
            const auto bounds = browser_view->GetBoundsInScreen();
            const auto window = browser_view->GetWidget()->GetNativeWindow();
            ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
                gfx::Point(bounds.CenterPoint().x(), bounds.bottom() - 60),
                window));
            ASSERT_TRUE(ui_controls::SendMouseEvents(
                ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
                ui_controls::kNoAccelerator, window));
          }
        }));
    EXPECT_EQ(OmniboxPopupState::kNone, state->popup_state());
  }
  theme->SetBrowserColorScheme(original_scheme);
#if BUILDFLAG(IS_WIN)
  browser_widget->SetZOrderLevel(original_z_order);
#endif
}

#endif
