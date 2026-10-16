// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/product_logo.h"

#include "components/grit/components_scaled_resources.h"

namespace yee::branding {

ui::ImageModel ProductLogoIcon(bool dark_mode) {
  return ui::ImageModel::FromResourceId(dark_mode ? IDR_PRODUCT_LOGO_WHITE
                                                  : IDR_PRODUCT_LOGO);
}

}  // namespace yee::branding
