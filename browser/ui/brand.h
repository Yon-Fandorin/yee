// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BRAND_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BRAND_H_

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "base/version_info/version_info.h"

namespace yee::branding {

// Chromium's generated version metadata reads the configured BRANDING input.
// Product UI must use this accessor instead of embedding the working name.
inline std::u16string ProductName() {
  return base::UTF8ToUTF16(version_info::GetProductName());
}

}  // namespace yee::branding

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BRAND_H_
