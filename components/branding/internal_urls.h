// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_YEE_BRANDING_INTERNAL_URLS_H_
#define COMPONENTS_YEE_BRANDING_INTERNAL_URLS_H_

#include <string>
#include <string_view>

#include "url/gurl.h"

namespace yee::branding {

// Stable implementation hosts: persisted tabs survive brand changes.
inline constexpr char kProductSettingsHost[] = "yee-settings";
inline constexpr char kProductDownloadsHost[] = "yee-downloads";

const char* InternalURLScheme();
bool IsInternalURLScheme(std::string_view scheme);

// Navigation and persisted URLs use Chromium's WebUI scheme. Product-facing
// addresses use the current brand without changing WebUI origins or bindings.
// Original settings and downloads keep their chrome:// addresses.
GURL CanonicalInternalURL(const GURL& url);
GURL DisplayInternalURL(const GURL& url);
GURL SettingsURL();
GURL DownloadsURL();
std::u16string DisplayInternalURLText(const GURL& url,
                                      std::u16string formatted_url);

}  // namespace yee::branding

#endif  // COMPONENTS_YEE_BRANDING_INTERNAL_URLS_H_
