// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_WEBUI_THEME_HANDLER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_WEBUI_THEME_HANDLER_H_

#include <memory>

namespace content {
class WebUIMessageHandler;
}
namespace yee {
std::unique_ptr<content::WebUIMessageHandler> CreateThemeHandler();
}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_WEBUI_THEME_HANDLER_H_
