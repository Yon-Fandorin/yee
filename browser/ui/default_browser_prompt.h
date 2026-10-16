// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_DEFAULT_BROWSER_PROMPT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_DEFAULT_BROWSER_PROMPT_H_

#include <string>

#include "ui/base/models/image_model.h"

namespace yee::branding {

ui::ImageModel DefaultBrowserPromptIcon(bool dark_mode);
std::u16string DefaultBrowserPromptMessage(bool can_pin_to_taskbar);

}  // namespace yee::branding

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_DEFAULT_BROWSER_PROMPT_H_
