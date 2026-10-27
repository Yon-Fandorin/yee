// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_HELPER_H_
#define CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_HELPER_H_

#include <map>
#include <string>

#include "base/functional/callback.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace yee::projects {

// 세션의 UUID와 현재 WebContents의 관찰만 담당한다. Clone에는 복제하지 않는다.
class ProjectTabHelper : public content::WebContentsObserver,
                         public content::WebContentsUserData<ProjectTabHelper> {
 public:
  static constexpr char kSessionKey[] = "yee.project_page_id";
  ~ProjectTabHelper() override;
  static std::string PageId(content::WebContents* contents);
  static void Restore(content::WebContents* contents,
                      const std::map<std::string, std::string>& extra_data);

  const std::string& page_id() const { return page_id_; }
  void Bind(std::string page_id, base::RepeatingClosure metadata_changed);

 private:
  friend class content::WebContentsUserData<ProjectTabHelper>;
  explicit ProjectTabHelper(content::WebContents* contents);
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void TitleWasSet(content::NavigationEntry* entry) override;

  std::string page_id_;
  base::RepeatingClosure metadata_changed_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_YEE_PROJECTS_PROJECT_TAB_HELPER_H_
