// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/webui/theme_handler.h"

#include <string>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/ui/views/yee/theme_tint.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"

namespace yee {
namespace {
class ThemeHandler : public content::WebUIMessageHandler,
                     public content::WebContentsObserver {
 public:
  void RegisterMessages() override {
    Observe(web_ui()->GetWebContents());
    web_ui()->RegisterMessageCallback(
        "getYeeThemeAccent",
        base::BindRepeating(&ThemeHandler::GetAccent, base::Unretained(this)));
  }

 private:
  base::DictValue Colors() const {
    const auto accent = ResolveThemeAccent(*web_contents());
    const auto hex = [](SkColor color) {
      return base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                                SkColorGetG(color), SkColorGetB(color));
    };
    base::DictValue colors;
    colors.Set("accent", accent ? hex(*accent) : std::string());
    colors.Set("container", accent ? hex(web_contents()->GetColorProvider().GetColor(
                                        ui::kColorSysPrimaryContainer))
                                   : std::string());
    return colors;
  }

  void GetAccent(const base::ListValue& args) {
    CHECK_EQ(args.size(), 1u);
    AllowJavascript();
    ResolveJavascriptCallback(args[0], Colors());
  }

  void OnColorProviderChanged() override {
    if (IsJavascriptAllowed()) {
      FireWebUIListener("yee-theme-accent-changed", Colors());
    }
  }
};
}  // namespace

std::unique_ptr<content::WebUIMessageHandler> CreateThemeHandler() {
  return std::make_unique<ThemeHandler>();
}
}  // namespace yee
