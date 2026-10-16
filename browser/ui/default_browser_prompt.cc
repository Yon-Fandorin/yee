// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/default_browser_prompt.h"

#include "chrome/browser/ui/views/yee/brand.h"
#include "chrome/browser/ui/views/yee/product_logo.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "ui/base/l10n/l10n_util.h"

namespace yee::branding {

ui::ImageModel DefaultBrowserPromptIcon(bool dark_mode) {
  return ProductLogoIcon(dark_mode);
}

std::u16string DefaultBrowserPromptMessage(bool can_pin_to_taskbar) {
  return l10n_util::GetStringFUTF16(
      can_pin_to_taskbar ? IDS_YEE_DEFAULT_BROWSER_PIN_INFOBAR_TEXT
                         : IDS_YEE_DEFAULT_BROWSER_INFOBAR_TEXT,
      ProductName());
}

}  // namespace yee::branding
