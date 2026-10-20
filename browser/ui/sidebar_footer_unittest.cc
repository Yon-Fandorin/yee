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
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
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

gfx::Insets FooterSurfaceInsetsForTesting() {
  return gfx::Insets::VH(
      kSidebarMetrics.sidebar_footer_surface_vertical_inset(),
      kSidebarMetrics.sidebar_footer_surface_horizontal_inset());
}

TEST(SidebarFooterPolicyTest, LocalFallbackDoesNotInventRemoteTenant) {
  const SidebarFooterModel model =
      CreateLocalSidebarFooterModel(u"Yongjun Kim");

  ASSERT_EQ(model.contexts.size(), 1u);
  EXPECT_EQ(model.selected_context_index, 0u);
  EXPECT_EQ(model.contexts[0].tenant_name,
            l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_LOCAL));
  EXPECT_TRUE(model.contexts[0].is_local_context);
  EXPECT_EQ(model.contexts[0].workspace_name,
            l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_PERSONAL));
  EXPECT_EQ(model.contexts[0].account_name, u"Yongjun Kim");
  EXPECT_FALSE(model.contexts[0].account_name_is_placeholder);
  EXPECT_FALSE(model.memory_available);

  const SidebarFooterModel empty_account = CreateLocalSidebarFooterModel({});
  ASSERT_EQ(empty_account.contexts.size(), 1u);
  EXPECT_EQ(empty_account.contexts[0].account_name,
            l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_LOCAL_PROFILE));
  EXPECT_TRUE(empty_account.contexts[0].account_name_is_placeholder);
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
        SidebarMemoryChangedCallback(),
        base::BindRepeating(
            [](int* locks) {
              ++*locks;
              return base::ScopedClosureRunner(
                  base::BindOnce([](int* locks) { --*locks; }, locks));
            },
            &popup_locks_)));
    widget_->Show();
  }

  void TearDown() override {
    CloseBubble();
    footer_ = nullptr;
    widget_.reset();
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(0, popup_locks_);
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

  void ExpectFooterSurface(bool paint_background) {
    ASSERT_EQ(paint_background, footer_->background() != nullptr);
    if (paint_background) {
      EXPECT_NE(0u, SkColorGetA(footer_->background()->color().ResolveToSkColor(
                        footer_->GetColorProvider())));
      EXPECT_EQ(
          footer_->background()->GetRoundedCornerRadii(),
          gfx::RoundedCornersF(kSidebarMetrics.sidebar_footer_corner_radius));
    }

    ASSERT_TRUE(footer_->GetBorder());
    EXPECT_EQ(FooterSurfaceInsetsForTesting(), footer_->GetInsets());
    EXPECT_TRUE(footer_->GetBorder()->GetMinimumSize().IsEmpty());
  }

  raw_ptr<views::View> footer_ = nullptr;
  int popup_locks_ = 0;

private:
  std::unique_ptr<views::Widget> widget_;
};

TEST_F(SidebarFooterViewTest, BubbleUsesContentWidthAndTracksAnchorPosition) {
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());
  EXPECT_EQ(menu->GetWidget()->ownership(),
            views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  ASSERT_EQ(menu->children().size(), 4u);
  for (int id :
       {kSidebarFooterRootContextViewId, kSidebarFooterRootBrowserToolsViewId,
        kSidebarFooterRootMemoryViewId}) {
    views::View* const row = menu->GetViewByID(id);
    ASSERT_TRUE(row);
    EXPECT_EQ(row->GetPreferredSize().height(),
              kSidebarMetrics.sidebar_footer_menu_row_height);
    const std::optional<gfx::RRectF> highlight =
        views::HighlightPathGenerator::GetRoundRectForView(row);
    ASSERT_TRUE(highlight.has_value());
    EXPECT_FLOAT_EQ(highlight->GetSimpleRadius(),
                    kSidebarMetrics.sidebar_footer_menu_corner_radius);
  }
  const int menu_width = menu->GetPreferredSize().width();
  EXPECT_GE(menu_width, kSidebarMetrics.sidebar_footer_menu_min_width);
  EXPECT_LE(menu_width, kSidebarMetrics.sidebar_footer_menu_max_width);
  EXPECT_EQ(menu->GetWidget()->widget_delegate()->GetAccessibleWindowTitle(),
            l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_MENU_TITLE));
  views::BubbleDialogDelegate* const bubble_delegate =
      menu->GetWidget()->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(bubble_delegate);
  gfx::Rect expected_anchor = footer_->GetBoundsInScreen();
  expected_anchor.Offset(0, -kSidebarMetrics.sidebar_footer_menu_gap);
  EXPECT_EQ(bubble_delegate->GetAnchorRect(), expected_anchor);

  for (int sidebar_width : {160, 300, 400}) {
    footer_->SetBoundsRect(gfx::Rect(0, 0, sidebar_width, footer_->height()));
    EXPECT_EQ(menu->GetPreferredSize().width(), menu_width);
    expected_anchor = footer_->GetBoundsInScreen();
    expected_anchor.Offset(0, -kSidebarMetrics.sidebar_footer_menu_gap);
    EXPECT_EQ(bubble_delegate->GetAnchorRect(), expected_anchor);
  }
}

TEST_F(SidebarFooterViewTest, CompactMenuKeepsActionTitlesVisible) {
  footer_->SetBoundsRect(gfx::Rect(0, 0, 184, footer_->height()));
  ui::AXNodeData trigger_data;
  footer_->GetViewAccessibility().GetAccessibleNodeData(&trigger_data);
  const std::u16string trigger_name =
      trigger_data.GetString16Attribute(ax::mojom::StringAttribute::kName);
  EXPECT_NE(trigger_name.find(u"Yongjun Kim"), std::u16string::npos);
  EXPECT_EQ(trigger_name.find(u"Local ·"), std::u16string::npos);
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  EXPECT_GT(menu->GetPreferredSize().width(), footer_->width());
  views::View* const tools =
      menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId);
  views::View* const memory = menu->GetViewByID(kSidebarFooterRootMemoryViewId);
  ASSERT_TRUE(tools);
  ASSERT_TRUE(memory);
  EXPECT_EQ(tools->parent(), memory->parent());
  EXPECT_EQ(tools->x(), memory->x());
  EXPECT_LE(tools->bounds().bottom(), memory->y());
  for (int id :
       {kSidebarFooterRootBrowserToolsViewId, kSidebarFooterRootMemoryViewId}) {
    auto* row = static_cast<HoverButton*>(menu->GetViewByID(id));
    ASSERT_TRUE(row);
    EXPECT_GT(row->title()->width(), 0);
    EXPECT_GE(row->title()->width(), row->title()->GetPreferredSize().width());
    ui::AXNodeData row_data;
    row->GetViewAccessibility().GetAccessibleNodeData(&row_data);
    EXPECT_EQ(row_data.GetString16Attribute(ax::mojom::StringAttribute::kName),
              row->title()->GetText());
  }
}

TEST_F(SidebarFooterViewTest, PointerOpenDoesNotPaintASelectedCard) {
  views::View* menu = OpenBubble();
  ASSERT_TRUE(menu);
  views::View* initial_action =
      menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId);
  ASSERT_TRUE(initial_action);
  EXPECT_FALSE(initial_action->background());
  views::FocusRing* const focus_ring = views::FocusRing::Get(initial_action);
  ASSERT_TRUE(focus_ring);
  EXPECT_FALSE(focus_ring->ShouldPaintForTesting());
  auto* button = static_cast<views::Button*>(initial_action);
  button->SetState(views::Button::STATE_HOVERED);
  EXPECT_TRUE(initial_action->background());
  button->SetState(views::Button::STATE_NORMAL);
  EXPECT_FALSE(initial_action->background());

  CloseBubble();
  ui::KeyEvent key(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  views::test::ButtonTestApi(static_cast<views::Button*>(footer_))
      .NotifyClick(key);
  menu = GetSidebarFooterMenuForTesting(*footer_);
  ASSERT_TRUE(menu);
  initial_action = menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId);
  ASSERT_TRUE(initial_action);
  EXPECT_TRUE(initial_action->HasFocus());
  EXPECT_TRUE(views::FocusRing::Get(initial_action)->ShouldPaintForTesting());
}

TEST_F(SidebarFooterViewTest, ArrowNavigationPreservesPopupWidth) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  const int width = menu->GetPreferredSize().width();
  const auto send_key = [menu](ui::KeyboardCode code) {
    ui::KeyEvent key(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    menu->GetWidget()->OnKeyEvent(&key);
    EXPECT_TRUE(key.handled());
  };
  menu->GetViewByID(kSidebarFooterRootContextViewId)->RequestFocus();
  send_key(ui::VKEY_DOWN);
  views::View* const tools =
      menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId);
  ASSERT_TRUE(tools);
  EXPECT_TRUE(tools->HasFocus());
  ASSERT_TRUE(views::FocusRing::Get(tools));
  EXPECT_TRUE(views::FocusRing::Get(tools)->ShouldPaintForTesting());
  EXPECT_FALSE(tools->background());
  // 같은 행으로 포인터가 들어와도 이전 키보드 윤곽은 남지 않는다.
  ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(), ui::EF_NONE,
                         ui::EF_NONE);
  tools->OnMouseEntered(entered);
  EXPECT_TRUE(tools->HasFocus());
  EXPECT_FALSE(views::FocusRing::Get(tools)->ShouldPaintForTesting());
  send_key(ui::VKEY_HOME);
  EXPECT_TRUE(views::FocusRing::Get(tools)->ShouldPaintForTesting());
  ui::MouseEvent moved(ui::EventType::kMouseMoved, gfx::Point(2, 2),
                       gfx::Point(2, 2), ui::EventTimeForNow(), ui::EF_NONE,
                       ui::EF_NONE);
  tools->OnMouseMoved(moved);
  EXPECT_FALSE(views::FocusRing::Get(tools)->ShouldPaintForTesting());
  EXPECT_TRUE(tools->background());
  ui::MouseEvent pressed(ui::EventType::kMousePressed, gfx::Point(2, 2),
                         gfx::Point(2, 2), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  EXPECT_TRUE(tools->OnMousePressed(pressed));
  EXPECT_EQ(views::InkDropState::ACTION_PENDING,
            views::InkDrop::Get(tools)->GetInkDrop()->GetTargetInkDropState());
  tools->OnMouseCaptureLost();
  EXPECT_EQ(views::InkDropState::HIDDEN,
            views::InkDrop::Get(tools)->GetInkDrop()->GetTargetInkDropState());
  tools->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(tools->HasFocus());
  send_key(ui::VKEY_DOWN);
  views::View* const memory = menu->GetViewByID(kSidebarFooterRootMemoryViewId);
  ASSERT_TRUE(memory);
  EXPECT_TRUE(memory->HasFocus());
  EXPECT_TRUE(views::FocusRing::Get(memory)->ShouldPaintForTesting());
  send_key(ui::VKEY_UP);
  EXPECT_TRUE(tools->HasFocus());
  const ui::Accelerator back(ui::VKEY_ESCAPE, ui::EF_NONE);
  for (int id :
       {kSidebarFooterRootBrowserToolsViewId, kSidebarFooterRootContextViewId,
        kSidebarFooterRootMemoryViewId}) {
    SCOPED_TRACE(id);
    Click(menu->GetViewByID(id));
    EXPECT_EQ(menu->GetPreferredSize().width(), width);
    views::View* const back_row = menu->children()[0];
    EXPECT_TRUE(back_row->HasFocus());
    EXPECT_FALSE(views::FocusRing::Get(back_row)->ShouldPaintForTesting());
    if (id == kSidebarFooterRootMemoryViewId) {
      // 비활성 행만 남은 화면에서도 Tab은 같은 Back 행에 키보드 윤곽을 켠다.
      menu->GetFocusManager()->AdvanceFocus(/*reverse=*/false);
      EXPECT_TRUE(back_row->HasFocus());
      EXPECT_TRUE(views::FocusRing::Get(back_row)->ShouldPaintForTesting());
    }
    EXPECT_TRUE(menu->GetFocusManager()->ProcessAccelerator(back));
    ASSERT_EQ(menu, GetSidebarFooterMenuForTesting(*footer_));
    EXPECT_TRUE(menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId));
    EXPECT_TRUE(views::FocusRing::Get(
                    menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId))
                    ->ShouldPaintForTesting());
    EXPECT_EQ(menu->GetPreferredSize().width(), width);
  }
}

TEST_F(SidebarFooterViewTest, LongNamesKeepThePopupBoundedAndAccessible) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  footer_ = nullptr;
  auto model = CreateLocalSidebarFooterModel(std::u16string(120, u'a'));
  model.contexts[0].workspace_name = std::u16string(160, u'W');
  const std::u16string workspace_name = model.contexts[0].workspace_name;
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      std::move(model), SidebarContextSelectedCallback(),
      SidebarFooterBrowserActionCallback(), SidebarMemoryChangedCallback()));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 160, 50));
  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->GetPreferredSize().width(),
            kSidebarMetrics.sidebar_footer_menu_max_width);
  Click(menu->GetViewByID(kSidebarFooterRootContextViewId));
  ASSERT_EQ(menu->children().size(), 3u);
  auto* const context = static_cast<HoverButton*>(menu->children()[2]);
  EXPECT_GT(context->title()->width(), 0);
  ui::AXNodeData data;
  context->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_NE(data.GetString16Attribute(ax::mojom::StringAttribute::kName)
                .find(workspace_name),
            std::u16string::npos);
}

TEST_F(SidebarFooterViewTest, InteractionStatesUseOnlyTheYeeSurface) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  const std::optional<gfx::RRectF> highlight =
      views::HighlightPathGenerator::GetRoundRectForView(footer_);

  ASSERT_TRUE(highlight.has_value());
  EXPECT_TRUE(highlight->HasRoundedCorners());
  EXPECT_FLOAT_EQ(highlight->GetSimpleRadius(),
                  kSidebarMetrics.sidebar_footer_corner_radius);
  auto *const button = static_cast<views::Button *>(footer_.get());
  ASSERT_TRUE(footer_->GetColorProvider());

  button->SetState(views::Button::STATE_NORMAL);
  ExpectFooterSurface(/*paint_background=*/false);

  button->SetState(views::Button::STATE_HOVERED);
  ExpectFooterSurface(/*paint_background=*/true);
  views::FocusRing *const focus_ring = views::FocusRing::Get(footer_);
  ASSERT_TRUE(focus_ring);
  EXPECT_FALSE(focus_ring->ShouldPaintForTesting());

  button->SetState(views::Button::STATE_NORMAL);
  ExpectFooterSurface(/*paint_background=*/false);
  footer_->RequestFocus();
  EXPECT_FALSE(focus_ring->ShouldPaintForTesting());
  ExpectFooterSurface(/*paint_background=*/false);
  footer_->GetFocusManager()->SetFocusedView(nullptr);
  footer_->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ExpectFooterSurface(/*paint_background=*/true);
  EXPECT_EQ(focus_ring->GetColorId(), ui::kColorSysStateFocusRing);
  EXPECT_TRUE(focus_ring->ShouldPaintForTesting());

  views::InkDropHost *const ink_drop_host = views::InkDrop::Get(footer_);
  ASSERT_TRUE(ink_drop_host);
  std::unique_ptr<views::InkDrop> ink_drop = ink_drop_host->CreateInkDrop();
  ASSERT_TRUE(ink_drop);
  ink_drop->SetHovered(true);
  ink_drop->SetFocused(true);
  EXPECT_FALSE(ink_drop->IsHighlightFadingInOrVisible());
}

TEST_F(SidebarFooterViewTest, WorkspaceMarkAppearsOnlyInFooter) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  const auto* const footer_button =
      static_cast<const HoverButton*>(footer_.get());
  ASSERT_TRUE(footer_->GetColorProvider());
  EXPECT_EQ(footer_button->title()->GetEnabledColor(),
            footer_->GetColorProvider()->GetColor(ui::kColorLabelForeground));

  views::View* const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_EQ(menu->children().size(), 4u);

  views::View* const footer_mark =
      footer_->GetViewByID(kSidebarFooterWorkspaceMarkViewId);
  ASSERT_TRUE(footer_mark);
  views::View* const chevron =
      footer_->GetViewByID(kSidebarFooterChevronViewId);
  ASSERT_TRUE(chevron);
  EXPECT_EQ(footer_, chevron->parent());
  EXPECT_TRUE(footer_mark->background());
  EXPECT_FALSE(menu->GetViewByID(kSidebarFooterWorkspaceMarkViewId));
  for (int id :
       {kSidebarFooterRootBrowserToolsViewId, kSidebarFooterRootMemoryViewId}) {
    views::View* const action_icon =
        menu->GetViewByID(id)->GetViewByID(kSidebarFooterMenuActionIconViewId);
    ASSERT_TRUE(action_icon);
    EXPECT_FALSE(action_icon->background());
  }
  Click(menu->GetViewByID(kSidebarFooterRootContextViewId));
  EXPECT_FALSE(menu->GetViewByID(kSidebarFooterWorkspaceMarkViewId));
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

  Click(menu->GetViewByID(kSidebarFooterRootContextViewId));
  ASSERT_EQ(menu->children().size(), 3u);
  ui::AXNodeData back_data;
  menu->children()[0]->GetViewAccessibility().GetAccessibleNodeData(&back_data);
  EXPECT_EQ(
      back_data.GetString16Attribute(ax::mojom::StringAttribute::kDescription),
      l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_BACK));
  ui::AXNodeData context_data;
  menu->children()[2]->GetViewAccessibility().GetAccessibleNodeData(
      &context_data);
  EXPECT_TRUE(
      context_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}

TEST_F(SidebarFooterViewTest, WorkspaceMarkPreservesSupplementaryCodePoints) {
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
  ASSERT_TRUE(workspace_mark);
  EXPECT_EQ(workspace_mark->GetText(), u"\U0001F680");
}

TEST_F(SidebarFooterViewTest, ContextSwitchPreservesOptionalAccountLabel) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  footer_ = nullptr;
  SidebarFooterModel model;
  model.contexts = {
      {.workspace_name = u"Workspace"},
      {.workspace_name = u"Personal", .account_name = u"Account"}};
  footer_ = widget()->SetContentsView(CreateSidebarFooterView(
      std::move(model), SidebarContextSelectedCallback(),
      SidebarFooterBrowserActionCallback(), SidebarMemoryChangedCallback()));
  auto* trigger = static_cast<HoverButton*>(footer_.get());
  views::View* const labels = trigger->title()->parent();
  ASSERT_EQ(labels->children().size(), 2u);
  auto* account = static_cast<views::Label*>(labels->children()[1]);

  for (size_t index : {0u, 1u, 0u}) {
    SCOPED_TRACE(index);
    views::View* const menu = OpenBubble();
    ASSERT_TRUE(menu);
    Click(menu->GetViewByID(kSidebarFooterRootContextViewId));
    Click(menu->children()[2 + index]);
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
    EXPECT_EQ(index == 1u, account->GetVisible());
    EXPECT_EQ(index == 1u ? u"Account" : u"", account->GetText());

    // 폭 변경 후에도 native 자동 갱신이 계정의 접근성 이름을 지우지 않는다.
    footer_->SetBoundsRect(gfx::Rect(0, 0, 160, 50));
    footer_->DeprecatedLayoutImmediately();
    ui::AXNodeData data;
    footer_->GetViewAccessibility().GetAccessibleNodeData(&data);
    const std::u16string name =
        data.GetString16Attribute(ax::mojom::StringAttribute::kName);
    EXPECT_NE(name.find(index == 1u ? u"Personal" : u"Workspace"),
              std::u16string::npos);
    EXPECT_EQ(index == 1u, name.find(u"Account") != std::u16string::npos);
    footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));
  }
}

TEST_F(SidebarFooterViewTest, TriggerReclickClosesTheSameBubble) {
  ASSERT_TRUE(OpenBubble());
  EXPECT_NE(
      views::InkDropState::ACTIVATED,
      views::InkDrop::Get(footer_)->GetInkDrop()->GetTargetInkDropState());
  EXPECT_EQ(1, popup_locks_);
  Click(footer_);
  // 닫기 콜백 이후에도 실제 Widget을 삭제할 때까지 잠금을 유지한다.
  EXPECT_EQ(1, popup_locks_);
  widget()->ThemeChanged();
  base::RunLoop().RunUntilIdle();
  widget()->ThemeChanged();
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  EXPECT_EQ(0, popup_locks_);
}

TEST_F(SidebarFooterViewTest,
       PointerDismissDoesNotRestoreFocusOrOutlinedSurface) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  CloseBubble();
  footer_ = nullptr;
  auto root = std::make_unique<views::View>();
  footer_ = root->AddChildView(CreateSidebarFooterView(
      CreateLocalSidebarFooterModel(u"Yongjun Kim"),
      SidebarContextSelectedCallback(), SidebarFooterBrowserActionCallback(),
      SidebarMemoryChangedCallback()));
  views::View* const focus_target =
      root->AddChildView(std::make_unique<views::View>());
  focus_target->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  widget()->SetBounds(gfx::Rect(0, 0, 240, 70));
  widget()->SetContentsView(std::move(root));
  footer_->SetBoundsRect(gfx::Rect(0, 0, 240, 50));
  focus_target->SetBoundsRect(gfx::Rect(0, 50, 240, 20));

  views::FocusManager *const focus_manager = footer_->GetFocusManager();
  ASSERT_TRUE(focus_manager);
  // 현재 포커스와 비활성 창에 저장된 포커스 모두 닫힌 Footer를 복원하면 안
  // 된다.
  for (bool stored_only : {false, true}) {
    SCOPED_TRACE(stored_only);
    views::View *const menu = OpenBubble();
    ASSERT_TRUE(menu);
    focus_manager->SetFocusedViewWithReason(
        footer_, views::FocusManager::FocusChangeReason::kFocusRestore);
    if (stored_only) {
      focus_manager->StoreFocusedView(/*clear_native_focus=*/false);
    }
    ASSERT_EQ(stored_only ? nullptr : footer_, focus_manager->GetFocusedView());
    ASSERT_EQ(footer_, focus_manager->GetStoredFocusView());
    menu->GetWidget()->CloseWithReason(views::Widget::ClosedReason::kLostFocus);
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
    EXPECT_FALSE(footer_->HasFocus());
    EXPECT_EQ(nullptr, focus_manager->GetStoredFocusView());
    EXPECT_FALSE(focus_manager->RestoreFocusedView());
  }

  views::View *const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());
  ExpectFooterSurface(/*paint_background=*/true);
  footer_->GetFocusManager()->SetFocusedViewWithReason(
      focus_target, views::FocusManager::FocusChangeReason::kDirectFocusChange);
  menu->GetWidget()->CloseWithReason(views::Widget::ClosedReason::kLostFocus);
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  EXPECT_FALSE(footer_->HasFocus());
  EXPECT_TRUE(focus_target->HasFocus());
  EXPECT_EQ(focus_target, focus_manager->GetStoredFocusView());
  ExpectFooterSurface(/*paint_background=*/false);
}

TEST_F(SidebarFooterViewTest, EscapeDismissRestoresKeyboardTraversalFocus) {
  views::View *const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());
  footer_->GetFocusManager()->SetFocusedView(nullptr);
  EXPECT_TRUE(menu->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE)));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  EXPECT_TRUE(footer_->HasFocus());
  ASSERT_TRUE(footer_->GetFocusManager());
  EXPECT_EQ(views::FocusManager::FocusChangeReason::kFocusTraversal,
            footer_->GetFocusManager()->focus_change_reason());
  views::FocusRing *const focus_ring = views::FocusRing::Get(footer_);
  ASSERT_TRUE(focus_ring);
  EXPECT_TRUE(focus_ring->ShouldPaintForTesting());
  ExpectFooterSurface(/*paint_background=*/true);
}

TEST_F(SidebarFooterViewTest,
       ThemeChangeAfterNativeTeardownHasNoDanglingBubble) {
  views::View *const menu = OpenBubble();
  ASSERT_TRUE(menu);
  ASSERT_TRUE(menu->GetWidget());

  // CloseNow는 정상 닫기 콜백을 건너뛰어도 anchor와 잠금을 정리해야 한다.
  EXPECT_EQ(1, popup_locks_);
  menu->GetWidget()->CloseNow();
  EXPECT_EQ(0, popup_locks_);
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  widget()->ThemeChanged();

  // 이전 팝업의 지연 삭제 전에 키보드로 다시 열어도 새 팝업은 유지한다.
  ui::KeyEvent key(ui::EventType::kKeyReleased, ui::VKEY_RETURN, ui::EF_NONE);
  views::test::ButtonTestApi(static_cast<views::Button*>(footer_))
      .NotifyClick(key);
  ASSERT_TRUE(GetSidebarFooterMenuForTesting(*footer_));
  EXPECT_EQ(1, popup_locks_);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(GetSidebarFooterMenuForTesting(*footer_));
  EXPECT_EQ(1, popup_locks_);
  CloseBubble();
  EXPECT_EQ(0, popup_locks_);
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
    ASSERT_EQ(menu->children().size(), 4u);
    Click(menu->GetViewByID(kSidebarFooterRootBrowserToolsViewId));
    ASSERT_EQ(menu->children().size(), 7u);
    Click(menu->children()[index + 2]);
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer_));
  }

  EXPECT_EQ(received_actions,
            (std::vector<SidebarFooterBrowserAction>{
                SidebarFooterBrowserAction::kReopenClosedTab,
                SidebarFooterBrowserAction::kDownloads,
                SidebarFooterBrowserAction::kHistory,
                SidebarFooterBrowserAction::kTabsFromOtherDevices,
                SidebarFooterBrowserAction::kManageExtensions}));
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
  ASSERT_EQ(menu->children().size(), 4u);
  Click(menu->GetViewByID(kSidebarFooterRootMemoryViewId));
  Click(menu->GetViewByID(kSidebarFooterMemoryToggleViewId));
  EXPECT_TRUE(memory_enabled);
}

}  // namespace
}  // namespace yee
