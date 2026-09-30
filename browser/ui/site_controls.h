// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_

#include <cstddef>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/image_button.h"

class Browser;

namespace content {
class WebContents;
}

namespace views {
class Label;
class View;
}  // namespace views

namespace yee {

enum class SiteControlsSection {
  kProtection,
  kPageInfo,
};

// Location-bar action for Yee's combined protection and page-information
// panel. The button observes only the active WebContents helper selected by
// LocationBarView; it does not own browser tab state.
class SiteControlsButton : public views::ImageButton,
                           public content::WebContentsObserver {
  METADATA_HEADER(SiteControlsButton, views::ImageButton)

 public:
  explicit SiteControlsButton(Browser* browser);
  SiteControlsButton(const SiteControlsButton&) = delete;
  SiteControlsButton& operator=(const SiteControlsButton&) = delete;
  ~SiteControlsButton() override;

  void Update(content::WebContents* contents, bool hide_for_omnibox_input);
  void SetIconColor(std::optional<SkColor> color);

 private:
  void OnPressed();
  void OnBlockingStateChanged();
  void RefreshVisualState();
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void WebContentsDestroyed() override;

  const raw_ptr<Browser> browser_;
  raw_ptr<views::Label> badge_ = nullptr;
  base::CallbackListSubscription helper_subscription_;
  base::CallbackListSubscription service_subscription_;
  std::optional<SkColor> icon_color_;
  size_t blocked_count_ = 0;
};

// Opens the same Yee-owned panel from either the protection action or the site
// identity icon. Chromium's PageInfo implementation remains untouched.
bool ShowSiteControlsBubble(views::View* anchor,
                            Browser* browser,
                            content::WebContents* contents,
                            SiteControlsSection initial_section);
bool IsSiteControlsBubbleShowing();

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_
