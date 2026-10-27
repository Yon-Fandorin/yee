// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_PAGE_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_PAGE_BUTTON_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/ui/views/yee/projects/project_row_view.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/context_menu_controller.h"
#include "url/gurl.h"

class Profile;
namespace favicon {
class FaviconDriver;
}

namespace yee::projects {

struct ProjectPageItem {
  std::string id;
  std::u16string title;
  std::u16string host;
  bool live = false;
  GURL url;
  base::WeakPtr<tabs::TabInterface> tab;
  bool active = false;
};

using PageMenuCallback = base::RepeatingCallback<
    void(views::View*, const gfx::Point&, ui::mojom::MenuSourceType)>;

// UUID 참조의 조작만 제공한다. 실제 닫기·컨텍스트 명령은 native 탭에 위임한다.
// 파비콘은 열린 contents 또는 프로필의 기존 로컬 favicon 저장소에서 받는다.
class ProjectPageButton : public ProjectRowButton,
                          public content::WebContentsObserver,
                          public favicon::FaviconDriverObserver,
                          public views::ContextMenuController {
  METADATA_HEADER(ProjectPageButton, ProjectRowButton)

 public:
  ProjectPageButton(Profile* profile,
                    const ProjectPageItem& page,
                    base::RepeatingClosure open,
                    base::RepeatingClosure close,
                    PageMenuCallback menu);
  ~ProjectPageButton() override;

 private:
  void OnFaviconUpdated(favicon::FaviconDriver* driver,
                        NotificationIconType type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override;
  void WebContentsDestroyed() override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType type) override;
  void SetFavicon(const gfx::Image& image);

  base::RepeatingClosure close_;
  PageMenuCallback menu_;
  bool live_icon_received_ = false;
  base::ScopedObservation<favicon::FaviconDriver,
                          favicon::FaviconDriverObserver>
      favicon_observation_{this};
  base::CancelableTaskTracker favicon_tasks_;
  base::WeakPtrFactory<ProjectPageButton> weak_factory_{this};
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PROJECTS_PROJECT_PAGE_BUTTON_H_
