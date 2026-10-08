// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SETTINGS_CONTENT_BLOCKING_SETTINGS_HANDLER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SETTINGS_CONTENT_BLOCKING_SETTINGS_HANDLER_H_
#include <memory>
namespace content {
class WebUIMessageHandler;
}
namespace yee {
std::unique_ptr<content::WebUIMessageHandler>
CreateContentBlockingSettingsHandler();
}
#endif
