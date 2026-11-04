// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/shell_policy.h"

#include "chrome/browser/ui/views/yee/shell_features.h"

namespace yee {

bool UsesExpandedSidebarPresentation() {
  return IsShellEnabled();
}

bool ShouldPrioritizeSidebarTabDrag(int dragged_tab_count,
                                    int source_tab_count,
                                    bool is_group_drag,
                                    bool uses_vertical_tab_strip) {
  return UsesExpandedSidebarPresentation() && uses_vertical_tab_strip &&
         !is_group_drag && dragged_tab_count == 1 && source_tab_count == 1;
}

}  // namespace yee
