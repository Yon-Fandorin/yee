// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/sidebar_sections.h"

namespace yee {
namespace {

static_assert(!IsSidebarSectionPlaced(SidebarSection::kPins));
static_assert(!IsSidebarSectionPlaced(SidebarSection::kBookmarks));
static_assert(IsSidebarSectionPlaced(SidebarSection::kGroups));
static_assert(IsSidebarSectionPlaced(SidebarSection::kTabs));
static_assert(!IsSidebarSectionPlaced(SidebarSection::kChat));
static_assert(!IsSidebarSectionPlaced(SidebarSection::kAgentHistory));
static_assert(SidebarSectionAllocatedHeight(SidebarSection::kPins) == 0);
static_assert(SidebarSectionAllocatedHeight(SidebarSection::kChat) == 0);
static_assert(SidebarSectionAllocatedHeight(SidebarSection::kAgentHistory) ==
              0);

}  // namespace
}  // namespace yee
