// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BOOKMARKS_BOOKMARKS_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BOOKMARKS_BOOKMARKS_UI_H_

#include "chrome/browser/ui/webui/bookmarks/bookmarks_ui.h"
#include "content/public/browser/webui_config.h"

namespace yee {
// Chromium owns bookmark storage, extension APIs, and policy notifications.
class BookmarksUI : public ::BookmarksUI {
 public:
  explicit BookmarksUI(content::WebUI* web_ui);
  ~BookmarksUI() override;
};

class BookmarksUIConfig : public content::WebUIConfig {
 public:
  BookmarksUIConfig();
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};
}  // namespace yee
#endif
