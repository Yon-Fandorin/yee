// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/ui/views/yee/customization.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/webui/customization/tab_layout.mojom.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/document_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace yee {
namespace {

class CustomizationHandler final
    : public content::DocumentService<mojom::TabLayoutHandler> {
 public:
  CustomizationHandler(content::RenderFrameHost& frame,
                       mojo::PendingReceiver<mojom::TabLayoutHandler> receiver)
      : DocumentService(frame, std::move(receiver)),
        contents_(content::WebContents::FromRenderFrameHost(&frame)) {
    preferences_.Init(
        Profile::FromBrowserContext(contents_->GetBrowserContext())
            ->GetPrefs());
  }
  void Observe(mojo::PendingRemote<mojom::TabLayoutPage> page) override {
    // 같은 문서의 재연결도 이전 observer와 구독을 정리한 뒤 연결한다.
    preferences_.RemoveAll();
    browser_changed_ = {};
    page_.reset();
    page_.Bind(std::move(page));
    page_.set_disconnect_handler(base::BindOnce(
        &CustomizationHandler::ResetAndDeleteThis, base::Unretained(this)));
    preferences_.Add(prefs::kVerticalTabsEnabled,
                     base::BindRepeating(&CustomizationHandler::Publish,
                                         base::Unretained(this)));
    browser_changed_ = webui::RegisterBrowserWindowInterfaceChanged(
        contents_, base::BindRepeating(&CustomizationHandler::Publish,
                                       base::Unretained(this)));
    Publish();
  }
  void SetTabLayout(bool vertical) override {
    SetCustomizationTabLayout(*contents_, vertical);
    Publish();
  }

 private:
  void Publish() {
    if (!page_.is_bound())
      return;
    const auto state = GetCustomizationTabLayout(*contents_);
    page_->SetTabLayout(state.vertical, state.enabled);
  }
  const raw_ptr<content::WebContents> contents_;
  PrefChangeRegistrar preferences_;
  base::CallbackListSubscription browser_changed_;
  mojo::Remote<mojom::TabLayoutPage> page_;
};

}  // namespace

void BindCustomizationTabLayout(
    content::RenderFrameHost& frame,
    mojo::PendingReceiver<mojom::TabLayoutHandler> receiver) {
  new CustomizationHandler(frame, std::move(receiver));
}

bool SupportsToolbarCustomization() {
  return !IsShellEnabled();
}

void ConfigureCustomizationDataSource(content::WebUIDataSource& source) {
  source.AddBoolean("showThemeSnapshot", !IsShellEnabled());
  source.AddBoolean("showToolbarCustomization", SupportsToolbarCustomization());
  source.AddBoolean("yeeCustomization", IsShellEnabled());
  source.AddLocalizedString("yeeAppearanceDescription",
                            IDS_YEE_WEBUI_CUSTOMIZATION_DESCRIPTION);
  source.AddLocalizedString("yeeTabLayout", IDS_YEE_WEBUI_TAB_LAYOUT);
  source.AddLocalizedString("yeeTabLayoutTop", IDS_YEE_WEBUI_TAB_LAYOUT_TOP);
  source.AddLocalizedString("yeeTabLayoutSide", IDS_YEE_WEBUI_TAB_LAYOUT_SIDE);
}

CustomizationTabLayout GetCustomizationTabLayout(
    content::WebContents& contents) {
  const auto* pref = Profile::FromBrowserContext(contents.GetBrowserContext())
                         ->GetPrefs()
                         ->FindPreference(prefs::kVerticalTabsEnabled);
  auto* browser = webui::GetBrowserWindowInterface(&contents);
  return {pref->GetValue()->GetBool(),
          IsShellEnabled() && browser &&
              tabs::VerticalTabStripStateController::From(browser) &&
              pref->IsUserModifiable()};
}

void SetCustomizationTabLayout(content::WebContents& contents, bool vertical) {
  if (!GetCustomizationTabLayout(contents).enabled) {
    return;
  }
  tabs::VerticalTabStripStateController::From(
      webui::GetBrowserWindowInterface(&contents))
      ->SetVerticalTabsEnabled(vertical);
}
}  // namespace yee
