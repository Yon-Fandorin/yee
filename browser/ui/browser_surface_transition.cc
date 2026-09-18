// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_transition.h"

#include "ui/views/view.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(yee::SurfaceGeometryTransitionEpoch*)

namespace yee {

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
  return BrowserSurfaceChromeParticipation{
      .header_participates =
          !split && state.toolbar_participates &&
          state.stable_state !=
              SurfaceGeometryStableState::kFullscreenHiddenChrome,
      .sidebar_participates = state.sidebar_participates};
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
