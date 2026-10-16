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
inline constexpr char kProductHistoryHost[] = "yee-history";
inline constexpr char kProductBookmarksHost[] = "yee-bookmarks";
inline constexpr char kProductExtensionsHost[] = "yee-extensions";

const char* InternalURLScheme();
bool IsInternalURLScheme(std::string_view scheme);

// 탐색·저장 URL은 Chromium WebUI scheme을 사용한다. 제품 주소 표시는 origin과
// binding을 바꾸지 않고 현재 브랜드를 따른다. 원래 내부 화면은 chrome://를 유지한다.
GURL CanonicalInternalURL(const GURL& url);
GURL DisplayInternalURL(const GURL& url);
GURL SettingsURL();
GURL DownloadsURL();
GURL HistoryURL();
GURL BookmarksURL();
GURL ExtensionsURL();
std::u16string DisplayInternalURLText(const GURL& url,
                                      std::u16string formatted_url);

}  // namespace yee::branding

#endif  // COMPONENTS_YEE_BRANDING_INTERNAL_URLS_H_
