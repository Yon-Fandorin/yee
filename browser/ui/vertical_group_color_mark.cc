// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/vertical_group_color_mark.h"

#include <algorithm>
#include <cmath>

#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/size.h"

namespace yee {
namespace {

constexpr SkColor kUnreadDotColor = SkColorSetRGB(0xD0, 0x89, 0x3A);
constexpr SkColor kUnreadDotRingColor = SkColorSetRGB(0xA8, 0xD2, 0xC8);
constexpr SkColor kNeedsInputMixColor = SkColorSetRGB(0xC9, 0x89, 0x3A);
constexpr float kNeedsInputHaloAlpha = 0.18f;
constexpr float kAudioHaloColorAlpha = 0.22f;
constexpr float kAudioHaloMinOpacity = 0.35f;
constexpr float kAudioHaloMaxOpacity = 0.70f;
constexpr float kAudioHaloMinScale = 0.72f;

gfx::Size SlotSize() {
  const int size = kSidebarMetrics.group_mark_slot_size;
  return gfx::Size(size, size);
}

}  // namespace

VerticalGroupColorMark::VerticalGroupColorMark() {
  SetCanProcessEventsWithinSubtree(false);
  halo_animation_.SetTweenType(gfx::Tween::EASE_IN_OUT);
  halo_animation_.SetThrobDuration(base::Milliseconds(900));
}

VerticalGroupColorMark::~VerticalGroupColorMark() {
  halo_animation_.Stop();
}

void VerticalGroupColorMark::SetMarkColor(SkColor color) {
  if (color_ == color) {
    return;
  }
  color_ = color;
  SchedulePaint();
}

void VerticalGroupColorMark::SetNeedsInput(bool needs_input) {
  if (needs_input_ == needs_input) {
    return;
  }
  needs_input_ = needs_input;
  UpdateHaloAnimation();
}

void VerticalGroupColorMark::SetAudioActive(bool audio_active) {
  if (audio_active_ == audio_active) {
    return;
  }
  audio_active_ = audio_active;
  UpdateHaloAnimation();
}

void VerticalGroupColorMark::SetUnread(bool unread) {
  if (unread_ == unread) {
    return;
  }
  unread_ = unread;
  SchedulePaint();
}

gfx::Size VerticalGroupColorMark::CalculatePreferredSize(
    const views::SizeBounds&) const {
  return SlotSize();
}

gfx::Size VerticalGroupColorMark::GetMinimumSize() const {
  return SlotSize();
}

void VerticalGroupColorMark::OnPaint(gfx::Canvas* canvas) {
  if (SkColorGetA(color_) == 0) {
    return;
  }

  const gfx::Rect bounds = GetLocalBounds();
  if (bounds.IsEmpty()) {
    return;
  }

  const gfx::PointF center(bounds.CenterPoint());
  const float mark_radius = kSidebarMetrics.group_mark_size / 2.0f;
  const bool show_needs_input = needs_input_;
  const bool show_audio = audio_active_ && !needs_input_;
  const bool show_unread = unread_;

  if (show_audio) {
    const double throb = gfx::Animation::ShouldRenderRichAnimation()
                             ? halo_animation_.GetCurrentValue()
                             : 1.0;
    const float scale = static_cast<float>(gfx::Tween::DoubleValueBetween(
        throb, kAudioHaloMinScale, 1.0));
    const float opacity = static_cast<float>(gfx::Tween::DoubleValueBetween(
        throb, kAudioHaloMinOpacity, kAudioHaloMaxOpacity));
    cc::PaintFlags halo_flags;
    halo_flags.setAntiAlias(true);
    halo_flags.setStyle(cc::PaintFlags::kFill_Style);
    halo_flags.setColor(SkColorSetA(
        color_, static_cast<SkAlpha>(std::clamp(
                    std::lround(kAudioHaloColorAlpha * opacity * 255.0f), 0L,
                    255L))));
    canvas->DrawCircle(center, (bounds.width() / 2.0f) * scale, halo_flags);
  }

  if (show_needs_input) {
    cc::PaintFlags glow_flags;
    glow_flags.setAntiAlias(true);
    glow_flags.setStyle(cc::PaintFlags::kFill_Style);
    glow_flags.setColor(SkColorSetA(
        color_, static_cast<SkAlpha>(std::lround(kNeedsInputHaloAlpha * 255))));
    canvas->DrawCircle(center, mark_radius + 3.0f, glow_flags);

    cc::PaintFlags ring_flags;
    ring_flags.setAntiAlias(true);
    ring_flags.setStyle(cc::PaintFlags::kStroke_Style);
    ring_flags.setStrokeWidth(1.0f);
    ring_flags.setColor(color_utils::AlphaBlend(color_, kNeedsInputMixColor,
                                                /*alpha=*/0.55f));
    canvas->DrawCircle(center, mark_radius + 1.0f, ring_flags);
  }

  cc::PaintFlags mark_flags;
  mark_flags.setAntiAlias(true);
  mark_flags.setStyle(cc::PaintFlags::kFill_Style);
  mark_flags.setColor(color_);
  canvas->DrawCircle(center, mark_radius, mark_flags);

  if (show_unread) {
    const float dot = kSidebarMetrics.group_unread_dot_size;
    const gfx::PointF dot_center(center.x() + mark_radius - 0.5f,
                                 center.y() - mark_radius + 0.5f);
    cc::PaintFlags ring_flags;
    ring_flags.setAntiAlias(true);
    ring_flags.setStyle(cc::PaintFlags::kFill_Style);
    ring_flags.setColor(kUnreadDotRingColor);
    canvas->DrawCircle(dot_center, (dot / 2.0f) + 1.5f, ring_flags);

    cc::PaintFlags dot_flags;
    dot_flags.setAntiAlias(true);
    dot_flags.setStyle(cc::PaintFlags::kFill_Style);
    dot_flags.setColor(kUnreadDotColor);
    canvas->DrawCircle(dot_center, dot / 2.0f, dot_flags);
  }
}

void VerticalGroupColorMark::AnimationProgressed(
    const gfx::Animation* animation) {
  if (animation == &halo_animation_) {
    SchedulePaint();
  }
}

void VerticalGroupColorMark::UpdateHaloAnimation() {
  const bool should_animate =
      audio_active_ && !needs_input_ &&
      gfx::Animation::ShouldRenderRichAnimation();
  if (should_animate) {
    if (!halo_animation_.is_animating()) {
      halo_animation_.StartThrobbing(-1);
    }
  } else if (halo_animation_.is_animating()) {
    halo_animation_.Stop();
  }
  SchedulePaint();
}

BEGIN_METADATA(VerticalGroupColorMark)
END_METADATA

}  // namespace yee
