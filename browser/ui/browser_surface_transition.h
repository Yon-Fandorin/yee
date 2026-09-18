// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_TRANSITION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_TRANSITION_H_

#include <cstdint>

#include "ui/base/class_property.h"

namespace views {
class View;
}

namespace yee {

// The stable fullscreen state used by Browser Surface layout. Immersive
// reveal is intentionally not a distinct state: the revealed top container is
// an overlay and must not reflow the underlying Surface.
enum class SurfaceGeometryStableState {
  kWindowed,
  kFullscreenHiddenChrome,
  kFullscreenWithToolbar,
};

// Records the native path that owns the authoritative completion callback.
// The state machine is platform-neutral, but keeping this value in the epoch
// makes accidental synchronous assumptions visible in tests and diagnostics.
enum class SurfaceGeometryPlatformPath {
  kMacLegacy,
  kMacNativeAsync,
  kWindowsLegacy,
  kWindowsAsync,
  kLinuxLegacy,
  kLinuxAsync,
  kOther,
};

enum class SurfaceGeometryTransitionPhase {
  kStable,
  kAwaitingAuthoritativeDestination,
};

struct BrowserSurfaceChromeState {
  SurfaceGeometryStableState stable_state =
      SurfaceGeometryStableState::kWindowed;
  bool toolbar_participates = false;
  bool sidebar_participates = false;

  bool operator==(const BrowserSurfaceChromeState&) const = default;
};

struct SurfaceGeometryTransitionEpoch {
  BrowserSurfaceChromeState committed;
  BrowserSurfaceChromeState destination;
  SurfaceGeometryPlatformPath platform_path =
      SurfaceGeometryPlatformPath::kOther;
  SurfaceGeometryTransitionPhase phase =
      SurfaceGeometryTransitionPhase::kStable;
  uint64_t generation = 0;
  uint64_t committed_generation = 0;

  bool operator==(const SurfaceGeometryTransitionEpoch&) const = default;
};

// Starts a new transition generation without changing the geometry currently
// presented. Starting a reverse transition supersedes the preceding target.
uint64_t BeginSurfaceGeometryTransition(
    SurfaceGeometryTransitionEpoch& epoch,
    const BrowserSurfaceChromeState& destination,
    SurfaceGeometryPlatformPath platform_path);

// Commits only the destination requested by the newest generation. A stale
// callback whose observed fullscreen mode belongs to an older generation is
// ignored. Native toolbar/Sidebar participation is sampled at commit time.
bool CommitSurfaceGeometryTransition(SurfaceGeometryTransitionEpoch& epoch,
                                     const BrowserSurfaceChromeState& observed);

// While a transition is pending, returns the last committed state. Once the
// native callback commits, live stable participation is allowed to track
// ordinary non-fullscreen chrome changes without creating another epoch.
BrowserSurfaceChromeState ResolveBrowserSurfaceChromeStateForLayout(
    SurfaceGeometryTransitionEpoch& epoch,
    const BrowserSurfaceChromeState& live_state);

struct BrowserSurfaceChromeParticipation {
  bool header_participates = false;
  bool sidebar_participates = false;

  bool operator==(const BrowserSurfaceChromeParticipation&) const = default;
};

BrowserSurfaceChromeParticipation ResolveBrowserSurfaceChromeParticipation(
    const BrowserSurfaceChromeState& state,
    bool split);

SurfaceGeometryTransitionEpoch& InitializeSurfaceGeometryTransition(
    views::View& browser_view,
    const BrowserSurfaceChromeState& initial_state);
SurfaceGeometryTransitionEpoch* GetSurfaceGeometryTransition(
    views::View& browser_view);
const SurfaceGeometryTransitionEpoch* GetSurfaceGeometryTransition(
    const views::View& browser_view);

}  // namespace yee

DECLARE_UI_CLASS_PROPERTY_TYPE(yee::SurfaceGeometryTransitionEpoch*)

namespace yee {

extern const ui::ClassProperty<SurfaceGeometryTransitionEpoch*>* const
    kSurfaceGeometryTransitionEpochKey;

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BROWSER_SURFACE_TRANSITION_H_
