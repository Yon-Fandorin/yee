// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/yee_branding/internal_urls.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/yee_branding/internal_url_scheme.h"

namespace yee::branding {
namespace {
constexpr char kWebUIScheme[] = "chrome";
constexpr std::u16string_view kWebUIPrefix = u"chrome://";
}  // namespace

const char* InternalURLScheme() {
  return kInternalURLScheme;
}

bool IsInternalURLScheme(std::string_view scheme) {
  return base::EqualsCaseInsensitiveASCII(scheme, kInternalURLScheme);
}

GURL CanonicalInternalURL(const GURL& url) {
  if (!url.is_valid() || !IsInternalURLScheme(url.scheme())) {
    return url;
  }
  GURL::Replacements replacements;
  replacements.SetSchemeStr(kWebUIScheme);
  if (url.host() == "settings") {
    replacements.SetHostStr(kProductSettingsHost);
  }
  return url.ReplaceComponents(replacements);
}

GURL DisplayInternalURL(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIs(kWebUIScheme) ||
      url.host() == "settings") {
    return url;
  }
  GURL::Replacements replacements;
  replacements.SetSchemeStr(kInternalURLScheme);
  if (url.host() == kProductSettingsHost) {
    replacements.SetHostStr("settings");
  }
  return url.ReplaceComponents(replacements);
}

GURL SettingsURL() {
  return GURL(std::string(kWebUIScheme) + "://" + kProductSettingsHost + "/");
}

std::u16string DisplayInternalURLText(const GURL& url,
                                      std::u16string formatted_url) {
  if (!url.is_valid() || !url.SchemeIs(kWebUIScheme) ||
      !base::StartsWith(formatted_url, kWebUIPrefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return formatted_url;
  }
  const auto displayed = DisplayInternalURL(url);
  if (displayed == url) {
    return formatted_url;
  }
  const auto original_host = base::ASCIIToUTF16(url.host());
  const auto host_start = kWebUIPrefix.size();
  if (formatted_url.compare(host_start, original_host.size(), original_host) ==
      0) {
    formatted_url.replace(host_start, original_host.size(),
                          base::ASCIIToUTF16(displayed.host()));
  }
  formatted_url.replace(0, kWebUIPrefix.size(),
                        base::ASCIIToUTF16(InternalURLScheme()) + u"://");
  return formatted_url;
}

}  // namespace yee::branding
