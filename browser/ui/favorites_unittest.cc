// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/favorites.h"

#include <memory>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/test/scoped_rtl_for_testing.h"
#include "base/test/bind.h"
#include "base/test/scoped_command_line.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "chrome/browser/ui/views/yee/shell_policy.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/layout/layout_provider.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

class FavoritesPreviewAppliedStateTest : public testing::Test {
 protected:
  FavoritesPreviewAppliedStateTest() {
    tab_strip_.SetBoundsRect(gfx::Rect(244, 600));
  }

  FavoritesDragPreviewAppliedState& Show(int leading_inset = 0,
                                         bool over_favorites = false) {
    ShowFavoritesDragPreview(tab_strip_, gfx::Point(60, 120), over_favorites,
                             leading_inset, /*favorites_target_item_count=*/0,
                             ui::ImageModel(), u"Preview", gfx::Vector2d(16, 8),
                             gfx::Size(60, 32));
    return *FavoritesDragPreviewAppliedState::From(preview());
  }

  views::View& preview() {
    return *tab_strip_.GetViewByID(kSidebarFavoritesDragPreviewViewId);
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::SingleThreadTaskEnvironment::MainThreadType::UI,
      base::test::SingleThreadTaskEnvironment::TimeSource::MOCK_TIME};
  gfx::AnimationTestApi::RenderModeResetter rich_animation_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  gfx::AnimationTestApi::PrefersReducedMotionResetter reduced_motion_ =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  gfx::ScopedAnimationDurationScaleMode duration_scale_{
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION};
  views::LayoutProvider layout_provider_;
  views::View tab_strip_;
};

TEST_F(FavoritesPreviewAppliedStateTest,
       SameUpdateAndHidePublishAppliedValues) {
  EXPECT_FALSE(FavoritesDragPreviewAppliedState::From(tab_strip_));
  auto& state = Show();
  const auto first = state.GetSnapshot();
  EXPECT_FALSE(first.owner.is_empty());
  EXPECT_EQ(1u, first.apply_serial);
  EXPECT_EQ(preview().bounds(), first.bounds);
  EXPECT_EQ(
      TabDragPreviewBoundsInTabStrip(tab_strip_, gfx::Point(60, 120), 0,
                                     gfx::Vector2d(16, 8), gfx::Size(60, 32)),
      first.bounds);
  EXPECT_EQ(preview().GetBoundsInScreen(), first.screen_bounds);
  EXPECT_TRUE(first.visible);
  EXPECT_FALSE(first.running);
  std::vector<FavoritesDragPreviewAppliedSnapshot> snapshots;
  auto subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot& snapshot) {
        snapshots.push_back(snapshot);
      }));
  EXPECT_TRUE(snapshots.empty());
  Show();
  HideFavoritesDragPreview(tab_strip_);
  HideFavoritesDragPreview(tab_strip_);
  // 창 없는 fixture의 TaskEnvironment에서 신호 callback/삭제 작업을 전달한다.
  task_environment_.RunUntilIdle();
  ASSERT_EQ(3u, snapshots.size());
  for (size_t i = 0; i < snapshots.size(); ++i) {
    EXPECT_EQ(first.owner, snapshots[i].owner);
    EXPECT_EQ(i + 2, snapshots[i].apply_serial);
    EXPECT_EQ(first.bounds, snapshots[i].bounds);
    EXPECT_FALSE(snapshots[i].running);
  }
  EXPECT_TRUE(snapshots[0].visible);
  EXPECT_FALSE(snapshots[1].visible);
  EXPECT_FALSE(snapshots[2].visible);
  EXPECT_FALSE(preview().GetVisible());
}

TEST_F(FavoritesPreviewAppliedStateTest, PublishesBothOwnedAnimations) {
  auto& state = Show();
  std::vector<FavoritesDragPreviewAppliedSnapshot> snapshots;
  auto subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot& snapshot) {
        snapshots.push_back(snapshot);
      }));
  Show(/*leading_inset=*/24);
  EXPECT_TRUE(state.GetSnapshot().running);
  const auto start = state.GetSnapshot();
  task_environment_.FastForwardBy(base::Milliseconds(32));
  EXPECT_GT(state.GetSnapshot().apply_serial, start.apply_serial);
  EXPECT_NE(start.bounds, state.GetSnapshot().bounds);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(state.GetSnapshot().running);
  EXPECT_EQ(
      TabDragPreviewBoundsInTabStrip(tab_strip_, gfx::Point(60, 120), 24,
                                     gfx::Vector2d(16, 8), gfx::Size(60, 32)),
      state.GetSnapshot().bounds);

  Show(/*leading_inset=*/0, /*over_favorites=*/true);
  Show();
  EXPECT_TRUE(state.GetSnapshot().running);
  const auto region_start = state.GetSnapshot();
  task_environment_.FastForwardBy(base::Milliseconds(32));
  EXPECT_GT(state.GetSnapshot().apply_serial, region_start.apply_serial);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(state.GetSnapshot().running);
  EXPECT_EQ(preview().bounds(), state.GetSnapshot().bounds);
  for (size_t i = 1; i < snapshots.size(); ++i) {
    EXPECT_GT(snapshots[i].apply_serial, snapshots[i - 1].apply_serial);
  }
}

TEST_F(FavoritesPreviewAppliedStateTest, HideCancelsMotionBeforePublishing) {
  auto& state = Show();
  Show(/*leading_inset=*/24);
  ASSERT_TRUE(state.GetSnapshot().running);
  int calls = 0;
  auto subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot& snapshot) {
        ++calls;
        EXPECT_FALSE(snapshot.visible);
        EXPECT_FALSE(snapshot.running);
      }));
  HideFavoritesDragPreview(tab_strip_);
  // 창 없는 fixture의 TaskEnvironment에서 신호 callback/삭제 작업을 전달한다.
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1, calls);
  const auto hidden = state.GetSnapshot();
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(hidden, state.GetSnapshot());
  EXPECT_EQ(1, calls);
}

TEST_F(FavoritesPreviewAppliedStateTest, SubscriptionAndWeakPtrFollowPreview) {
  auto& state = Show();
  const auto owner = state.GetSnapshot().owner;
  auto weak = state.GetWeakPtr();
  int calls = 0;
  auto subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot&) { ++calls; }));
  Show(/*leading_inset=*/24);
  // 창 없는 fixture의 TaskEnvironment에서 신호 callback/삭제 작업을 전달한다.
  task_environment_.RunUntilIdle();
  EXPECT_GT(calls, 0);
  subscription = {};
  const int before_unsubscribe = calls;
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(before_unsubscribe, calls);

  subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot&) { ++calls; }));
  Show(/*leading_inset=*/0);
  ASSERT_TRUE(state.GetSnapshot().running);
  const int before_destruction = calls;
  tab_strip_.RemoveChildViewT(&preview()).reset();
  EXPECT_FALSE(weak);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(before_destruction, calls);
  EXPECT_NE(owner, Show().GetSnapshot().owner);
}

TEST_F(FavoritesPreviewAppliedStateTest,
       OwnerDeletionFromProgressNotificationIsSafe) {
  auto& state = Show();
  const auto initial_bounds = state.GetSnapshot().bounds;
  auto weak = state.GetWeakPtr();
  auto subscription = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot& snapshot) {
        if (snapshot.running && snapshot.bounds != initial_bounds) {
          tab_strip_.RemoveChildViewT(&preview()).reset();
        }
      }));
  int calls_after_deletion = 0;
  auto later = state.Subscribe(base::BindLambdaForTesting(
      [&](const FavoritesDragPreviewAppliedSnapshot&) {
        if (!weak) {
          ++calls_after_deletion;
        }
      }));
  Show(/*leading_inset=*/24);
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_FALSE(weak);
  EXPECT_FALSE(tab_strip_.GetViewByID(kSidebarFavoritesDragPreviewViewId));
  EXPECT_EQ(0, calls_after_deletion);
}

TEST(FavoritesPolicyTest, LayoutTimingAppliesOnlyToTheYeeVerticalDock) {
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->RemoveSwitch(
      "disable-yee-shell-scaffold");
  EXPECT_FALSE(GetFavoritesLayoutAnimationSettings(/*vertical=*/false));
  const auto settings = GetFavoritesLayoutAnimationSettings(/*vertical=*/true);
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->duration,
            base::Milliseconds(kSidebarMetrics.favorites_shift_duration_ms));
  EXPECT_EQ(settings->tween, gfx::Tween::ACCEL_40_DECEL_100_3);
  EXPECT_EQ(settings->retarget_reset_threshold, 0.0);
  command_line.GetProcessCommandLine()->AppendSwitch(
      "disable-yee-shell-scaffold");
  EXPECT_FALSE(GetFavoritesLayoutAnimationSettings(/*vertical=*/true));
}

TEST(FavoritesDragIntentTest, ResolvesEveryCrossRegionState) {
  const int max_count = kSidebarMetrics.favorites_max_count;

  EXPECT_EQ(ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/true,
                                       /*over_favorites=*/true, max_count, 0),
            FavoritesDragIntent::kNone);
  EXPECT_EQ(ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/true,
                                       /*over_favorites=*/false, max_count, 0),
            FavoritesDragIntent::kUnpin);
  EXPECT_EQ(ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/false,
                                       /*over_favorites=*/false, max_count, 1),
            FavoritesDragIntent::kNone);
  EXPECT_EQ(
      ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/false,
                                 /*over_favorites=*/true, max_count - 1, 1),
      FavoritesDragIntent::kPin);
  EXPECT_EQ(ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/false,
                                       /*over_favorites=*/true, max_count, 1),
            FavoritesDragIntent::kRejected);
  EXPECT_EQ(
      ResolveFavoritesDragIntent(/*dragging_pinned_tabs=*/false,
                                 /*over_favorites=*/true, max_count - 1, 2),
      FavoritesDragIntent::kRejected);
}

TEST(FavoritesPolicyTest, CapacityIncludesEveryIncomingTab) {
  const int max_count = kSidebarMetrics.favorites_max_count;

  EXPECT_TRUE(CanAddFavorite(max_count - 1));
  EXPECT_TRUE(CanAddFavorite(max_count - 2, 2));
  EXPECT_FALSE(CanAddFavorite(max_count));
  EXPECT_FALSE(CanAddFavorite(max_count - 1, 2));

  // Removing Favorites is never blocked, including sessions restored above
  // the user-facing cap.
  EXPECT_TRUE(CanAddFavorite(max_count + 1, 0));
  EXPECT_TRUE(CanAddFavorite(max_count + 1, -1));
}

TEST(FavoritesPolicyTest, LoneTabGetsSidebarFirstRefusalOnly) {
  EXPECT_TRUE(ShouldPrioritizeSidebarTabDrag(
      /*dragged_tab_count=*/1, /*source_tab_count=*/1,
      /*is_group_drag=*/false, /*uses_vertical_tab_strip=*/true));

  EXPECT_FALSE(ShouldPrioritizeSidebarTabDrag(
      /*dragged_tab_count=*/1, /*source_tab_count=*/2,
      /*is_group_drag=*/false, /*uses_vertical_tab_strip=*/true));
  EXPECT_FALSE(ShouldPrioritizeSidebarTabDrag(
      /*dragged_tab_count=*/2, /*source_tab_count=*/2,
      /*is_group_drag=*/false, /*uses_vertical_tab_strip=*/true));
  EXPECT_FALSE(ShouldPrioritizeSidebarTabDrag(
      /*dragged_tab_count=*/1, /*source_tab_count=*/1,
      /*is_group_drag=*/true, /*uses_vertical_tab_strip=*/true));
  EXPECT_FALSE(ShouldPrioritizeSidebarTabDrag(
      /*dragged_tab_count=*/1, /*source_tab_count=*/1,
      /*is_group_drag=*/false, /*uses_vertical_tab_strip=*/false));
}

TEST(FavoritesDockLayoutTest, UsesContractColumnsAndWrapsWithoutTopInset) {
  std::vector<views::View> children(kSidebarMetrics.favorites_max_columns + 1);
  std::vector<FavoritesDockItem> items;
  for (auto& child : children) {
    items.push_back({.view = &child});
  }

  const views::ProposedLayout layout = CalculateFavoritesDockLayout(
      items, views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false);

  ASSERT_EQ(layout.child_layouts.size(), items.size());
  const int columns = kSidebarMetrics.favorites_max_columns;
  const int inset = kSidebarMetrics.tab_strip_horizontal_padding;
  const int gap = kSidebarMetrics.favorites_cell_gap;
  const int height = kSidebarMetrics.tab_icon_design_width +
                     2 * kSidebarMetrics.favorites_cell_vertical_padding;
  const int cell_width = (244 - 2 * inset - (columns - 1) * gap) / columns;
  const int rows = (items.size() + columns - 1) / columns;
  EXPECT_EQ(layout.host_size, gfx::Size(244, rows * height + (rows - 1) * gap));
  for (size_t i = 0; i < items.size(); ++i) {
    EXPECT_EQ(layout.child_layouts[i].bounds,
              gfx::Rect(inset + (i % columns) * (cell_width + gap),
                        (i / columns) * (height + gap), cell_width, height));
  }
}

TEST(FavoritesDockLayoutTest, IncomingSlotCanLandOnEitherSideOfOneFavorite) {
  views::View favorite;
  const std::vector<FavoritesDockItem> items = {{.view = &favorite}};

  const views::ProposedLayout before = CalculateFavoritesDockLayout(
      items, views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false, /*incoming_slot=*/0);
  const views::ProposedLayout after = CalculateFavoritesDockLayout(
      items, views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false, /*incoming_slot=*/1);

  ASSERT_EQ(before.child_layouts.size(), 1u);
  ASSERT_EQ(after.child_layouts.size(), 1u);
  const int inset = kSidebarMetrics.tab_strip_horizontal_padding;
  const int gap = kSidebarMetrics.favorites_cell_gap;
  const int height = kSidebarMetrics.tab_icon_design_width +
                     2 * kSidebarMetrics.favorites_cell_vertical_padding;
  const int width = (244 - 2 * inset - gap) / 2;
  EXPECT_EQ(before.child_layouts[0].bounds,
            gfx::Rect(inset + width + gap, 0, width, height));
  EXPECT_EQ(after.child_layouts[0].bounds, gfx::Rect(inset, 0, width, height));
}

TEST(FavoritesDockLayoutTest, HiddenSourceKeepsOnlySameDockLandingHole) {
  views::View source;
  views::View neighbor;

  const views::ProposedLayout same_dock = CalculateFavoritesDockLayout(
      {{.view = &source, .hidden = true, .floating = false},
       {.view = &neighbor}},
      views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false);
  const views::ProposedLayout cross_region = CalculateFavoritesDockLayout(
      {{.view = &source, .hidden = true, .floating = true},
       {.view = &neighbor}},
      views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false);

  ASSERT_EQ(same_dock.child_layouts.size(), 2u);
  EXPECT_FALSE(same_dock.child_layouts[0].visible);
  const int inset = kSidebarMetrics.tab_strip_horizontal_padding;
  const int gap = kSidebarMetrics.favorites_cell_gap;
  const int height = kSidebarMetrics.tab_icon_design_width +
                     2 * kSidebarMetrics.favorites_cell_vertical_padding;
  const int width = (244 - 2 * inset - gap) / 2;
  EXPECT_EQ(same_dock.child_layouts[1].bounds,
            gfx::Rect(inset + width + gap, 0, width, height));

  ASSERT_EQ(cross_region.child_layouts.size(), 2u);
  EXPECT_FALSE(cross_region.child_layouts[0].visible);
  EXPECT_EQ(cross_region.child_layouts[1].bounds,
            gfx::Rect(inset, 0, 244 - 2 * inset, height));
}

class FavoritesDragGeometryTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    tab_strip_ = widget_->SetContentsView(std::make_unique<views::View>());
    widget_->SetBounds(gfx::Rect(100, 100, 320, 600));
    widget_->ShowInactive();
  }

  void TearDown() override {
    base::i18n::ScopedRTLForTesting direction(false);
    tab_strip_ = nullptr;
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  views::View& tab_strip() { return *tab_strip_; }

 private:
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> tab_strip_ = nullptr;
};

TEST_F(FavoritesDragGeometryTest, TabPreviewMirrorsLogicalLeadingInset) {
  constexpr int kLeadingInset = 16;
  const int pad = kSidebarMetrics.tab_strip_horizontal_padding;
  const int row_width = tab_strip().width() - 2 * pad - kLeadingInset;
  const gfx::Point pointer_in_screen =
      tab_strip().GetBoundsInScreen().origin() + gfx::Vector2d(80, 120);
  const gfx::Size source_size(80, 32);
  const gfx::Vector2d source_grab_offset(20, 8);

  base::i18n::ScopedRTLForTesting ltr_direction(false);
  const gfx::Rect ltr = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, kLeadingInset, source_grab_offset,
      source_size);
  EXPECT_EQ(ltr.x(), pad + kLeadingInset);
  EXPECT_EQ(ltr.width(), row_width);

  base::i18n::ScopedRTLForTesting rtl_direction(true);
  const gfx::Rect rtl = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, kLeadingInset, source_grab_offset,
      source_size);
  EXPECT_EQ(rtl.x(), pad);
  EXPECT_EQ(rtl.width(), row_width);
}

TEST_F(FavoritesDragGeometryTest, TabPreviewWithoutInsetIsSymmetric) {
  const int pad = kSidebarMetrics.tab_strip_horizontal_padding;
  const gfx::Point pointer_in_screen =
      tab_strip().GetBoundsInScreen().origin() + gfx::Vector2d(80, 120);

  base::i18n::ScopedRTLForTesting ltr_direction(false);
  const gfx::Rect ltr = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, /*tab_leading_inset=*/0,
      /*source_grab_offset=*/gfx::Vector2d(),
      /*source_tile_size=*/gfx::Size());

  base::i18n::ScopedRTLForTesting rtl_direction(true);
  const gfx::Rect rtl = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, /*tab_leading_inset=*/0,
      /*source_grab_offset=*/gfx::Vector2d(),
      /*source_tile_size=*/gfx::Size());

  EXPECT_EQ(ltr.x(), pad);
  EXPECT_EQ(rtl.x(), pad);
  EXPECT_EQ(ltr.width(), rtl.width());
}

TEST_F(FavoritesDragGeometryTest, DockPreviewScalesTheGrabPointWithTheTile) {
  const gfx::Point pointer_in_screen(300, 280);
  const gfx::Rect tile = FavoritesDraggedTileForDockInScreen(
      tab_strip(), pointer_in_screen,
      /*source_grab_offset=*/gfx::Vector2d(20, 16),
      /*source_tile_size=*/gfx::Size(80, 64),
      /*target_item_count=*/2);

  // 모양 전환 뒤에도 잡은 지점은 각 축의 1/4 위치를 유지한다.
  const int height = kSidebarMetrics.tab_icon_design_width +
                     2 * kSidebarMetrics.favorites_cell_vertical_padding;
  const int width =
      (tab_strip().width() - 2 * kSidebarMetrics.tab_strip_horizontal_padding -
       kSidebarMetrics.favorites_cell_gap) /
      2;
  EXPECT_EQ(tile.size(), gfx::Size(width, height));
  EXPECT_EQ(pointer_in_screen - tile.origin(),
            gfx::Vector2d(width / 4, height / 4));
}

TEST_F(FavoritesDragGeometryTest, IncomingTileSelectsBothSidesOfOneFavorite) {
  const gfx::Rect dock = tab_strip().GetBoundsInScreen();
  const int inset = kSidebarMetrics.tab_strip_horizontal_padding;
  const int gap = kSidebarMetrics.favorites_cell_gap;
  const int height = kSidebarMetrics.tab_icon_design_width +
                     2 * kSidebarMetrics.favorites_cell_vertical_padding;
  const int width = (dock.width() - 2 * inset - gap) / 2;
  const gfx::Rect left(dock.x() + inset, dock.y(), width, height);
  const gfx::Rect right(dock.x() + inset + width + gap, dock.y(), width,
                        height);

  EXPECT_EQ(FavoritesInsertIndexForTile(tab_strip(), left,
                                        /*item_count=*/1,
                                        /*reserve_incoming_slot=*/true),
            0);
  EXPECT_EQ(FavoritesInsertIndexForTile(tab_strip(), right,
                                        /*item_count=*/1,
                                        /*reserve_incoming_slot=*/true),
            1);
}

TEST_F(FavoritesDragGeometryTest, DockMagnetExtendsSidewaysButNotBelow) {
  auto* dock = tab_strip().AddChildView(std::make_unique<views::View>());
  dock->SetID(kSidebarFavoritesDockViewId);
  dock->SetBounds(/*x=*/8, /*y=*/40, /*width=*/200, /*height=*/32);
  const gfx::Rect dock_bounds = dock->GetBoundsInScreen();

  EXPECT_TRUE(
      PointHitsFavoritesDropTarget(tab_strip(), dock_bounds.CenterPoint()));
  EXPECT_TRUE(PointHitsFavoritesDropTarget(
      tab_strip(),
      gfx::Point(dock_bounds.right() + 40, dock_bounds.CenterPoint().y())));
  EXPECT_TRUE(PointHitsFavoritesDropTarget(
      tab_strip(),
      gfx::Point(dock_bounds.CenterPoint().x(), dock_bounds.y() - 1)));
  EXPECT_FALSE(PointHitsFavoritesDropTarget(
      tab_strip(),
      gfx::Point(dock_bounds.CenterPoint().x(), dock_bounds.bottom() + 1)));

  base::i18n::ScopedRTLForTesting direction(true);
  EXPECT_TRUE(PointHitsFavoritesDropTarget(
      tab_strip(),
      gfx::Point(dock_bounds.x() - 4, dock_bounds.CenterPoint().y())));
}

TEST_F(FavoritesDragGeometryTest, EmptyDockFallbackLeavesTabBodyAvailable) {
  const gfx::Rect strip = tab_strip().GetBoundsInScreen();
  const int x = strip.x() + kSidebarMetrics.section_horizontal_inset;

  EXPECT_TRUE(
      PointHitsFavoritesDropTarget(tab_strip(), gfx::Point(x, strip.y())));
  EXPECT_FALSE(
      PointHitsFavoritesDropTarget(tab_strip(), gfx::Point(x, strip.y() + 2)));
}

TEST_F(FavoritesDragGeometryTest, DropZoneSnapsToDocumentedOpenHeight) {
  views::View* drop_zone = tab_strip().AddChildView(CreateFavoritesDropZone());

  SetFavoritesDropZoneOpen(*drop_zone, /*open=*/true, /*animate=*/false);
  EXPECT_TRUE(drop_zone->GetVisible());
  EXPECT_EQ(drop_zone->GetPreferredSize(
                views::SizeBounds(/*width=*/244, /*height=*/600)),
            gfx::Size(244, kSidebarMetrics.favorites_drop_zone_height));

  SetFavoritesDropZoneOpen(*drop_zone, /*open=*/false, /*animate=*/false);
  EXPECT_FALSE(drop_zone->GetVisible());
  EXPECT_EQ(drop_zone->GetPreferredSize(
                views::SizeBounds(/*width=*/244, /*height=*/600)),
            gfx::Size(244, 0));
}

TEST_F(FavoritesDragGeometryTest, RealPreviewUsesTheSharedTabGeometry) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  const gfx::Point pointer_in_screen =
      tab_strip().GetBoundsInScreen().origin() + gfx::Vector2d(100, 140);
  const gfx::Vector2d grab_offset(20, 8);
  const gfx::Size source_size(80, 32);
  constexpr int kGroupInset = 16;

  ShowFavoritesDragPreview(tab_strip(), pointer_in_screen,
                           /*over_favorites=*/false, kGroupInset,
                           /*favorites_target_item_count=*/0, ui::ImageModel(),
                           u"Preview", grab_offset, source_size);
  views::View* preview =
      tab_strip().GetViewByID(kSidebarFavoritesDragPreviewViewId);
  ASSERT_NE(preview, nullptr);
  EXPECT_TRUE(preview->GetVisible());
  EXPECT_EQ(preview->bounds(), TabDragPreviewBoundsInTabStrip(
                                   tab_strip(), pointer_in_screen, kGroupInset,
                                   grab_offset, source_size));

  HideFavoritesDragPreview(tab_strip());
  EXPECT_FALSE(preview->GetVisible());
}

TEST_F(FavoritesDragGeometryTest, PreviewScreenBoundsMatchRowsInLtrAndRtl) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  auto* const row = tab_strip().AddChildView(std::make_unique<views::View>());
  const int pad = kSidebarMetrics.tab_strip_horizontal_padding;
  for (bool rtl : {false, true}) {
    base::i18n::ScopedRTLForTesting direction(rtl);
    for (int inset : {0, 10, 16}) {
      SCOPED_TRACE(::testing::Message() << "rtl=" << rtl << " inset=" << inset);
      row->SetBounds(pad + inset, 100, tab_strip().width() - 2 * pad - inset,
                     kSidebarMetrics.tab_row_height);
      const gfx::Point pointer = row->GetBoundsInScreen().CenterPoint();
      ShowFavoritesDragPreview(
          tab_strip(), pointer, /*over_favorites=*/false, inset,
          /*favorites_target_item_count=*/0, ui::ImageModel(), u"Preview",
          /*grab_offset=*/gfx::Vector2d(20, 8),
          /*source_tile_size=*/gfx::Size(80, 32));
      auto* const preview =
          tab_strip().GetViewByID(kSidebarFavoritesDragPreviewViewId);
      ASSERT_TRUE(preview && preview->GetVisible());
      EXPECT_EQ(row->GetBoundsInScreen().x(), preview->GetBoundsInScreen().x());
      EXPECT_EQ(row->width(), preview->width());
      const gfx::Rect physical_target = TabDragPreviewBoundsInTabStrip(
          tab_strip(), pointer, inset, gfx::Vector2d(20, 8), gfx::Size(80, 32));
      EXPECT_EQ(physical_target, preview->GetMirroredBounds());
      HideFavoritesDragPreview(tab_strip());
    }
  }
}

TEST_F(FavoritesDragGeometryTest,
       ReducedMotionRetargetsPreviewImmediatelyInLtrAndRtl) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  const gfx::Point pointer_in_screen =
      tab_strip().GetBoundsInScreen().origin() + gfx::Vector2d(100, 140);

  for (bool rtl : {false, true}) {
    base::i18n::ScopedRTLForTesting direction(rtl);
    ShowFavoritesDragPreview(
        tab_strip(), pointer_in_screen, /*over_favorites=*/false,
        /*tab_leading_inset=*/0, /*favorites_target_item_count=*/0,
        ui::ImageModel(), u"Preview", /*grab_offset=*/gfx::Vector2d(20, 8),
        /*source_tile_size=*/gfx::Size(80, 32));
    ShowFavoritesDragPreview(
        tab_strip(), pointer_in_screen, /*over_favorites=*/false,
        /*tab_leading_inset=*/16, /*favorites_target_item_count=*/0,
        ui::ImageModel(), u"Preview", /*grab_offset=*/gfx::Vector2d(20, 8),
        /*source_tile_size=*/gfx::Size(80, 32));

    views::View* preview =
        tab_strip().GetViewByID(kSidebarFavoritesDragPreviewViewId);
    ASSERT_NE(preview, nullptr);
    const gfx::Rect preview_bounds = preview->GetMirroredBounds();
    EXPECT_EQ(preview_bounds,
              TabDragPreviewBoundsInTabStrip(
                  tab_strip(), pointer_in_screen, /*tab_leading_inset=*/16,
                  /*source_grab_offset=*/gfx::Vector2d(20, 8),
                  /*source_tile_size=*/gfx::Size(80, 32)));
    HideFavoritesDragPreview(tab_strip());
  }
}

TEST_F(FavoritesDragGeometryTest, ReducedMotionDropZoneLifecycleIsAtomic) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  views::View* drop_zone = tab_strip().AddChildView(CreateFavoritesDropZone());

  SetFavoritesDropZoneOpen(*drop_zone, /*open=*/true, /*animate=*/true);
  EXPECT_TRUE(drop_zone->GetVisible());
  EXPECT_EQ(drop_zone->GetPreferredSize(
                views::SizeBounds(/*width=*/244, /*height=*/600)),
            gfx::Size(244, kSidebarMetrics.favorites_drop_zone_height));

  SetFavoritesDropZoneOpen(*drop_zone, /*open=*/false, /*animate=*/true);
  EXPECT_FALSE(drop_zone->GetVisible());
  EXPECT_EQ(drop_zone->GetPreferredSize(
                views::SizeBounds(/*width=*/244, /*height=*/600)),
            gfx::Size(244, 0));
}

}  // namespace
}  // namespace yee
