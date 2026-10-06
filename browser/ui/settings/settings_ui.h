// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SETTINGS_SETTINGS_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SETTINGS_SETTINGS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace yee {
class SettingsUI : public content::WebUIController {
 public:
  explicit SettingsUI(content::WebUI* web_ui);
  ~SettingsUI() override;
};

class SettingsUIConfig : public content::DefaultWebUIConfig<SettingsUI> {
 public:
  SettingsUIConfig();
};
}  // namespace yee
#endif
