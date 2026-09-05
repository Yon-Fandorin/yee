// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_transition.h"

#include <array>

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

constexpr BrowserSurfaceChromeState kWindowedChrome{
    SurfaceGeometryStableState::kWindowed,
    /*toolbar_participates=*/true,
    /*sidebar_participates=*/true};
constexpr BrowserSurfaceChromeState kHiddenFullscreenChrome{
    SurfaceGeometryStableState::kFullscreenHiddenChrome,
    /*toolbar_participates=*/false,
    /*sidebar_participates=*/true};

TEST(BrowserSurfaceTransitionTest,
     PendingTransitionKeepsLastCommittedChromeParticipation) {
  SurfaceGeometryTransitionEpoch epoch;
  epoch.committed = kWindowedChrome;
  epoch.destination = kWindowedChrome;

  EXPECT_EQ(1u, BeginSurfaceGeometryTransition(
                    epoch, kHiddenFullscreenChrome,
                    SurfaceGeometryPlatformPath::kMacNativeAsync));
  EXPECT_EQ(kWindowedChrome, ResolveBrowserSurfaceChromeStateForLayout(
                                 epoch, kHiddenFullscreenChrome));
  EXPECT_EQ(SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination,
            epoch.phase);

  EXPECT_TRUE(CommitSurfaceGeometryTransition(epoch, kHiddenFullscreenChrome));
  EXPECT_EQ(kHiddenFullscreenChrome, ResolveBrowserSurfaceChromeStateForLayout(
                                         epoch, kHiddenFullscreenChrome));
  EXPECT_EQ(1u, epoch.committed_generation);
  EXPECT_EQ(SurfaceGeometryTransitionPhase::kStable, epoch.phase);
}

TEST(BrowserSurfaceTransitionTest,
     ReverseRejectsLateCallbackAndCommitsNewestGeneration) {
  constexpr std::array paths{
      SurfaceGeometryPlatformPath::kMacLegacy,
      SurfaceGeometryPlatformPath::kMacNativeAsync,
      SurfaceGeometryPlatformPath::kWindowsLegacy,
      SurfaceGeometryPlatformPath::kWindowsAsync,
      SurfaceGeometryPlatformPath::kLinuxLegacy,
      SurfaceGeometryPlatformPath::kLinuxAsync,
  };

  for (const SurfaceGeometryPlatformPath path : paths) {
    SurfaceGeometryTransitionEpoch epoch;
    epoch.committed = kWindowedChrome;
    epoch.destination = kWindowedChrome;

    EXPECT_EQ(1u, BeginSurfaceGeometryTransition(epoch, kHiddenFullscreenChrome,
                                                 path));
    EXPECT_EQ(2u, BeginSurfaceGeometryTransition(epoch, kWindowedChrome, path));
    EXPECT_FALSE(
        CommitSurfaceGeometryTransition(epoch, kHiddenFullscreenChrome));
    EXPECT_EQ(kWindowedChrome, epoch.committed);
    EXPECT_EQ(0u, epoch.committed_generation);
    EXPECT_EQ(SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination,
              epoch.phase);

    EXPECT_TRUE(CommitSurfaceGeometryTransition(epoch, kWindowedChrome));
    EXPECT_EQ(2u, epoch.committed_generation);
    EXPECT_EQ(SurfaceGeometryTransitionPhase::kStable, epoch.phase);
  }
}

TEST(BrowserSurfaceTransitionTest,
     HiddenFullscreenSuppressesHeaderButKeepsActualSidebarDecision) {
  EXPECT_EQ((BrowserSurfaceChromeParticipation{/*header_participates=*/false,
                                               /*sidebar_participates=*/true}),
            ResolveBrowserSurfaceChromeParticipation(kHiddenFullscreenChrome,
                                                     /*split=*/false));

  BrowserSurfaceChromeState always_toolbar = kHiddenFullscreenChrome;
  always_toolbar.stable_state =
      SurfaceGeometryStableState::kFullscreenWithToolbar;
  always_toolbar.toolbar_participates = true;
  EXPECT_EQ((BrowserSurfaceChromeParticipation{/*header_participates=*/true,
                                               /*sidebar_participates=*/true}),
            ResolveBrowserSurfaceChromeParticipation(always_toolbar,
                                                     /*split=*/false));
  EXPECT_EQ((BrowserSurfaceChromeParticipation{/*header_participates=*/false,
                                               /*sidebar_participates=*/true}),
            ResolveBrowserSurfaceChromeParticipation(always_toolbar,
                                                     /*split=*/true));
}

TEST(BrowserSurfaceTransitionTest,
     StableAlwaysToolbarPreferenceChangeUsesLiveNativeState) {
  SurfaceGeometryTransitionEpoch epoch;
  epoch.committed = kHiddenFullscreenChrome;
  epoch.destination = kHiddenFullscreenChrome;

  BrowserSurfaceChromeState always_toolbar = kHiddenFullscreenChrome;
  always_toolbar.stable_state =
      SurfaceGeometryStableState::kFullscreenWithToolbar;
  always_toolbar.toolbar_participates = true;
  EXPECT_EQ(always_toolbar,
            ResolveBrowserSurfaceChromeStateForLayout(epoch, always_toolbar));
  EXPECT_EQ(always_toolbar, epoch.committed);
  EXPECT_EQ(always_toolbar, epoch.destination);
}

}  // namespace
}  // namespace yee
