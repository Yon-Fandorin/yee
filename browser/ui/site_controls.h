// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_

#include <cstddef>
#include <optional>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
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

// Yee 사이트 보호 팝업을 여는 주소창 버튼이다.
// LocationBarView가 선택한 활성 WebContents의 helper만 관찰하며 탭 상태는 소유하지 않는다.
class SiteControlsButton : public views::ImageButton,
                           public content::WebContentsObserver {
  METADATA_HEADER(SiteControlsButton, views::ImageButton)

 public:
  explicit SiteControlsButton(Browser* browser);
  SiteControlsButton(const SiteControlsButton&) = delete;
  SiteControlsButton& operator=(const SiteControlsButton&) = delete;
  ~SiteControlsButton() override;

  void Update(content::WebContents* contents, bool hide_for_omnibox_input);
  void SetSurfacePresentation(
      std::optional<BrowserSurfacePresentation> presentation);

 private:
  void OnPressed(const ui::Event& event);
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnBlockingStateChanged();
  void RefreshVisualState();
  void StateChanged(ButtonState old_state) override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;
  void WebContentsDestroyed() override;

  const raw_ptr<Browser> browser_;
  raw_ptr<views::Label> badge_ = nullptr;
  base::CallbackListSubscription helper_subscription_;
  base::CallbackListSubscription service_subscription_;
  std::optional<BrowserSurfacePresentation> presentation_;
  size_t blocked_count_ = 0;
  bool bubble_was_open_on_mouse_press_ = false;
};

// Yee 보호 팝업을 연다. 사이트 정보 아이콘은 Chromium PageInfo를 사용한다.
bool ShowSiteControlsBubble(views::View* anchor,
                            Browser* browser,
                            content::WebContents* contents);
void CloseSiteControlsBubble();
bool IsSiteControlsBubbleShowing();

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_SITE_CONTROLS_H_
