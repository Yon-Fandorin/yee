// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_FOOTER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_FOOTER_H_

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "ui/gfx/geometry/point.h"

namespace views {
class View;
}

namespace yee {

inline constexpr int kSidebarFooterViewId = 92100;
inline constexpr int kSidebarFooterRootContextViewId = 92101;
inline constexpr int kSidebarFooterRootBrowserToolsViewId = 92102;
inline constexpr int kSidebarFooterRootMemoryViewId = 92103;
inline constexpr int kSidebarFooterMenuViewId = 92104;
inline constexpr int kSidebarFooterAvatarViewId = 92105;
inline constexpr int kSidebarFooterAvatarIconViewId = 92106;
inline constexpr int kSidebarFooterAvatarLabelViewId = 92107;
inline constexpr int kSidebarFooterWorkspaceMarkViewId = 92108;
inline constexpr int kSidebarFooterMenuActionIconViewId = 92109;

enum class SidebarFooterScreen {
  kRoot,
  kContext,
  kBrowserTools,
  kMemory,
};

inline constexpr std::array<SidebarFooterScreen, 3> kSidebarFooterRootScreens{
    {SidebarFooterScreen::kContext, SidebarFooterScreen::kBrowserTools,
     SidebarFooterScreen::kMemory}};

constexpr SidebarFooterScreen ParentSidebarFooterScreen(
    SidebarFooterScreen screen) {
  switch (screen) {
    case SidebarFooterScreen::kRoot:
      return SidebarFooterScreen::kRoot;
    case SidebarFooterScreen::kContext:
    case SidebarFooterScreen::kBrowserTools:
    case SidebarFooterScreen::kMemory:
      return SidebarFooterScreen::kRoot;
  }
}

// A deliberately narrow bridge to useful Chromium-owned destinations that
// are otherwise several interactions away. General Settings, Profile, and the
// reserved Bookmarks surface do not belong in this list.
enum class SidebarFooterBrowserAction {
  kReopenClosedTab,
  kDownloads,
  kHistory,
  kTabsFromOtherDevices,
  kManageExtensions,
};

inline constexpr std::array<SidebarFooterBrowserAction, 5>
    kSidebarFooterBrowserToolsActions{
        {SidebarFooterBrowserAction::kReopenClosedTab,
         SidebarFooterBrowserAction::kDownloads,
         SidebarFooterBrowserAction::kHistory,
         SidebarFooterBrowserAction::kTabsFromOtherDevices,
         SidebarFooterBrowserAction::kManageExtensions}};

struct SidebarContextItem {
  std::u16string tenant_name;
  std::u16string workspace_name;
  std::u16string account_name;
  bool account_name_is_placeholder = false;
};

struct SidebarFooterModel {
  std::vector<SidebarContextItem> contexts;
  size_t selected_context_index = 0;
  bool memory_available = false;
  bool memory_enabled = false;
};

// Creates the non-fictitious fallback used until Yee's tenant provider is
// connected. The local Chromium profile supplies the account label; the
// fallback never claims that a remote organization exists.
SidebarFooterModel CreateLocalSidebarFooterModel(std::u16string account_name);

using SidebarContextSelectedCallback =
    base::RepeatingCallback<void(size_t context_index)>;
using SidebarMemoryChangedCallback =
    base::RepeatingCallback<void(bool memory_enabled)>;
using SidebarFooterBrowserActionCallback =
    base::RepeatingCallback<void(SidebarFooterBrowserAction action)>;

// Yee owns this View and its single, in-place switching bubble. Chromium
// provides the host position, executes the curated Browser actions, and
// forwards product-model changes for context and workspace memory.
std::unique_ptr<views::View> CreateSidebarFooterView(
    SidebarFooterModel model,
    SidebarContextSelectedCallback context_selected_callback,
    SidebarFooterBrowserActionCallback browser_action_callback,
    SidebarMemoryChangedCallback memory_changed_callback);

void SetSidebarFooterVisible(views::View& view, bool visible);
bool IsSidebarFooterPositionInWindowCaption(const views::View& view,
                                            const gfx::Point& point);
views::View* GetSidebarFooterMenuForTesting(views::View& view);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SIDEBAR_FOOTER_H_
