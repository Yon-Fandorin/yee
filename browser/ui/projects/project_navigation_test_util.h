// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_

#include <optional>

#include "base/functional/callback.h"

namespace yee::projects {

// 실제 Cocoa 메뉴의 열림을 확인하고 선택한 명령을 실행한 뒤 닫는다.
bool RunWithNativeContextMenuForTesting(
    base::OnceClosure show_menu,
    std::optional<int> command_id = std::nullopt);

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_TEST_UTIL_H_
