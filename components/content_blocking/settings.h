// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#ifndef COMPONENTS_YEE_CONTENT_BLOCKING_SETTINGS_H_
#define COMPONENTS_YEE_CONTENT_BLOCKING_SETTINGS_H_
#include "base/feature_list.h"
#include "url/gurl.h"

namespace base {
class CommandLine;
}
namespace yee::content_blocking {
BASE_DECLARE_FEATURE(kYeeContentBlocking);
bool EnabledForSite(const GURL& site);
// Reserved .test rules are opt-in and never part of normal page protection.
bool TestRulesEnabled();
void CopySettingsToChild(base::CommandLine* child);
}  // namespace yee::content_blocking
#endif
