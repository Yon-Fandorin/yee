// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_native_view.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/views/animations/side_panel_animations.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/side_panel/side_panel_animation_content_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel_coordinator.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/test/views_test_utils.h"

class YeeSidePanelCoordinatorTest : public InProcessBrowserTest {
 protected:
  SidePanelCoordinator* coordinator() {
    return SidePanelCoordinator::From(browser());
  }
  SidePanelRegistry* global_registry() {
    return SidePanelRegistry::From(browser());
  }
};

IN_PROC_BROWSER_TEST_F(YeeSidePanelCoordinatorTest,
                       ShowFromAnimationReparentsContentView) {
  // Deregister and reregister kAboutThisSite side panel with kToolbar
  // SidePanelType.
  global_registry()->Deregister(
      SidePanelEntry::Key(SidePanelEntryId::kAboutThisSite));
  auto* registry = SidePanelRegistry::From(browser()->GetActiveTabInterface());
  std::unique_ptr<SidePanelEntry> entry = std::make_unique<SidePanelEntry>(
      SidePanelType::kToolbar,
      SidePanelEntry::Key(SidePanelEntryId::kAboutThisSite),
      base::BindRepeating([](SidePanelEntryScope&) {
        auto view = SidePanelNativeView(std::make_unique<views::View>());
        view->SetPaintToLayer();
        view->layer()->SetFillsBoundsOpaquely(false);
        return view;
      }),
      /*default_content_width_callback=*/base::NullCallback());
  registry->Register(std::move(entry));
  coordinator()->SetNoDelaysForTesting(true);

  auto* side_panel =
      BrowserView::GetBrowserViewForBrowser(browser())->side_panel();

  // Set a custom container to control the animation time.
  auto* animation_coordinator = BrowserAnimationController::From(browser());
  auto container = base::MakeRefCounted<gfx::AnimationContainer>();
  animation_coordinator->SetAnimationContainerForTesting(
      SidePanelAnimations::kSidePanel, container.get());
  gfx::AnimationContainerTestApi test_api(container.get());

  coordinator()->ShowFrom(SidePanelEntryKey(SidePanelEntryId::kAboutThisSite),
                          gfx::Rect(0, 0, 100, 100));
  // Advance the animation by 150ms, at this point the browser view should own
  // the contents view.
  test_api.IncrementTime(base::Milliseconds(150));
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  views::test::RunScheduledLayout(browser_view);
  ASSERT_NE(BrowserView::GetBrowserViewForBrowser(browser())
                ->GetBrowserViewLayoutForTesting()
                ->side_panel_animation_content(),
            nullptr);
  ASSERT_EQ(side_panel->GetContentParentView()->children().size(), 0u);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& native_result = *layout->last_native_layout_for_testing();
  ASSERT_TRUE(native_result.side_panel.animation_content_bounds);
  EXPECT_EQ(*native_result.side_panel.animation_content_bounds,
            browser_view->GetSidePanelAnimationContent()->bounds());
  EXPECT_EQ(native_result.side_panel.current_bounds, side_panel->bounds());
  EXPECT_EQ(native_result.side_panel.local_clip_bounds,
            side_panel->clip_to_visible_area_for_testing());
  EXPECT_EQ(1, native_result.top_container_calculation_count);

  // Advance the animation to its end, at this point the contents view should be
  // reparented to the side panel's ContentParentView.
  test_api.IncrementTime(base::Milliseconds(350));
  ASSERT_EQ(BrowserView::GetBrowserViewForBrowser(browser())
                ->GetBrowserViewLayoutForTesting()
                ->side_panel_animation_content(),
            nullptr);
  ASSERT_EQ(side_panel->GetContentParentView()->children().size(), 1u);
}
