// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_HISTORY_HISTORY_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_HISTORY_HISTORY_UI_H_

#include "chrome/browser/ui/webui/history/history_ui.h"
#include "content/public/browser/webui_config.h"

namespace yee {
// Keep HistoryUI's controller type, Mojo handlers, and navigation messages.
class HistoryUI : public ::HistoryUI {
 public:
  explicit HistoryUI(content::WebUI* web_ui);
  ~HistoryUI() override;
};

class HistoryUIConfig : public content::WebUIConfig {
 public:
  HistoryUIConfig();
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};
}  // namespace yee
#endif
