// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_WEBUI_WEBUI_RESOURCES_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_WEBUI_WEBUI_RESOURCES_H_

#include <string>
#include <string_view>
#include <vector>

namespace content {
class WebUI;
class WebUIDataSource;
}  // namespace content

namespace yee {
// Serves the shared frontend and translations at the page's own origin.
content::WebUIDataSource* CreatePageDataSource(
    content::WebUI* web_ui,
    std::string_view host,
    int title_id,
    std::vector<std::string> document_paths = {});
}  // namespace yee
#endif
