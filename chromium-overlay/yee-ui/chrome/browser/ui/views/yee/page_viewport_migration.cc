// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/page_viewport_migration.h"

#include "base/notreached.h"

namespace yee {

PageViewportChildPolicy GetPageViewportChildPolicy(
    PageViewportChildFamily family) {
  using Host = PageViewportChildHost;
  using Status = PageViewportMigrationStatus;

  switch (family) {
    case PageViewportChildFamily::kContentsWebView:
    case PageViewportChildFamily::kDataProtectionOverlay:
    case PageViewportChildFamily::kIndigoOverlay:
    case PageViewportChildFamily::kReadAnythingImmersive:
    case PageViewportChildFamily::kActorOverlay:
      return {Host::kPageTargetHost, Status::kApproved};

    case PageViewportChildFamily::kAiOverlayDialog:
    case PageViewportChildFamily::kGlicSelection:
    case PageViewportChildFamily::kToastAnchor:
      return {Host::kViewportOverlayHost, Status::kApproved};

    case PageViewportChildFamily::kDevToolsWebView:
    case PageViewportChildFamily::kDevToolsScrim:
    case PageViewportChildFamily::kNtpFooter:
    case PageViewportChildFamily::kContentsScrim:
      return {Host::kContentsContainer, Status::kBlockedByFamilyGates};

    case PageViewportChildFamily::kGlicContextBorder:
    case PageViewportChildFamily::kPaneHeader:
    case PageViewportChildFamily::kPaneEmphasis:
    case PageViewportChildFamily::kContainerOutline:
    case PageViewportChildFamily::kCaptureBorder:
      return {Host::kContentsContainer, Status::kPermanentDirect};
  }

  NOTREACHED();
}

}  // namespace yee
