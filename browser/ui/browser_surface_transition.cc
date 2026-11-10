// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_transition.h"

#include <memory>
#include <optional>

#include "base/time/time.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

namespace yee {

class BrowserSurfaceFullscreenAnimation : public views::AnimationDelegateViews {
 public:
  explicit BrowserSurfaceFullscreenAnimation(views::View& view)
      : AnimationDelegateViews(&view), view_(&view) {
    animation_.SetSlideDuration(base::Milliseconds(140));
    animation_.SetTweenType(gfx::Tween::EASE_IN_OUT);
  }

  BrowserSurfaceFullscreenProgress Update(
      const BrowserSurfaceChromeState& state,
      const gfx::Rect& client_bounds,
      bool unobstructed,
      const SurfaceGeometryTransitionEpoch* epoch) {
    if (epoch && epoch->phase != SurfaceGeometryTransitionPhase::kStable) {
      last_animation_bounds_.reset();
      animation_.Reset(animation_.GetCurrentValue());
      return {animation_.GetCurrentValue(), false};
    }

    const uint64_t generation = epoch ? epoch->generation : 0;
    const bool can_animate =
        unobstructed &&
        state.stable_state ==
            SurfaceGeometryStableState::kFullscreenHiddenChrome &&
        gfx::Animation::ShouldRenderRichAnimation() &&
        !gfx::Animation::PrefersReducedMotion();
    const double destination = state.content_fullscreen ? 1.0 : 0.0;
    if (!can_animate || last_animation_bounds_ != client_bounds ||
        last_generation_ != generation ||
        animation_.GetCurrentValue() == destination) {
      animation_.Reset(destination);
    } else if (content_fullscreen_ != state.content_fullscreen) {
      if (state.content_fullscreen) {
        animation_.Show();
      } else {
        animation_.Hide();
      }
    }
    content_fullscreen_ = state.content_fullscreen;
    last_animation_bounds_ =
        can_animate ? std::make_optional(client_bounds) : std::nullopt;
    last_generation_ = generation;
    return {animation_.GetCurrentValue(), animation_.is_animating()};
  }

  void AnimationProgressed(const gfx::Animation*) override {
    if (auto* view = view_.view()) {
      view->InvalidateLayout();
    }
  }

 private:
  views::ViewTracker view_;
  gfx::SlideAnimation animation_{this};
  std::optional<gfx::Rect> last_animation_bounds_;
  uint64_t last_generation_ = 0;
  bool content_fullscreen_ = false;
};

}  // namespace yee

DEFINE_UI_CLASS_PROPERTY_TYPE(yee::SurfaceGeometryTransitionEpoch*)
DEFINE_UI_CLASS_PROPERTY_TYPE(yee::BrowserSurfaceFullscreenAnimation*)

namespace yee {

namespace {
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(BrowserSurfaceFullscreenAnimation,
                                   kBrowserSurfaceFullscreenAnimationKey)
}  // namespace

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(SurfaceGeometryTransitionEpoch,
                                   kSurfaceGeometryTransitionEpochKey)

uint64_t BeginSurfaceGeometryTransition(
    SurfaceGeometryTransitionEpoch& epoch,
    const BrowserSurfaceChromeState& destination,
    SurfaceGeometryPlatformPath platform_path) {
  ++epoch.generation;
  epoch.destination = destination;
  epoch.platform_path = platform_path;
  epoch.phase =
      SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination;
  return epoch.generation;
}

bool CommitSurfaceGeometryTransition(
    SurfaceGeometryTransitionEpoch& epoch,
    const BrowserSurfaceChromeState& observed) {
  if (epoch.phase ==
          SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination &&
      observed.stable_state != epoch.destination.stable_state) {
    return false;
  }

  epoch.committed = observed;
  epoch.destination = observed;
  epoch.committed_generation = epoch.generation;
  epoch.phase = SurfaceGeometryTransitionPhase::kStable;
  return true;
}

BrowserSurfaceChromeState ResolveBrowserSurfaceChromeStateForLayout(
    SurfaceGeometryTransitionEpoch& epoch,
    const BrowserSurfaceChromeState& live_state) {
  if (epoch.phase ==
      SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination) {
    return epoch.committed;
  }

  // Outside a transition, native state is authoritative. This also handles an
  // always-show-toolbar preference change made while already fullscreen.
  epoch.committed = live_state;
  epoch.destination = live_state;
  return live_state;
}

BrowserSurfaceChromeParticipation ResolveBrowserSurfaceChromeParticipation(
    const BrowserSurfaceChromeState& state,
    bool split) {
  if (state.content_fullscreen) {
    return {};
  }
  return BrowserSurfaceChromeParticipation{
      .header_participates =
          !split && state.toolbar_participates &&
          state.stable_state !=
              SurfaceGeometryStableState::kFullscreenHiddenChrome,
      .sidebar_participates = state.sidebar_participates};
}

BrowserSurfaceFullscreenProgress ResolveBrowserSurfaceFullscreenProgress(
    views::View& browser_view,
    const BrowserSurfaceChromeState& state,
    const gfx::Rect& client_bounds,
    bool unobstructed) {
  auto* animation =
      browser_view.GetProperty(kBrowserSurfaceFullscreenAnimationKey);
  if (!animation) {
    if (state.stable_state == SurfaceGeometryStableState::kWindowed) {
      return {};
    }
    animation = browser_view.SetProperty(
        kBrowserSurfaceFullscreenAnimationKey,
        std::make_unique<BrowserSurfaceFullscreenAnimation>(browser_view));
  }
  return animation->Update(state, client_bounds, unobstructed,
                           GetSurfaceGeometryTransition(browser_view));
}

SurfaceGeometryTransitionEpoch& InitializeSurfaceGeometryTransition(
    views::View& browser_view,
    const BrowserSurfaceChromeState& initial_state) {
  SurfaceGeometryTransitionEpoch initial;
  initial.committed = initial_state;
  initial.destination = initial_state;
  return *browser_view.SetProperty(kSurfaceGeometryTransitionEpochKey,
                                   std::move(initial));
}

SurfaceGeometryTransitionEpoch* GetSurfaceGeometryTransition(
    views::View& browser_view) {
  return browser_view.GetProperty(kSurfaceGeometryTransitionEpochKey);
}

const SurfaceGeometryTransitionEpoch* GetSurfaceGeometryTransition(
    const views::View& browser_view) {
  return browser_view.GetProperty(kSurfaceGeometryTransitionEpochKey);
}

}  // namespace yee
