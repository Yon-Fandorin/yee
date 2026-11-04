// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/numerics/clamped_math.h"
#include "base/numerics/ranges.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_edit_model.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_view.h"
#include "chrome/browser/ui/omnibox/omnibox_theme.h"
#include "chrome/browser/ui/side_panel/side_panel_action_callback.h"
#include "chrome/browser/ui/side_panel/side_panel_enums.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/split_tab_highlight_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/toolbar/bookmark_sub_menu_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bar_view.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_button.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_outline.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/custom_floating_corner.h"
#include "chrome/browser/ui/views/frame/multi_contents_background_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_drop_target_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_drop_target_controller.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/global_media_controls/media_toolbar_button_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/location_bar/location_icon_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_presenter_base.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_view_views.h"
#include "chrome/browser/ui/views/omnibox/omnibox_popup_webui_base_content.h"
#include "chrome/browser/ui/views/omnibox/omnibox_result_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_row_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/omnibox/rounded_omnibox_results_frame.h"
#include "chrome/browser/ui/views/page_info/page_info_main_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/test/split_view_interactive_test_mixin.h"
#include "chrome/browser/ui/views/test/tab_strip_interactive_test_mixin.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/ui/views/yee/yee_colors.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/prefs/pref_service.h"
#include "components/security_state/core/security_state.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_collection.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_devtools_protocol_client.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "ui/actions/actions.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/interaction/state_observer.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/display/screen.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event_modifiers.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_delegate.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/snapshot/snapshot_aura.h"
#endif

namespace {
void CaptureYeeWidgetForTesting(views::Widget* widget,
                                const base::FilePath& path) {
  widget->LayoutRootViewIfNecessary();
  auto* compositor = widget->GetCompositor();
  compositor->ScheduleFullRedraw();
  ASSERT_TRUE(ui::WaitForNextFrameToBePresented(compositor, base::Seconds(5)));
#if BUILDFLAG(IS_WIN)
  // 실제 Aura 합성 출력을 읽는다. 캡처 실행에는
  // --enable-pixel-output-in-tests가 필요하며, GL 그리기가 없는 결과는 시각
  // 검증 근거로 사용하지 않는다.
  const gfx::Rect bounds(widget->GetWindowBoundsInScreen().size());
  base::test::TestFuture<gfx::Image> snapshot;
  ui::GrabWindowSnapshotAura(widget->GetNativeWindow(), bounds,
                             snapshot.GetCallback());
  const auto image = snapshot.Take();
  ASSERT_FALSE(image.IsEmpty());
  const auto* bitmap = image.ToSkBitmap();
  bool varied = false;
  for (int y = 0; y < bitmap->height() && !varied; ++y) {
    for (int x = 0; x < bitmap->width(); ++x) {
      if (bitmap->getColor(x, y) != bitmap->getColor(0, 0)) {
        varied = true;
        break;
      }
    }
  }
  ASSERT_TRUE(varied) << "A uniform frame is not visual validation evidence.";
  auto png = image.As1xPNGBytes();
#else
  const gfx::Rect bounds(widget->GetWindowBoundsInScreen().size());
  base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
  ui::GrabWindowSnapshotAsPNG(widget->GetNativeWindow(), bounds,
                              snapshot.GetCallback());
  auto png = snapshot.Take();
#endif
  ASSERT_TRUE(png && png->size());
  ASSERT_TRUE(
      base::WriteFile(path, static_cast<base::span<const uint8_t>>(*png)));
}

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kNewTab);
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kSecondTab);
}  // namespace

namespace {

class YeeContentsViewUiTest
    : public SplitViewInteractiveTestMixin<
          TabStripInteractiveTestMixin<InteractiveBrowserTest>> {
 public:
  YeeContentsViewUiTest() = default;

  void SetUpOnMainThread() override {
    SplitViewInteractiveTestMixin::SetUpOnMainThread();
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kTabSearchPinnedToTabstrip, true);
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  void TearDownOnMainThread() override {
    browser()->GetProfile()->GetPrefs()->ClearPref(
        prefs::kTabSearchPinnedToTabstrip);
    SplitViewInteractiveTestMixin::TearDownOnMainThread();
  }

 protected:
  LocationBarView* location_bar() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->toolbar()
        ->location_bar_view();
  }

  yee::BrowserSurfaceColorController* YeeSurfaceSourceForTab(int index) {
    return yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
        tab_strip_model()->GetWebContentsAt(index));
  }

  void SetYeeSurfaceColorForTab(int index, SkColor color) {
    YeeSurfaceSourceForTab(index)->SetPageSurfaceColorForTesting(color);
  }

  bool YeeToolbarButtonMatchesPresentation(
      ToolbarButton* button,
      const yee::BrowserSurfacePresentation& presentation) {
    if (!button) {
      return false;
    }
    const auto resolve_model_color =
        [button](views::Button::ButtonState state) -> std::optional<SkColor> {
      const std::optional<ui::ImageModel>& model = button->GetImageModel(state);
      if (!model.has_value() || !model->IsVectorIcon()) {
        return std::nullopt;
      }
      return model->GetVectorIcon().color().ResolveToSkColor(
          button->GetColorProvider());
    };
    return resolve_model_color(views::Button::STATE_NORMAL) ==
               presentation.primary &&
           resolve_model_color(views::Button::STATE_DISABLED) ==
               presentation.disabled;
  }

  bool YeeRestingTextViewMatchesPresentation(
      views::View* resting,
      const yee::BrowserSurfacePresentation& presentation) {
    if (!resting || resting->children().size() != 3) {
      return false;
    }
    auto* const origin =
        views::AsViewClass<views::Label>(resting->children()[0]);
    auto* const title =
        views::AsViewClass<views::Label>(resting->children()[2]);
    return origin && title &&
           origin->GetBackgroundColor() == presentation.surface &&
           title->GetBackgroundColor() == presentation.surface &&
           origin->GetEnabledColor() == presentation.primary &&
           title->GetEnabledColor() == presentation.secondary;
  }

  bool YeeRestingTextMatchesPresentation(
      MultiContentsViewMiniToolbar* header,
      const yee::BrowserSurfacePresentation& presentation) {
    return header && YeeRestingTextViewMatchesPresentation(
                         header->yee_resting_text_for_testing(), presentation);
  }

  bool YeeAdjacentOmniboxLabelsMatchColors(SkColor background,
                                           SkColor ime,
                                           SkColor additional,
                                           SkColor ai_hint) {
    constexpr std::u16string_view kImeCompletion = u"주소완성";
    LocationBarView* const view = location_bar();
    view->SetImeInlineAutocompletion(kImeCompletion);
    views::Label* const ime_label =
        view->GetImeInlineAutocompleteViewForTesting();
    views::Label* const additional_label =
        view->GetOmniboxAdditionalTextViewForTesting();
    views::Label* const ai_hint_label = view->GetAiModeHintLabelForTesting();
    const bool matches = ime_label && additional_label && ai_hint_label &&
                         ime_label->GetVisible() &&
                         ime_label->GetText() == kImeCompletion &&
                         ime_label->GetBackgroundColor() == background &&
                         additional_label->GetBackgroundColor() == background &&
                         ai_hint_label->GetBackgroundColor() == background &&
                         ime_label->GetEnabledColor() == ime &&
                         additional_label->GetEnabledColor() == additional &&
                         ai_hint_label->GetEnabledColor() == ai_hint;
    view->SetImeInlineAutocompletion(u"");
    return matches;
  }

  bool YeePopupBoundsMatchLocationBar(views::Widget* popup_widget) {
    if (!popup_widget) {
      return false;
    }
    gfx::Rect expected_bounds = location_bar()->GetBoundsInScreen();
    expected_bounds.Inset(
        -RoundedOmniboxResultsFrame::GetLocationBarAlignmentInsets(
            /*compact_shell_alignment=*/true));
    expected_bounds.Inset(-RoundedOmniboxResultsFrame::GetShadowInsets(
        /*compact_shell_alignment=*/true));
    const gfx::Rect popup_bounds = popup_widget->GetWindowBoundsInScreen();
    return popup_bounds.x() == expected_bounds.x() &&
           popup_bounds.y() == expected_bounds.y() &&
           popup_bounds.right() == expected_bounds.right();
  }

  bool YeePaneHeaderMatchesPresentation(
      MultiContentsViewMiniToolbar* header,
      const yee::BrowserSurfacePresentation& presentation) {
    return header && header->background() &&
           header->background()->color().ResolveToSkColor(
               header->GetColorProvider()) == presentation.surface &&
           YeeToolbarButtonMatchesPresentation(
               header->yee_back_button_for_testing(), presentation) &&
           YeeRestingTextMatchesPresentation(header, presentation);
  }

  views::Widget* OpenYeeOmniboxPopup(
      const std::u16string& text = u"yee header regression") {
    LocationBarView* const location_bar_view = location_bar();
    location_bar_view->FocusLocation(/*is_user_initiated=*/false,
                                     /*clear_focus_if_failed=*/false);
    OmniboxController* const controller =
        location_bar_view->GetOmniboxController();
    controller->edit_model()->SetUserText(text);
    AutocompleteInput input(
        text, metrics::OmniboxEventProto::BLANK,
        ChromeAutocompleteSchemeClassifier(browser()->GetProfile()));
    input.set_omit_asynchronous_matches(true);
    controller->StartAutocomplete(input);
    base::RunLoop().RunUntilIdle();

    OmniboxPopupView* const popup_view =
        location_bar_view->GetOmniboxPopupView();
    if (!popup_view || !controller->IsPopupOpen()) {
      return nullptr;
    }
    if (OmniboxPopupPresenterBase* const presenter = popup_view->presenter()) {
      return presenter->get_widget_for_testing();
    }
    auto* const views_popup = static_cast<OmniboxPopupViewViews*>(popup_view);
    views_popup->UpdatePopupAppearance();
    return views_popup->GetWidget();
  }

  views::Widget* YeeOmniboxPopupWidget(OmniboxPopupView* popup_view) {
    if (!popup_view) {
      return nullptr;
    }
    if (OmniboxPopupPresenterBase* const presenter = popup_view->presenter()) {
      return presenter->get_widget_for_testing();
    }
    return static_cast<OmniboxPopupViewViews*>(popup_view)->GetWidget();
  }

  int YeeOmniboxPopupProviderRebuildCount(OmniboxPopupView* popup_view) {
    if (!popup_view) {
      return 0;
    }
    if (OmniboxPopupPresenterBase* const presenter = popup_view->presenter()) {
      return presenter->GetCompactShellProviderRebuildCountForTesting();
    }
    return static_cast<OmniboxPopupViewViews*>(popup_view)
        ->GetCompactShellProviderRebuildCountForTesting();
  }

  void CloseYeeOmniboxPopup() {
    LocationBarView* const location_bar_view = location_bar();
    location_bar_view->GetOmniboxController()->StopAutocomplete(
        /*clear_result=*/true);
    if (OmniboxPopupView* const popup_view =
            location_bar_view->GetOmniboxPopupView()) {
      popup_view->UpdatePopupAppearance();
      if (OmniboxPopupPresenterBase* const presenter =
              popup_view->presenter()) {
        presenter->Hide();
      }
    }
  }

  bool YeeOmniboxPopupMatchesLocationBar() {
    LocationBarView* const location_bar_view = location_bar();
    const std::optional<SkColor> popup_surface =
        location_bar_view->GetCompactShellPopupSurfaceColor();
    if (!location_bar_view->IsCompactShellMode() ||
        !popup_surface.has_value()) {
      return false;
    }

    views::Widget* const popup_widget = OpenYeeOmniboxPopup();
    if (!popup_widget) {
      return false;
    }
    return YeePopupBoundsMatchLocationBar(popup_widget) &&
           popup_widget->GetColorProvider()->GetColor(
               kColorOmniboxResultsBackground) == *popup_surface;
  }
};

class YeeMultiContentsViewUiTest
    : public YeeContentsViewUiTest,
      public ::testing::WithParamInterface<split_tabs::SplitTabLayout> {
 protected:
  GURL GetTestUrl() { return embedded_test_server()->GetURL("/title1.html"); }

  auto EnterSplitView(int active_tab,
                      std::optional<int> other_tab = std::nullopt,
                      double ratio = 0.5) {
    return SplitViewInteractiveTestMixin::EnterSplitView(active_tab, other_tab,
                                                         GetParam(), ratio);
  }

  auto CreateTabsAndEnterSplitView() {
    auto result = Steps(
        AddInstrumentedTab(kNewTab, GURL(chrome::kChromeUISettingsURL), 0),
        CheckResult([=, this]() { return tab_strip_model()->count(); }, 2u),
        EnterSplitView(0, 1));
    AddDescriptionPrefix(result, "CreateTabsAndEnterSplitView()");
    return result;
  }

  // MultiContentsView의 Layout 재정의로 크기 변경이 다음 레이아웃까지 지연될 수
  // 있다. PollView와 WaitForState로 해당 레이아웃 적용을 기다린다.
  using MultiContentsViewLayoutObserver =
      views::test::PollingViewObserver<bool, MultiContentsView>;

  auto CheckResizeValues(
      base::RepeatingCallback<bool(double, double)> check,
      ui::test::StateIdentifier<MultiContentsViewLayoutObserver> observer_id) {
    auto result = Steps(
        PollView(observer_id, kMultiContentsViewElementId,
                 [check](const MultiContentsView* multi_contents_view) -> bool {
                   const gfx::Size& start_size =
                       multi_contents_view->start_contents_view_for_testing()
                           ->parent()
                           ->size();
                   const gfx::Size& end_size =
                       multi_contents_view->end_contents_view_for_testing()
                           ->parent()
                           ->size();
                   switch (multi_contents_view->GetSplitLayout()) {
                     case split_tabs::SplitTabLayout::kSideBySide:
                       return check.Run(start_size.width(), end_size.width());
                     case split_tabs::SplitTabLayout::kStacked:
                       return check.Run(start_size.height(), end_size.height());
                     default:
                       NOTREACHED();
                   }
                 }),
        WaitForState(observer_id, true), StopObservingState(observer_id));
    AddDescriptionPrefix(result, "CheckResizeValues()");
    return result;
  }

  auto CheckResizeWithId(
      int resize_amount,
      base::RepeatingCallback<bool(double, double)> check,
      ui::test::StateIdentifier<MultiContentsViewLayoutObserver> observer_id) {
    auto result = Steps(Do([resize_amount, this]() {
                          multi_contents_view()->OnResize(resize_amount, true);
                        }),
                        CheckResizeValues(check, observer_id));
    AddDescriptionPrefix(result, "CheckResizeWithId()");
    return result;
  }

  // 키보드로 크기를 바꾼 뒤 contents view 크기를 확인한다.
  auto CheckResizeKey(
      ui::KeyboardCode key_code,
      base::RepeatingCallback<bool(double, double)> check,
      ui::test::StateIdentifier<MultiContentsViewLayoutObserver> observer_id) {
    auto result = Steps(
        WithView(MultiContentsResizeHandle::kMultiContentsResizeHandleElementId,
                 [key_code](MultiContentsResizeHandle* handle) {
                   ui::KeyEvent press(ui::EventType::kKeyPressed, key_code,
                                      ui::EF_NONE);
                   handle->GetWidget()->OnKeyEvent(&press);
                   ui::KeyEvent release(ui::EventType::kKeyReleased, key_code,
                                        ui::EF_NONE);
                   handle->GetWidget()->OnKeyEvent(&release);
                 }),
        CheckResizeValues(check, observer_id));
    AddDescriptionPrefix(result, "CheckResizeKey()");
    return result;
  }

  // 분할 축을 따라 contents 크기를 바꾼다.
  auto ResizeContents(int size) {
    auto result = Steps(Do([size, this]() {
      BrowserView::GetBrowserViewForBrowser(browser())->SetContentsSize(
          GetParam() == split_tabs::SplitTabLayout::kSideBySide
              ? gfx::Size(size, 1000)
              : gfx::Size(1000, size));
    }));
    AddDescriptionPrefix(result, "ResizeContents()");
    return result;
  }

  // gfx::Rect에서 분할 축의 길이를 읽는다.
  auto GetSize(gfx::Rect rect) {
    return GetParam() == split_tabs::SplitTabLayout::kSideBySide
               ? rect.width()
               : rect.height();
  }

  // gfx::Size에서 분할 축의 길이를 읽는다.
  auto GetSize(gfx::Size size) {
    return GetParam() == split_tabs::SplitTabLayout::kSideBySide
               ? size.width()
               : size.height();
  }

  auto CheckActiveContentsHasFocus() {
    return CheckView(
        kMultiContentsViewElementId,
        [](MultiContentsView* multi_contents_view) -> bool {
          return multi_contents_view->GetActiveContentsView()->HasFocus();
        });
  }

  auto OpenContextualSidePanel() {
    using SidePanelViewObserver =
        views::test::PollingViewObserver<bool, SidePanel>;
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(SidePanelViewObserver,
                                        kSidePanelViewObserver);

    return Steps(Do(base::BindLambdaForTesting([this]() {
                   chrome::ExecuteCommandWithContext(
                       browser(), IDC_SHOW_CUSTOMIZE_CHROME_SIDE_PANEL,
                       actions::ActionInvocationContext::Builder()
                           .SetProperty(kSidePanelOpenTriggerKey,
                                        SidePanelOpenTrigger::kToolbarButton)
                           .Build());
                 })),
                 PollView(kSidePanelViewObserver, kSidePanelElementId,
                          [](const SidePanel* side_panel) -> bool {
                            return side_panel->width() > 0 &&
                                   side_panel->height() > 0;
                          }),
                 WaitForState(kSidePanelViewObserver, true));
  }
};

INSTANTIATE_TEST_SUITE_P(
    SplitTabLayout,
    YeeMultiContentsViewUiTest,
    testing::Values(split_tabs::SplitTabLayout::kSideBySide,
                    split_tabs::SplitTabLayout::kStacked));

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeSingleAndSplitHeadersShareLocationBarGeometry) {
  ASSERT_TRUE(yee::IsShellEnabled());
  using LocationBarRestoreObserver =
      views::test::PollingViewObserver<bool, LocationBarView>;
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(LocationBarRestoreObserver,
                                      kLocationBarRestored);
  int single_location_bar_center_y = 0;
  RunTestSequence(
      Check([&]() {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser());
        LocationBarView* location_bar =
            browser_view->toolbar()->location_bar_view();
        single_location_bar_center_y =
            location_bar->GetBoundsInScreen().CenterPoint().y();
        return location_bar->parent() == browser_view->toolbar() &&
               browser_view->toolbar()->height() ==
                   yee::kSidebarMetrics.toolbar_height &&
               location_bar->height() ==
                   yee::kSidebarMetrics.browser_surface_location_bar_height;
      }),
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0), Check([&]() {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser());
        MultiContentsViewMiniToolbar* const active_header =
            multi_contents_view()->mini_toolbar_for_testing(0);
        LocationBarView* location_bar =
            browser_view->toolbar()->location_bar_view();
        const int split_location_bar_center_y =
            location_bar->GetBoundsInScreen().CenterPoint().y();
        return split_location_bar_center_y >=
                   single_location_bar_center_y - 1 &&
               split_location_bar_center_y <=
                   single_location_bar_center_y + 1 &&
               location_bar->height() ==
                   yee::kSidebarMetrics.browser_surface_location_bar_height &&
               location_bar->parent() == active_header &&
               active_header->height() ==
                   yee::kSidebarMetrics.split_pane_header_height -
                       yee::kSidebarMetrics.split_pane_content_stroke_inset &&
               multi_contents_view()->mini_toolbar_for_testing(1)->height() ==
                   yee::kSidebarMetrics.split_pane_header_height -
                       yee::kSidebarMetrics.split_pane_content_stroke_inset;
      }),
      Do([&]() { tab_strip_model()->ActivateTabAt(1); }),
      WaitForActiveTabChange(1), Check([&]() {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser());
        return browser_view->toolbar()->location_bar_view()->parent() ==
               multi_contents_view()->mini_toolbar_for_testing(1);
      }),
      ExitSplitView(1), WaitForActiveTabChange(1),
      PollView(kLocationBarRestored, kLocationBarElementId,
               [&](const LocationBarView* location_bar) {
                 BrowserView* browser_view =
                     BrowserView::GetBrowserViewForBrowser(browser());
                 return location_bar->parent() == browser_view->toolbar() &&
                        location_bar->height() ==
                            yee::kSidebarMetrics
                                .browser_surface_location_bar_height &&
                        location_bar->GetBoundsInScreen().CenterPoint().y() ==
                            single_location_bar_center_y;
               }),
      WaitForState(kLocationBarRestored, true),
      StopObservingState(kLocationBarRestored));
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeRenderedBoundaryTracksDelayedCssWithoutMetadata) {
  RunTestSequence(CreateTabsAndEnterSplitView());
  auto* const contents = tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      contents,
      GURL("data:text/html,<style>html,body{margin:0;background:rgb(255,0,0)}"
           "[id=edge]{height:8px;background:rgb(20,40,60)}</style>"
           "<div id='edge'></div><p>Real compositor boundary fixture</p>")));
  auto* const source =
      yee::BrowserSurfaceColorController::GetOrCreateForWebContents(contents);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return source->GetColor() == SkColorSetRGB(20, 40, 60); }));
  EXPECT_EQ(source->GetColor(),
            location_bar()->GetCompactShellPopupSurfaceColor());
  const auto background = contents->GetBackgroundColor();
  const auto metadata = contents->GetThemeColor();
  // 탐색·스크롤·입력·메타데이터 변경 없이 초기 80/100-ms 안정화 샘플 이후에
  // 그린다.
  ASSERT_TRUE(content::ExecJs(contents, R"(
    new Promise(resolve => setTimeout(() => {
      document.getElementById('edge').style.background = 'rgb(80,100,120)';
      resolve();
    }, 1000));
  )"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return source->GetColor() == SkColorSetRGB(80, 100, 120); }));
  EXPECT_EQ(background, contents->GetBackgroundColor());
  EXPECT_EQ(metadata, contents->GetThemeColor());
  EXPECT_EQ(source->GetColor(),
            location_bar()->GetCompactShellPopupSurfaceColor());
  ASSERT_TRUE(content::ExecJs(contents, R"(
    const button = document.createElement('button');
    button.id = 'change-edge';
    button.textContent = 'Change only the boundary';
    button.onclick = () => {
      document.getElementById('edge').style.background = 'rgb(30,60,90)';
    };
    document.body.appendChild(button);
  )"));
  content::SimulateMouseClickOrTapElementWithId(contents, "change-edge");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return source->GetColor() == SkColorSetRGB(30, 60, 90); }));
  EXPECT_EQ(background, contents->GetBackgroundColor());
  EXPECT_EQ(metadata, contents->GetThemeColor());
  EXPECT_EQ(source->GetColor(),
            location_bar()->GetCompactShellPopupSurfaceColor());
}

IN_PROC_BROWSER_TEST_F(YeeContentsViewUiTest,
                       YeeMediaButtonTracksHeaderAndVisibility) {
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  tab_strip_model()->GetActiveWebContents()->Focus();
  auto* toolbar = BrowserView::GetBrowserViewForBrowser(browser())->toolbar();
  auto* media = views::AsViewClass<MediaToolbarButtonView>(
      toolbar->media_button()->GetBubbleAnchor().GetIfView());
  ASSERT_TRUE(media);
  const size_t media_index = toolbar->GetIndexOf(media).value();
  ASSERT_GT(media_index, 0u);
  auto* divider = toolbar->children()[media_index - 1].get();
  EXPECT_EQ(gfx::Size(1, yee::kSidebarMetrics.shell_control_separator_height),
            divider->GetPreferredSize());
  media->Hide();
  EXPECT_FALSE(divider->GetVisible());
  const auto capture_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-media-header-capture-dir");
  const auto capture = [&](const std::string& name) {
    if (capture_dir.empty()) {
      return;
    }
    CaptureYeeWidgetForTesting(toolbar->GetWidget(),
                               capture_dir.AppendASCII(name));
  };
  int surface_index = 0;
  for (SkColor surface : {SK_ColorWHITE, SK_ColorBLACK, SK_ColorRED}) {
    ASSERT_TRUE(content::ExecJs(
        tab_strip_model()->GetActiveWebContents(),
        content::JsReplace("document.body.style.background = $1;",
                           color_utils::SkColorToRgbaString(surface))));
    SetYeeSurfaceColorForTab(0, surface);
    const auto presentation = YeeSurfaceSourceForTab(0)->GetPresentation();
    ASSERT_TRUE(presentation);
    // 재생 전 숨겨진 버튼에도 변경된 색이 전달되어야 한다.
    media->Show();
    EXPECT_TRUE(divider->GetVisible());
    EXPECT_TRUE(YeeToolbarButtonMatchesPresentation(media, *presentation));
    EXPECT_EQ(presentation->resting_divider,
              divider->background()->color().ResolveToSkColor(
                  divider->GetColorProvider()));
    EXPECT_EQ(gfx::Size(yee::kSidebarMetrics.shell_control_size,
                        yee::kSidebarMetrics.shell_control_size),
              media->GetPreferredSize());
    RunScheduledLayouts();
    // native URL을 덮는 표면까지 실제 마우스 오버 경로로 검사한다.
    auto* resting = location_bar()->GetCompactShellRestingTextForTesting();
    ASSERT_TRUE(resting);
    ASSERT_TRUE(resting->GetVisible());
    EXPECT_TRUE(
        toolbar->IsPositionInWindowCaption(divider->bounds().CenterPoint()));
    // macOS의 tracking-area 입력이 실제 대상 NSView에 전달되도록 창을 지정한다.
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        location_bar()->GetBoundsInScreen().CenterPoint(),
        location_bar()->GetWidget()->GetNativeWindow()));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return location_bar()->GetBackgroundColorForTesting() ==
             presentation->location_hover;
    }));
    auto hovered = *presentation;
    hovered.surface = presentation->location_hover;
    EXPECT_TRUE(YeeRestingTextViewMatchesPresentation(
        location_bar()->GetCompactShellRestingTextForTesting(), hovered));
    capture("media-hover-" + std::to_string(surface_index) + ".png");
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        media->GetBoundsInScreen().CenterPoint(),
        media->GetWidget()->GetNativeWindow()));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return location_bar()->GetBackgroundColorForTesting() == surface;
    }));
    EXPECT_TRUE(YeeRestingTextViewMatchesPresentation(
        location_bar()->GetCompactShellRestingTextForTesting(), *presentation));
    capture("media-rest-" + std::to_string(surface_index++) + ".png");
    media->Hide();
    EXPECT_FALSE(divider->GetVisible());
  }
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeePresentationFollowsSingleAndSplitSources) {
  ASSERT_TRUE(yee::IsShellEnabled());
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  const auto window_bounds =
      BrowserView::GetBrowserViewForBrowser(browser())->GetBoundsInScreen();
  // 분할 Header가 이동해도 Rest 색상 검사 중 커서가 Header 위에 놓이지 않게
  // 한다.
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(gfx::Point(
      window_bounds.CenterPoint().x(), window_bounds.bottom() - 80)));
  const SkColor kSurfaceA = SkColorSetRGB(0x18, 0x22, 0x34);
  const SkColor kSurfaceB = SkColorSetRGB(0xE4, 0xD8, 0xC4);
  const SkColor kSurfaceC = SkColorSetRGB(0x36, 0x58, 0x72);
  std::optional<yee::BrowserSurfacePresentation> presentation_a;
  std::optional<yee::BrowserSurfacePresentation> presentation_b;
  std::optional<yee::BrowserSurfacePresentation> presentation_c;
  views::Widget* open_popup = nullptr;
  OmniboxPopupView* open_popup_view = nullptr;
  int popup_rebuild_count = 0;
  int rehost_sync_count = 0;
  uint64_t popup_revision_before_transition = 0;

  const auto location_bar_matches = [this](
                                        const yee::BrowserSurfacePresentation&
                                            presentation) {
    LocationBarView* const view = location_bar();
    BrowserView* const browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    const auto checks = std::to_array<std::pair<const char*, bool>>({
        {"popup surface",
         view->GetCompactShellPopupSurfaceColor() == presentation.surface},
        {"background",
         view->GetBackgroundColorForTesting() == presentation.surface},
        {"adjacent labels", YeeAdjacentOmniboxLabelsMatchColors(
                                presentation.surface, presentation.primary,
                                presentation.primary, presentation.secondary)},
        {"resting text",
         YeeRestingTextViewMatchesPresentation(
             view->GetCompactShellRestingTextForTesting(), presentation)},
        {"back button",
         YeeToolbarButtonMatchesPresentation(
             browser_view->toolbar_button_provider()->GetBackButton(),
             presentation)},
        {"location icon", view->location_icon_view()->GetForegroundColor() ==
                              presentation.primary},
        {"security chip", view->GetSecurityChipColorForTesting(
                              security_state::NONE) == presentation.primary},
        {"danger chip",
         view->GetSecurityChipColorForTesting(security_state::DANGEROUS) ==
             yee::ResolveBrowserSurfaceSemanticColor(
                 presentation.surface,
                 view->GetColorProvider()->GetColor(
                     kColorOmniboxSecurityChipDangerous))},
    });
    for (const auto& [name, matches] : checks) {
      LOG_IF(ERROR, !matches)
          << "Yee LocationBar mismatch: " << name << ", expected surface="
          << color_utils::SkColorToRgbaString(presentation.surface)
          << ", background="
          << color_utils::SkColorToRgbaString(
                 view->GetBackgroundColorForTesting());
    }
    return std::all_of(checks.begin(), checks.end(),
                       [](const auto& check) { return check.second; });
  };

  RunTestSequence(
      Do([&]() {
        SetYeeSurfaceColorForTab(0, kSurfaceA);
        ASSERT_TRUE(base::test::RunUntil([&]() {
          return location_bar()->GetBackgroundColorForTesting() == kSurfaceA;
        }));
        presentation_a = YeeSurfaceSourceForTab(0)->GetPresentation();
        rehost_sync_count =
            location_bar()->GetCompactShellRehostSyncCountForTesting();
      }),
      Check([&]() {
        return presentation_a.has_value() &&
               location_bar_matches(*presentation_a);
      }),
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0), Check([&]() {
        return location_bar()->GetCompactShellRehostSyncCountForTesting() ==
                   rehost_sync_count + 1 &&
               location_bar()->parent() ==
                   multi_contents_view()->mini_toolbar_for_testing(0);
      }),
      Do([&]() {
        SetYeeSurfaceColorForTab(0, kSurfaceA);
        SetYeeSurfaceColorForTab(1, kSurfaceB);
        presentation_a = YeeSurfaceSourceForTab(0)->GetPresentation();
        presentation_b = YeeSurfaceSourceForTab(1)->GetPresentation();
        rehost_sync_count =
            location_bar()->GetCompactShellRehostSyncCountForTesting();
      }),
      Check([&]() {
        return presentation_a.has_value() && presentation_b.has_value() &&
               location_bar_matches(*presentation_a) &&
               YeePaneHeaderMatchesPresentation(
                   multi_contents_view()->mini_toolbar_for_testing(0),
                   *presentation_a) &&
               YeePaneHeaderMatchesPresentation(
                   multi_contents_view()->mini_toolbar_for_testing(1),
                   *presentation_b);
      }),
      Do([&]() { tab_strip_model()->ActivateTabAt(1); }),
      WaitForActiveTabChange(1), Check([&]() {
        return presentation_b.has_value() &&
               location_bar()->GetCompactShellRehostSyncCountForTesting() ==
                   rehost_sync_count + 1 &&
               location_bar()->parent() ==
                   multi_contents_view()->mini_toolbar_for_testing(1) &&
               location_bar_matches(*presentation_b);
      }),
      Check([&]() {
        open_popup = OpenYeeOmniboxPopup();
        open_popup_view = location_bar()->GetOmniboxPopupView();
        if (!open_popup || !open_popup_view || !presentation_b.has_value() ||
            open_popup->GetColorProvider()->GetColor(
                kColorOmniboxResultsBackground) != presentation_b->surface) {
          return false;
        }
        // 색 전환은 논리상 열린 상태가 아니라 실제 표시된 팝업을 대상으로 한다.
        if (!base::test::RunUntil([&]() {
              auto* const current = YeeOmniboxPopupWidget(open_popup_view);
              return current == open_popup && current && current->IsVisible() &&
                     location_bar()->GetFocusManager()->GetFocusedView() ==
                         location_bar()->omnibox_view();
            })) {
          return false;
        }
        popup_rebuild_count =
            YeeOmniboxPopupProviderRebuildCount(open_popup_view);
        rehost_sync_count =
            location_bar()->GetCompactShellRehostSyncCountForTesting();
        const auto& current_presentation =
            YeeSurfaceSourceForTab(1)->GetPresentation();
        if (!current_presentation) {
          return false;
        }
        popup_revision_before_transition = current_presentation->popup_revision;
        YeeSurfaceSourceForTab(1)->TransitionToPageSurfaceColorForTesting(
            kSurfaceC);
        return true;
      }),
      PollUntil(
          [&]() {
            const auto& presentation =
                YeeSurfaceSourceForTab(1)->GetPresentation();
            return presentation && presentation->surface == kSurfaceC &&
                   presentation->popup_revision ==
                       popup_revision_before_transition + 1;
          },
          "페이지 색상과 팝업 revision의 전환 완료를 기다린다"),
      Check([&]() {
        presentation_c = YeeSurfaceSourceForTab(1)->GetPresentation();
        auto* const current_popup = YeeOmniboxPopupWidget(open_popup_view);
        const bool matches =
            presentation_c.has_value() &&
            presentation_c->surface == kSurfaceC &&
            presentation_c->popup_revision ==
                popup_revision_before_transition + 1 &&
            open_popup_view && current_popup == open_popup && current_popup &&
            current_popup->IsVisible() &&
            location_bar()->GetOmniboxController()->IsPopupOpen() &&
            location_bar()->GetFocusManager()->GetFocusedView() ==
                location_bar()->omnibox_view() &&
            YeeOmniboxPopupProviderRebuildCount(open_popup_view) ==
                popup_rebuild_count + 1 &&
            location_bar()->GetCompactShellRehostSyncCountForTesting() ==
                rehost_sync_count &&
            current_popup->GetColorProvider()->GetColor(
                kColorOmniboxResultsBackground) == kSurfaceC;
        const bool location_matches =
            presentation_c && location_bar_matches(*presentation_c);
        LOG_IF(ERROR, !matches || !location_matches)
            << "Yee visible popup transition: same widget="
            << (current_popup == open_popup)
            << ", visible=" << (current_popup && current_popup->IsVisible())
            << ", open="
            << location_bar()->GetOmniboxController()->IsPopupOpen()
            << ", native editor focus="
            << (location_bar()->GetFocusManager()->GetFocusedView() ==
                location_bar()->omnibox_view())
            << ", logical focus=" << location_bar()->HasFocus()
            << ", rebuild=" << popup_rebuild_count << "->"
            << YeeOmniboxPopupProviderRebuildCount(open_popup_view)
            << ", rehost=" << rehost_sync_count << "->"
            << location_bar()->GetCompactShellRehostSyncCountForTesting()
            << ", popup background="
            << color_utils::SkColorToRgbaString(
                   current_popup ? current_popup->GetColorProvider()->GetColor(
                                       kColorOmniboxResultsBackground)
                                 : SK_ColorTRANSPARENT);
        return matches && location_matches;
      }),
      Do([&]() {
        auto* const presenter = open_popup_view->presenter();
        if (!presenter) {
          return;
        }
        ASSERT_TRUE(presenter->GetWebUIContent());
        auto* const contents =
            presenter->GetWebUIContent()->GetWrappedWebContents();
        ASSERT_TRUE(contents);
        ASSERT_TRUE(content::WaitForLoadStop(contents));
        auto* const current_popup = YeeOmniboxPopupWidget(open_popup_view);
        ASSERT_EQ(open_popup, current_popup);
        ASSERT_TRUE(current_popup);
        ASSERT_TRUE(current_popup->IsVisible());
        // WebUI가 native hover 역할과 같은 보정된 색을 실제 행에 전달해야 한다.
        const SkColor expected = presentation_c->popup_hover;
        EXPECT_EQ(expected, current_popup->GetColorProvider()->GetColor(
                                kColorOmniboxResultsBackgroundHovered));
        EXPECT_TRUE(base::test::RunUntil([&]() {
          return content::EvalJs(
                     contents, content::JsReplace(
                                   R"JS(
            (() => {
              const row = document.querySelector('omnibox-popup-app')?.shadowRoot
                  ?.querySelector('cr-searchbox-dropdown')?.shadowRoot
                  ?.querySelector('cr-searchbox-match');
              if (!row) return false;
              const probe = document.createElement('span');
              probe.style.backgroundColor = getComputedStyle(row)
                  .getPropertyValue('--color-searchbox-results-background-hovered');
              const actual = probe.style.backgroundColor;
              probe.style.backgroundColor = $1;
              return !!actual && actual === probe.style.backgroundColor;
            })()
          )JS",
                                   color_utils::SkColorToRgbaString(expected)))
              .ExtractBool();
        }));
      }),
      Do([&]() {
        rehost_sync_count =
            location_bar()->GetCompactShellRehostSyncCountForTesting();
        // 분할 해제는 활성 페이지로 포커스를 돌려줄 수 있으며 이때 팝업을 닫는
        // 정책은 Chromium이 소유한다. 열린 팝업의 표현 갱신을 확인했으므로 구조
        // 전환 전에 닫는다.
        CloseYeeOmniboxPopup();
      }),
      ExitSplitView(1), WaitForActiveTabChange(1), Check([&]() {
        return presentation_c.has_value() &&
               location_bar()->GetCompactShellRehostSyncCountForTesting() ==
                   rehost_sync_count + 1 &&
               location_bar()->parent() ==
                   BrowserView::GetBrowserViewForBrowser(browser())
                       ->toolbar() &&
               location_bar_matches(*presentation_c);
      }),
      Check([&]() {
        BrowserView* const browser_view =
            BrowserView::GetBrowserViewForBrowser(browser());
        LocationBarView* const view = location_bar();
        // 원본 탭은 custom C를 유지한다. 임시 native 적용·관측·복원을
        // 한 콜백에서 끝내어 배치가 원본 색을 다시 적용하는 작업과 분리한다.
        browser_view->toolbar()->SetCompactShellSurfacePresentation(
            yee::UseNativeBrowserSurfaceColors(*presentation_c));
        yee::BrowserSurfacePresentation native_expected = *presentation_c;
        native_expected.primary =
            view->GetColorProvider()->GetColor(kColorToolbarButtonIcon);
        native_expected.disabled =
            view->GetColorProvider()->GetColor(kColorToolbarButtonIconInactive);
        const auto checks = std::to_array<std::pair<const char*, bool>>({
            {"팝업 표면 초기화",
             !view->GetCompactShellPopupSurfaceColor().has_value()},
            {"주소창 배경", view->GetBackgroundColorForTesting() ==
                                view->GetColorProvider()->GetColor(
                                    kColorLocationBarBackground)},
            {"뒤로 버튼",
             YeeToolbarButtonMatchesPresentation(
                 browser_view->toolbar_button_provider()->GetBackButton(),
                 native_expected)},
            {"사이트 아이콘",
             view->location_icon_view()->GetForegroundColor() ==
                 view->GetColorProvider()->GetColor(kColorOmniboxText)},
            {"보안 칩",
             view->GetSecurityChipColorForTesting(security_state::NONE) ==
                 view->GetColorProvider()->GetColor(kColorOmniboxText)},
            {"위험 칩",
             view->GetSecurityChipColorForTesting(security_state::DANGEROUS) ==
                 view->GetColorProvider()->GetColor(
                     kColorOmniboxSecurityChipDangerous)},
            {"주소창 인접 레이블",
             YeeAdjacentOmniboxLabelsMatchColors(
                 view->GetColorProvider()->GetColor(
                     kColorLocationBarBackground),
                 view->GetColorProvider()->GetColor(kColorOmniboxText),
                 view->GetColorProvider()->GetColor(kColorOmniboxResultsUrl),
                 view->GetColorProvider()->GetColor(kColorOmniboxTextDimmed))},
        });
        for (const auto& [name, matches] : checks) {
          LOG_IF(ERROR, !matches) << "Yee native color reset: " << name
                                  << ", actual=" << matches << ", expected=true"
                                  << ", background="
                                  << color_utils::SkColorToRgbaString(
                                         view->GetBackgroundColorForTesting())
                                  << ", native background="
                                  << color_utils::SkColorToRgbaString(
                                         view->GetColorProvider()->GetColor(
                                             kColorLocationBarBackground));
        }
        const bool matches =
            std::all_of(checks.begin(), checks.end(),
                        [](const auto& check) { return check.second; });
        browser_view->toolbar()->SetCompactShellSurfacePresentation(
            *presentation_c);
        return matches;
      }));
}

class YeePopupWindowUiTest : public InteractiveBrowserTest {};

IN_PROC_BROWSER_TEST_F(YeePopupWindowUiTest, PaneHeadersRetainNativeControls) {
  ASSERT_TRUE(yee::IsShellEnabled());
  BrowserView* const normal_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(normal_view);
  auto* const normal_contents =
      static_cast<MultiContentsView*>(normal_view->contents_container());
  ASSERT_TRUE(normal_contents);
  EXPECT_TRUE(normal_contents->mini_toolbar_for_testing(0)
                  ->yee_sidebar_button_for_testing());
  EXPECT_TRUE(normal_contents->mini_toolbar_for_testing(1)
                  ->yee_sidebar_button_for_testing());

  auto* const popup_browser = CreateBrowserWindow(BrowserWindowCreateParams(
      BrowserWindowInterface::Type::TYPE_POPUP, browser()->GetProfile(),
      /*from_user_gesture=*/true));
  ASSERT_TRUE(popup_browser);
  EXPECT_FALSE(tabs::VerticalTabStripStateController::From(popup_browser));

  BrowserView* const popup_view =
      BrowserView::GetBrowserViewForBrowser(popup_browser);
  ASSERT_TRUE(popup_view);
  auto* const contents =
      static_cast<MultiContentsView*>(popup_view->contents_container());
  ASSERT_TRUE(contents);
  EXPECT_FALSE(
      contents->mini_toolbar_for_testing(0)->yee_sidebar_button_for_testing());
  EXPECT_FALSE(
      contents->mini_toolbar_for_testing(1)->yee_sidebar_button_for_testing());
  EXPECT_FALSE(contents->mini_toolbar_for_testing(0)
                   ->has_yee_navigation_controls_for_testing());
  EXPECT_FALSE(contents->mini_toolbar_for_testing(1)
                   ->has_yee_navigation_controls_for_testing());
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeInactivePaneHeaderActivatesAddressEditing) {
  ASSERT_TRUE(yee::IsShellEnabled());
  const char kInactivePaneAddress[] = "비활성 pane 주소 미리보기";
  content::WebContents* first_contents = nullptr;
  content::WebContents* second_contents = nullptr;
  RunTestSequence(
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0), Do([&]() {
        first_contents = tab_strip_model()->GetWebContentsAt(0);
        second_contents = tab_strip_model()->GetWebContentsAt(1);
      }),
      Check([this]() {
        const bool brought_to_front =
            ui_test_utils::BringBrowserWindowToFront(browser());
        auto* const widget = multi_contents_view()->GetWidget();
        RecordProperty("yee_inactive_header_brought_to_front",
                       brought_to_front);
        RecordProperty("yee_inactive_header_widget_active", widget->IsActive());
        RecordProperty("yee_inactive_header_widget_visible",
                       widget->IsVisible());
        return brought_to_front && widget->IsActive() && widget->IsVisible();
      }),
      PollUntil(
          [this]() {
            auto* const header =
                multi_contents_view()->mini_toolbar_for_testing(1);
            auto* const address = header->yee_resting_text_for_testing();
            if (!address || !address->IsDrawn() ||
                address->GetBoundsInScreen().IsEmpty()) {
              return false;
            }
            auto* const root = header->GetWidget()->GetRootView();
            const gfx::Point target = views::View::ConvertPointFromScreen(
                root, address->GetBoundsInScreen().CenterPoint());
            return root->GetEventHandlerForPoint(target) == header;
          },
          "비활성 주소 표면의 실제 배치와 입력 대상을 기다린다"),
      Do([this]() {
        auto* const header = multi_contents_view()->mini_toolbar_for_testing(1);
        auto* const root = header->GetWidget()->GetRootView();
        const gfx::Point legacy = views::View::ConvertPointFromScreen(
            root, header->GetBoundsInScreen().CenterPoint());
        auto* const hit = root->GetEventHandlerForPoint(legacy);
        LOG(ERROR) << "YEE_INACTIVE_HEADER_INPUT header="
                   << header->GetBoundsInScreen().ToString() << " address="
                   << header->yee_resting_text_for_testing()
                          ->GetBoundsInScreen()
                          .ToString()
                   << " old_center_target="
                   << (hit ? hit->GetClassName() : "null")
                   << " old_center_hits_close="
                   << (hit == header->close_button_for_testing());
      }),
      NameView(kInactivePaneAddress,
               base::BindLambdaForTesting([this]() -> views::View* {
                 return multi_contents_view()
                     ->mini_toolbar_for_testing(1)
                     ->yee_resting_text_for_testing();
               })),
      MoveMouseTo(kInactivePaneAddress, CenterPoint()), Check([this]() {
        auto* const header = multi_contents_view()->mini_toolbar_for_testing(1);
        auto* const address = header->yee_resting_text_for_testing();
        auto* const root = header->GetWidget()->GetRootView();
        const gfx::Point cursor =
            display::Screen::Get()->GetCursorScreenPoint();
        LOG(ERROR) << "YEE_INACTIVE_HEADER_PRECLICK cursor="
                   << cursor.ToString()
                   << " address=" << address->GetBoundsInScreen().ToString()
                   << " active=" << header->GetWidget()->IsActive()
                   << " visible=" << header->GetWidget()->IsVisible();
        return header->GetWidget()->IsActive() &&
               header->GetWidget()->IsVisible() &&
               address->GetBoundsInScreen().Contains(cursor) &&
               root->GetEventHandlerForPoint(
                   views::View::ConvertPointFromScreen(root, cursor)) == header;
      }),
      ClickMouse(),
      Do([]() { RecordProperty("yee_inactive_header_click_completed", 1); }),
      Check([&]() {
        return tab_strip_model()->count() == 2 &&
               tab_strip_model()->GetWebContentsAt(0) == first_contents &&
               tab_strip_model()->GetWebContentsAt(1) == second_contents &&
               tab_strip_model()->GetTabAtIndex(0)->GetSplit().has_value() &&
               tab_strip_model()->GetTabAtIndex(0)->GetSplit() ==
                   tab_strip_model()->GetTabAtIndex(1)->GetSplit();
      }),
      WaitForActiveTabChange(1), Check([&]() {
        BrowserView* browser_view =
            BrowserView::GetBrowserViewForBrowser(browser());
        LocationBarView* location_bar =
            browser_view->toolbar()->location_bar_view();
        return location_bar->parent() ==
                   multi_contents_view()->mini_toolbar_for_testing(1) &&
               location_bar->HasFocus();
      }));
}
IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeOmniboxPopupFollowsSingleAndSplitHeader) {
  ASSERT_TRUE(yee::IsShellEnabled());
  RunTestSequence(
      Check([this]() { return YeeOmniboxPopupMatchesLocationBar(); }),
      Do([this]() { CloseYeeOmniboxPopup(); }), CreateTabsAndEnterSplitView(),
      WaitForActiveTabChange(0), Check([this]() {
        return location_bar()->IsCompactShellSplitMode() &&
               location_bar()->parent() ==
                   multi_contents_view()->mini_toolbar_for_testing(0) &&
               YeeOmniboxPopupMatchesLocationBar();
      }),
      Do([this]() { CloseYeeOmniboxPopup(); }), Do([this]() {
        ASSERT_FALSE(location_bar()->GetOmniboxController()->IsPopupOpen());
        // 새 탭 관찰 전에 알림이 끝나 이전 탭 상태만 남은 경우를 재현한다.
        split_tabs::SplitTabHighlightController::From(browser())
            ->OnOmniboxPopupVisibilityChanged(true);
        ASSERT_TRUE(split_tabs::SplitTabHighlightController::From(browser())
                        ->ShouldHighlight());
        tab_strip_model()->ActivateTabAt(1);
      }),
      WaitForActiveTabChange(1), Check([this]() {
        return !location_bar()->GetOmniboxController()->IsPopupOpen() &&
               !split_tabs::SplitTabHighlightController::From(browser())
                    ->ShouldHighlight() &&
               multi_contents_view()->mini_toolbar_for_testing(1)->IsDrawn() &&
               location_bar()->IsCompactShellSplitMode() &&
               location_bar()->parent() ==
                   multi_contents_view()->mini_toolbar_for_testing(1) &&
               YeeOmniboxPopupMatchesLocationBar();
      }),
      Do([this]() { CloseYeeOmniboxPopup(); }), ExitSplitView(1),
      WaitForActiveTabChange(1), Check([this]() {
        return !location_bar()->IsCompactShellSplitMode() &&
               location_bar()->parent() ==
                   BrowserView::GetBrowserViewForBrowser(browser())
                       ->toolbar() &&
               YeeOmniboxPopupMatchesLocationBar();
      }),
      Do([this]() { CloseYeeOmniboxPopup(); }));
}

IN_PROC_BROWSER_TEST_F(YeeContentsViewUiTest,
                       YeeOmniboxSuggestionsHaveBalancedInsets) {
  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  // 첫 표시 신호가 오기 전 준비된 WebUI 문서에도 같은 제안 목록 여백이
  // 필요하다.
  auto* const initial_popup = location_bar()->GetOmniboxPopupView();
  ASSERT_TRUE(initial_popup);
  if (auto* presenter = initial_popup->presenter()) {
    presenter->EnsureWebUIContentCreated();
    auto* const webui_content = presenter->GetWebUIContent();
    ASSERT_TRUE(webui_content);
    auto* const contents = webui_content->GetWrappedWebContents();
    ASSERT_TRUE(contents);
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return content::EvalJs(
                 contents,
                 content::JsReplace(
                     R"(
        document.documentElement?.style.getPropertyValue(
            '--omnibox-popup-results-padding-top') === $1 + 'px'
      )",
                     yee::kSidebarMetrics.omnibox_popup_results_padding))
          .ExtractBool();
    }));
  }
  const auto capture_dir =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-omnibox-popup-capture-dir");
  const auto capture = [&](views::Widget* widget, const std::string& name) {
    if (capture_dir.empty()) {
      return;
    }
    CaptureYeeWidgetForTesting(widget,
                               capture_dir.AppendASCII("single-" + name));
  };
  int surface_index = 0;
  for (SkColor surface :
       {SkColorSetRGB(244, 245, 247), SkColorSetRGB(48, 69, 96)}) {
    ASSERT_TRUE(content::ExecJs(
        tab_strip_model()->GetActiveWebContents(),
        content::JsReplace("document.body.style.background = $1;",
                           color_utils::SkColorToRgbaString(surface))));
    SetYeeSurfaceColorForTab(0, surface);
    views::Widget* const popup_widget =
        OpenYeeOmniboxPopup(u"https://www.dogdrip.net");
    ASSERT_TRUE(popup_widget);
    const std::string prefix = std::to_string(surface_index++);
    SCOPED_TRACE(prefix);
    if (auto* presenter = location_bar()->GetOmniboxPopupView()->presenter()) {
      auto* webui_content = presenter->GetWebUIContent();
      auto* contents = webui_content->GetWrappedWebContents();
      ASSERT_TRUE(contents);
      ASSERT_TRUE(content::ExecJs(contents, R"(
        window.yeePopupRowsForTesting = () => {
          const app = document.querySelector('omnibox-popup-app');
          const dropdown = app?.shadowRoot?.querySelector('cr-searchbox-dropdown');
          return [...(dropdown?.shadowRoot?.querySelectorAll('cr-searchbox-match') || [])]
              .map(match => match.shadowRoot?.querySelector('.container:not(.actions)'))
              .filter(row => row && row.getBoundingClientRect().height > 0);
        };
      )"));
      ASSERT_TRUE(base::test::RunUntil([&] {
        return content::EvalJs(
                   contents,
                   content::JsReplace(
                       R"(
          yeePopupRowsForTesting().length >= 2 &&
          getComputedStyle(document.querySelector('omnibox-popup-app')
              .shadowRoot.querySelector('.dropdownContainer')).paddingTop === $1 + 'px'
        )",
                       yee::kSidebarMetrics.omnibox_popup_results_padding))
            .ExtractBool();
      }));
      EXPECT_EQ(true,
                content::EvalJs(
                    contents,
                    content::JsReplace(
                        R"(
        (() => {
          const wrapper = document.querySelector('omnibox-popup-app')
              .shadowRoot.querySelector('.dropdownContainer');
          const style = getComputedStyle(wrapper);
          return style.paddingTop === $1 + 'px' && style.paddingBottom === $1 + 'px' &&
              style.borderTopWidth === $2 + 'px' && yeePopupRowsForTesting().every(row => {
                const s = getComputedStyle(row);
                return s.marginInlineStart === $3 + 'px' && s.marginInlineEnd === $3 + 'px' &&
                    s.borderTopLeftRadius === $4 + 'px' && s.borderTopRightRadius === $4 + 'px' &&
                    s.borderBottomLeftRadius === $4 + 'px' && s.borderBottomRightRadius === $4 + 'px';
              });
        })()
      )",
                        yee::kSidebarMetrics.omnibox_popup_results_padding,
                        yee::kSidebarMetrics.omnibox_popup_separator_height,
                        yee::kSidebarMetrics.omnibox_popup_row_inset,
                        yee::kSidebarMetrics.omnibox_popup_row_corner_radius)));
      capture(BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
              prefix + "-selected-window.png");
      capture(popup_widget, prefix + "-selected-popup.png");
      // WebPreferences의 inForcedColors만 바꾸면 Blink의 색상 캐시가
      // 무효화되지 않는다. 정식 media emulation 경로로 CSS 반응을 검사한다.
      content::TestDevToolsProtocolClient media_emulation;
      media_emulation.AttachToWebContents(contents);
      base::ScopedClosureRunner detach_media_emulation(base::BindOnce(
          &content::TestDevToolsProtocolClient::DetachProtocolClient,
          base::Unretained(&media_emulation)));
      for (const auto& [forced_colors, contrast] :
           {std::pair(false, std::string_view("more")),
            std::pair(true, std::string_view("custom")),
            std::pair(true, std::string_view("less")),
            std::pair(false, std::string_view("no-preference"))}) {
        SCOPED_TRACE(testing::Message() << "forced_colors=" << forced_colors
                                        << " contrast=" << contrast);
        base::ListValue features;
        features.Append(base::DictValue()
                            .Set("name", "forced-colors")
                            .Set("value", forced_colors ? "active" : "none"));
        features.Append(base::DictValue()
                            .Set("name", "prefers-contrast")
                            .Set("value", contrast));
        ASSERT_TRUE(media_emulation.SendCommandSync(
            "Emulation.setEmulatedMedia",
            base::DictValue().Set("features", std::move(features))));
        EXPECT_TRUE(base::test::RunUntil([&] {
          return content::EvalJs(
                     contents,
                     content::JsReplace(R"(
            (() => {
              const match = yeePopupRowsForTesting()
                  .map(row => row.getRootNode().host)
                  .find(match => match.matches('[selected]'));
              const indicator = match?.shadowRoot.querySelector('#focusIndicator');
              return matchMedia('(forced-colors: active)').matches === $1 &&
                  matchMedia('(prefers-contrast: more)').matches === $2 &&
                  indicator && getComputedStyle(indicator).display === $3;
            })()
          )",
                                        forced_colors, contrast == "more",
                                        forced_colors || contrast == "more"
                                            ? "block"
                                            : "none"))
              .ExtractBool();
        }));
      }
      ASSERT_TRUE(media_emulation.SendCommandSync(
          "Emulation.setEmulatedMedia",
          base::DictValue().Set("features", base::ListValue())));
      detach_media_emulation.RunAndReset();
      // DOM 결과가 먼저 생겨도 native WebView는 아직 1 DIP일 수 있다.
      // 자동 크기 조절을 마친 뒤 실제 화면 좌표를 구한다.
      ASSERT_TRUE(base::test::RunUntil([&] {
        popup_widget->LayoutRootViewIfNecessary();
        const int rows_bottom = content::EvalJs(contents, R"(
          (() => {
            const rows = yeePopupRowsForTesting();
            return rows.length >= 2
                ? Math.ceil(rows.at(-1).getBoundingClientRect().bottom) : 0;
          })()
        )")
                                    .ExtractInt();
        return rows_bottom > 0 && webui_content->height() >= rows_bottom &&
               webui_content->width() > 1 &&
               popup_widget->GetLayer()->opacity() == 1.0f;
      }));
      popup_widget->GetCompositor()->ScheduleFullRedraw();
      ASSERT_TRUE(ui::WaitForNextFrameToBePresented(
          popup_widget->GetCompositor(), base::Seconds(5)));
      gfx::Point hover = webui_content->GetBoundsInScreen().origin();
      hover.Offset(content::EvalJs(contents, R"(
        Math.floor(yeePopupRowsForTesting()[1].getBoundingClientRect().x +
                   yeePopupRowsForTesting()[1].getBoundingClientRect().width / 2)
      )")
                       .ExtractInt(),
                   content::EvalJs(contents, R"(
        Math.floor(yeePopupRowsForTesting()[1].getBoundingClientRect().y +
                   yeePopupRowsForTesting()[1].getBoundingClientRect().height / 2)
      )")
                       .ExtractInt());
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          hover, popup_widget->GetNativeWindow()));
      ASSERT_TRUE(base::test::RunUntil([&] {
        // 결과가 교체되는 프레임에는 행이 잠시 사라질 수 있다.
        return content::EvalJs(
                   contents,
                   "Boolean(yeePopupRowsForTesting()[1]?.matches(':hover'))")
            .ExtractBool();
      }));
      capture(BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
              prefix + "-hover-window.png");
      capture(popup_widget, prefix + "-hover-popup.png");
      ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_DOWN,
                                                  false, false, false, false));
      EXPECT_TRUE(base::test::RunUntil([&]() {
        return content::EvalJs(contents, R"(
          yeePopupRowsForTesting()[1]?.getRootNode().host.matches('[selected]') === true
        )")
            .ExtractBool();
      }));
      ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_UP, false,
                                                  false, false, false));
      EXPECT_TRUE(base::test::RunUntil([&]() {
        return content::EvalJs(contents, R"(
          yeePopupRowsForTesting()[0]?.getRootNode().host.matches('[selected]') === true
        )")
            .ExtractBool();
      }));
      for (int attempt = 0;
           attempt < 2 && location_bar()->GetOmniboxController()->IsPopupOpen();
           ++attempt) {
        ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
            browser(), ui::VKEY_ESCAPE, false, false, false, false));
      }
      EXPECT_FALSE(location_bar()->GetOmniboxController()->IsPopupOpen());
      CloseYeeOmniboxPopup();
      continue;
    }
    auto* popup = static_cast<OmniboxPopupViewViews*>(
        location_bar()->GetOmniboxPopupView());
    popup_widget->LayoutRootViewIfNecessary();
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return popup_widget->GetLayer()->opacity() == 1.0f; }));
    std::vector<OmniboxRowView*> rows;
    for (views::View* child : popup->children()) {
      auto* row = views::AsViewClass<OmniboxRowView>(child);
      if (row && row->GetVisible()) {
        rows.push_back(row);
      }
    }
    ASSERT_GE(rows.size(), 2u);
    // 구분선을 포함해 실제 배치된 행의 여백을 측정한다.
    EXPECT_EQ(yee::kSidebarMetrics.omnibox_popup_results_padding,
              rows.front()->y() -
                  yee::kSidebarMetrics.omnibox_popup_separator_height);
    EXPECT_EQ(yee::kSidebarMetrics.omnibox_popup_results_padding,
              popup->height() - rows.back()->bounds().bottom());
    for (OmniboxRowView* row : rows) {
      EXPECT_EQ(yee::kSidebarMetrics.omnibox_popup_row_inset,
                row->result_view()->x());
      EXPECT_EQ(yee::kSidebarMetrics.omnibox_popup_row_inset,
                row->width() - row->result_view()->bounds().right());
    }
    capture(BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
            prefix + "-selected-window.png");
    capture(popup_widget, prefix + "-selected-popup.png");
    ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
        rows[1]->result_view()->GetBoundsInScreen().CenterPoint(),
        popup_widget->GetNativeWindow()));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return rows[1]->result_view()->GetThemeState() ==
             OmniboxPartState::HOVERED;
    }));
    capture(BrowserView::GetBrowserViewForBrowser(browser())->GetWidget(),
            prefix + "-hover-window.png");
    capture(popup_widget, prefix + "-hover-popup.png");
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_DOWN, false,
                                                false, false, false));
    EXPECT_EQ(1u, popup->GetSelectedIndex());
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_UP, false,
                                                false, false, false));
    EXPECT_EQ(0u, popup->GetSelectedIndex());
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                                false, false, false, false));
    if (location_bar()->GetOmniboxController()->IsPopupOpen()) {
      // native Omnibox는 먼저 임시 제안 텍스트를 복원할 수 있다.
      EXPECT_EQ(0u, popup->GetSelectedIndex());
      ASSERT_TRUE(ui_test_utils::SendKeyPressSync(browser(), ui::VKEY_ESCAPE,
                                                  false, false, false, false));
    }
    EXPECT_FALSE(location_bar()->GetOmniboxController()->IsPopupOpen());
    CloseYeeOmniboxPopup();
  }
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeSidebarControlsPersistAcrossSplitAndCollapse) {
  ASSERT_TRUE(yee::IsShellEnabled());
  std::array<views::View*, 4> original_controls{};
  const auto controls_match = [this, &original_controls](bool visible) {
    auto* region = BrowserView::GetBrowserViewForBrowser(browser())
                       ->vertical_tab_strip_region_view_for_testing();
    views::View* actions = region->sidebar_header_actions();
    views::View* footer = region->sidebar_footer_for_testing();
    if ((!visible && region->y() != yee::kSidebarMetrics.titlebar_height) ||
        !actions || !footer || actions->parent() != region ||
        footer->parent() != region || !actions->GetVisible() ||
        actions->children().size() != 2 || footer->GetVisible() != visible) {
      return false;
    }
    int actions_count = 0;
    int footer_count = 0;
    for (views::View* child : region->children()) {
      actions_count += child->GetID() == yee::kSidebarHeaderActionsViewId;
      footer_count += child->GetID() == yee::kSidebarFooterViewId;
    }
    views::View* create = actions->GetViewByID(yee::kSidebarHeaderCreateViewId);
    views::View* agent = actions->GetViewByID(yee::kSidebarHeaderAgentViewId);
    const auto footer_index = region->GetIndexOf(footer);
    views::View* tab_strip_container = region->GetTabStripView();
    if (!tab_strip_container) {
      return false;
    }
    // Footer와 native 탭 본문이 만나는 공통 부모에서 배치 순서를 확인한다.
    while (tab_strip_container->parent() != region) {
      tab_strip_container = tab_strip_container->parent();
      if (!tab_strip_container) {
        return false;
      }
    }
    const auto tab_strip_index = region->GetIndexOf(tab_strip_container);
    if (actions_count != 1 || footer_count != 1 || !create || !agent ||
        actions->children()[0] != create || actions->children()[1] != agent ||
        create->GetVisible() != visible || agent->GetVisible() != visible ||
        !footer_index || !tab_strip_index ||
        *footer_index <= *tab_strip_index) {
      return false;
    }
    const std::array<views::View*, 4> controls{actions, create, agent, footer};
    if (!original_controls[0]) {
      original_controls = controls;
    }
    return controls == original_controls;
  };

  RunTestSequence(
      Check([&]() { return controls_match(true); }),
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0),
      Check([&]() { return controls_match(true); }), Do([&]() {
        tabs::VerticalTabStripStateController::From(browser())->RequestCollapse(
            true);
      }),
      WaitForEvent(kTabStripRegionElementId,
                   VerticalTabStripRegionView::kAnimationCompletedEvent),
      Do([&]() { RunScheduledLayouts(); }),
      Check([&]() { return controls_match(false); }), Do([&]() {
        tabs::VerticalTabStripStateController::From(browser())->RequestCollapse(
            false);
      }),
      WaitForEvent(kTabStripRegionElementId,
                   VerticalTabStripRegionView::kAnimationCompletedEvent),
      Check([&]() { return controls_match(true); }));
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeSplitCommandsUpdateChromiumModel) {
  ASSERT_TRUE(yee::IsShellEnabled());
  using SplitExitObserver =
      views::test::PollingViewObserver<bool, MultiContentsView>;
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(SplitExitObserver, kSplitExitObserver);
  const split_tabs::SplitTabLayout initial_layout = GetParam();
  const split_tabs::SplitTabLayout toggled_layout =
      initial_layout == split_tabs::SplitTabLayout::kSideBySide
          ? split_tabs::SplitTabLayout::kStacked
          : split_tabs::SplitTabLayout::kSideBySide;
  tabs::TabInterface* original_start_tab = nullptr;
  tabs::TabInterface* original_end_tab = nullptr;
  RunTestSequence(
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0),
      Do([&]() { multi_contents_view()->OnToggleSplitLayout(); }), Check([&]() {
        const auto split = tab_strip_model()->GetActiveTab()->GetSplit();
        return split.has_value() &&
               tab_strip_model()
                       ->GetSplitData(*split)
                       ->visual_data()
                       ->split_layout() == toggled_layout &&
               tab_strip_model()
                       ->GetSplitData(*split)
                       ->visual_data()
                       ->split_ratio() == 0.5;
      }),
      Do([&]() {
        const auto split = tab_strip_model()->GetActiveTab()->GetSplit();
        CHECK(split.has_value());
        const std::vector<tabs::TabInterface*> tabs =
            tab_strip_model()->GetSplitData(*split)->ListTabs();
        CHECK_EQ(tabs.size(), 2u);
        original_start_tab = tabs[0];
        original_end_tab = tabs[1];
        multi_contents_view()->OnSwap();
      }),
      Check([&]() {
        const auto split = tab_strip_model()->GetActiveTab()->GetSplit();
        if (!split.has_value()) {
          return false;
        }
        const std::vector<tabs::TabInterface*> tabs =
            tab_strip_model()->GetSplitData(*split)->ListTabs();
        return tabs.size() == 2u && tabs[0] == original_end_tab &&
               tabs[1] == original_start_tab;
      }),
      Do([&]() { multi_contents_view()->OnExitSplit(); }),
      PollView(
          kSplitExitObserver, kMultiContentsViewElementId,
          [](const MultiContentsView* view) { return !view->IsInSplitView(); }),
      WaitForState(kSplitExitObserver, true));
}

// split에서 contents view와 resize 영역의 합으로 inset을 확인한다.
// TODO(crbug.com/397777917): MultiContentsView가 interior margin을 쓰게 되면
// 크기 비교 대신 margin 설정 여부를 검사한다.
IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest, InsetsOnlyInSplit) {
  // split이 아닐 때 세로 크기를 비교하면 상단 구분선 높이도 반영한다.
  const int kTopSeparatorHeight =
      (!yee::IsShellEnabled() &&
       GetParam() == split_tabs::SplitTabLayout::kStacked)
          ? views::Separator::kThickness
          : 0;
  RunTestSequence(
      Check([&]() {
        gfx::Rect active_contents_bounds =
            multi_contents_view()->GetActiveContentsView()->bounds();
        gfx::Rect multi_contents_view_bounds = multi_contents_view()->bounds();
        return GetSize(active_contents_bounds) ==
               GetSize(multi_contents_view_bounds) - kTopSeparatorHeight;
      }),
      CreateTabsAndEnterSplitView(), Check([&]() {
        int contents_and_resize_size =
            GetSize(multi_contents_view()->GetActiveContentsView()->bounds()) +
            GetSize(
                multi_contents_view()->GetInactiveContentsView()->bounds()) +
            GetSize(multi_contents_view()->resize_area_for_testing()->bounds());
        return yee::IsShellEnabled()
                   ? multi_contents_view()
                         ->split_view_insets_for_testing()
                         .IsEmpty()
                   : contents_and_resize_size <
                         GetSize(multi_contents_view()->bounds());
      }));
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       YeeDividerMouseDragResizesBothPanesAndKeepsTabs) {
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(MultiContentsViewLayoutObserver,
                                      kDraggedLayoutObserver);
  content::WebContents* start_contents = nullptr;
  content::WebContents* end_contents = nullptr;
  std::optional<split_tabs::SplitTabId> split_id;
  double initial_start_size = 0;
  double initial_end_size = 0;
  double initial_ratio = 0;
  gfx::Point drag_destination;
  const bool horizontal = GetParam() == split_tabs::SplitTabLayout::kSideBySide;

  RunTestSequence(
      CreateTabsAndEnterSplitView(), Check([&]() {
        auto* const area = multi_contents_view()->resize_area_for_testing();
        return area && area->IsDrawn() && !area->size().IsEmpty() &&
               multi_contents_view()
                   ->start_contents_view_for_testing()
                   ->GetWebContents() &&
               multi_contents_view()
                   ->end_contents_view_for_testing()
                   ->GetWebContents();
      }),
      Do([&]() {
        auto* const start =
            multi_contents_view()->start_contents_view_for_testing();
        auto* const end =
            multi_contents_view()->end_contents_view_for_testing();
        start_contents = start->GetWebContents();
        end_contents = end->GetWebContents();
        split_id = tab_strip_model()->GetSplitForTab(0);
        initial_start_size = GetSize(start->parent()->size());
        initial_end_size = GetSize(end->parent()->size());
        initial_ratio = multi_contents_view()->GetSplitRatio();
        drag_destination = multi_contents_view()
                               ->resize_area_for_testing()
                               ->GetBoundsInScreen()
                               .CenterPoint();
        // RTL에서도 논리 start pane이 커지는 쪽으로 실제 포인터를 옮긴다.
        drag_destination.Offset(
            horizontal ? (base::i18n::IsRTL() ? -80 : 80) : 0,
            horizontal ? 0 : 80);
      }),
      Check([&]() { return split_id.has_value(); }),
      // OnResize를 직접 호출하지 않고 실제 resize area의 입력 경로를 검증한다.
      MoveMouseTo(MultiContentsResizeArea::kMultiContentsResizeAreaElementId),
      DragMouseTo(
          base::BindLambdaForTesting([&]() { return drag_destination; }),
          /*release=*/false),
      CheckResizeValues(
          base::BindLambdaForTesting([&](double start, double end) {
            return start > initial_start_size + 20 &&
                   end < initial_end_size - 20;
          }),
          kDraggedLayoutObserver),
      Check([&]() {
        return multi_contents_view()->resize_area_for_testing()->is_resizing();
      }),
      ReleaseMouse(), Check([&]() {
        auto* const area = multi_contents_view()->resize_area_for_testing();
        return !area->is_resizing() && !area->GetWidget()->HasCapture();
      }),
      Check([&]() {
        return multi_contents_view()->GetSplitRatio() > initial_ratio;
      }),
      Check([&]() {
        return multi_contents_view()
                       ->start_contents_view_for_testing()
                       ->GetWebContents() == start_contents &&
               multi_contents_view()
                       ->end_contents_view_for_testing()
                       ->GetWebContents() == end_contents &&
               tab_strip_model()->count() == 2 &&
               tab_strip_model()->active_index() == 0 &&
               tab_strip_model()->GetSplitForTab(0) == split_id &&
               tab_strip_model()->GetSplitForTab(1) == split_id;
      }));
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       ResizeMouseDoubleClickEqualizesSplitViews) {
  using MultiContentsViewRatioObserver =
      views::test::PollingViewObserver<bool, MultiContentsView>;
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(MultiContentsViewRatioObserver,
                                      kMultiContentsViewRatioObserver);
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWebContentsId);
  RunTestSequence(
      InstrumentTab(kWebContentsId),
      NavigateWebContents(kWebContentsId, chrome::ChromeUINewTabURLAsGURL()),
      CreateTabsAndEnterSplitView(), Check([&]() {
        return multi_contents_view()
                   ->start_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == GURL(chrome::kChromeUISettingsURL);
      }),
      Check([&]() {
        return multi_contents_view()
                   ->end_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == chrome::ChromeUINewTabURLAsGURL();
      }),
      CheckResult([this]() { return tab_strip_model()->active_index(); }, 0),
      Do([&]() { multi_contents_view()->OnResize(100, true); }),
      Check([&]() { return multi_contents_view()->GetSplitRatio() != 0.5; }),
      // 마우스 더블클릭은 제스처 더블탭처럼 pane 순서를 유지하며 50:50으로
      // 복원한다.
      Do([&]() {
        auto* resize_area = multi_contents_view()->resize_area_for_testing();
        gfx::Point center(resize_area->width() / 2, resize_area->height() / 2);
        ui::MouseEvent press_event(
            ui::EventType::kMousePressed, center, center, ui::EventTimeForNow(),
            ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
        ui::MouseEvent release_event(ui::EventType::kMouseReleased, center,
                                     center, ui::EventTimeForNow(),
                                     ui::EF_LEFT_MOUSE_BUTTON,
                                     ui::EF_LEFT_MOUSE_BUTTON);
        press_event.SetClickCount(2);
        release_event.SetClickCount(2);
        resize_area->OnMousePressed(press_event);
        resize_area->OnMouseReleased(release_event);
      }),
      PollView(kMultiContentsViewRatioObserver, kMultiContentsViewElementId,
               [](const MultiContentsView* multi_contents_view) {
                 return multi_contents_view->GetSplitRatio() == 0.5;
               }),
      WaitForState(kMultiContentsViewRatioObserver, true), Check([&]() {
        return multi_contents_view()
                   ->start_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == GURL(chrome::kChromeUISettingsURL);
      }),
      Check([&]() {
        return multi_contents_view()
                   ->end_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == chrome::ChromeUINewTabURLAsGURL();
      }),
      CheckResult([this]() { return tab_strip_model()->active_index(); }, 0),
      CheckActiveContentsHasFocus());
}

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest,
                       ResizeGestureDoubleTapEqualizesSplitViews) {
  using MultiContentsViewRatioObserver =
      views::test::PollingViewObserver<bool, MultiContentsView>;
  DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(MultiContentsViewRatioObserver,
                                      kMultiContentsViewRatioObserver);
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kWebContentsId);
  RunTestSequence(
      // split을 만든 뒤 WebContents와 활성 인덱스를 확인한다.
      InstrumentTab(kWebContentsId),
      NavigateWebContents(kWebContentsId, chrome::ChromeUINewTabURLAsGURL()),
      CreateTabsAndEnterSplitView(), Check([&]() {
        return multi_contents_view()
                   ->start_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == GURL(chrome::kChromeUISettingsURL);
      }),
      Check([&]() {
        return multi_contents_view()
                   ->end_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == chrome::ChromeUINewTabURLAsGURL();
      }),
      CheckResult([this]() { return tab_strip_model()->active_index(); }, 0),
      Do([&]() { multi_contents_view()->OnResize(100, true); }),
      Check([&]() { return multi_contents_view()->GetSplitRatio() != 0.5; }),
      // resize 영역의 더블탭으로 pane 순서를 유지하며 같은 크기로 복원한다.
      Do([&]() {
        auto* resize_area = multi_contents_view()->resize_area_for_testing();
        gfx::Point center(resize_area->width() / 2, resize_area->height() / 2);
        ui::GestureEventDetails details(ui::EventType::kGestureTap);
        details.set_tap_count(2);
        ui::GestureEvent gesture_event(center.x(), center.y(), ui::EF_NONE,
                                       ui::EventTimeForNow(), details);
        resize_area->OnGestureEvent(&gesture_event);
      }),
      PollView(kMultiContentsViewRatioObserver, kMultiContentsViewElementId,
               [&](const MultiContentsView* multi_contents_view) -> bool {
                 return multi_contents_view->GetSplitRatio() == 0.5;
               }),
      WaitForState(kMultiContentsViewRatioObserver, true), Check([&]() {
        return multi_contents_view()
                   ->start_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == GURL(chrome::kChromeUISettingsURL);
      }),
      Check([&]() {
        return multi_contents_view()
                   ->end_contents_view_for_testing()
                   ->GetWebContents()
                   ->GetVisibleURL() == chrome::ChromeUINewTabURLAsGURL();
      }),
      CheckResult([this]() { return tab_strip_model()->active_index(); }, 0),
      CheckActiveContentsHasFocus());
}

#if !BUILDFLAG(IS_CHROMEOS)
IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest, RoundedCornersForSplitView) {
  RunTestSequence(
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0),
      // contents web view의 모서리가 둥근지 확인한다.
      CheckView(kMultiContentsViewElementId,
                [](MultiContentsView* multi_contents_view) -> bool {
                  return !multi_contents_view->GetActiveContentsView()
                              ->GetBackgroundRadii()
                              .IsEmpty() &&
                         !multi_contents_view->GetInactiveContentsView()
                              ->GetBackgroundRadii()
                              .IsEmpty();
                }),
      // 탭바에 일반 탭을 추가한다.
      AddInstrumentedTab(kSecondTab, GURL(chrome::kChromeUISettingsURL), 2),
      WaitForActiveTabChange(2),
      // Yee의 단일 Browser Surface는 하단 hard clip을 유지하며 upstream은
      // 둥글지 않은 일반 contents view로 돌아간다.
      CheckView(kMultiContentsViewElementId,
                [](MultiContentsView* multi_contents_view) -> bool {
                  const bool empty =
                      multi_contents_view->GetActiveContentsView()
                          ->GetBackgroundRadii()
                          .IsEmpty();
                  return yee::IsShellEnabled() ? !empty : empty;
                }));
}
#endif

IN_PROC_BROWSER_TEST_P(YeeMultiContentsViewUiTest, BackgroundVisibility) {
  RunTestSequence(
      CreateTabsAndEnterSplitView(), WaitForActiveTabChange(0),
      // Yee의 Split Canvas는 투명한 배치 영역이며 각 pane 카드가 표면을
      // 소유한다. upstream Chromium은 공통 split 바탕을 유지한다.
      CheckView(kMultiContentsViewElementId,
                [](MultiContentsView* multi_contents_view) -> bool {
                  const bool visible =
                      multi_contents_view->background_view_for_testing()
                          ->GetVisible();
                  return yee::IsShellEnabled() ? !visible : visible;
                }),
      // 탭바에 일반 탭을 추가한다.
      AddInstrumentedTab(kSecondTab, GURL(chrome::kChromeUISettingsURL), 2),
      WaitForActiveTabChange(2),
      CheckView(kMultiContentsViewElementId,
                [](MultiContentsView* multi_contents_view) -> bool {
                  return !multi_contents_view->background_view_for_testing()
                              ->GetVisible();
                }));
}

}  // namespace
