// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/favorites.h"

#include <memory>

#include "base/i18n/rtl.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

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

TEST(FavoritesDockLayoutTest, UsesFourColumnsAndWrapsWithoutTopInset) {
  views::View first;
  views::View second;
  views::View third;
  views::View fourth;
  views::View fifth;
  const std::vector<FavoritesDockItem> items = {
      {.view = &first},  {.view = &second}, {.view = &third},
      {.view = &fourth}, {.view = &fifth},
  };

  const views::ProposedLayout layout = CalculateFavoritesDockLayout(
      items, views::SizeBounds(/*width=*/244, /*height=*/600),
      /*contains_split=*/false);

  ASSERT_EQ(layout.child_layouts.size(), 5u);
  EXPECT_EQ(layout.host_size, gfx::Size(244, 72));
  EXPECT_EQ(layout.child_layouts[0].bounds, gfx::Rect(8, 0, 51, 32));
  EXPECT_EQ(layout.child_layouts[1].bounds, gfx::Rect(67, 0, 51, 32));
  EXPECT_EQ(layout.child_layouts[2].bounds, gfx::Rect(126, 0, 51, 32));
  EXPECT_EQ(layout.child_layouts[3].bounds, gfx::Rect(185, 0, 51, 32));
  EXPECT_EQ(layout.child_layouts[4].bounds, gfx::Rect(8, 40, 51, 32));
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
  EXPECT_EQ(before.child_layouts[0].bounds, gfx::Rect(126, 0, 110, 32));
  EXPECT_EQ(after.child_layouts[0].bounds, gfx::Rect(8, 0, 110, 32));
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
  EXPECT_EQ(same_dock.child_layouts[1].bounds, gfx::Rect(126, 0, 110, 32));

  ASSERT_EQ(cross_region.child_layouts.size(), 2u);
  EXPECT_FALSE(cross_region.child_layouts[0].visible);
  EXPECT_EQ(cross_region.child_layouts[1].bounds, gfx::Rect(8, 0, 228, 32));
}

class FavoritesDragGeometryTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    tab_strip_ = widget_->SetContentsView(std::make_unique<views::View>());
    widget_->SetBounds(gfx::Rect(100, 100, 320, 600));
    widget_->Show();
  }

  void TearDown() override {
    base::i18n::SetRTLForTesting(false);
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

  base::i18n::SetRTLForTesting(false);
  const gfx::Rect ltr = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, kLeadingInset, source_grab_offset,
      source_size);
  EXPECT_EQ(ltr.x(), pad + kLeadingInset);
  EXPECT_EQ(ltr.width(), row_width);

  base::i18n::SetRTLForTesting(true);
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

  base::i18n::SetRTLForTesting(false);
  const gfx::Rect ltr = TabDragPreviewBoundsInTabStrip(
      tab_strip(), pointer_in_screen, /*tab_leading_inset=*/0,
      /*source_grab_offset=*/gfx::Vector2d(),
      /*source_tile_size=*/gfx::Size());

  base::i18n::SetRTLForTesting(true);
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

  // At 320 DIP, two cells are 148x32. The grabbed quarter-point remains the
  // quarter-point after the shape transition.
  EXPECT_EQ(tile.size(), gfx::Size(148, 32));
  EXPECT_EQ(pointer_in_screen - tile.origin(), gfx::Vector2d(37, 8));
}

TEST_F(FavoritesDragGeometryTest, IncomingTileSelectsBothSidesOfOneFavorite) {
  const gfx::Rect dock = tab_strip().GetBoundsInScreen();
  const gfx::Rect left(dock.x() + 8, dock.y(), 148, 32);
  const gfx::Rect right(dock.x() + 164, dock.y(), 148, 32);

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

  base::i18n::SetRTLForTesting(true);
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
            gfx::Size(244, 76));

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

TEST_F(FavoritesDragGeometryTest,
       ReducedMotionRetargetsPreviewImmediatelyInLtrAndRtl) {
  auto disable_animation = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED);
  const gfx::Point pointer_in_screen =
      tab_strip().GetBoundsInScreen().origin() + gfx::Vector2d(100, 140);

  for (bool rtl : {false, true}) {
    base::i18n::SetRTLForTesting(rtl);
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
    EXPECT_EQ(preview->bounds(),
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
            gfx::Size(244, 76));

  SetFavoritesDropZoneOpen(*drop_zone, /*open=*/false, /*animate=*/true);
  EXPECT_FALSE(drop_zone->GetVisible());
  EXPECT_EQ(drop_zone->GetPreferredSize(
                views::SizeBounds(/*width=*/244, /*height=*/600)),
            gfx::Size(244, 0));
}

}  // namespace
}  // namespace yee
