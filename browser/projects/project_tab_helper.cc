// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_projects/project_tab_helper.h"

#include <utility>

#include "base/uuid.h"
#include "content/public/browser/navigation_handle.h"

namespace yee::projects {

WEB_CONTENTS_USER_DATA_KEY_IMPL(ProjectTabHelper);

ProjectTabHelper::ProjectTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<ProjectTabHelper>(*contents) {}

ProjectTabHelper::~ProjectTabHelper() = default;

std::string ProjectTabHelper::PageId(content::WebContents* contents) {
  auto* helper = contents ? FromWebContents(contents) : nullptr;
  return helper ? helper->page_id() : std::string();
}

void ProjectTabHelper::Restore(
    content::WebContents* contents,
    const std::map<std::string, std::string>& extra_data) {
  auto it = extra_data.find(kSessionKey);
  if (it == extra_data.end() ||
      !base::Uuid::ParseLowercase(it->second).is_valid()) {
    return;
  }
  CreateForWebContents(contents);
  FromWebContents(contents)->Bind(it->second, {});
}

void ProjectTabHelper::Bind(std::string page_id,
                            base::RepeatingClosure metadata_changed) {
  page_id_ = std::move(page_id);
  metadata_changed_ = std::move(metadata_changed);
}

void ProjectTabHelper::DidFinishNavigation(content::NavigationHandle* handle) {
  if (handle->IsInPrimaryMainFrame() && handle->HasCommitted() &&
      !handle->IsErrorPage() && metadata_changed_) {
    metadata_changed_.Run();
  }
}

void ProjectTabHelper::TitleWasSet(content::NavigationEntry* entry) {
  if (metadata_changed_) {
    metadata_changed_.Run();
  }
}

}  // namespace yee::projects
