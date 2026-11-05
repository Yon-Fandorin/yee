// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"

#include <memory>
#include <tuple>
#include <vector>

#include "base/auto_reset.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/test/scoped_command_line.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/layout/layout_manager_base.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace {

class MockAnimatingLayoutManagerDelegate
    : public TabCollectionAnimatingLayoutManager::Delegate {
 public:
  MOCK_METHOD(bool, IsViewDragging, (const views::View&), (const, override));
  MOCK_METHOD(void, OnAnimationEnded, (), (override));
};

class TestLayoutManager : public views::LayoutManagerBase {
 public:
  explicit TestLayoutManager(
      TabCollectionAnimatingLayoutManager::AnimationAxis axis)
      : axis_(axis) {}
  ~TestLayoutManager() override = default;

 protected:
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    views::ProposedLayout layout;

    if (axis_ ==
        TabCollectionAnimatingLayoutManager::AnimationAxis::kVertical) {
      // Place children at full width, starting from the top of the parent.
      int y = 0;
      for (const auto& child : host_view()->children()) {
        const int child_height = child->GetPreferredSize().height() > 0
                                     ? child->GetPreferredSize().height()
                                     : 20;
        layout.child_layouts.emplace_back(child, child->GetVisible(),
                                          gfx::Rect(0, y, 100, child_height));
        y += child_height;
      }

      // Calculate the total height based on all child layouts.
      layout.host_size = gfx::Size(size_bounds.width().value_or(100), y);
    } else if (axis_ == TabCollectionAnimatingLayoutManager::AnimationAxis::
                            kHorizontalWrappingVertically) {
      // Place children horizontally within a fixed-width parent.
      int x = 0;
      for (const auto& child : host_view()->children()) {
        const int child_width = child->GetPreferredSize().width() > 0
                                    ? child->GetPreferredSize().width()
                                    : 20;
        layout.child_layouts.emplace_back(child, child->GetVisible(),
                                          gfx::Rect(x, 0, child_width, 100));
        x += child_width;
      }

      // Calculate host size with fixed bounded width and total height.
      layout.host_size = gfx::Size(size_bounds.width().value_or(100), 100);
    } else {
      // Place children at full height, starting from the left of the parent.
      int x = 0;
      for (const auto& child : host_view()->children()) {
        const int child_width = child->GetPreferredSize().width() > 0
                                    ? child->GetPreferredSize().width()
                                    : 20;
        layout.child_layouts.emplace_back(child, child->GetVisible(),
                                          gfx::Rect(x, 0, child_width, 100));
        x += child_width;
      }

      // Calculate the total width based on all child layouts.
      layout.host_size = gfx::Size(x, size_bounds.height().value_or(100));
    }
    return layout;
  }

 private:
  TabCollectionAnimatingLayoutManager::AnimationAxis axis_;
};

}  // namespace

class YeeTabCollectionAnimatingLayoutManagerTest
    : public views::ViewsTestBase,
      public testing::WithParamInterface<
          std::tuple<bool,
                     TabCollectionAnimatingLayoutManager::AnimationAxis>> {
 public:
  YeeTabCollectionAnimatingLayoutManagerTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~YeeTabCollectionAnimatingLayoutManagerTest() override = default;

  // views::ViewsTestBase:
  void SetUp() override {
    views::ViewsTestBase::SetUp();

    // Force rich animation because these tests expect to see in-progress values
    // for animated properties. Even when scale mode is ZERO_DURATION the test
    // expects the animation to complete asynchronously.
    render_mode_lock_ = gfx::AnimationTestApi::SetRichAnimationRenderMode(
        gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);

    animation_mode_ = std::make_unique<gfx::ScopedAnimationDurationScaleMode>(
        IsAnimationDurationEnabled()
            ? gfx::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION
            : gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);

    // mock clock 검사에는 native focus가 필요 없다. ShowInactive 이후에도
    // macOS가 이 창을 활성 창으로 고르지 않도록 생성 시점에 차단한다.
    auto params = CreateParamsForTestWidget(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    widget_ = CreateTestWidget(std::move(params));
    host_view_ = widget_->SetContentsView(std::make_unique<views::View>());
    animation_coordinator_ = std::make_unique<
        TabCollectionAnimatingLayoutManager::AnimationCoordinator>(*host_view_);
    // Detach from the widget's compositor animation runner so animations are
    // driven deterministically by `MOCK_TIME` timers rather than asynchronous
    // GPU swap acks.
    animation_coordinator_->SetView(nullptr);
    layout_manager_delegate_ = std::make_unique<
        testing::NiceMock<MockAnimatingLayoutManagerDelegate>>();
    layout_manager_ = host_view_->SetLayoutManager(
        std::make_unique<TabCollectionAnimatingLayoutManager>(
            std::make_unique<TestLayoutManager>(animation_axis()),
            *layout_manager_delegate_.get(), *animation_coordinator_,
            animation_axis()));
    widget_->ShowInactive();
  }
  void TearDown() override {
    EXPECT_FALSE(widget_->IsActive());
    layout_manager_ = nullptr;
    host_view_ = nullptr;
    widget_.reset();
    animation_coordinator_.reset();
    layout_manager_delegate_.reset();
    animation_mode_.reset();
    views::ViewsTestBase::TearDown();
  }

  bool IsAnimationDurationEnabled() const { return std::get<0>(GetParam()); }
  TabCollectionAnimatingLayoutManager::AnimationAxis animation_axis() const {
    return std::get<1>(GetParam());
  }

  TabCollectionAnimatingLayoutManager* layout_manager() {
    return layout_manager_;
  }
  MockAnimatingLayoutManagerDelegate* layout_manager_delegate() {
    return layout_manager_delegate_.get();
  }
  TabCollectionAnimatingLayoutManager::AnimationCoordinator&
  animation_coordinator() {
    return *animation_coordinator_;
  }
  views::View* host_view() { return host_view_; }
  views::Widget* widget() { return widget_.get(); }

 private:
  std::unique_ptr<base::AutoReset<gfx::Animation::RichAnimationRenderMode>>
      render_mode_lock_;
  std::unique_ptr<gfx::ScopedAnimationDurationScaleMode> animation_mode_;
  std::unique_ptr<MockAnimatingLayoutManagerDelegate> layout_manager_delegate_;
  std::unique_ptr<TabCollectionAnimatingLayoutManager::AnimationCoordinator>
      animation_coordinator_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> host_view_;
  raw_ptr<TabCollectionAnimatingLayoutManager> layout_manager_;
};

TEST_P(YeeTabCollectionAnimatingLayoutManagerTest,
       RemoveChildDuringActiveAnimation) {
  widget()->SetBounds(gfx::Rect(0, 0, 100, 100));
  auto* const child1 =
      host_view()->AddChildView(std::make_unique<views::View>());
  auto* const child2 =
      host_view()->AddChildView(std::make_unique<views::View>());
  widget()->LayoutRootViewIfNecessary();
  task_environment()->FastForwardBy(base::Seconds(1));
  widget()->LayoutRootViewIfNecessary();

  host_view()->ReorderChildView(child2, 0);
  host_view()->InvalidateLayout();
  widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(layout_manager()->is_animating());

  // Detaching a dragged tab can destroy a child while the previous layout is
  // still animating. The layout manager must release checked pointer indexes
  // before the View's lifetime ends.
  std::unique_ptr<views::View> removed = host_view()->RemoveChildViewT(child1);
  removed.reset();
  host_view()->InvalidateLayout();
  widget()->LayoutRootViewIfNecessary();
  task_environment()->FastForwardBy(base::Seconds(1));
  widget()->LayoutRootViewIfNecessary();

  EXPECT_FALSE(layout_manager()->is_animating());
  EXPECT_EQ(host_view()->children().size(), 1u);
  EXPECT_EQ(host_view()->children().front(), child2);
}

TEST_P(YeeTabCollectionAnimatingLayoutManagerTest,
       YeeDragOverlayPreservesOnlyVerticalSourceOpacity) {
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->RemoveSwitch(
      "disable-yee-shell-scaffold");
  widget()->SetBounds(gfx::Rect(0, 0, 100, 100));
  auto* source = host_view()->AddChildView(std::make_unique<views::View>());
  widget()->LayoutRootViewIfNecessary();
  task_environment()->FastForwardBy(base::Seconds(1));
  widget()->LayoutRootViewIfNecessary();
  source->SetPaintToLayer();
  source->layer()->SetFillsBoundsOpaquely(false);
  source->layer()->SetOpacity(0.0f);
  ON_CALL(*layout_manager_delegate(), IsViewDragging(testing::Ref(*source)))
      .WillByDefault(testing::Return(true));

  host_view()->AddChildView(std::make_unique<views::View>());
  host_view()->InvalidateLayout();
  widget()->LayoutRootViewIfNecessary();
  ASSERT_TRUE(source->layer());
  // 가로 탭은 native가 실물을 드래그하고 세로 Sidebar만 별도 미리보기를 쓴다.
  EXPECT_EQ(
      animation_axis() ==
              TabCollectionAnimatingLayoutManager::AnimationAxis::kHorizontal
          ? 1.0f
          : 0.0f,
      source->layer()->opacity());
}

TEST_P(YeeTabCollectionAnimatingLayoutManagerTest,
       YeeFavoriteClockRetargetDoesNotChangeSharedTabClock) {
  gfx::ScopedAnimationDurationScaleMode normal_duration_mode(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  testing::NiceMock<MockAnimatingLayoutManagerDelegate> dock_delegate;
  std::unique_ptr<TabCollectionAnimatingLayoutManager::AnimationCoordinator>
      dock_clock;
  auto dock = std::make_unique<views::View>();
  dock_clock = std::make_unique<
      TabCollectionAnimatingLayoutManager::AnimationCoordinator>(
      *dock,
      base::Milliseconds(yee::kSidebarMetrics.favorites_shift_duration_ms),
      gfx::Tween::ACCEL_40_DECEL_100_3, 0.0);
  dock_clock->SetView(nullptr);
  auto* dock_manager = dock->SetLayoutManager(
      std::make_unique<TabCollectionAnimatingLayoutManager>(
          std::make_unique<TestLayoutManager>(animation_axis()), dock_delegate,
          *dock_clock, animation_axis()));
  widget()->SetBounds(gfx::Rect(0, 0, 100, 100));
  dock->SetBounds(0, 0, 100, 100);
  widget()->LayoutRootViewIfNecessary();
  views::test::RunScheduledLayout(dock.get());
  task_environment()->FastForwardBy(base::Seconds(1));

  host_view()->AddChildView(std::make_unique<views::View>());
  auto* first = dock->AddChildView(std::make_unique<views::View>());
  host_view()->InvalidateLayout();
  dock->InvalidateLayout();
  widget()->LayoutRootViewIfNecessary();
  views::test::RunScheduledLayout(dock.get());
  task_environment()->FastForwardBy(base::Milliseconds(250));
  widget()->LayoutRootViewIfNecessary();
  views::test::RunScheduledLayout(dock.get());
  // 일반 탭의 시계가 끝나도 Favorites의 이동은 계속된다.
  ASSERT_FALSE(layout_manager()->is_animating());
  ASSERT_TRUE(dock_manager->is_animating());
  const gfx::Rect before_retarget = first->bounds();

  dock->AddChildView(std::make_unique<views::View>());
  dock->InvalidateLayout();
  views::test::RunScheduledLayout(dock.get());
  EXPECT_EQ(first->bounds(), before_retarget);
  EXPECT_EQ(dock_clock->starting_offset(), 0.0);
  EXPECT_EQ(dock_clock->current_offset(), 0.0);
  EXPECT_EQ(animation_coordinator().current_offset(), 1.0);
  task_environment()->FastForwardBy(base::Milliseconds(250));
  views::test::RunScheduledLayout(dock.get());
  EXPECT_TRUE(dock_manager->is_animating());
  EXPECT_FALSE(layout_manager()->is_animating());
  task_environment()->FastForwardBy(base::Milliseconds(250));
  views::test::RunScheduledLayout(dock.get());
  EXPECT_FALSE(dock_manager->is_animating());

  // 진행 중 호스트가 사라져도 남은 tick이 해제된 manager를 호출하지 않는다.
  dock->AddChildView(std::make_unique<views::View>());
  dock->InvalidateLayout();
  views::test::RunScheduledLayout(dock.get());
  ASSERT_TRUE(dock_manager->is_animating());
  dock.reset();
  task_environment()->FastForwardBy(base::Seconds(1));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    YeeTabCollectionAnimatingLayoutManagerTest,
    testing::Combine(
        testing::Bool(),
        testing::Values(
            TabCollectionAnimatingLayoutManager::AnimationAxis::kVertical,
            TabCollectionAnimatingLayoutManager::AnimationAxis::kHorizontal,
            TabCollectionAnimatingLayoutManager::AnimationAxis::
                kHorizontalWrappingVertically)),
    [](const testing::TestParamInfo<
        YeeTabCollectionAnimatingLayoutManagerTest::ParamType>& info) {
      const bool duration_enabled = std::get<0>(info.param);
      const auto axis = std::get<1>(info.param);
      std::string axis_str;
      switch (axis) {
        case TabCollectionAnimatingLayoutManager::AnimationAxis::kVertical:
          axis_str = "Vertical";
          break;
        case TabCollectionAnimatingLayoutManager::AnimationAxis::kHorizontal:
          axis_str = "Horizontal";
          break;
        case TabCollectionAnimatingLayoutManager::AnimationAxis::
            kHorizontalWrappingVertically:
          axis_str = "HorizontalWrappingVertically";
          break;
      }
      return base::StrCat({
          duration_enabled ? "AnimationDurationEnabled"
                           : "AnimationDurationDisabled",
          "_",
          axis_str,
      });
    });
