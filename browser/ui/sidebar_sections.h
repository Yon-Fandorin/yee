// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_SECTIONS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_SECTIONS_H_

#include <array>

#include "chrome/browser/ui/views/yee/yee_ui.h"

namespace yee {

// Product sections of the Yee sidebar column, top to bottom. This is the
// layout contract for the sidebar menu. Reserved slots are allocated here so
// later placement has a stable order; they must not be inserted into the view
// tree until explicitly placed.
enum class SidebarSection {
  kPins,
  kBookmarks,
  kGroups,
  kTabs,
  kChat,
  kAgentHistory,
};

enum class SidebarSectionPlacement {
  // Slot exists in the column contract but has no view and takes no space.
  kReserved,
  // Currently provided by Chromium's vertical tab strip. Yee does not own
  // the view tree for this slot yet.
  kHosted,
};

struct SidebarSectionSlot {
  SidebarSection section;
  int view_id;
  SidebarSectionPlacement placement;
};

inline constexpr int kSidebarPinsViewId = 92004;
inline constexpr int kSidebarChatViewId = 92005;
inline constexpr int kSidebarAgentHistoryViewId = 92006;

// Chromium pinned tabs are presented as the Favorites icon dock at the
// top of the sidebar. `kPins` stays reserved for Arc-style pinned tabs.
inline constexpr std::array<SidebarSectionSlot, 6> kSidebarSectionSlots{{
    {SidebarSection::kPins, kSidebarPinsViewId,
     SidebarSectionPlacement::kReserved},
    {SidebarSection::kBookmarks, kSidebarBookmarksButtonViewId,
     SidebarSectionPlacement::kReserved},
    {SidebarSection::kGroups, 0, SidebarSectionPlacement::kHosted},
    {SidebarSection::kTabs, 0, SidebarSectionPlacement::kHosted},
    {SidebarSection::kChat, kSidebarChatViewId,
     SidebarSectionPlacement::kReserved},
    {SidebarSection::kAgentHistory, kSidebarAgentHistoryViewId,
     SidebarSectionPlacement::kReserved},
}};

constexpr const SidebarSectionSlot* FindSidebarSectionSlot(
    SidebarSection section) {
  for (const SidebarSectionSlot& slot : kSidebarSectionSlots) {
    if (slot.section == section) {
      return &slot;
    }
  }
  return nullptr;
}

constexpr bool IsSidebarSectionPlaced(SidebarSection section) {
  const SidebarSectionSlot* const slot = FindSidebarSectionSlot(section);
  return slot && slot->placement == SidebarSectionPlacement::kHosted;
}

// Unplaced reserved slots contribute no height. Hosted slots are still laid
// out by Chromium, not by this contract.
constexpr int SidebarSectionAllocatedHeight(SidebarSection section) {
  if (!IsSidebarSectionPlaced(section)) {
    return 0;
  }
  switch (section) {
    case SidebarSection::kPins:
    case SidebarSection::kBookmarks:
    case SidebarSection::kGroups:
    case SidebarSection::kTabs:
    case SidebarSection::kChat:
    case SidebarSection::kAgentHistory:
      return 0;
  }
}

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_SECTIONS_H_
