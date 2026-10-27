// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_PRESENTATION_H_

#include <optional>

#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"

class BrowserView;
namespace views {
class View;
}

namespace yee::projects {

// 허브가 보이는 동안 공통 Header·Fill·Outline에 같은 표면을 제공한다.
std::optional<BrowserSurfacePresentation> GetProjectHubSurfacePresentation(
    const BrowserView* view);

// Native 호스트가 소유한 그룹 멤버 들여쓰기를 프로젝트 행에도 전달한다.
void ConfigureProjectSidebarPresentation(views::View& sidebar,
                                         int group_member_inset);

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_NAVIGATION_PRESENTATION_H_
