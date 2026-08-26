// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/split_pane_controls.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor_extra/shadow.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/mouse_watcher.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

constexpr int kControlButtonSize = 30;
constexpr int kControlButtonGap = 2;
constexpr int kControlSurfaceInset = 4;
constexpr int kControlSurfaceCornerRadius = 19;
constexpr int kControlSurfaceShadowElevation = 6;
constexpr int kControlSurfaceOutlineOpacity = 0x70;
constexpr int kControlSurfaceShadowKeyAlphaLight = 0x1C;
constexpr int kControlSurfaceShadowAmbientAlphaLight = 0x0C;
constexpr int kControlSurfaceShadowKeyAlphaDark = 0x2C;
constexpr int kControlSurfaceShadowAmbientAlphaDark = 0x16;
constexpr int kControlRevealDurationMs = 190;
constexpr int kControlHideDurationMs = 80;
constexpr gfx::Tween::Type kControlRevealTween = gfx::Tween::EASE_IN_OUT;
constexpr gfx::Tween::Type kControlHideTween = gfx::Tween::EASE_IN_OUT;
constexpr int kControlRevealDelayMs = 70;
constexpr int kControlExitDelayMs = 32;
constexpr int kControlAnchorGap = 10;
constexpr int kSideBySideControlAnchorGap = 18;
constexpr int kControlExitSlop = 2;
constexpr float kControlRevealStartScale = 0.94f;
constexpr float kResizeHandleDimmedOpacity = 0.20f;
constexpr float kResizeHandleEmphasizedOpacity = 0.72f;
constexpr int kResizeHandleCornerRadius = 2;
// Keep the low-opacity resting mark on a perceptible linear fade. Restore it
// only after the active mark has fully disappeared, avoiding a muddy overlap.
constexpr int kResizeHandleTransitionDurationMs = 150;
constexpr int kResizeHandleRestoreDurationMs = 220;
constexpr int kDirectionalIconReplayDurationMs = 500;
constexpr int kRemoveIconReplayDurationMs = 320;
constexpr float kIconReplayRewindFraction = 0.15f;
constexpr gfx::Tween::Type kIconPreviewTween = gfx::Tween::ACCEL_40_DECEL_100_3;
constexpr float kControlIconSize = 20.0f;
constexpr float kControlIconStrokeWidth = 1.35f;
constexpr float kControlIconCueStrokeWidth = 1.25f;
constexpr float kControlIconCornerRadius = 2.75f;

enum class SplitPaneControlAction {
  kToggleLayout,
  kReverseOrder,
  kSeparateViews,
};

class SplitResizeAreaBackground : public views::Background,
                                  public gfx::AnimationDelegate {
public:
  DECLARE_SAFE_CAST_TARGET()

  SplitResizeAreaBackground(views::View &owner, views::View &handle)
      : owner_(&owner), handle_(&handle), rest_visibility_animation_(this),
        active_visibility_animation_(this) {
    // View destroys its children before its Background. Track both views so
    // animation callbacks and member destruction cannot retain the handle
    // after it has been deleted.
    owner_.SetTrackEntireViewHierarchy(true);
    rest_visibility_animation_.Reset(1.0);
    active_visibility_animation_.Reset(0.0);
    SyncVisuals();
  }

  SplitResizeAreaBackground(const SplitResizeAreaBackground &) = delete;
  SplitResizeAreaBackground &
  operator=(const SplitResizeAreaBackground &) = delete;
  ~SplitResizeAreaBackground() override = default;

  void Update(gfx::Size marker_size, bool active, bool animate,
              const gfx::Transform &target_transform) {
    const bool size_changed = marker_size_ != marker_size;
    marker_size_ = marker_size;
    const bool rich_animation = animate &&
                                gfx::Animation::ShouldRenderRichAnimation() &&
                                !gfx::Animation::PrefersReducedMotion();
    views::View *const handle = handle_.view();
    if (!handle || !handle->layer()) {
      return;
    }
    if (!rich_animation) {
      active_ = active;
      rest_visibility_animation_.Reset(active_ ? 0.0 : 1.0);
      active_visibility_animation_.Reset(active_ ? 1.0 : 0.0);
      pending_transform_.reset();
      handle->layer()->GetAnimator()->StopAnimating();
      handle->layer()->SetTransform(target_transform);
      SyncVisuals();
      return;
    }

    const bool transform_changed =
        !handle->layer()->GetTargetTransform().ApproximatelyEqual(
            target_transform);
    if (active) {
      pending_transform_.reset();
      if (transform_changed) {
        handle->layer()->GetAnimator()->StopAnimating();
        handle->layer()->SetTransform(target_transform);
      }
    } else if (transform_changed) {
      if (active_visibility_animation_.GetCurrentValue() == 0.0) {
        handle->layer()->GetAnimator()->StopAnimating();
        handle->layer()->SetTransform(target_transform);
      } else {
        pending_transform_ = target_transform;
      }
    }

    if (active_ == active) {
      if (size_changed) {
        if (views::View *const owner = owner_.view()) {
          owner->SchedulePaint();
        }
      }
      return;
    }
    active_ = active;

    if (active_) {
      rest_visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      active_visibility_animation_.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
      rest_visibility_animation_.SetSlideDuration(
          base::Milliseconds(kResizeHandleTransitionDurationMs));
      active_visibility_animation_.SetSlideDuration(
          base::Milliseconds(kResizeHandleTransitionDurationMs));
      rest_visibility_animation_.Hide();
      active_visibility_animation_.Show();
    } else {
      // Keep the resting mark hidden until the active mark is fully gone.
      rest_visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      active_visibility_animation_.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      rest_visibility_animation_.Hide();
      active_visibility_animation_.SetSlideDuration(
          base::Milliseconds(kResizeHandleTransitionDurationMs));
      active_visibility_animation_.Hide();
      if (active_visibility_animation_.GetCurrentValue() == 0.0) {
        RestoreRestingMarker();
      }
    }
  }

  void Paint(gfx::Canvas *canvas, views::View *view) const override {
    const ui::ColorProvider *const color_provider = view->GetColorProvider();
    if (!color_provider || marker_size_.IsEmpty()) {
      return;
    }

    const gfx::RectF marker((view->width() - marker_size_.width()) / 2.0f,
                            (view->height() - marker_size_.height()) / 2.0f,
                            marker_size_.width(), marker_size_.height());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(
        color_provider->GetColor(ui::kColorSysOnSurface),
        static_cast<SkAlpha>(SK_AlphaOPAQUE * kResizeHandleDimmedOpacity *
                             rest_visibility_animation_.GetCurrentValue())));
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(marker, kResizeHandleCornerRadius, flags);
  }

  void AnimationProgressed(const gfx::Animation *animation) override {
    SyncVisuals();
  }

  void AnimationEnded(const gfx::Animation *animation) override {
    if (animation == &active_visibility_animation_ && !active_) {
      RestoreRestingMarker();
    }
    SyncVisuals();
  }

private:
  void RestoreRestingMarker() {
    views::View *const handle = handle_.view();
    if (pending_transform_ && handle && handle->layer()) {
      handle->layer()->GetAnimator()->StopAnimating();
      handle->layer()->SetTransform(*pending_transform_);
      pending_transform_.reset();
    }
    rest_visibility_animation_.SetTweenType(gfx::Tween::SMOOTH_IN_OUT);
    rest_visibility_animation_.SetSlideDuration(
        base::Milliseconds(kResizeHandleRestoreDurationMs));
    rest_visibility_animation_.Show();
  }

  void SyncVisuals() {
    const float active_value =
        static_cast<float>(active_visibility_animation_.GetCurrentValue());
    if (views::View *const handle = handle_.view(); handle && handle->layer()) {
      handle->layer()->SetOpacity(kResizeHandleEmphasizedOpacity *
                                  active_value);
    }
    if (views::View *const owner = owner_.view()) {
      owner->SchedulePaint();
    }
  }

  views::ViewTracker owner_;
  views::ViewTracker handle_;
  gfx::Size marker_size_;
  gfx::SlideAnimation rest_visibility_animation_;
  gfx::SlideAnimation active_visibility_animation_;
  std::optional<gfx::Transform> pending_transform_;
  bool active_ = false;
};

DEFINE_SAFE_CAST_SUBCLASS(SplitResizeAreaBackground, views::Background)

void UpdateResizeAreaBackground(views::View &handle, bool active, bool animate,
                                const gfx::Transform &target_transform) {
  views::View *const resize_area = handle.parent();
  if (!resize_area) {
    return;
  }
  gfx::Size marker_size = handle.GetPreferredSize();
  if (marker_size.IsEmpty()) {
    marker_size = handle.size();
  }

  SplitResizeAreaBackground *background = nullptr;
  if (resize_area->background() &&
      resize_area->background()->IsA<SplitResizeAreaBackground>()) {
    background = resize_area->background()->AsA<SplitResizeAreaBackground>();
  } else {
    auto new_background =
        std::make_unique<SplitResizeAreaBackground>(*resize_area, handle);
    background = new_background.get();
    resize_area->SetBackground(std::move(new_background));
  }
  background->Update(marker_size, active, animate, target_transform);
}

class SplitPaneControlsMouseWatcherHost : public views::MouseWatcherHost {
public:
  SplitPaneControlsMouseWatcherHost(const views::View *controls,
                                    const views::ViewTracker *resize_anchor)
      : controls_(controls), resize_anchor_(resize_anchor) {}

  SplitPaneControlsMouseWatcherHost(const SplitPaneControlsMouseWatcherHost &) =
      delete;
  SplitPaneControlsMouseWatcherHost &
  operator=(const SplitPaneControlsMouseWatcherHost &) = delete;
  ~SplitPaneControlsMouseWatcherHost() override = default;

  bool Contains(const gfx::Point &screen_point, EventType type) override {
    const views::View *const resize_anchor = resize_anchor_->view();
    if (type == EventType::kExit || !controls_->GetWidget() || !resize_anchor ||
        !resize_anchor->GetWidget()) {
      return false;
    }

    const gfx::Rect controls_bounds = controls_->GetBoundsInScreen();
    const gfx::Rect resize_bounds = resize_anchor->GetBoundsInScreen();

    // Keep a tiny stability margin around the surface, but do not use a large
    // halo on every side: that makes dismissal feel delayed after the pointer
    // has clearly left. The only larger monitored area is the direct bridge
    // between the surface and the divider.
    gfx::Rect controls_zone = controls_bounds;
    controls_zone.Inset(-kControlExitSlop);

    gfx::Rect bridge;
    if (controls_bounds.bottom() <= resize_bounds.y()) {
      bridge = gfx::Rect(controls_bounds.x(), controls_bounds.bottom(),
                         controls_bounds.width(),
                         resize_bounds.y() - controls_bounds.bottom());
    } else if (resize_bounds.bottom() <= controls_bounds.y()) {
      bridge = gfx::Rect(controls_bounds.x(), resize_bounds.bottom(),
                         controls_bounds.width(),
                         controls_bounds.y() - resize_bounds.bottom());
    } else if (controls_bounds.right() <= resize_bounds.x()) {
      bridge = gfx::Rect(controls_bounds.right(), controls_bounds.y(),
                         resize_bounds.x() - controls_bounds.right(),
                         controls_bounds.height());
    } else if (resize_bounds.right() <= controls_bounds.x()) {
      bridge = gfx::Rect(resize_bounds.right(), controls_bounds.y(),
                         controls_bounds.x() - resize_bounds.right(),
                         controls_bounds.height());
    }

    return controls_zone.Contains(screen_point) ||
           bridge.Contains(screen_point) ||
           resize_bounds.Contains(screen_point);
  }

private:
  raw_ptr<const views::View> controls_;
  raw_ptr<const views::ViewTracker> resize_anchor_;
};

class SplitPaneControlButton : public ToolbarButton {
  METADATA_HEADER(SplitPaneControlButton, ToolbarButton)

public:
  SplitPaneControlButton(PressedCallback callback,
                         SplitPaneControlAction action)
      : ToolbarButton(std::move(callback)), action_(action) {
    ApplyShellControlStyle(*this);
    SetPreferredSize(gfx::Size(kControlButtonSize, kControlButtonSize));
    SetCustomCornerRadius(kControlButtonSize / 2);
    SetProperty(views::kMarginsKey, gfx::Insets());
    views::InkDrop::Get(this)->SetBaseColor(ui::kColorSysStateHoverOnSubtle);
    views::InkDrop::Get(this)->SetVisibleOpacity(1.0f);
    views::InkDrop::Get(this)->SetHighlightOpacity(1.0f);
    preview_animation_.SetTweenType(gfx::Tween::LINEAR);
  }

  SplitPaneControlButton(const SplitPaneControlButton &) = delete;
  SplitPaneControlButton &operator=(const SplitPaneControlButton &) = delete;
  ~SplitPaneControlButton() override { preview_animation_.Stop(); }

  void SetPaneState(bool side_by_side, bool active_at_start) {
    if (side_by_side_ == side_by_side && active_at_start_ == active_at_start) {
      return;
    }
    side_by_side_ = side_by_side;
    active_at_start_ = active_at_start;
    RestartPreviewIfNeeded();
    SchedulePaint();
  }

  void SetFocusPreview(bool preview) {
    if (focus_preview_ == preview) {
      return;
    }
    focus_preview_ = preview;
    UpdatePreview();
  }

  void StateChanged(ButtonState old_state) override {
    ToolbarButton::StateChanged(old_state);
    const bool hover_preview =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (hover_preview_ == hover_preview) {
      return;
    }
    hover_preview_ = hover_preview;
    UpdatePreview();
  }

  void PaintButtonContents(gfx::Canvas *canvas) override {
    const SkColor foreground = GetForegroundColor(GetState());
    const gfx::PointF origin((width() - kControlIconSize) / 2.0f,
                             (height() - kControlIconSize) / 2.0f);
    const float timeline =
        preview_active_
            ? static_cast<float>(preview_animation_.GetCurrentValue())
            : 1.0f;

    switch (action_) {
    case SplitPaneControlAction::kToggleLayout:
      PaintLayoutPreview(canvas, origin, foreground, timeline);
      break;
    case SplitPaneControlAction::kReverseOrder:
      PaintReversePreview(canvas, origin, foreground, timeline);
      break;
    case SplitPaneControlAction::kSeparateViews:
      PaintSeparatePreview(canvas, origin, foreground, timeline);
      break;
    }
  }

  SkColor GetForegroundColor(ButtonState state) const override {
    const ui::ColorProvider *const color_provider = GetColorProvider();
    if (!color_provider) {
      return ToolbarButton::GetForegroundColor(state);
    }
    switch (state) {
    case STATE_HOVERED:
    case STATE_PRESSED:
      return color_provider->GetColor(ui::kColorSysOnSurface);
    case STATE_DISABLED:
      return color_provider->GetColor(ui::kColorSysStateDisabled);
    case STATE_NORMAL:
      return color_provider->GetColor(ui::kColorSysOnSurfaceSubtle);
    case STATE_COUNT:
      break;
    }
    return color_provider->GetColor(ui::kColorSysOnSurfaceSubtle);
  }

  void AnimationProgressed(const gfx::Animation *animation) override {
    SchedulePaint();
  }

  void AnimationEnded(const gfx::Animation *animation) override {
    if (animation == &preview_animation_) {
      preview_active_ = false;
    }
    SchedulePaint();
  }

private:
  struct ReplayState {
    bool rewinding = false;
    float rewind = 1.0f;
    float action = 1.0f;
  };

  static ReplayState ResolveReplayState(float timeline) {
    if (timeline < kIconReplayRewindFraction) {
      return {.rewinding = true,
              .rewind = timeline / kIconReplayRewindFraction,
              .action = 0.0f};
    }
    const float raw_action = (timeline - kIconReplayRewindFraction) /
                             (1.0f - kIconReplayRewindFraction);
    return {.rewinding = false,
            .rewind = 1.0f,
            .action = static_cast<float>(
                gfx::Tween::CalculateValue(kIconPreviewTween, raw_action))};
  }

  static void PaintLine(gfx::Canvas *canvas, const gfx::PointF &start,
                        const gfx::PointF &end, SkColor color, SkAlpha alpha,
                        float stroke_width = kControlIconStrokeWidth) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(color, alpha));
    flags.setStrokeWidth(stroke_width);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawLine(start, end, flags);
  }

  static void PaintArrow(gfx::Canvas *canvas, const gfx::PointF &start,
                         const gfx::PointF &end, SkColor color, SkAlpha alpha) {
    const float dx = end.x() - start.x();
    const float dy = end.y() - start.y();
    const float length = std::hypot(dx, dy);
    if (length == 0.0f) {
      return;
    }
    const float unit_x = dx / length;
    const float unit_y = dy / length;
    const float head_length = std::min(2.5f, length * 0.45f);
    const float head_width = std::min(1.5f, length * 0.28f);
    const gfx::PointF head_base(end.x() - unit_x * head_length,
                                end.y() - unit_y * head_length);
    const gfx::PointF head_left(head_base.x() - unit_y * head_width,
                                head_base.y() + unit_x * head_width);
    const gfx::PointF head_right(head_base.x() + unit_y * head_width,
                                 head_base.y() - unit_x * head_width);

    PaintLine(canvas, start, end, color, alpha, kControlIconCueStrokeWidth);
    PaintLine(canvas, end, head_left, color, alpha, kControlIconCueStrokeWidth);
    PaintLine(canvas, end, head_right, color, alpha,
              kControlIconCueStrokeWidth);
  }

  static void PaintFrame(gfx::Canvas *canvas, const gfx::PointF &origin,
                         SkColor color, SkAlpha alpha) {
    const gfx::RectF frame(origin.x() + 2.25f, origin.y() + 3.25f, 15.5f,
                           13.5f);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(color, alpha));
    flags.setStrokeWidth(kControlIconStrokeWidth);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawRoundRect(frame, kControlIconCornerRadius, flags);
  }

  static std::pair<gfx::PointF, gfx::PointF>
  DividerLine(const gfx::PointF &origin, bool side_by_side) {
    if (side_by_side) {
      return {gfx::PointF(origin.x() + 10.0f, origin.y() + 5.25f),
              gfx::PointF(origin.x() + 10.0f, origin.y() + 14.75f)};
    }
    return {gfx::PointF(origin.x() + 5.25f, origin.y() + 10.0f),
            gfx::PointF(origin.x() + 14.75f, origin.y() + 10.0f)};
  }

  static void PaintDivider(gfx::Canvas *canvas, const gfx::PointF &origin,
                           bool side_by_side, SkColor color, SkAlpha alpha) {
    const auto divider = DividerLine(origin, side_by_side);
    PaintLine(canvas, divider.first, divider.second, color, alpha);
  }

  static void PaintBrokenDivider(gfx::Canvas *canvas, const gfx::PointF &origin,
                                 bool side_by_side, SkColor color,
                                 SkAlpha alpha, float gap) {
    if (gap >= 4.75f) {
      return;
    }
    const auto divider = DividerLine(origin, side_by_side);
    const gfx::PointF center(origin.x() + 10.0f, origin.y() + 10.0f);
    if (side_by_side) {
      PaintLine(canvas, divider.first,
                gfx::PointF(center.x(), center.y() - gap), color, alpha);
      PaintLine(canvas, gfx::PointF(center.x(), center.y() + gap),
                divider.second, color, alpha);
      return;
    }
    PaintLine(canvas, divider.first, gfx::PointF(center.x() - gap, center.y()),
              color, alpha);
    PaintLine(canvas, gfx::PointF(center.x() + gap, center.y()), divider.second,
              color, alpha);
  }

  static void PaintRemoveMark(gfx::Canvas *canvas, const gfx::PointF &origin,
                              SkColor color, SkAlpha alpha) {
    const gfx::PointF center(origin.x() + 10.0f, origin.y() + 10.0f);
    PaintLine(canvas, gfx::PointF(center.x() - 1.25f, center.y()),
              gfx::PointF(center.x() + 1.25f, center.y()), color, alpha,
              kControlIconCueStrokeWidth);
  }

  static gfx::PointF InterpolatePoint(const gfx::PointF &from,
                                      const gfx::PointF &to, float value) {
    return gfx::PointF(gfx::Tween::FloatValueBetween(value, from.x(), to.x()),
                       gfx::Tween::FloatValueBetween(value, from.y(), to.y()));
  }

  void PaintLayoutPreview(gfx::Canvas *canvas, const gfx::PointF &origin,
                          SkColor color, float timeline) const {
    const ReplayState replay = ResolveReplayState(timeline);
    const bool target_side_by_side = !side_by_side_;
    const gfx::PointF center(origin.x() + 10.0f, origin.y() + 10.0f);
    PaintFrame(canvas, origin, color, 0xD8);
    if (preview_active_ && replay.rewinding) {
      PaintDivider(canvas, origin, target_side_by_side, color,
                   static_cast<SkAlpha>(0xC8 * (1.0f - replay.rewind)));
      return;
    }
    const auto target = DividerLine(origin, target_side_by_side);
    const SkAlpha line_alpha =
        static_cast<SkAlpha>(0xC8 * std::min(replay.action * 4.0f, 1.0f));
    PaintLine(canvas, InterpolatePoint(center, target.first, replay.action),
              InterpolatePoint(center, target.second, replay.action), color,
              line_alpha);
  }

  void PaintReversePreview(gfx::Canvas *canvas, const gfx::PointF &origin,
                           SkColor color, float timeline) const {
    const ReplayState replay = ResolveReplayState(timeline);
    if (preview_active_ && replay.rewinding) {
      const float alpha = 1.0f - replay.rewind;
      if (side_by_side_) {
        PaintArrow(canvas, gfx::PointF(origin.x() + 4.0f, origin.y() + 7.0f),
                   gfx::PointF(origin.x() + 16.0f, origin.y() + 7.0f), color,
                   static_cast<SkAlpha>(0xD0 * alpha));
        PaintArrow(canvas, gfx::PointF(origin.x() + 16.0f, origin.y() + 13.0f),
                   gfx::PointF(origin.x() + 4.0f, origin.y() + 13.0f), color,
                   static_cast<SkAlpha>(0x90 * alpha));
        return;
      }
      PaintArrow(canvas, gfx::PointF(origin.x() + 7.0f, origin.y() + 16.0f),
                 gfx::PointF(origin.x() + 7.0f, origin.y() + 4.0f), color,
                 static_cast<SkAlpha>(0x90 * alpha));
      PaintArrow(canvas, gfx::PointF(origin.x() + 13.0f, origin.y() + 4.0f),
                 gfx::PointF(origin.x() + 13.0f, origin.y() + 16.0f), color,
                 static_cast<SkAlpha>(0xD0 * alpha));
      return;
    }
    constexpr float kArrowStagger = 0.12f;
    const float forward_progress = replay.action >= 1.0f - kArrowStagger
                                       ? 1.0f
                                       : replay.action / (1.0f - kArrowStagger);
    const float reverse_progress =
        replay.action <= kArrowStagger
            ? 0.0f
            : (replay.action - kArrowStagger) / (1.0f - kArrowStagger);
    if (side_by_side_) {
      const gfx::PointF forward_start(origin.x() + 4.0f, origin.y() + 7.0f);
      const gfx::PointF forward_end(origin.x() + 16.0f, origin.y() + 7.0f);
      const gfx::PointF reverse_start(origin.x() + 16.0f, origin.y() + 13.0f);
      const gfx::PointF reverse_end(origin.x() + 4.0f, origin.y() + 13.0f);
      PaintArrow(canvas, forward_start,
                 InterpolatePoint(forward_start, forward_end, forward_progress),
                 color, 0xD0);
      PaintArrow(canvas, reverse_start,
                 InterpolatePoint(reverse_start, reverse_end, reverse_progress),
                 color, 0x90);
      return;
    }
    const gfx::PointF upward_start(origin.x() + 7.0f, origin.y() + 16.0f);
    const gfx::PointF upward_end(origin.x() + 7.0f, origin.y() + 4.0f);
    const gfx::PointF downward_start(origin.x() + 13.0f, origin.y() + 4.0f);
    const gfx::PointF downward_end(origin.x() + 13.0f, origin.y() + 16.0f);
    PaintArrow(canvas, upward_start,
               InterpolatePoint(upward_start, upward_end, reverse_progress),
               color, 0x90);
    PaintArrow(canvas, downward_start,
               InterpolatePoint(downward_start, downward_end, forward_progress),
               color, 0xD0);
  }

  void PaintSeparatePreview(gfx::Canvas *canvas, const gfx::PointF &origin,
                            SkColor color, float timeline) const {
    const ReplayState replay = ResolveReplayState(timeline);
    PaintFrame(canvas, origin, color, 0xD8);
    if (!preview_requested_) {
      PaintBrokenDivider(canvas, origin, side_by_side_, color, 0xA8, 3.0f);
      PaintRemoveMark(canvas, origin, color, 0xC8);
      return;
    }
    if (preview_active_ && replay.rewinding) {
      const SkAlpha action_alpha =
          static_cast<SkAlpha>(0xC8 * (1.0f - replay.rewind));
      PaintBrokenDivider(canvas, origin, side_by_side_, color, action_alpha,
                         3.0f);
      PaintRemoveMark(canvas, origin, color, action_alpha);
      PaintDivider(canvas, origin, side_by_side_, color,
                   static_cast<SkAlpha>(0xB8 * replay.rewind));
      return;
    }
    PaintBrokenDivider(canvas, origin, side_by_side_, color, 0xB8,
                       replay.action * 5.0f);
  }

  bool ShouldPreview() const { return hover_preview_ || focus_preview_; }

  void UpdatePreview() {
    const bool preview = ShouldPreview();
    if (preview_requested_ == preview) {
      return;
    }
    preview_requested_ = preview;

    const bool rich_animation = gfx::Animation::ShouldRenderRichAnimation() &&
                                !gfx::Animation::PrefersReducedMotion();
    if (!preview || !rich_animation) {
      preview_animation_.Stop();
      preview_active_ = false;
      SchedulePaint();
      return;
    }

    preview_active_ = true;
    preview_animation_.Reset(0.0);
    const int duration_ms = action_ == SplitPaneControlAction::kSeparateViews
                                ? kRemoveIconReplayDurationMs
                                : kDirectionalIconReplayDurationMs;
    preview_animation_.SetSlideDuration(base::Milliseconds(duration_ms));
    preview_animation_.Show();
  }

  void RestartPreviewIfNeeded() {
    if (!ShouldPreview()) {
      return;
    }
    preview_animation_.Stop();
    preview_active_ = false;
    preview_requested_ = false;
    UpdatePreview();
  }

  const SplitPaneControlAction action_;
  gfx::SlideAnimation preview_animation_{this};
  bool hover_preview_ = false;
  bool focus_preview_ = false;
  bool preview_requested_ = false;
  bool preview_active_ = false;
  bool side_by_side_ = true;
  bool active_at_start_ = true;
};

BEGIN_METADATA(SplitPaneControlButton)
END_METADATA

class SplitPaneControlsView : public views::View,
                              public views::FocusChangeListener,
                              public views::MouseWatcherListener,
                              public gfx::AnimationDelegate {
public:
  SplitPaneControlsView(SplitPaneControlCallbacks callbacks,
                        views::View *resize_anchor)
      : callbacks_(std::move(callbacks)), resize_anchor_(resize_anchor) {
    CHECK(callbacks_.toggle_layout);
    CHECK(callbacks_.reverse_order);
    CHECK(callbacks_.exit_split);
    CHECK(callbacks_.set_resize_handle_anchored);
    CHECK(resize_anchor_.view());

    SetID(kSplitPaneControlsViewId);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.0f);
    SetVisible(false);

    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets(kControlSurfaceInset), kControlButtonGap));

    layout_button_ = AddChildView(
        CreateButton(SplitPaneControlAction::kToggleLayout,
                     base::BindRepeating(&SplitPaneControlsView::OnToggleLayout,
                                         base::Unretained(this))));
    reverse_button_ = AddChildView(
        CreateButton(SplitPaneControlAction::kReverseOrder,
                     base::BindRepeating(&SplitPaneControlsView::OnReverseOrder,
                                         base::Unretained(this))));
    exit_button_ = AddChildView(
        CreateButton(SplitPaneControlAction::kSeparateViews,
                     base::BindRepeating(&SplitPaneControlsView::OnExitSplit,
                                         base::Unretained(this))));

    SetButtonText(*reverse_button_, IDS_SPLIT_TAB_REVERSE_VIEWS);
    SetButtonText(*exit_button_, IDS_SPLIT_TAB_SEPARATE_VIEWS);
    UpdateIconsAndText();

    view_shadow_ = std::make_unique<views::ViewShadow>(
        this, kControlSurfaceShadowElevation);
    view_shadow_->SetRoundedCornerRadius(kControlSurfaceCornerRadius);

    mouse_watcher_ = std::make_unique<views::MouseWatcher>(
        std::make_unique<SplitPaneControlsMouseWatcherHost>(this,
                                                            &resize_anchor_),
        this);
    mouse_watcher_->set_notify_on_move_time(
        base::Milliseconds(kControlExitDelayMs));
    mouse_watcher_->set_notify_on_exit_time(
        base::Milliseconds(kControlExitDelayMs));

    reveal_animation_.SetTweenType(kControlRevealTween);
  }

  SplitPaneControlsView(const SplitPaneControlsView &) = delete;
  SplitPaneControlsView &operator=(const SplitPaneControlsView &) = delete;
  ~SplitPaneControlsView() override {
    reveal_timer_.Stop();
    mouse_watcher_->Stop();
    reveal_animation_.Stop();
  }

  void SetEnabledForSplit(bool enabled) {
    if (enabled_ == enabled) {
      return;
    }
    enabled_ = enabled;
    if (!enabled_) {
      Dismiss();
    }
  }

  void UpdateAnchor(const gfx::Point &anchor_in_parent) {
    if (position_locked_ || anchor_in_parent_ == anchor_in_parent) {
      return;
    }
    anchor_in_parent_ = anchor_in_parent;
    if (parent()) {
      parent()->InvalidateLayout();
    }
  }

  void SetAnchorHovered(bool hovered) {
    if (anchor_hovered_ == hovered) {
      return;
    }
    anchor_hovered_ = hovered;
    UpdateRequestedVisibility();
  }

  void Dismiss() {
    reveal_timer_.Stop();
    mouse_watcher_->Stop();
    anchor_hovered_ = false;
    pointer_in_control_region_ = false;
    position_locked_ = false;
    SnapTo(false);
  }

  void UpdateState(bool side_by_side, bool active_at_start) {
    if (side_by_side_ == side_by_side && active_at_start_ == active_at_start) {
      return;
    }
    side_by_side_ = side_by_side;
    active_at_start_ = active_at_start;
    UpdateIconsAndText();
  }

  void AddedToWidget() override {
    views::View::AddedToWidget();
    GetFocusManager()->AddFocusChangeListener(this);
    UpdateTheme();
  }

  void RemovedFromWidget() override {
    if (GetFocusManager()) {
      GetFocusManager()->RemoveFocusChangeListener(this);
    }
    views::View::RemovedFromWidget();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateTheme();
  }

  void OnDidChangeFocus(views::View *before, views::View *now) override {
    const bool had_focus = before && Contains(before);
    const bool has_focus = now && Contains(now);
    for (SplitPaneControlButton *button :
         {layout_button_.get(), reverse_button_.get(), exit_button_.get()}) {
      button->SetFocusPreview(now == button);
    }
    if (had_focus != has_focus) {
      UpdateRequestedVisibility();
    }
  }

  void MouseMovedOutOfHost() override {
    pointer_in_control_region_ = false;
    anchor_hovered_ = false;
    UpdateRequestedVisibility();
  }

  void AnimationProgressed(const gfx::Animation *animation) override {
    UpdateReveal();
  }

  void AnimationEnded(const gfx::Animation *animation) override {
    UpdateReveal();
    if (!target_open_ && reveal_animation_.GetCurrentValue() == 0.0) {
      SetVisible(false);
      position_locked_ = false;
    }
  }

  gfx::Rect GetAnchoredBounds(const gfx::Rect &parent_bounds) const {
    gfx::Rect bounds(GetPreferredSize());
    const gfx::Point anchor =
        anchor_in_parent_.value_or(parent_bounds.CenterPoint());

    const int anchor_gap =
        side_by_side_ ? kSideBySideControlAnchorGap : kControlAnchorGap;
    const int above_y = anchor.y() - anchor_gap - bounds.height();
    const int below_y = anchor.y() + anchor_gap;
    bounds.set_x(anchor.x() - bounds.width() / 2);
    bounds.set_y(above_y >= parent_bounds.y() ? above_y : below_y);

    bounds.AdjustToFit(parent_bounds);
    return bounds;
  }

  void OnPaint(gfx::Canvas *canvas) override {
    const ui::ColorProvider *const color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }

    const SkColor surface_color =
        color_provider->GetColor(ui::kColorSysSurface);
    const SkColor outline_color = color_utils::AlphaBlend(
        color_provider->GetColor(ui::kColorSysOutline), surface_color,
        static_cast<SkAlpha>(kControlSurfaceOutlineOpacity));

    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(0.5f);
    if (bounds.IsEmpty()) {
      return;
    }

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(surface_color);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(bounds, kControlSurfaceCornerRadius - 0.5f, fill);

    cc::PaintFlags outline;
    outline.setAntiAlias(true);
    outline.setColor(outline_color);
    outline.setStrokeWidth(1.0f);
    outline.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawRoundRect(bounds, kControlSurfaceCornerRadius - 0.5f, outline);
  }

private:
  std::unique_ptr<SplitPaneControlButton>
  CreateButton(SplitPaneControlAction action,
               views::Button::PressedCallback callback) {
    return std::make_unique<SplitPaneControlButton>(std::move(callback),
                                                    action);
  }

  void SetButtonText(SplitPaneControlButton &button, int string_id) {
    const std::u16string text = l10n_util::GetStringUTF16(string_id);
    button.SetTooltipText(text);
    button.GetViewAccessibility().SetName(text);
  }

  void UpdateIconsAndText() {
    for (SplitPaneControlButton *button :
         {layout_button_.get(), reverse_button_.get(), exit_button_.get()}) {
      button->SetPaneState(side_by_side_, active_at_start_);
    }
    SetButtonText(*layout_button_, side_by_side_
                                       ? IDS_SPLIT_TAB_SHOW_STACKED
                                       : IDS_SPLIT_TAB_SHOW_SIDE_BY_SIDE);
  }

  void UpdateTheme() {
    const ui::ColorProvider *const color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }
    for (SplitPaneControlButton *button :
         {layout_button_.get(), reverse_button_.get(), exit_button_.get()}) {
      button->SchedulePaint();
    }

    const SkColor surface_color =
        color_provider->GetColor(ui::kColorSysSurface);
    const bool dark = color_utils::IsDark(surface_color);
    const SkColor key =
        SkColorSetA(SK_ColorBLACK, dark ? kControlSurfaceShadowKeyAlphaDark
                                        : kControlSurfaceShadowKeyAlphaLight);
    const SkColor ambient = SkColorSetA(
        SK_ColorBLACK, dark ? kControlSurfaceShadowAmbientAlphaDark
                            : kControlSurfaceShadowAmbientAlphaLight);
    const ui::Shadow::ElevationToColorsMap shadow_colors{
        {kControlSurfaceShadowElevation, {key, ambient}}};
    view_shadow_->shadow()->SetElevationToColorsMap(shadow_colors);
    SchedulePaint();
  }

  bool ContainsFocus() const {
    return GetFocusManager() && Contains(GetFocusManager()->GetFocusedView());
  }

  bool ShouldStayOpen() const {
    return enabled_ &&
           (anchor_hovered_ || pointer_in_control_region_ || ContainsFocus());
  }

  void UpdateRequestedVisibility() {
    if (ShouldStayOpen()) {
      if (GetVisible()) {
        StartReveal(true);
      } else if (!reveal_timer_.IsRunning()) {
        reveal_timer_.Start(FROM_HERE,
                            base::Milliseconds(kControlRevealDelayMs), this,
                            &SplitPaneControlsView::ShowIfRequested);
      }
      return;
    }
    reveal_timer_.Stop();
    if (!GetVisible()) {
      return;
    }
    HideIfIdle();
  }

  void ShowIfRequested() {
    if (!ShouldStayOpen()) {
      return;
    }
    position_locked_ = true;
    pointer_in_control_region_ = true;
    StartReveal(true);
    if (GetWidget()) {
      mouse_watcher_->Start(GetWidget()->GetNativeWindow());
    }
  }

  void HideIfIdle() {
    if (!ShouldStayOpen()) {
      StartReveal(false);
    }
  }

  void StartReveal(bool open) {
    if (target_open_ == open) {
      if (open && GetVisible() && reveal_animation_.IsShowing()) {
        return;
      }
      if (!open && (!GetVisible() || reveal_animation_.IsClosing())) {
        return;
      }
    }
    target_open_ = open;
    SetResizeHandleAnchored(open);

    const bool rich_animation = gfx::Animation::ShouldRenderRichAnimation() &&
                                !gfx::Animation::PrefersReducedMotion();
    if (!rich_animation) {
      SnapTo(open);
      return;
    }

    if (open) {
      SetVisible(true);
      if (parent()) {
        parent()->InvalidateLayout();
      }
      reveal_animation_.SetTweenType(kControlRevealTween);
      reveal_animation_.SetSlideDuration(
          base::Milliseconds(kControlRevealDurationMs));
      reveal_animation_.Show();
    } else {
      mouse_watcher_->Stop();
      pointer_in_control_region_ = false;
      reveal_animation_.SetTweenType(kControlHideTween);
      reveal_animation_.SetSlideDuration(
          base::Milliseconds(kControlHideDurationMs));
      reveal_animation_.Hide();
    }
  }

  void SnapTo(bool open) {
    reveal_timer_.Stop();
    if (!open) {
      mouse_watcher_->Stop();
      pointer_in_control_region_ = false;
      position_locked_ = false;
    }
    reveal_animation_.Reset(open ? 1.0 : 0.0);
    target_open_ = open;
    SetResizeHandleAnchored(open);
    SetVisible(open);
    UpdateReveal();
  }

  void UpdateReveal() {
    const float value = static_cast<float>(reveal_animation_.GetCurrentValue());
    layer()->SetOpacity(value);
    const float scale =
        kControlRevealStartScale + (1.0f - kControlRevealStartScale) * value;
    gfx::Transform transform;
    const gfx::PointF center(GetLocalBounds().CenterPoint());
    transform.Translate(center.x(), center.y());
    transform.Scale(scale, scale);
    transform.Translate(-center.x(), -center.y());
    layer()->SetTransform(transform);
    if (view_shadow_ && view_shadow_->shadow()->shadow_layer()) {
      view_shadow_->shadow()->shadow_layer()->SetOpacity(value);
    }
  }

  void SetResizeHandleAnchored(bool anchored) {
    if (resize_handle_anchored_ == anchored) {
      return;
    }
    resize_handle_anchored_ = anchored;
    callbacks_.set_resize_handle_anchored.Run(anchored);
  }

  void OnToggleLayout(const ui::Event &) { callbacks_.toggle_layout.Run(); }
  void OnReverseOrder(const ui::Event &) { callbacks_.reverse_order.Run(); }
  void OnExitSplit(const ui::Event &) { callbacks_.exit_split.Run(); }

  SplitPaneControlCallbacks callbacks_;
  raw_ptr<SplitPaneControlButton> layout_button_ = nullptr;
  raw_ptr<SplitPaneControlButton> reverse_button_ = nullptr;
  raw_ptr<SplitPaneControlButton> exit_button_ = nullptr;
  std::unique_ptr<views::ViewShadow> view_shadow_;
  views::ViewTracker resize_anchor_;
  std::unique_ptr<views::MouseWatcher> mouse_watcher_;
  gfx::SlideAnimation reveal_animation_{this};
  base::OneShotTimer reveal_timer_;
  std::optional<gfx::Point> anchor_in_parent_;
  bool enabled_ = false;
  bool anchor_hovered_ = false;
  bool pointer_in_control_region_ = false;
  bool position_locked_ = false;
  bool target_open_ = false;
  bool side_by_side_ = true;
  bool active_at_start_ = true;
  bool resize_handle_anchored_ = false;
};

SplitPaneControlsView &AsSplitPaneControls(views::View &controls) {
  CHECK_EQ(controls.GetID(), kSplitPaneControlsViewId);
  return static_cast<SplitPaneControlsView &>(controls);
}

} // namespace

std::unique_ptr<views::View>
CreateSplitPaneControlsView(SplitPaneControlCallbacks callbacks,
                            views::View *resize_anchor) {
  return std::make_unique<SplitPaneControlsView>(std::move(callbacks),
                                                 resize_anchor);
}

void SetSplitPaneControlsEnabled(views::View &controls, bool enabled) {
  AsSplitPaneControls(controls).SetEnabledForSplit(enabled);
}

void UpdateSplitPaneControlsAnchor(views::View &controls,
                                   const gfx::Point &anchor_in_parent) {
  AsSplitPaneControls(controls).UpdateAnchor(anchor_in_parent);
}

void SetSplitPaneControlsAnchorHovered(views::View &controls, bool hovered) {
  AsSplitPaneControls(controls).SetAnchorHovered(hovered);
}

void DismissSplitPaneControls(views::View &controls) {
  AsSplitPaneControls(controls).Dismiss();
}

void UpdateSplitPaneControls(views::View &controls, bool side_by_side,
                             bool active_at_start) {
  AsSplitPaneControls(controls).UpdateState(side_by_side, active_at_start);
}

gfx::Rect GetSplitPaneControlsBounds(views::View &controls,
                                     const gfx::Rect &parent_bounds) {
  return AsSplitPaneControls(controls).GetAnchoredBounds(parent_bounds);
}

void UpdateSplitResizeHandleAppearance(views::View &handle, bool emphasized) {
  if (!handle.layer()) {
    return;
  }
  UpdateResizeAreaBackground(handle, emphasized, /*animate=*/true,
                             handle.layer()->GetTargetTransform());
  const ui::ColorProvider *const color_provider = handle.GetColorProvider();
  if (!color_provider) {
    return;
  }
  handle.SetBackground(views::CreateRoundedRectBackground(
      color_provider->GetColor(ui::kColorSysOnSurface),
      kResizeHandleCornerRadius));
  handle.SchedulePaint();
}

void UpdateSplitResizeHandleAnchor(views::View &handle,
                                   const gfx::Vector2dF &offset,
                                   bool emphasized, bool animate) {
  if (!handle.layer()) {
    return;
  }
  gfx::Transform transform;
  transform.Translate(offset.x(), offset.y());
  UpdateResizeAreaBackground(handle, emphasized, animate, transform);
}

} // namespace yee
