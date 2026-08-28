// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/split_pane_controls.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

class SplitPaneControlsTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    root_ = widget_->SetContentsView(std::make_unique<views::View>());
    widget_->SetBounds(gfx::Rect(0, 0, 600, 500));

    anchor_ = root_->AddChildView(std::make_unique<views::View>());
    anchor_->SetBounds(295, 0, 10, 500);
    controls_ = root_->AddChildView(CreateSplitPaneControlsView(
        SplitPaneControlCallbacks{
            base::BindRepeating(&SplitPaneControlsTest::Increment,
                                base::Unretained(this), &toggle_count_),
            base::BindRepeating(&SplitPaneControlsTest::Increment,
                                base::Unretained(this), &reverse_count_),
            base::BindRepeating(&SplitPaneControlsTest::Increment,
                                base::Unretained(this), &exit_count_),
            base::BindRepeating(&SplitPaneControlsTest::SetHandleAnchored,
                                base::Unretained(this))},
        anchor_));
    controls_->SetBoundsRect(
        GetSplitPaneControlsBounds(*controls_, root_->GetLocalBounds()));
    widget_->Show();
  }

  void TearDown() override {
    controls_ = nullptr;
    anchor_ = nullptr;
    root_ = nullptr;
    widget_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  views::Button* button(size_t index) {
    CHECK_LT(index, controls_->children().size());
    return static_cast<views::Button*>(controls_->children()[index]);
  }

  void Click(size_t index) {
    ui::MouseEvent event(ui::EventType::kMouseReleased, gfx::Point(),
                         gfx::Point(), ui::EventTimeForNow(),
                         ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
    views::test::ButtonTestApi(button(index)).NotifyClick(event);
  }

  raw_ptr<views::View> controls_ = nullptr;
  int toggle_count_ = 0;
  int reverse_count_ = 0;
  int exit_count_ = 0;
  bool handle_anchored_ = false;

 private:
  void Increment(int* value) { ++*value; }
  void SetHandleAnchored(bool anchored) { handle_anchored_ = anchored; }

  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> root_ = nullptr;
  raw_ptr<views::View> anchor_ = nullptr;
};

TEST_F(SplitPaneControlsTest, ButtonsKeepDocumentedOrderAndCallbacks) {
  ASSERT_EQ(controls_->children().size(), 3u);
  for (size_t i = 0; i < controls_->children().size(); ++i) {
    EXPECT_FALSE(button(i)->GetTooltipText().empty());
    EXPECT_EQ(button(i)->GetTooltipText(),
              button(i)->GetViewAccessibility().GetCachedName());
  }

  Click(0);
  Click(1);
  Click(2);
  EXPECT_EQ(toggle_count_, 1);
  EXPECT_EQ(reverse_count_, 1);
  EXPECT_EQ(exit_count_, 1);
}

TEST_F(SplitPaneControlsTest, LayoutTooltipTracksCurrentOrientation) {
  const std::u16string side_by_side_tooltip = button(0)->GetTooltipText();
  UpdateSplitPaneControls(*controls_, /*side_by_side=*/false,
                          /*active_at_start=*/true);

  EXPECT_FALSE(button(0)->GetTooltipText().empty());
  EXPECT_NE(button(0)->GetTooltipText(), side_by_side_tooltip);
  EXPECT_EQ(button(0)->GetTooltipText(),
            button(0)->GetViewAccessibility().GetCachedName());
}

TEST_F(SplitPaneControlsTest, ReducedMotionRevealAndDismissAreAtomic) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  SetSplitPaneControlsEnabled(*controls_, true);
  SetSplitPaneControlsAnchorHovered(*controls_, true);
  task_environment()->FastForwardBy(base::Milliseconds(70));

  EXPECT_TRUE(controls_->GetVisible());
  EXPECT_TRUE(handle_anchored_);
  EXPECT_FLOAT_EQ(controls_->layer()->opacity(), 1.0f);

  DismissSplitPaneControls(*controls_);
  EXPECT_FALSE(controls_->GetVisible());
  EXPECT_FALSE(handle_anchored_);
  EXPECT_FLOAT_EQ(controls_->layer()->opacity(), 0.0f);
}

TEST_F(SplitPaneControlsTest, AnchoredBoundsStayInsideSplitCanvas) {
  UpdateSplitPaneControlsAnchor(*controls_, gfx::Point(4, 4));
  const gfx::Rect top_left =
      GetSplitPaneControlsBounds(*controls_, gfx::Rect(600, 500));
  EXPECT_TRUE(gfx::Rect(600, 500).Contains(top_left));

  UpdateSplitPaneControlsAnchor(*controls_, gfx::Point(596, 496));
  const gfx::Rect bottom_right =
      GetSplitPaneControlsBounds(*controls_, gfx::Rect(600, 500));
  EXPECT_TRUE(gfx::Rect(600, 500).Contains(bottom_right));
}

}  // namespace
}  // namespace yee
