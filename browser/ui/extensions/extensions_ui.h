// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_EXTENSIONS_EXTENSIONS_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_EXTENSIONS_EXTENSIONS_UI_H_

#include "chrome/browser/ui/webui/extensions/extensions_ui.h"
#include "content/public/browser/webui_config.h"

namespace yee {
// 확장 상태·관리 정책·native 확인창은 Chromium이 소유한다.
class ExtensionsUI : public extensions::ExtensionsUI {
 public:
  explicit ExtensionsUI(content::WebUI* web_ui);
  ~ExtensionsUI() override;
};

class ExtensionsUIConfig : public content::WebUIConfig {
 public:
  ExtensionsUIConfig();
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};
}  // namespace yee
#endif
