// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_

#include <memory>
#include <optional>

#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "ui/color/color_provider_source.h"

namespace content {
class WebContents;
}

namespace yee {
// 신뢰된 WebUI 팝업 문서가 준비된 뒤, 결과를 보이기 전에 Views와 같은
// 제안 목록 여백과 곡률을 적용한다. 아직 준비되지 않은 문서는 수정하지 않으며
// 호출자는 DOMContentLoaded에서 준비된 문서에 다시 적용한다.
void ApplyOmniboxPopupStyle(content::WebContents* contents);
void SetEmbeddedOmniboxNativeFocus(content::WebContents* contents,
                                   bool focused);

// BrowserView에 포함된 WebUI는 별도 팝업 위젯과 같은 페이지 팔레트를 쓴다.
class EmbeddedOmniboxPopupColors final : public ui::ColorProviderSource {
 public:
  EmbeddedOmniboxPopupColors();
  ~EmbeddedOmniboxPopupColors() override;

  void SetColorProviderKey(ui::ColorProviderKey key);
  void SetPresentation(const BrowserSurfacePresentation* presentation);

  const ui::ColorProvider* GetColorProvider() const override;
  ui::RendererColorMap GetRendererColorMap(
      ui::ColorProviderKey::ColorMode color_mode,
      ui::ColorProviderKey::ForcedColors forced_colors) const override;

 private:
  ui::ColorProviderKey GetColorProviderKey() const override;
  void Rebuild();

  std::optional<ui::ColorProviderKey> key_;
  std::optional<BrowserSurfacePresentation> presentation_;
  std::unique_ptr<ui::ColorProvider> provider_;
};
}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_OMNIBOX_POPUP_STYLE_H_
