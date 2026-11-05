// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "base/memory/raw_ref.h"

#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/glic/public/features.h"
#include "chrome/browser/glic/selection/selection_overlay_controller.h"
#include "chrome/browser/glic/test_support/glic_browser_test.h"
#include "chrome/browser/glic/test_support/glic_test_util.h"
#include "chrome/browser/selection/mojom/action.mojom.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view_tracker.h"

namespace yee::test {

// 테스트가 보유한 문서·View만 읽는다. overlay 생성과 layout은 제품이 수행한다.
class SelectionOverlayAppliedObserver {
 public:
  enum class Probe { kShown, kClosed, kPaneAligned, kNotSplit };
  explicit SelectionOverlayAppliedObserver(AppliedViewsObservers& signals)
      : signals_(signals) {}
  ~SelectionOverlayAppliedObserver() {
    for (const auto& watch : watches_)
      signals_->OwnerDestroyed(watch->token);
  }
  AppliedOwnerToken Observe(content::WebContents& contents,
                            BrowserView& browser,
                            Probe probe,
                            views::View* overlay = nullptr) {
    auto watch = std::make_unique<Watch>();
    watch->contents = contents.GetWeakPtr();
    watch->browser.SetView(&browser);
    watch->overlay.SetView(overlay);
    watch->probe = probe;
    const auto token = watch->token;
    signals_->ObserveView(browser);
    signals_->ObserveWidget(*browser.GetWidget());
    signals_->TieToView(token, browser);
    if (overlay)
      signals_->TieToView(token, *overlay);
    signals_->RegisterSignal(
        token, AppliedConditionKind::kViewStateMatches,
        base::BindRepeating(&Watch::Read, base::Unretained(watch.get())));
    watches_.push_back(std::move(watch));
    return token;
  }

 private:
  struct Watch {
    std::optional<AppliedSnapshot> Read() {
      if (!contents || !browser.view())
        return std::nullopt;
      auto* const controller =
          glic::SelectionOverlayController::FromTabWebContents(contents.get());
      if (!controller)
        return std::nullopt;
      bool matched = false;
      if (probe == Probe::kNotSplit) {
        auto* const tab = tabs::TabInterface::GetFromContents(contents.get());
        matched = tab && !tab->IsSplit();
      } else if (probe == Probe::kShown) {
        matched = controller->state() ==
                  glic::SelectionOverlayController::State::kOverlay;
      } else if (probe == Probe::kClosed) {
        matched = controller->state() ==
                  glic::SelectionOverlayController::State::kOff;
      } else {
        auto* const browser_view = static_cast<BrowserView*>(browser.view());
        auto* const container =
            browser_view->GetContentsContainerViewFor(contents.get());
        auto* const host = controller->GetOverlayViewForTesting();
        auto* const expected = overlay.view();
        matched = container && host && expected &&
                  controller->GetOverlayWebViewForTesting() == expected &&
                  expected->parent() == host && expected->IsDrawn() &&
                  expected->GetBoundsInScreen() ==
                      container->contents_view()->GetBoundsInScreen();
      }
      return AppliedSnapshot{
          .owner = token,
          .value = ViewStateMatches{.visible = matched},
          .coordinates = GetAppliedViewCoordinates(*browser.view())};
    }
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    base::WeakPtr<content::WebContents> contents;
    views::ViewTracker browser;
    views::ViewTracker overlay;
    Probe probe = Probe::kShown;
  };
  raw_ref<AppliedViewsObservers> signals_;
  std::vector<std::unique_ptr<Watch>> watches_;
};
}  // namespace yee::test

namespace glic {
class YeeSelectionOverlayBrowserTest : public GlicBrowserTest {
 public:
  YeeSelectionOverlayBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(::features::kGlicCaptureRegion);
  }
  void TearDownOnMainThread() override {
    overlay_observer_.reset();
    applied_.Stop();
    GlicBrowserTest::TearDownOnMainThread();
  }

 protected:
  yee::test::AppliedCondition OverlayCondition(
      tabs::TabInterface& tab,
      yee::test::SelectionOverlayAppliedObserver::Probe probe,
      views::View* overlay = nullptr) {
    if (!overlay_observer_)
      overlay_observer_ =
          std::make_unique<yee::test::SelectionOverlayAppliedObserver>(
              applied_.views());
    applied_.source().ObserveTabModel(*GetBrowser()->GetTabStripModel());
    const auto owner = overlay_observer_->Observe(
        *tab.GetContents(),
        *BrowserView::GetBrowserViewForBrowser(GetBrowser()), probe, overlay);
    return applied_.Condition(owner,
                              yee::test::ViewStateMatches{.visible = true},
                              yee::test::AppliedMatch::kViewVisible);
  }
  base::test::ScopedFeatureList scoped_feature_list_;
  yee::test::AppliedTestContext<yee::test::AppliedBrowserObservers> applied_;
  std::unique_ptr<yee::test::SelectionOverlayAppliedObserver> overlay_observer_;
};

IN_PROC_BROWSER_TEST_F(YeeSelectionOverlayBrowserTest,
                       SelectionUsedFromController) {
  base::HistogramTester histogram_tester;

  // 1. Navigate to a valid page.
  tabs::TabInterface* tab = CreateAndActivateTab(GetSimpleTestUrl());

  // 2. Open Glic.
  ASSERT_OK_AND_ASSIGN(auto* instance, OpenGlicForActiveTab());

  // 3. Show the selection overlay.
  content::WebContents* web_contents = tab->GetContents();
  auto* controller =
      SelectionOverlayController::FromTabWebContents(web_contents);
  ASSERT_TRUE(controller);
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *tab, yee::test::SelectionOverlayAppliedObserver::Probe::kShown)},
      [&] { controller->Show(/*options=*/nullptr); }));

  // 5. Adjust region.
  static_cast<selection::SelectionOverlayPageHandler*>(controller)
      ->AdjustRegion(
          selection::SelectedRegion::New(
              base::UnguessableToken::Create(),
              selection::RegionShape::NewRect(gfx::RectF(10, 10, 10, 10))),
          /*is_using_keyboard=*/false);

  // 6. Submit user input and verify metrics.
  SimulateUserInputSubmitted(instance, mojom::WebClientMode::kText);
  histogram_tester.ExpectBucketCount(
      "Glic.Instance.InputSubmitted.SelectionCount", 1, 1);

  // Submit another input, should still log 1.
  SimulateUserInputSubmitted(instance, mojom::WebClientMode::kText);
  histogram_tester.ExpectBucketCount(
      "Glic.Instance.InputSubmitted.SelectionCount", 1, 2);

  // Close the overlay.
  controller->Close();

  // Submit another input, should log 0.
  SimulateUserInputSubmitted(instance, mojom::WebClientMode::kText);
  histogram_tester.ExpectBucketCount(
      "Glic.Instance.InputSubmitted.SelectionCount", 0, 1);
  histogram_tester.ExpectTotalCount(
      "Glic.Instance.InputSubmitted.SelectionCount", 3);
}

IN_PROC_BROWSER_TEST_F(YeeSelectionOverlayBrowserTest,
                       SelectionStaysScopedThroughSplitLifecycle) {
  tabs::TabInterface* sibling_tab = CreateAndActivateTab(GetSimpleTestUrl());
  tabs::TabInterface* selection_tab = CreateAndActivateTab(GetSimpleTestUrl());
  TabStripModel* const tab_strip_model = GetBrowser()->GetTabStripModel();
  ASSERT_TRUE(sibling_tab);
  ASSERT_TRUE(selection_tab);

  tab_strip_model->AddToNewSplit(
      {tab_strip_model->GetIndexOfTab(sibling_tab)},
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_TRUE(sibling_tab->IsSplit());
  ASSERT_TRUE(selection_tab->IsSplit());
  ASSERT_TRUE(selection_tab->IsActivated());

  ASSERT_OK(OpenGlicForActiveTab());
  auto* const controller = SelectionOverlayController::FromTabWebContents(
      selection_tab->GetContents());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kShown)},
      [&] { controller->Show(/*options=*/nullptr); }));

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(GetBrowser());
  ASSERT_TRUE(browser_view);
  views::WebView* const overlay = controller->GetOverlayWebViewForTesting();
  ASSERT_TRUE(overlay);
  const auto overlay_matches_owning_pane = [&]() {
    auto* const container =
        browser_view->GetContentsContainerViewFor(selection_tab->GetContents());
    auto* const host = controller->GetOverlayViewForTesting();
    return container && host &&
           controller->GetOverlayWebViewForTesting() == overlay &&
           overlay->parent() == host && overlay->IsDrawn() &&
           overlay->GetBoundsInScreen() ==
               container->contents_view()->GetBoundsInScreen();
  };
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kPaneAligned,
          overlay)},
      [] {}));
  ASSERT_TRUE(overlay_matches_owning_pane());

  tab_strip_model->ReverseTabsInSplit(selection_tab->GetSplit().value());
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kPaneAligned,
          overlay)},
      [] {}));
  ASSERT_TRUE(overlay_matches_owning_pane());

  tab_strip_model->ActivateTabAt(tab_strip_model->GetIndexOfTab(sibling_tab));
  EXPECT_TRUE(overlay_matches_owning_pane());
  tab_strip_model->ActivateTabAt(tab_strip_model->GetIndexOfTab(selection_tab));
  EXPECT_TRUE(overlay_matches_owning_pane());

  tab_strip_model->CloseWebContentsAt(
      tab_strip_model->GetIndexOfTab(sibling_tab), TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kNotSplit)},
      [] {}));
  ASSERT_FALSE(selection_tab->IsSplit());
  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kPaneAligned,
          overlay)},
      [] {}));
  ASSERT_TRUE(overlay_matches_owning_pane());

  ASSERT_TRUE(applied_.Run(
      {OverlayCondition(
          *selection_tab,
          yee::test::SelectionOverlayAppliedObserver::Probe::kClosed)},
      [&] { controller->Close(); }));
  EXPECT_TRUE(
      browser_view->GetContentsContainerViewFor(selection_tab->GetContents())
          ->contents_view()
          ->GetEnabled());
}

}  // namespace glic
