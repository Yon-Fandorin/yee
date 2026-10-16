// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/side_panel_colors.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_recipe.h"

namespace yee {
void AddSidePanelColorMixer(ui::ColorProvider& provider,
                            const ui::ColorProviderKey& key) {
  if (!IsShellEnabled() ||
      key.contrast_mode == ui::ColorProviderKey::ContrastMode::kHigh ||
      key.forced_colors != ui::ColorProviderKey::ForcedColors::kNone) {
    return;
  }

  auto& mixer = provider.AddMixer();
  mixer[kColorSidePanelBackground] = {ui::kColorSysBase};
  mixer[kColorSidePanelContentBackground] = {ui::kColorSysBase};
  mixer[kColorSidePanelCardBackground] = {ui::kColorSysBase};
  mixer[kColorSidePanelCardPrimaryForeground] = {ui::kColorSysOnSurface};
  mixer[kColorSidePanelCardSecondaryForeground] = {
      ui::kColorSysOnSurfaceSubtle};
  mixer[kColorSidePanelEntryTitle] = {ui::kColorSysOnSurface};
  mixer[kColorSidePanelEntryIcon] = {ui::kColorSysOnSurface};
  mixer[kColorSidePanelHeaderButtonIcon] = {ui::kColorSysOnSurface};
  mixer[kColorSidePanelHeaderButtonIconDisabled] = {ui::kColorSysStateDisabled};
}
}  // namespace yee
