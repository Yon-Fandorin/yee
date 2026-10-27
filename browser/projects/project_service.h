// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_H_
#define CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;

namespace yee::projects {

struct Project {
  std::string id;
  std::u16string name;
  std::u16string description;
  std::vector<std::string> page_ids;
};

struct ProjectPage {
  std::string id;
  std::string project_id;
  GURL url;
  std::u16string title;
};

// 프로필의 프로젝트와 페이지 참조만 저장한다. 실제 탭의 수명은 소유하지 않는다.
class ProjectService : public KeyedService {
 public:
  static constexpr char kPrefName[] = "yee.projects.state";
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  explicit ProjectService(PrefService* prefs);
  ~ProjectService() override;

  bool writable() const { return writable_; }
  const std::vector<Project>& projects() const { return projects_; }
  const Project* FindProject(const std::string& id) const;
  const ProjectPage* FindPage(const std::string& id) const;
  base::CallbackListSubscription Subscribe(base::RepeatingClosure callback);

  std::optional<std::string> CreateProject(
      const std::u16string& name,
      const std::u16string& description = {});
  bool RenameProject(const std::string& id, const std::u16string& name);
  bool UpdateProject(const std::string& id,
                     const std::u16string& name,
                     const std::u16string& description);
  static bool CanStorePageURL(const GURL& url);
  bool DeleteProject(const std::string& id);

  // 기존 UUID는 같은 프로젝트에서 재사용한다. 다른 프로젝트로의 이동에는
  // 사용자가 확인한 allow_move와 이전 소유자 expected_project_id가 필요하다.
  std::optional<std::string> AttachPage(
      const std::string& project_id,
      const GURL& url,
      const std::u16string& title,
      const std::string& existing_id = {},
      const std::string& expected_project_id = {},
      bool allow_move = false);
  bool UpdatePage(const std::string& id,
                  const GURL& url,
                  const std::u16string& title);
  bool RemovePage(const std::string& id);

 private:
  void Load();
  void Save();

  const raw_ptr<PrefService> prefs_;
  bool writable_ = true;
  std::vector<Project> projects_;
  std::map<std::string, ProjectPage> pages_;
  base::RepeatingClosureList changed_;
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_H_
