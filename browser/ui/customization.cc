// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/customization.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"

namespace yee {
bool SupportsToolbarCustomization() {
  return !IsShellEnabled();
}

void ConfigureCustomizationDataSource(content::WebUIDataSource& source) {
  source.AddBoolean("showThemeSnapshot", !IsShellEnabled());
  source.AddBoolean("showToolbarCustomization", SupportsToolbarCustomization());
  source.AddBoolean("yeeCustomization", IsShellEnabled());
  source.AddLocalizedString("yeeAppearanceDescription",
                            IDS_YEE_WEBUI_CUSTOMIZATION_DESCRIPTION);
  source.AddLocalizedString("yeeTabLayout", IDS_YEE_WEBUI_TAB_LAYOUT);
  source.AddLocalizedString("yeeTabLayoutTop", IDS_YEE_WEBUI_TAB_LAYOUT_TOP);
  source.AddLocalizedString("yeeTabLayoutSide", IDS_YEE_WEBUI_TAB_LAYOUT_SIDE);
}

CustomizationTabLayout GetCustomizationTabLayout(
    content::WebContents& contents) {
  const auto* pref = Profile::FromBrowserContext(contents.GetBrowserContext())
                         ->GetPrefs()
                         ->FindPreference(prefs::kVerticalTabsEnabled);
  auto* browser = webui::GetBrowserWindowInterface(&contents);
  return {pref->GetValue()->GetBool(),
          IsShellEnabled() && browser &&
              tabs::VerticalTabStripStateController::From(browser) &&
              pref->IsUserModifiable()};
}

void SetCustomizationTabLayout(content::WebContents& contents, bool vertical) {
  if (!GetCustomizationTabLayout(contents).enabled) {
    return;
  }
  tabs::VerticalTabStripStateController::From(
      webui::GetBrowserWindowInterface(&contents))
      ->SetVerticalTabsEnabled(vertical);
}
}  // namespace yee
