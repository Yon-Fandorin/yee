// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SHELL_POLICY_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SHELL_POLICY_H_

#include "chrome/browser/ui/views/yee/sidebar_metrics.h"

namespace yee {

// Yee's collapsed sidebar is an 8px edge target, not Chromium's 56px icon
// rail. Tab, group, and favorites presentation therefore stays on the
// expanded contract when the strip is pinned open or revealed on hover.
bool UsesExpandedSidebarPresentation();

// A lone tab starts as a sidebar organization drag so Favorites and the Tab
// list get first refusal. Leaving the sidebar still hands the drag back to
// Chromium's normal window move path. Group-header drags keep Chromium's
// all-tabs behavior.
bool ShouldPrioritizeSidebarTabDrag(int dragged_tab_count,
                                    int source_tab_count,
                                    bool is_group_drag,
                                    bool uses_vertical_tab_strip);

// `adding` is how many currently unpinned tabs would become favorites.
inline bool CanAddFavorite(int pinned_count, int adding = 1) {
  return adding <= 0 ||
         pinned_count + adding <= kSidebarMetrics.favorites_max_count;
}

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SHELL_POLICY_H_
