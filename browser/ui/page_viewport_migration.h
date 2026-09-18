// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_MIGRATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_MIGRATION_H_

#include <cstddef>

namespace yee {

// Every ContentsContainerView family whose parent is relevant to the page
// viewport boundary. Adding a family requires an explicit migration policy and
// a hierarchy assertion before it can enter either persistent host.
enum class PageViewportChildFamily {
  kContentsWebView,
  kDevToolsWebView,
  kDevToolsScrim,
  kNtpFooter,
  kDataProtectionOverlay,
  kIndigoOverlay,
  kAiOverlayDialog,
  kReadAnythingImmersive,
  kContentsScrim,
  kActorOverlay,
  kGlicSelection,
  kGlicContextBorder,
  kToastAnchor,
  kPaneHeader,
  kPaneEmphasis,
  kContainerOutline,
  kCaptureBorder,
  kMaxValue = kCaptureBorder,
};

inline constexpr size_t kPageViewportChildFamilyCount =
    static_cast<size_t>(PageViewportChildFamily::kMaxValue) + 1;

enum class PageViewportChildHost {
  kContentsContainer,
  kPageTargetHost,
  kViewportOverlayHost,
};

enum class PageViewportMigrationStatus {
  // The child stays direct until its family-specific coordinate, lookup,
  // focus, native-surface, and teardown gates pass.
  kBlockedByFamilyGates,

  // The child has passed its gates and may use the named persistent host.
  kApproved,

  // Card chrome or another native owner whose direct parent is intentional.
  kPermanentDirect,
};

struct PageViewportChildPolicy {
  PageViewportChildHost host = PageViewportChildHost::kContentsContainer;
  PageViewportMigrationStatus status =
      PageViewportMigrationStatus::kBlockedByFamilyGates;

  friend bool operator==(const PageViewportChildPolicy&,
                         const PageViewportChildPolicy&) = default;
};

PageViewportChildPolicy GetPageViewportChildPolicy(
    PageViewportChildFamily family);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_MIGRATION_H_
