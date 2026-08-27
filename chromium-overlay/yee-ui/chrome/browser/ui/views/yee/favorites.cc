// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/favorites.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/weak_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/path_effect.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/yee/vertical_tab_text_view.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

int DockHorizontalInset() {
  return kSidebarMetrics.tab_strip_horizontal_padding;
}

int DockMinCell() {
  return kSidebarMetrics.favorites_cell_size;
}

int DockCellHeight() {
  return kSidebarMetrics.tab_icon_design_width +
         2 * kSidebarMetrics.favorites_cell_vertical_padding;
}

int DockGap() {
  return kSidebarMetrics.favorites_cell_gap;
}

int DockMaxColumns() {
  return kSidebarMetrics.favorites_max_columns;
}

int CountLaidOutItems(const std::vector<FavoritesDockItem>& items) {
  int count = 0;
  for (const FavoritesDockItem& item : items) {
    // Hidden+floating is a collapsed source slot. Hidden without floating is
    // an unpainted landing hole and still consumes a cell.
    if (item.view && !(item.hidden && item.floating)) {
      ++count;
    }
  }
  return count;
}

int ColumnsForWidth(int inner_width) {
  const int min_cell = DockMinCell();
  const int gap = DockGap();
  if (inner_width < min_cell) {
    return 1;
  }
  return std::max(1, (inner_width + gap) / (min_cell + gap));
}

struct DockGrid {
  int columns = 1;
  int cell_width = DockMinCell();
  int cell_height = DockCellHeight();
};

DockGrid ComputeDockGrid(int item_count, int available_width) {
  DockGrid grid;
  const int inset = DockHorizontalInset();
  const int gap = DockGap();
  const int min_cell = DockMinCell();
  const int inner =
      available_width > 0 ? std::max(0, available_width - 2 * inset) : 0;
  const int count = std::max(1, item_count);

  int columns = std::min(count, DockMaxColumns());
  if (inner > 0) {
    columns = std::min(columns, ColumnsForWidth(inner));
  }
  grid.columns = std::max(1, columns);

  if (inner > 0) {
    grid.cell_width =
        std::max(min_cell, (inner - (grid.columns - 1) * gap) / grid.columns);
  } else {
    grid.cell_width = min_cell;
  }
  // Width shares the row. Height is only the favicon plus vertical padding.
  grid.cell_height = DockCellHeight();
  return grid;
}

gfx::Size EmptyDockSize(int available_width) {
  const DockGrid grid = ComputeDockGrid(1, available_width);
  const int horizontal_inset = DockHorizontalInset();
  const int width =
      available_width > 0
          ? available_width
          : grid.cell_width + 2 * horizontal_inset;
  return gfx::Size(width, grid.cell_height);
}

gfx::Size LiveFavoriteCellSize(const views::View& tab_strip) {
  const auto* scroll =
      views::AsViewClass<views::ScrollView>(const_cast<views::View*>(
          tab_strip.GetViewByID(kSidebarFavoritesDockViewId)));
  if (scroll && scroll->contents()) {
    for (views::View* child : scroll->contents()->children()) {
      if (child->GetVisible() && !child->size().IsEmpty()) {
        return child->size();
      }
    }
  }
  return gfx::Size(DockMinCell(), DockCellHeight());
}

}  // namespace

gfx::Size FavoritesDockMinimumSize(int /*item_count*/) {
  const int horizontal_inset = DockHorizontalInset();
  const int cell = DockMinCell();
  return gfx::Size(cell + 2 * horizontal_inset, cell);
}

views::ProposedLayout CalculateFavoritesDockLayout(
    const std::vector<FavoritesDockItem>& items,
    const views::SizeBounds& size_bounds,
    bool contains_split,
    std::optional<int> incoming_slot) {
  views::ProposedLayout layouts;
  const int horizontal_inset = DockHorizontalInset();
  const int gap = DockGap();
  const int available_width = size_bounds.width().value_or(0);
  const int laid_out = CountLaidOutItems(items);

  if (laid_out == 0) {
    layouts.host_size = EmptyDockSize(available_width);
    return layouts;
  }

  const int slot_count = incoming_slot ? laid_out + 1 : laid_out;
  const DockGrid grid = ComputeDockGrid(slot_count, available_width);
  const int child_width = grid.cell_width * (contains_split ? 2 : 1);
  const int child_height = grid.cell_height;

  int x = horizontal_inset;
  int y = 0;
  int column = 0;
  int content_right = horizontal_inset;
  int content_bottom = 0;
  int next_slot = 0;

  auto occupy_slot = [&]() {
    gfx::Rect slot(x, y, child_width, child_height);
    content_right = std::max(content_right, slot.right());
    content_bottom = std::max(content_bottom, slot.bottom());
    ++column;
    if (column >= grid.columns) {
      column = 0;
      x = horizontal_inset;
      y = slot.bottom() + gap;
    } else {
      x = slot.right() + gap;
    }
    return slot;
  };

  auto consume_incoming = [&]() {
    while (incoming_slot && next_slot == *incoming_slot) {
      occupy_slot();
      ++next_slot;
    }
  };

  for (const FavoritesDockItem& item : items) {
    if (!item.view) {
      continue;
    }
    if (item.hidden) {
      // In-dock drag keeps an unpainted hole so other tiles can shift.
      // Floating is a cross-region collapse: drop the source slot entirely.
      if (!item.floating) {
        consume_incoming();
        occupy_slot();
        ++next_slot;
      }
      layouts.child_layouts.emplace_back(item.view.get(), false, gfx::Rect());
      continue;
    }

    consume_incoming();
    gfx::Rect slot(x, y, child_width, child_height);
    if (!item.floating) {
      slot = occupy_slot();
      ++next_slot;
    }
    gfx::Rect bounds = slot;

    if (item.drag_offset) {
      bounds.set_y(item.drag_offset->y());
      int child_x = item.drag_offset->x();
      if (base::i18n::IsRTL() && available_width > 0) {
        child_x = available_width - child_x - child_width;
      }
      bounds.set_x(child_x);
    }

    layouts.child_layouts.emplace_back(item.view.get(), true, bounds);
  }

  consume_incoming();

  layouts.host_size =
      gfx::Size(available_width > 0
                    ? available_width
                    : content_right + horizontal_inset,
                content_bottom);
  return layouts;
}

gfx::Size FavoritesTileSize(const views::View& dock) {
  for (views::View* child : dock.children()) {
    if (child->GetVisible() && !child->size().IsEmpty()) {
      return child->size();
    }
  }
  const DockGrid grid = ComputeDockGrid(1, dock.width());
  return gfx::Size(grid.cell_width, grid.cell_height);
}

gfx::Rect FavoritesDraggedTileInScreen(const gfx::Point& pointer_in_screen,
                                       const gfx::Vector2d& grab_offset,
                                       const gfx::Size& tile_size) {
  return gfx::Rect(pointer_in_screen - grab_offset, tile_size);
}

std::optional<int> FavoritesInsertIndexForTile(const views::View& dock,
                                               const gfx::Rect& tile_in_screen,
                                               int item_count) {
  if (item_count <= 0) {
    return 0;
  }
  gfx::Rect tile = views::View::ConvertRectFromScreen(&dock, tile_in_screen);
  const bool rtl = base::i18n::IsRTL();
  if (rtl) {
    tile.set_x(dock.GetMirroredXForRect(tile));
  }
  const gfx::Point center = tile.CenterPoint();
  const int dock_w = dock.width();

  if ((!rtl && center.x() >= dock_w) || (rtl && center.x() < 0)) {
    return item_count;
  }
  if (center.y() < 0) {
    return 0;
  }

  const DockGrid grid = ComputeDockGrid(item_count, dock_w);
  const int horizontal_inset = DockHorizontalInset();
  const int gap = DockGap();
  const int stride_x = grid.cell_width + gap;
  const int stride_y = grid.cell_height + gap;
  const int last_index = item_count - 1;
  const int last_row = last_index / grid.columns;
  const int last_col = last_index % grid.columns;
  const int last_bottom = (last_row + 1) * stride_y - gap;
  if (center.y() >= last_bottom) {
    return item_count;
  }

  int col =
      stride_x > 0 ? (center.x() - horizontal_inset) / stride_x : 0;
  int row = stride_y > 0 ? center.y() / stride_y : 0;
  col = std::clamp(col, 0, grid.columns - 1);
  row = std::max(0, row);
  if (row > last_row || (row == last_row && col > last_col)) {
    return item_count;
  }
  return std::clamp(row * grid.columns + col, 0, item_count);
}

bool FavoritesDockUsesSeparator() {
  return false;
}

void ApplyFavoritesDockStyle(views::View& dock) {
  // Tiles carry the dimmed fill. The dock itself stays clear so the cells
  // sit on glass the way Arc's Favorites row does.
  dock.SetBackground(nullptr);
}

void PaintFavoritesCellBackground(gfx::Canvas* canvas,
                                  const gfx::Rect& bounds,
                                  SidebarItemVisualState state,
                                  double hover_progress,
                                  bool frame_active,
                                  const ui::ColorProvider& color_provider) {
  if (!canvas || bounds.IsEmpty()) {
    return;
  }

  gfx::RectF card(bounds);
  card.Inset(0.5f);
  const float radius =
      static_cast<float>(kSidebarMetrics.favorites_dock_corner_radius);

  const SidebarItemColors colors = ResolveSidebarItemColors(
      color_provider, state, hover_progress, frame_active,
      /*persistent_surface=*/true);

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(colors.fill);
  canvas->DrawRoundRect(card, radius, fill);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(1.0f);
  stroke.setColor(colors.stroke);
  canvas->DrawRoundRect(card, radius, stroke);
}

namespace {

constexpr SkColor kFavoritesDropZoneFill = SkColorSetARGB(54, 54, 93, 85);
constexpr float kFavoritesDropZoneTitleContrastRatio = 7.0f;

struct FavoritesDropZonePalette {
  SkColor fill;
  SkColor stroke;
  SkColor badge;
  SkColor star;
  SkColor title;
  SkColor subtitle;
};

FavoritesDropZonePalette ResolveFavoritesDropZonePalette(
    const ui::ColorProvider& color_provider) {
  const SkColor surface = color_utils::GetResultingPaintColor(
      kFavoritesDropZoneFill, ResolveShellContrastBackground(color_provider));
  const SkColor title =
      color_utils::BlendForMinContrast(
          color_provider.GetColor(ui::kColorLabelForeground), surface,
          std::nullopt, kFavoritesDropZoneTitleContrastRatio)
          .color;
  const SkColor subtitle =
      color_utils::BlendForMinContrast(
          color_provider.GetColor(ui::kColorLabelForegroundSecondary), surface,
          std::nullopt, color_utils::kMinimumReadableContrastRatio)
          .color;
  return {
      .fill = kFavoritesDropZoneFill,
      .stroke = SkColorSetA(title, 88),
      .badge = SkColorSetA(title, 38),
      .star = SkColorSetA(title, 210),
      .title = title,
      .subtitle = subtitle,
  };
}

void PaintFavoritesDropZoneSurface(gfx::Canvas* canvas,
                                   const gfx::Rect& bounds,
                                   const ui::ColorProvider& color_provider) {
  const FavoritesDropZonePalette palette =
      ResolveFavoritesDropZonePalette(color_provider);
  gfx::RectF card(bounds);
  card.Inset(0.5f);
  const float radius =
      static_cast<float>(kSidebarMetrics.favorites_dock_corner_radius);

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setColor(palette.fill);
  canvas->DrawRoundRect(card, radius, fill);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(1.25f);
  stroke.setColor(palette.stroke);
  const float intervals[] = {3.5f, 3.5f};
  stroke.setPathEffect(cc::PathEffect::MakeDash(intervals, 2, 0));
  canvas->DrawRoundRect(card, radius, stroke);

  const gfx::PointF star_center(card.CenterPoint().x(), card.y() + 18.0f);
  cc::PaintFlags badge;
  badge.setAntiAlias(true);
  badge.setColor(palette.badge);
  canvas->DrawCircle(star_center, 10.0f, badge);

  SkPathBuilder star;
  for (int i = 0; i < 5; ++i) {
    const float angle =
        static_cast<float>(i) * 4.0f * 3.14159265f / 5.0f - 3.14159265f / 2.0f;
    const gfx::PointF p(star_center.x() + 5.5f * std::cos(angle),
                        star_center.y() + 5.5f * std::sin(angle));
    if (i == 0) {
      star.moveTo(p.x(), p.y());
    } else {
      star.lineTo(p.x(), p.y());
    }
  }
  star.close();
  cc::PaintFlags star_paint;
  star_paint.setAntiAlias(true);
  star_paint.setColor(palette.star);
  canvas->DrawPath(star.detach(), star_paint);

  gfx::FontList title_font =
      gfx::FontList().Derive(0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM);
  gfx::FontList subtitle_font =
      gfx::FontList().Derive(-2, gfx::Font::NORMAL, gfx::Font::Weight::NORMAL);

  const int text_width = std::max(0, bounds.width() - 24);
  gfx::Rect title_bounds(12, static_cast<int>(star_center.y()) + 12, text_width,
                         16);
  gfx::Rect subtitle_bounds(12, title_bounds.bottom() + 1, text_width, 24);
  canvas->DrawStringRectWithFlags(u"Favorites로 끌어다 놓기", title_font,
                                  palette.title, title_bounds,
                                  gfx::Canvas::TEXT_ALIGN_CENTER);
  canvas->DrawStringRectWithFlags(
      u"자주 쓰는 사이트와 앱을 가까이 둡니다", subtitle_font, palette.subtitle,
      subtitle_bounds,
      gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::MULTI_LINE);
}

class FavoritesDropZoneGhost : public views::View,
                               public gfx::AnimationDelegate {
 public:
  FavoritesDropZoneGhost(const gfx::Rect& bounds, float opacity) {
    SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetBoundsRect(bounds);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    fade_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
    fade_animation_.SetSlideDuration(base::Milliseconds(
        kSidebarMetrics.favorites_drop_zone_commit_fade_duration_ms));
    fade_animation_.Reset(opacity);
    layer()->SetOpacity(opacity);
  }
  FavoritesDropZoneGhost(const FavoritesDropZoneGhost&) = delete;
  FavoritesDropZoneGhost& operator=(const FavoritesDropZoneGhost&) = delete;
  ~FavoritesDropZoneGhost() override { fade_animation_.Stop(); }

  void Start() { fade_animation_.Hide(); }

  void AnimationProgressed(const gfx::Animation*) override {
    layer()->SetOpacity(static_cast<float>(fade_animation_.GetCurrentValue()));
  }

  void AnimationEnded(const gfx::Animation*) override {
    AnimationProgressed(nullptr);
    if (fade_animation_.GetCurrentValue() != 0.0) {
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<FavoritesDropZoneGhost> ghost) {
                         if (ghost && ghost->parent()) {
                           ghost->parent()->RemoveChildViewT(ghost.get());
                         }
                       },
                       weak_factory_.GetWeakPtr()));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    PaintFavoritesDropZoneSurface(canvas, GetLocalBounds(),
                                  *GetColorProvider());
  }

 private:
  gfx::SlideAnimation fade_animation_{this};
  base::WeakPtrFactory<FavoritesDropZoneGhost> weak_factory_{this};
};

}  // namespace

class FavoritesDropZone : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(FavoritesDropZone, views::View)

 public:
  FavoritesDropZone() {
    SetID(kSidebarFavoritesDropZoneViewId);
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    SetVisible(false);
    layout_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
    surface_animation_.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
  }
  FavoritesDropZone(const FavoritesDropZone&) = delete;
  FavoritesDropZone& operator=(const FavoritesDropZone&) = delete;
  ~FavoritesDropZone() override {
    surface_reveal_timer_.Stop();
    layout_animation_.Stop();
    surface_animation_.Stop();
  }

  void SetOpen(bool open, bool animate) {
    if (open_ == open) {
      return;
    }
    open_ = open;
    surface_reveal_timer_.Stop();

    const bool rich_animation = gfx::Animation::ShouldRenderRichAnimation() &&
                                !gfx::Animation::PrefersReducedMotion();
    if (!open && !animate && rich_animation) {
      CreateCommitFadeGhost();
      SnapTo(false);
      return;
    }

    const bool should_animate = animate && rich_animation;
    if (!should_animate) {
      SnapTo(open);
      return;
    }

    if (open) {
      SetVisible(true);
      layout_animation_.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
      layout_animation_.SetSlideDuration(base::Milliseconds(
          kSidebarMetrics.favorites_drop_zone_open_duration_ms));
      layout_animation_.Show();

      surface_animation_.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
      surface_animation_.SetSlideDuration(base::Milliseconds(std::max(
          1, kSidebarMetrics.favorites_drop_zone_open_duration_ms -
                 kSidebarMetrics.favorites_drop_zone_surface_delay_ms)));
      if (surface_animation_.GetCurrentValue() > 0.0) {
        surface_animation_.Show();
      } else {
        surface_reveal_timer_.Start(
            FROM_HERE,
            base::Milliseconds(
                kSidebarMetrics.favorites_drop_zone_surface_delay_ms),
            base::BindOnce(&FavoritesDropZone::StartSurfaceReveal,
                           base::Unretained(this)));
      }
    } else {
      layout_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      layout_animation_.SetSlideDuration(base::Milliseconds(
          kSidebarMetrics.favorites_drop_zone_close_duration_ms));
      surface_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      surface_animation_.SetSlideDuration(base::Milliseconds(
          kSidebarMetrics.favorites_drop_zone_close_duration_ms));
      layout_animation_.Hide();
      surface_animation_.Hide();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int width = available_size.width().value_or(
        kSidebarMetrics.expanded_width -
        2 * kSidebarMetrics.section_horizontal_inset);
    return gfx::Size(
        width, base::ClampRound(kSidebarMetrics.favorites_drop_zone_height *
                                layout_animation_.GetCurrentValue()));
  }

  void AnimationProgressed(const gfx::Animation* animation) override {
    UpdateReveal(animation == &layout_animation_);
  }

  void AnimationEnded(const gfx::Animation* animation) override {
    UpdateReveal(animation == &layout_animation_);
    if (!open_ && layout_animation_.GetCurrentValue() == 0.0) {
      SetVisible(false);
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    PaintFavoritesDropZoneSurface(canvas, GetLocalBounds(),
                                  *GetColorProvider());
  }

 private:
  void StartSurfaceReveal() {
    if (open_) {
      surface_animation_.Show();
    }
  }

  void SnapTo(bool open) {
    layout_animation_.Reset(open ? 1.0 : 0.0);
    surface_animation_.Reset(open ? 1.0 : 0.0);
    SetVisible(open);
    UpdateReveal(true);
  }

  void CreateCommitFadeGhost() {
    if (!parent() || bounds().IsEmpty() || !GetVisible()) {
      return;
    }
    const float opacity = layer()->opacity();
    if (opacity <= 0.0f) {
      return;
    }
    std::unique_ptr<views::View> ghost =
        std::make_unique<FavoritesDropZoneGhost>(bounds(), opacity);
    auto* ghost_ptr = static_cast<FavoritesDropZoneGhost*>(ghost.get());
    parent()->AddChildView(std::move(ghost));
    ghost_ptr->Start();
  }

  void UpdateReveal(bool preferred_size_changed) {
    layer()->SetOpacity(
        static_cast<float>(surface_animation_.GetCurrentValue()));
    if (preferred_size_changed) {
      PreferredSizeChanged();
    }
  }

  bool open_ = false;
  gfx::SlideAnimation layout_animation_{this};
  gfx::SlideAnimation surface_animation_{this};
  base::OneShotTimer surface_reveal_timer_;
};

BEGIN_METADATA(FavoritesDropZone)
END_METADATA

std::unique_ptr<views::View> CreateFavoritesDropZone() {
  return std::make_unique<FavoritesDropZone>();
}

void SetFavoritesDropZoneOpen(views::View& drop_zone, bool open, bool animate) {
  if (auto* zone = views::AsViewClass<FavoritesDropZone>(&drop_zone)) {
    zone->SetOpen(open, animate);
  }
}

bool PointHitsFavoritesDropTarget(const views::View& tab_strip,
                                  const gfx::Point& point_in_screen) {
  gfx::Rect hit;
  for (int id :
       {kSidebarFavoritesDropZoneViewId, kSidebarFavoritesDockViewId}) {
    const views::View* const target = tab_strip.GetViewByID(id);
    if (!target || !target->GetVisible()) {
      continue;
    }
    const gfx::Rect bounds = target->GetBoundsInScreen();
    if (bounds.Contains(point_in_screen)) {
      return true;
    }
    hit.Union(bounds);
  }
  if (hit.IsEmpty()) {
    // Once the last Favorite's invitation has folded away, keep only the
    // existing one-row magnet above the Tab strip as a way back. A virtual
    // full-height target here would cover the first Tab insertion gap again.
    const gfx::Rect strip = tab_strip.GetBoundsInScreen();
    hit = gfx::Rect(
        strip.x() + kSidebarMetrics.section_horizontal_inset, strip.y(),
        std::max(0,
                 strip.width() - 2 * kSidebarMetrics.section_horizontal_inset),
        1);
  }

  // Same vertical band as the dock, extended to the window's content edge.
  // A favorite dragged past the last cell stays last instead of detaching.
  gfx::Rect magnet = hit;
  magnet.Inset(gfx::Insets::TLBR(-kSidebarMetrics.tab_row_height, 0, 0, 0));
  const views::Widget* const widget = tab_strip.GetWidget();
  if (!widget) {
    return false;
  }
  const gfx::Rect window = widget->GetWindowBoundsInScreen();
  if (base::i18n::IsRTL()) {
    magnet.set_x(window.x());
    magnet.set_width(std::max(0, hit.right() - window.x()));
  } else {
    magnet.set_width(std::max(0, window.right() - magnet.x()));
  }
  return magnet.Contains(point_in_screen);
}

class FavoritesDragPreview : public views::View {
  METADATA_HEADER(FavoritesDragPreview, views::View)

 public:
  FavoritesDragPreview() {
    SetID(kSidebarFavoritesDragPreviewViewId);
    SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.94f);

    icon_ = AddChildView(std::make_unique<views::ImageView>());
    const int icon_size = kSidebarMetrics.tab_icon_design_width;
    icon_->SetImageSize(gfx::Size(icon_size, icon_size));
    icon_->SetCanProcessEventsWithinSubtree(false);

    title_ = AddChildView(std::make_unique<VerticalTabTextView>());
    title_->SetCanProcessEventsWithinSubtree(false);
  }
  FavoritesDragPreview(const FavoritesDragPreview&) = delete;
  FavoritesDragPreview& operator=(const FavoritesDragPreview&) = delete;
  ~FavoritesDragPreview() override = default;

  void Update(const gfx::Point& point_in_screen,
              bool over_favorites,
              const ui::ImageModel& favicon,
              const std::u16string& title,
              const gfx::Vector2d& grab_offset,
              const gfx::Size& source_tile_size) {
    over_favorites_ = over_favorites;
    icon_->SetImage(favicon);
    title_->SetTitle(title);
    title_->SetVisible(!over_favorites_);
    UpdateColors();

    views::View* const strip = parent();
    if (!strip) {
      return;
    }

    const int pad = kSidebarMetrics.tab_strip_horizontal_padding;
    gfx::Size size = source_tile_size;
    gfx::Vector2d offset = grab_offset;
    if (over_favorites_) {
      const gfx::Size cell = LiveFavoriteCellSize(*strip);
      if (size.IsEmpty()) {
        size = cell;
      } else if (!cell.IsEmpty() && size.width() > cell.width() + 8) {
        offset = gfx::Vector2d(
            size.width() ? offset.x() * cell.width() / size.width()
                         : offset.x(),
            size.height() ? offset.y() * cell.height() / size.height()
                          : offset.y());
        size = cell;
      }
    } else {
      const gfx::Size row_size(
          std::max(DockMinCell(), strip->width() - 2 * pad),
          kSidebarMetrics.tab_row_height);
      if (!size.IsEmpty() && size != row_size) {
        offset = gfx::Vector2d(
            size.width() ? offset.x() * row_size.width() / size.width()
                         : offset.x(),
            size.height() ? offset.y() * row_size.height() / size.height()
                          : offset.y());
      }
      size = row_size;
    }

    gfx::Rect bounds = views::View::ConvertRectFromScreen(
        strip, FavoritesDraggedTileInScreen(point_in_screen, offset, size));
    if (!over_favorites_) {
      int x = pad;
      if (base::i18n::IsRTL()) {
        x = strip->width() - pad - size.width();
      }
      bounds.set_x(x);
      bounds.set_width(size.width());
      bounds.set_height(size.height());
    }
    // Do not AdjustToFit over Favorites: clamping shifts the tile relative
    // to the cursor at drag start.
    if (!over_favorites_) {
      bounds.AdjustToFit(strip->GetLocalBounds());
    }
    SetBoundsRect(bounds);
    SetVisible(true);
    strip->ReorderChildView(this, strip->children().size() - 1);
  }

  void Layout(PassKey) override {
    const int icon_size = kSidebarMetrics.tab_icon_design_width;
    if (over_favorites_) {
      icon_->SetBoundsRect(gfx::Rect((width() - icon_size) / 2,
                                     (height() - icon_size) / 2, icon_size,
                                     icon_size));
      return;
    }

    const int pad = kSidebarMetrics.tab_content_horizontal_padding;
    const int gap = kSidebarMetrics.bookmarks_image_label_spacing;
    icon_->SetBoundsRect(
        gfx::Rect(pad, (height() - icon_size) / 2, icon_size, icon_size));
    const int title_x = pad + icon_size + gap;
    title_->SetBoundsRect(
        gfx::Rect(title_x, 0, std::max(0, width() - title_x - pad), height()));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (over_favorites_) {
      PaintFavoritesCellBackground(canvas, GetLocalBounds(),
                                   SidebarItemVisualState::kDragging,
                                   /*hover_progress=*/1.0,
                                   GetWidget()
                                       ? GetWidget()->ShouldPaintAsActive()
                                       : true,
                                   *GetColorProvider());
    } else {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setColor(fill_color_);
      canvas->DrawRoundRect(
          GetLocalBounds(),
          static_cast<float>(kSidebarMetrics.group_header_corner_radius), fill);
    }
    views::View::OnPaint(canvas);
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateColors();
  }

 private:
  void UpdateColors() {
    const ui::ColorProvider* const provider = GetColorProvider();
    const bool frame_active =
        !GetWidget() || GetWidget()->ShouldPaintAsActive();
    const SidebarItemColors colors =
        provider ? ResolveSidebarItemColors(
                       *provider, SidebarItemVisualState::kDragging,
                       /*hover_progress=*/1.0, frame_active,
                       /*persistent_surface=*/true)
                 : SidebarItemColors();
    const SkColor foreground = colors.foreground;
    title_->SetColors(foreground, foreground);
    fill_color_ = colors.fill;
  }

  bool over_favorites_ = false;
  SkColor fill_color_ = SkColorSetARGB(210, 255, 255, 255);
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<VerticalTabTextView> title_ = nullptr;
};

BEGIN_METADATA(FavoritesDragPreview)
END_METADATA

void ShowFavoritesDragPreview(views::View& tab_strip,
                              const gfx::Point& point_in_screen,
                              bool over_favorites,
                              const ui::ImageModel& favicon,
                              const std::u16string& title,
                              const gfx::Vector2d& grab_offset,
                              const gfx::Size& source_tile_size) {
  auto* preview = static_cast<FavoritesDragPreview*>(
      tab_strip.GetViewByID(kSidebarFavoritesDragPreviewViewId));
  if (!preview) {
    preview = tab_strip.AddChildView(std::make_unique<FavoritesDragPreview>());
  }
  preview->Update(point_in_screen, over_favorites, favicon, title, grab_offset,
                  source_tile_size);
}

void HideFavoritesDragPreview(views::View& tab_strip) {
  if (views::View* const preview =
          tab_strip.GetViewByID(kSidebarFavoritesDragPreviewViewId)) {
    preview->SetVisible(false);
  }
}

bool CanFavoriteContext(const TabStripModel& model, int context_index) {
  if (!UsesExpandedSidebarPresentation()) {
    return true;
  }
  if (!model.ContainsIndex(context_index)) {
    return false;
  }

  int adding = 0;
  if (model.IsTabSelected(context_index)) {
    for (const auto& tab : model.selection_model().selected_tabs()) {
      if (tab && !tab->IsPinned()) {
        ++adding;
      }
    }
  } else if (!model.IsTabPinned(context_index)) {
    adding = 1;
  }
  return CanAddFavorite(model.IndexOfFirstNonPinnedTab(), adding);
}

bool CanFavoriteDrop(int pinned_count, int adding) {
  return !UsesExpandedSidebarPresentation() ||
         CanAddFavorite(pinned_count, adding);
}

}  // namespace yee
