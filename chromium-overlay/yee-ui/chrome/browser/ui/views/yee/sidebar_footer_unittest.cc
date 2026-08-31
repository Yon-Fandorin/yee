// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/sidebar_footer.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/color/color_id.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace yee {
namespace {

TEST(SidebarFooterPolicyTest, RootContainsOnlyApprovedDestinations) {
  EXPECT_EQ(kSidebarFooterRootScreens[0], SidebarFooterScreen::kContext);
  EXPECT_EQ(kSidebarFooterRootScreens[1], SidebarFooterScreen::kBrowserTools);
  EXPECT_EQ(kSidebarFooterRootScreens[2], SidebarFooterScreen::kMemory);
}

TEST(SidebarFooterPolicyTest, DetailScreensReturnToRoot) {
  EXPECT_EQ(ParentSidebarFooterScreen(SidebarFooterScreen::kContext),
            SidebarFooterScreen::kRoot);
  EXPECT_EQ(ParentSidebarFooterScreen(SidebarFooterScreen::kBrowserTools),
            SidebarFooterScreen::kRoot);
  EXPECT_EQ(ParentSidebarFooterScreen(SidebarFooterScreen::kMemory),
            SidebarFooterScreen::kRoot);
}

TEST(SidebarFooterPolicyTest, BrowserToolsContainOnlyCuratedShortcuts) {
  EXPECT_EQ(kSidebarFooterBrowserToolsActions[0],
            SidebarFooterBrowserAction::kReopenClosedTab);
  EXPECT_EQ(kSidebarFooterBrowserToolsActions[1],
            SidebarFooterBrowserAction::kDownloads);
  EXPECT_EQ(kSidebarFooterBrowserToolsActions[2],
            SidebarFooterBrowserAction::kHistory);
  EXPECT_EQ(kSidebarFooterBrowserToolsActions[3],
            SidebarFooterBrowserAction::kTabsFromOtherDevices);
  EXPECT_EQ(kSidebarFooterBrowserToolsActions[4],
            SidebarFooterBrowserAction::kManageExtensions);
}

TEST(SidebarFooterPolicyTest, LocalFallbackDoesNotInventRemoteTenant) {
  const SidebarFooterModel model =
      CreateLocalSidebarFooterModel(u"Yongjun Kim");

  ASSERT_EQ(model.contexts.size(), 1u);
  EXPECT_EQ(model.selected_context_index, 0u);
  EXPECT_EQ(model.contexts[0].tenant_name, u"Local");
  EXPECT_EQ(model.contexts[0].workspace_name, u"Personal");
  EXPECT_EQ(model.contexts[0].account_name, u"Yongjun Kim");
  EXPECT_FALSE(model.contexts[0].account_name_is_placeholder);
  EXPECT_FALSE(model.memory_available);
}

TEST(SidebarFooterPolicyTest, EmptyAccountGetsExplicitLocalLabel) {
  const SidebarFooterModel model = CreateLocalSidebarFooterModel({});

  ASSERT_EQ(model.contexts.size(), 1u);
  EXPECT_EQ(model.contexts[0].account_name, u"Local profile");
  EXPECT_TRUE(model.contexts[0].account_name_is_placeholder);
}

TEST(SidebarFooterPolicyTest, HoverMotionMatchesNativeTabRow) {
  EXPECT_EQ(base::Milliseconds(
                kSidebarMetrics.sidebar_row_hover_animation_duration_ms),
            TabView::kGlowHoverAnimationDuration);
}

class SidebarFooterViewTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    widget_->SetBounds(gfx::Rect(0, 0, 240, 50));
    footer_ = widget_->SetContentsView(CreateSidebarFooterView(
        CreateLocalSidebarFooterModel(u"Yongjun Kim"),
        SidebarContextSelectedCallback(), SidebarFooterBrowserActionCallback(),
        SidebarMemoryChangedCallback()));
    widget_->Show();
  }

  void TearDown() override {
    CloseBubble();
    footer_ = nullptr;
    widget_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  void Click(views::View* view) {
    ui::MouseEvent event(ui::EventType::kMouseReleased, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
    views::test::ButtonTestApi(static_cast<views::Button*>(view))
        .NotifyClick(event);
  }

  views::View* OpenBubble() {
    Click(footer_);
    return GetSidebarFooterMenuForTesting(*footer_);
  }

  void CloseBubble() {
    if (!footer_) {
      return;
    }
    if (views::View* const menu = GetSidebarFooterMenuForTesting(*footer_)) {
      if (menu->GetWidget()) {
        menu->GetWidget()->Close();
      }
      base::RunLoop().RunUntilIdle();
    }
  }

  views::Widget* widget() { return widget_.get(); }

  raw_ptr<views::View> footer_ = nullptr;

 private:
  std::unique_ptr<views::Widget> widget_;
};

TEST_F(SidebarFooterViewTest, BubbleTracksFooterWidthAndHasAccessibleTitle) {
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());
  EXPECT_EQ(menu->GetWidget()->ownership(),
            views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  ASSERT_EQ(menu->children().size(), 3u);
  for (views::View* const row : menu->children()) {
    EXPECT_EQ(row->GetPreferredSize().height(),
              kSidebarMetrics.sidebar_footer_root_row_height);
    const std::optional<gfx::RRectF> highlight =
        views::HighlightPathGenerator::GetRoundRectForView(row);
    ASSERT_TRUE(highlight.has_value());
    EXPECT_FLOAT_EQ(highlight->GetSimpleRadius(), 8.0f);
  }
  EXPECT_EQ(menu->GetPreferredSize().width(), footer_->width());
  EXPECT_EQ(menu->GetWidget()->widget_delegate()->GetAccessibleWindowTitle(),
            u"Workspace and account menu");
  views::BubbleDialogDelegate* const bubble_delegate =
      menu->GetWidget()->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(bubble_delegate);
  gfx::Rect expected_anchor = footer_->GetBoundsInScreen();
  expected_anchor.Offset(0, -kSidebarMetrics.sidebar_footer_menu_gap);
  EXPECT_EQ(bubble_delegate->GetAnchorRect(), expected_anchor);

  footer_->SetBoundsRect(gfx::Rect(0, 0, 300, footer_->height()));
  EXPECT_EQ(menu->GetPreferredSize().width(), footer_->width());
}

TEST_F(SidebarFooterViewTest, StateHighlightSharesTheFooterCornerRadius) {
  const std::optional<gfx::RRectF> highlight =
      views::HighlightPathGenerator::GetRoundRectForView(footer_);

  ASSERT_TRUE(highlight.has_value());
  EXPECT_TRUE(highlight->HasRoundedCorners());
  EXPECT_FLOAT_EQ(highlight->GetSimpleRadius(),
                  kSidebarMetrics.sidebar_footer_corner_radius);
}

TEST_F(SidebarFooterViewTest, InteractionStatesUseOnlyTheYeeSurface) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  auto* const button = static_cast<views::Button*>(footer_.get());
  ASSERT_TRUE(footer_->GetColorProvider());

  const auto surface_color = [this] {
    CHECK(footer_->background());
    return footer_->background()->color().ResolveToSkColor(
        footer_->GetColorProvider());
  };
  const auto surface_stroke = [this] {
    CHECK(footer_->GetBorder());
    return footer_->GetBorder()->color().ResolveToSkColor(
        footer_->GetColorProvider());
  };
  const auto expect_shared_radius = [this] {
    ASSERT_TRUE(footer_->background());
    EXPECT_EQ(
        footer_->background()->GetRoundedCornerRadii(),
        gfx::RoundedCornersF(kSidebarMetrics.sidebar_footer_corner_radius));
  };

  button->SetState(views::Button::STATE_NORMAL);
  const SkColor resting = surface_color();
  const SkColor resting_stroke = surface_stroke();
  const SidebarItemColors favorite_resting = ResolveSidebarItemColors(
      *footer_->GetColorProvider(), SidebarItemVisualState::kResting,
      /*hover_progress=*/0.0,
      /*frame_active=*/widget()->ShouldPaintAsActive(),
      /*persistent_surface=*/true);
  EXPECT_EQ(favorite_resting.fill, resting);
  EXPECT_EQ(favorite_resting.stroke, resting_stroke);
  EXPECT_NE(SK_ColorTRANSPARENT, resting_stroke);
  expect_shared_radius();

  button->SetState(views::Button::STATE_HOVERED);
  const SkColor hovered = surface_color();
  const SkColor hovered_stroke = surface_stroke();
  expect_shared_radius();
  EXPECT_NE(resting, hovered);
  EXPECT_NE(resting_stroke, hovered_stroke);
  views::FocusRing* const focus_ring = views::FocusRing::Get(footer_);
  ASSERT_TRUE(focus_ring);
  EXPECT_FALSE(focus_ring->ShouldPaintForTesting());

  button->SetState(views::Button::STATE_NORMAL);
  footer_->RequestFocus();
  EXPECT_FALSE(focus_ring->ShouldPaintForTesting());
  footer_->GetFocusManager()->SetFocusedView(nullptr);
  footer_->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  const SkColor focused = surface_color();
  expect_shared_radius();
  EXPECT_NE(resting, focused);
  EXPECT_EQ(focus_ring->GetColorId(), ui::kColorSysStateFocusRing);
  EXPECT_TRUE(focus_ring->ShouldPaintForTesting());

  views::InkDropHost* const ink_drop_host = views::InkDrop::Get(footer_);
  ASSERT_TRUE(ink_drop_host);
  std::unique_ptr<views::InkDrop> ink_drop = ink_drop_host->CreateInkDrop();
  ASSERT_TRUE(ink_drop);
  ink_drop->SetHovered(true);
  ink_drop->SetFocused(true);
  EXPECT_FALSE(ink_drop->IsHighlightFadingInOrVisible());
}

TEST_F(SidebarFooterViewTest, IdentityHierarchyUsesOneTonalMark) {
  const auto* const footer_button =
      static_cast<const HoverButton*>(footer_.get());
  ASSERT_TRUE(footer_->GetColorProvider());
  EXPECT_EQ(footer_button->title()->GetEnabledColor(),
            footer_->GetColorProvider()->GetColor(ui::kColorLabelForeground));

  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_EQ(menu->children().size(), 3u);

  views::View* const context_mark =
      menu->children()[0]->GetViewByID(kSidebarFooterWorkspaceMarkViewId);
  ASSERT_TRUE(context_mark);
  EXPECT_TRUE(context_mark->background());
  for (size_t index : {1u, 2u}) {
    views::View* const action_icon = menu->children()[index]->GetViewByID(
        kSidebarFooterMenuActionIconViewId);
    ASSERT_TRUE(action_icon);
    EXPECT_FALSE(action_icon->background());
  }
}

TEST_F(SidebarFooterViewTest, AccessibilityReportsPopupAndCurrentContext) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);

  ui::AXNodeData trigger_data;
  footer_->GetViewAccessibility().GetAccessibleNodeData(&trigger_data);
  EXPECT_EQ(trigger_data.GetHasPopup(), ax::mojom::HasPopup::kMenu);
  EXPECT_TRUE(trigger_data.HasState(ax::mojom::State::kCollapsed));
  EXPECT_FALSE(trigger_data.HasState(ax::mojom::State::kExpanded));

  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  trigger_data = ui::AXNodeData();
  footer_->GetViewAccessibility().GetAccessibleNodeData(&trigger_data);
  EXPECT_TRUE(trigger_data.HasState(ax::mojom::State::kExpanded));
  EXPECT_FALSE(trigger_data.HasState(ax::mojom::State::kCollapsed));

  Click(menu->children()[0]);
  ASSERT_EQ(menu->children().size(), 3u);
  ui::AXNodeData context_data;
  menu->children()[2]->GetViewAccessibility().GetAccessibleNodeData(
      &context_data);
  EXPECT_TRUE(
      context_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

TEST_F(SidebarFooterViewTest, LocalPlaceholderUsesGenericAccountIcon) {
  views::View* avatar = footer_->GetViewByID(kSidebarFooterAvatarViewId);
  ASSERT_TRUE(avatar);
  EXPECT_FALSE(
      avatar->GetViewByID(kSidebarFooterAvatarIconViewId)->GetVisible());
  EXPECT_TRUE(
      avatar->GetViewByID(kSidebarFooterAvatarLabelViewId)->GetVisible());

  footer_ = nullptr;
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      CreateLocalSidebarFooterModel({}), SidebarContextSelectedCallback(),
      SidebarFooterBrowserActionCallback(), SidebarMemoryChangedCallback()));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));

  avatar = footer_->GetViewByID(kSidebarFooterAvatarViewId);
  ASSERT_TRUE(avatar);
  EXPECT_TRUE(
      avatar->GetViewByID(kSidebarFooterAvatarIconViewId)->GetVisible());
  EXPECT_FALSE(
      avatar->GetViewByID(kSidebarFooterAvatarLabelViewId)->GetVisible());
}

TEST_F(SidebarFooterViewTest, IdentityMarksPreserveSupplementaryCodePoints) {
  footer_ = nullptr;
  SidebarFooterModel model;
  model.contexts.push_back({.tenant_name = u"Local",
                            .workspace_name = u"\U0001F680 Launch",
                            .account_name = u"\U0001F600 User"});
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      std::move(model), SidebarContextSelectedCallback(),
      SidebarFooterBrowserActionCallback(), SidebarMemoryChangedCallback()));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));

  const auto* const workspace_mark = static_cast<const views::Label*>(
      footer_->GetViewByID(kSidebarFooterWorkspaceMarkViewId));
  const auto* const account_mark = static_cast<const views::Label*>(
      footer_->GetViewByID(kSidebarFooterAvatarLabelViewId));
  ASSERT_TRUE(workspace_mark);
  ASSERT_TRUE(account_mark);
  EXPECT_EQ(workspace_mark->GetText(), u"\U0001F680");
  EXPECT_EQ(account_mark->GetText(), u"\U0001F600U");
}

TEST_F(SidebarFooterViewTest, TriggerReclickClosesTheSameBubble) {
  ASSERT_TRUE(OpenBubble());
  Click(footer_);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
}

TEST_F(SidebarFooterViewTest, ThemeChangeAfterCloseHasNoDanglingBubble) {
  ASSERT_TRUE(OpenBubble());

  Click(footer_);
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));

  // The Browser Widget can receive a ThemeChanged notification in the same
  // task that closes an anchored bubble and again after native teardown. Both
  // paths must be safe and leave no observer owned by a detached delegate.
  widget()->ThemeChanged();
  base::RunLoop().RunUntilIdle();
  widget()->ThemeChanged();

  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
}

TEST_F(SidebarFooterViewTest,
       ThemeChangeAfterNativeTeardownHasNoDanglingBubble) {
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());

  // CloseNow bypasses the synchronous Close callback. FooterBubble's native
  // teardown observer must still detach the anchor before GetWidget() becomes
  // null.
  menu->GetWidget()->CloseNow();
  widget()->ThemeChanged();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));

  // Native teardown releases the client-owned wrapper as well as detaching
  // the anchor, so the footer can immediately create and close a fresh menu.
  ASSERT_TRUE(OpenBubble());
  CloseBubble();
  widget()->ThemeChanged();
}

TEST_F(SidebarFooterViewTest, BrowserToolsDispatchOnlyCuratedActions) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  std::vector<SidebarFooterBrowserAction> received_actions;
  CloseBubble();
  footer_ = nullptr;
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      CreateLocalSidebarFooterModel(u"Yongjun Kim"),
      SidebarContextSelectedCallback(),
      base::BindRepeating(
          [](std::vector<SidebarFooterBrowserAction>* actions,
             SidebarFooterBrowserAction action) { actions->push_back(action); },
          &received_actions),
      SidebarMemoryChangedCallback()));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));

  for (size_t index = 0; index < kSidebarFooterBrowserToolsActions.size();
       ++index) {
    views::View* menu = OpenBubble();
    ASSERT_TRUE(menu);
    ASSERT_EQ(menu->children().size(), 3u);
    Click(menu->children()[1]);
    ASSERT_EQ(menu->children().size(), 6u);
    Click(menu->children()[index + 1]);
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  }

  EXPECT_EQ(received_actions, std::vector<SidebarFooterBrowserAction>(
                                  kSidebarFooterBrowserToolsActions.begin(),
                                  kSidebarFooterBrowserToolsActions.end()));
}

TEST_F(SidebarFooterViewTest, MemoryChangeLeavesTheViewThroughCallback) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  bool memory_enabled = false;
  CloseBubble();
  footer_ = nullptr;
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      [] {
        SidebarFooterModel model =
            CreateLocalSidebarFooterModel(u"Yongjun Kim");
        model.memory_available = true;
        return model;
      }(),
      SidebarContextSelectedCallback(), SidebarFooterBrowserActionCallback(),
      base::BindRepeating([](bool* value, bool enabled) { *value = enabled; },
                          &memory_enabled)));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));

  views::View* menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_EQ(menu->children().size(), 3u);
  Click(menu->children()[2]);
  ASSERT_EQ(menu->children().size(), 3u);
  Click(menu->children()[2]);
  EXPECT_TRUE(memory_enabled);
}

}  // namespace
}  // namespace yee
