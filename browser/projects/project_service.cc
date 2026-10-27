// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_projects/project_service.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace yee::projects {
namespace {

std::u16string CleanName(const std::u16string& name) {
  std::u16string result;
  base::TrimWhitespace(name, base::TRIM_ALL, &result);
  return result.size() <= 128 ? result : std::u16string();
}

bool ValidId(const std::string& id) {
  return base::Uuid::ParseLowercase(id).is_valid();
}

std::u16string PageTitle(const std::u16string& title, const GURL& url) {
  return title.empty() ? base::UTF8ToUTF16(url.host()) : title.substr(0, 1024);
}

}  // namespace

void ProjectService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPrefName);
}

ProjectService::ProjectService(PrefService* prefs) : prefs_(prefs) {
  Load();
}

ProjectService::~ProjectService() = default;

const Project* ProjectService::FindProject(const std::string& id) const {
  auto it = std::ranges::find(projects_, id, &Project::id);
  return it == projects_.end() ? nullptr : &*it;
}

const ProjectPage* ProjectService::FindPage(const std::string& id) const {
  auto it = pages_.find(id);
  return it == pages_.end() ? nullptr : &it->second;
}

base::CallbackListSubscription ProjectService::Subscribe(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

std::optional<std::string> ProjectService::CreateProject(
    const std::u16string& name,
    const std::u16string& description) {
  const auto cleaned = CleanName(name);
  if (!writable_ || cleaned.empty() || description.size() > 2048) {
    return std::nullopt;
  }
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  projects_.push_back({id, cleaned, description, {}});
  Save();
  return id;
}

bool ProjectService::RenameProject(const std::string& id,
                                   const std::u16string& name) {
  const auto* project = FindProject(id);
  return project && UpdateProject(id, name, project->description);
}

bool ProjectService::UpdateProject(const std::string& id,
                                   const std::u16string& name,
                                   const std::u16string& description) {
  const auto cleaned = CleanName(name);
  if (!writable_ || cleaned.empty() || description.size() > 2048) {
    return false;
  }
  auto it = std::ranges::find(projects_, id, &Project::id);
  if (it == projects_.end()) {
    return false;
  }
  it->name = cleaned;
  it->description = description;
  Save();
  return true;
}

bool ProjectService::CanStorePageURL(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password();
}

bool ProjectService::DeleteProject(const std::string& id) {
  if (!writable_ || !FindProject(id)) {
    return false;
  }
  std::erase_if(pages_, [&id](const auto& entry) {
    return entry.second.project_id == id;
  });
  std::erase_if(projects_,
                [&id](const Project& project) { return project.id == id; });
  Save();
  return true;
}

std::optional<std::string> ProjectService::AttachPage(
    const std::string& project_id,
    const GURL& url,
    const std::u16string& title,
    const std::string& existing_id,
    const std::string& expected_project_id,
    bool allow_move) {
  if (!writable_ || !FindProject(project_id) || !CanStorePageURL(url)) {
    return std::nullopt;
  }
  auto page = pages_.find(existing_id);
  if (!existing_id.empty() && page == pages_.end()) {
    return std::nullopt;
  }
  if (page != pages_.end()) {
    if (page->second.project_id == project_id) {
      return existing_id;
    }
    if (!allow_move || page->second.project_id != expected_project_id) {
      return std::nullopt;
    }
    auto old = std::ranges::find(projects_, expected_project_id, &Project::id);
    std::erase(old->page_ids, existing_id);
    page->second.project_id = project_id;
  } else {
    const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    page = pages_
               .emplace(id,
                        ProjectPage{id, project_id, url, PageTitle(title, url)})
               .first;
  }
  auto target = std::ranges::find(projects_, project_id, &Project::id);
  target->page_ids.push_back(page->first);
  const std::string result = page->first;
  Save();
  return result;
}

bool ProjectService::UpdatePage(const std::string& id,
                                const GURL& url,
                                const std::u16string& title) {
  auto page = pages_.find(id);
  if (!writable_ || page == pages_.end() || !CanStorePageURL(url)) {
    return false;
  }
  auto cleaned_title = PageTitle(title, url);
  if (page->second.url == url && page->second.title == cleaned_title) {
    return true;
  }
  page->second.url = url;
  page->second.title = std::move(cleaned_title);
  Save();
  return true;
}

bool ProjectService::RemovePage(const std::string& id) {
  if (!writable_ || !FindPage(id)) {
    return false;
  }
  for (auto& project : projects_) {
    std::erase(project.page_ids, id);
  }
  pages_.erase(id);
  Save();
  return true;
}

void ProjectService::Load() {
  const auto& state = prefs_->GetDict(kPrefName);
  if (state.empty()) {
    // 타입이 잘못된 사용자 값도 등록된 기본 {}로 읽힌다. 기본값에만 초기 생성을
    // 허용하며, 명시적인 빈 사전과 타입 불일치는 원본을 덮어쓰지 않는다.
    writable_ = !prefs_->FindPreference(kPrefName)->HasUserSetting();
    return;
  }
  // 알 수 없는 버전이나 손상된 참조는 원본을 보존하고 읽기 전용으로 전환한다.
  writable_ = false;
  if (state.FindInt("schema_version") != 1) {
    return;
  }
  const auto* projects = state.FindList("projects");
  const auto* pages = state.FindDict("pages");
  if (!projects || !pages) {
    return;
  }
  std::vector<Project> loaded_projects;
  std::map<std::string, ProjectPage> loaded_pages;
  std::set<std::string> project_ids;
  std::set<std::string> page_ids;
  for (const auto& value : *projects) {
    const auto* project = value.GetIfDict();
    if (!project) {
      return;
    }
    const auto* id = project->FindString("id");
    const auto* name = project->FindString("name");
    const auto* items = project->FindList("pages");
    if (!id || !ValidId(*id) || !project_ids.insert(*id).second || !name ||
        CleanName(base::UTF8ToUTF16(*name)).empty() || !items) {
      return;
    }
    const auto* description = project->FindString("description");
    if (project->contains("description") && !description) {
      return;
    }
    if (description && base::UTF8ToUTF16(*description).size() > 2048) {
      return;
    }
    Project record{*id,
                   base::UTF8ToUTF16(*name),
                   description ? base::UTF8ToUTF16(*description) : u"",
                   {}};
    for (const auto& item : *items) {
      const auto* page_id = item.GetIfString();
      if (!page_id || !ValidId(*page_id) || !page_ids.insert(*page_id).second) {
        return;
      }
      const auto* page = pages->FindDict(*page_id);
      if (!page) {
        return;
      }
      const auto* owner = page->FindString("project");
      const auto* url = page->FindString("url");
      const auto* title = page->FindString("title");
      if (!owner || *owner != *id || !url || !CanStorePageURL(GURL(*url)) ||
          !title) {
        return;
      }
      record.page_ids.push_back(*page_id);
      loaded_pages.emplace(
          *page_id,
          ProjectPage{*page_id, *id, GURL(*url),
                      PageTitle(base::UTF8ToUTF16(*title), GURL(*url))});
    }
    loaded_projects.push_back(std::move(record));
  }
  if (page_ids.size() != pages->size()) {
    return;
  }
  projects_ = std::move(loaded_projects);
  pages_ = std::move(loaded_pages);
  writable_ = true;
}

void ProjectService::Save() {
  base::DictValue state;
  state.Set("schema_version", 1);
  base::ListValue projects;
  for (const auto& project : projects_) {
    base::DictValue value;
    value.Set("id", project.id);
    value.Set("name", base::UTF16ToUTF8(project.name));
    value.Set("description", base::UTF16ToUTF8(project.description));
    base::ListValue pages;
    for (const auto& id : project.page_ids) {
      pages.Append(id);
    }
    value.Set("pages", std::move(pages));
    projects.Append(std::move(value));
  }
  state.Set("projects", std::move(projects));
  base::DictValue pages;
  for (const auto& [id, page] : pages_) {
    base::DictValue value;
    value.Set("project", page.project_id);
    value.Set("url", page.url.spec());
    value.Set("title", base::UTF16ToUTF8(page.title));
    pages.Set(id, std::move(value));
  }
  state.Set("pages", std::move(pages));
  prefs_->SetDict(kPrefName, std::move(state));
  changed_.Notify();
}

}  // namespace yee::projects
