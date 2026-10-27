// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_COORDINATOR_H_
#define CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_COORDINATOR_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"

class Profile;
class BrowserWindowInterface;

namespace yee::projects {
class ProjectService;

// 프로필 전체의 실제 TabInterface와 영속 UUID를 연결한다. URL로 탭을 추측하지
// 않는다.
class ProjectTabCoordinator : public KeyedService {
 public:
  static ProjectTabCoordinator* GetForProfile(Profile* profile);
  ProjectTabCoordinator(Profile* profile, ProjectService* service);
  ~ProjectTabCoordinator() override;
  void Shutdown() override;

  void WatchTab(tabs::TabInterface& tab);
  void PruneClosedTabs();
  tabs::TabInterface* FindLiveTab(const std::string& page_id,
                                  const BrowserWindowInterface* window) const;
  std::string PageId(tabs::TabInterface* tab) const;
  bool Attach(tabs::TabInterface& tab,
              const std::string& project_id,
              const std::string& expected_page_id,
              const std::string& expected_project_id = {},
              bool allow_move = false);
  bool BindRestoredPage(tabs::TabInterface& tab, const std::string& page_id);
  // 같은 창의 항목 재열기만 합친다. 창마다 독립된 WebContents를 유지한다.
  base::OnceClosure BeginReopenPage(const std::string& page_id,
                                    const BrowserWindowInterface* window);
  base::CallbackListSubscription Subscribe(base::RepeatingClosure callback);

 private:
  struct Binding {
    base::WeakPtr<tabs::TabInterface> tab;
    std::vector<base::CallbackListSubscription> subscriptions;
  };
  using OpeningKey = std::pair<SessionID, std::string>;

  tabs::TabInterface* FindLiveTab(const std::string& page_id,
                                  const BrowserWindowInterface* window,
                                  const tabs::TabInterface* ignored) const;

  void ObserveContents(tabs::TabInterface* tab,
                       content::WebContents* contents,
                       const std::string& page_id);
  void MetadataChanged(tabs::TabInterface* tab);
  void OnDiscard(tabs::TabInterface* tab,
                 content::WebContents* old_contents,
                 content::WebContents* new_contents);
  void OnDetach(tabs::TabInterface* tab,
                tabs::TabInterface::DetachReason reason);
  void OnInsert(tabs::TabInterface* tab);
  void OnProjectChanged();
  void SaveSessionIdentity(tabs::TabInterface* tab);

  const raw_ptr<Profile> profile_;
  raw_ptr<ProjectService> service_;
  std::map<tabs::TabInterface*, Binding> bindings_;
  std::set<OpeningKey> opening_pages_;
  base::RepeatingClosureList changed_;
  base::CallbackListSubscription service_subscription_;
  base::WeakPtrFactory<ProjectTabCoordinator> weak_factory_{this};
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_COORDINATOR_H_
