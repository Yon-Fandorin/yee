// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_

#include "base/functional/callback.h"

namespace yee::projects {

// 실제 Cocoa 메뉴의 열림을 확인한 뒤 tracking loop 안에서 닫는다.
bool RunWithNativeContextMenuAutoCloseForTesting(base::OnceClosure show_menu);

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_
