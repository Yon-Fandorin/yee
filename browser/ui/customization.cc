// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/customization.h"

#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
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
}
}  // namespace yee
