// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PRODUCT_LOGO_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PRODUCT_LOGO_H_

#include "ui/base/models/image_model.h"

namespace yee::branding {

// Native 제품 배너에 사용하는 투명 배경의 두 색 제품 로고다.
ui::ImageModel ProductLogoIcon(bool dark_mode);

}  // namespace yee::branding

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PRODUCT_LOGO_H_
