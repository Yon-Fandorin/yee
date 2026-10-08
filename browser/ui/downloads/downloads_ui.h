// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_DOWNLOADS_DOWNLOADS_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_DOWNLOADS_DOWNLOADS_UI_H_

#include "chrome/browser/ui/webui/downloads/downloads_ui.h"
#include "content/public/browser/webui_config.h"

namespace yee {
// Retain DownloadsUI's controller type so its existing Mojo binder and native
// download handler remain responsible for file actions and security checks.
class DownloadsUI : public ::DownloadsUI {
 public:
  explicit DownloadsUI(content::WebUI* web_ui);
  ~DownloadsUI() override;
};

class DownloadsUIConfig : public content::DefaultWebUIConfig<DownloadsUI> {
 public:
  DownloadsUIConfig();
};
}  // namespace yee
#endif
