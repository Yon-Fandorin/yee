// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_projects/project_tab_coordinator.h"

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "chrome/browser/yee_projects/project_service_factory.h"
#include "chrome/browser/yee_projects/project_tab_helper.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace yee::projects {
namespace {

class CoordinatorFactory : public ProfileKeyedServiceFactory {
 public:
  static CoordinatorFactory* GetInstance() {
    static base::NoDestructor<CoordinatorFactory> instance;
    return instance.get();
  }

  ProjectTabCoordinator* ForProfile(Profile* profile) {
    return static_cast<ProjectTabCoordinator*>(
        GetServiceForBrowserContext(profile, true));
  }

 private:
  friend class base::NoDestructor<CoordinatorFactory>;
  CoordinatorFactory()
      : ProfileKeyedServiceFactory(
            "YeeProjectTabs",
            ProfileSelections::Builder()
                .WithRegular(ProfileSelection::kOriginalOnly)
                .WithGuest(ProfileSelection::kNone)
                .WithSystem(ProfileSelection::kNone)
                .WithAshInternals(ProfileSelection::kNone)
                .Build()) {
    DependsOn(ProjectServiceFactory::GetInstance());
    DependsOn(SessionServiceFactory::GetInstance());
  }
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override {
    auto* profile = Profile::FromBrowserContext(context);
    return std::make_unique<ProjectTabCoordinator>(
        profile, ProjectServiceFactory::GetForProfile(profile));
  }
};

}  // namespace

ProjectTabCoordinator* ProjectTabCoordinator::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      profile->IsGuestSession()) {
    return nullptr;
  }
  return CoordinatorFactory::GetInstance()->ForProfile(profile);
}

ProjectTabCoordinator::ProjectTabCoordinator(Profile* profile,
                                             ProjectService* service)
    : profile_(profile), service_(service) {
  service_subscription_ = service_->Subscribe(base::BindRepeating(
      &ProjectTabCoordinator::OnProjectChanged, weak_factory_.GetWeakPtr()));
}

ProjectTabCoordinator::~ProjectTabCoordinator() = default;

void ProjectTabCoordinator::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
  service_subscription_ = {};
  bindings_.clear();
  opening_pages_.clear();
  service_ = nullptr;
}

void ProjectTabCoordinator::WatchTab(tabs::TabInterface& tab) {
  PruneClosedTabs();
  if (!service_ || tab.GetProfile() != profile_ || bindings_.contains(&tab)) {
    return;
  }
  Binding binding;
  binding.tab = tab.GetWeakPtr();
  binding.subscriptions.push_back(
      tab.RegisterWillDiscardContents(base::BindRepeating(
          &ProjectTabCoordinator::OnDiscard, weak_factory_.GetWeakPtr())));
  binding.subscriptions.push_back(tab.RegisterWillDetach(base::BindRepeating(
      &ProjectTabCoordinator::OnDetach, weak_factory_.GetWeakPtr())));
  binding.subscriptions.push_back(tab.RegisterDidInsert(base::BindRepeating(
      &ProjectTabCoordinator::OnInsert, weak_factory_.GetWeakPtr())));
  // 같은 창에서만 UUID 충돌을 해제한다. 다른 창의 독립 인스턴스는 유지한다.
  std::string id = ProjectTabHelper::PageId(tab.GetContents());
  if ((service_->writable() && !service_->FindPage(id)) ||
      FindLiveTab(id, tab.GetBrowserWindowInterface(), &tab)) {
    id.clear();
  }
  bindings_.emplace(&tab, std::move(binding));
  ObserveContents(&tab, tab.GetContents(), id);
  changed_.Notify();
}

void ProjectTabCoordinator::PruneClosedTabs() {
  const size_t previous_count = bindings_.size();
  std::erase_if(bindings_, [](const auto& entry) { return !entry.second.tab; });
  if (bindings_.size() != previous_count) {
    changed_.Notify();
  }
}

tabs::TabInterface* ProjectTabCoordinator::FindLiveTab(
    const std::string& page_id,
    const BrowserWindowInterface* window) const {
  return FindLiveTab(page_id, window, nullptr);
}

tabs::TabInterface* ProjectTabCoordinator::FindLiveTab(
    const std::string& page_id,
    const BrowserWindowInterface* window,
    const tabs::TabInterface* ignored) const {
  if (page_id.empty() || !window) {
    return nullptr;
  }
  for (const auto& [key, binding] : bindings_) {
    if (binding.tab && binding.tab.get() != ignored &&
        binding.tab->GetBrowserWindowInterface() == window &&
        ProjectTabHelper::PageId(binding.tab->GetContents()) == page_id) {
      return binding.tab.get();
    }
  }
  return nullptr;
}

std::string ProjectTabCoordinator::PageId(tabs::TabInterface* tab) const {
  return tab ? ProjectTabHelper::PageId(tab->GetContents()) : std::string();
}

bool ProjectTabCoordinator::Attach(tabs::TabInterface& tab,
                                   const std::string& project_id,
                                   const std::string& expected_page_id,
                                   const std::string& expected_project_id,
                                   bool allow_move) {
  if (!service_ || tab.GetProfile() != profile_ ||
      PageId(&tab) != expected_page_id) {
    return false;
  }
  WatchTab(tab);
  auto* contents = tab.GetContents();
  const auto* entry = contents->GetController().GetLastCommittedEntry();
  if (!entry || entry->GetPageType() != content::PAGE_TYPE_NORMAL) {
    return false;
  }
  auto id = service_->AttachPage(project_id, contents->GetLastCommittedURL(),
                                 contents->GetTitle(), expected_page_id,
                                 expected_project_id, allow_move);
  if (!id) {
    return false;
  }
  ObserveContents(&tab, contents, *id);
  SaveSessionIdentity(&tab);
  changed_.Notify();
  return true;
}

bool ProjectTabCoordinator::BindRestoredPage(tabs::TabInterface& tab,
                                             const std::string& page_id) {
  if (!service_ || tab.GetProfile() != profile_ ||
      !service_->FindPage(page_id) ||
      FindLiveTab(page_id, tab.GetBrowserWindowInterface(), &tab)) {
    return false;
  }
  WatchTab(tab);
  ObserveContents(&tab, tab.GetContents(), page_id);
  SaveSessionIdentity(&tab);
  changed_.Notify();
  return true;
}

base::CallbackListSubscription ProjectTabCoordinator::Subscribe(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

base::OnceClosure ProjectTabCoordinator::BeginReopenPage(
    const std::string& page_id,
    const BrowserWindowInterface* window) {
  if (!service_ || !window || window->GetProfile() != profile_ ||
      !window->GetSessionID().is_valid() || !service_->FindPage(page_id) ||
      FindLiveTab(page_id, window)) {
    return {};
  }
  OpeningKey key(window->GetSessionID(), page_id);
  if (!opening_pages_.insert(key).second)
    return {};
  return base::BindOnce(
      [](base::WeakPtr<ProjectTabCoordinator> coordinator, OpeningKey key) {
        if (coordinator)
          coordinator->opening_pages_.erase(key);
      },
      weak_factory_.GetWeakPtr(), std::move(key));
}

void ProjectTabCoordinator::ObserveContents(tabs::TabInterface* tab,
                                            content::WebContents* contents,
                                            const std::string& page_id) {
  ProjectTabHelper::CreateForWebContents(contents);
  ProjectTabHelper::FromWebContents(contents)->Bind(
      page_id, base::BindRepeating(&ProjectTabCoordinator::MetadataChanged,
                                   weak_factory_.GetWeakPtr(), tab));
}

void ProjectTabCoordinator::MetadataChanged(tabs::TabInterface* tab) {
  if (!service_ || !bindings_.contains(tab)) {
    return;
  }
  const auto id = PageId(tab);
  if (id.empty())
    return;
  auto* contents = tab->GetContents();
  auto* entry = contents->GetController().GetLastCommittedEntry();
  if (entry && entry->GetPageType() == content::PAGE_TYPE_NORMAL) {
    service_->UpdatePage(id, contents->GetLastCommittedURL(),
                         contents->GetTitle());
  }
  // 다른 창이 같은 metadata를 저장했어도 이 창의 표시 변경은 전달한다.
  changed_.Notify();
}

void ProjectTabCoordinator::OnDiscard(tabs::TabInterface* tab,
                                      content::WebContents* old_contents,
                                      content::WebContents* new_contents) {
  ObserveContents(tab, new_contents, ProjectTabHelper::PageId(old_contents));
  // 이전 WebContents의 파기는 탭 삭제가 아니다.
  ProjectTabHelper::FromWebContents(old_contents)->Bind({}, {});
}

void ProjectTabCoordinator::OnDetach(tabs::TabInterface* tab,
                                     tabs::TabInterface::DetachReason reason) {
  if (reason == tabs::TabInterface::DetachReason::kDelete) {
    // 닫기 취소 여부는 이 알림보다 나중에 정해진다. 실제 TabInterface의 수명이
    // 끝난 항목만 다음 작업에서 제거한다.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ProjectTabCoordinator::PruneClosedTabs,
                                  weak_factory_.GetWeakPtr()));
  }
}

void ProjectTabCoordinator::OnInsert(tabs::TabInterface* tab) {
  const auto id = PageId(tab);
  if (FindLiveTab(id, tab->GetBrowserWindowInterface(), tab)) {
    // 이동한 창에 같은 항목이 있어도 실제 탭을 닫거나 병합하지 않는다.
    ObserveContents(tab, tab->GetContents(), {});
  }
  SaveSessionIdentity(tab);
  changed_.Notify();
}

void ProjectTabCoordinator::OnProjectChanged() {
  for (const auto& [tab, binding] : bindings_) {
    if (binding.tab && !PageId(tab).empty() &&
        !service_->FindPage(PageId(tab))) {
      ObserveContents(tab, tab->GetContents(), {});
      SaveSessionIdentity(tab);
    }
  }
  changed_.Notify();
}

void ProjectTabCoordinator::SaveSessionIdentity(tabs::TabInterface* tab) {
  auto* helper =
      sessions::SessionTabHelper::FromWebContents(tab->GetContents());
  auto* session = SessionServiceFactory::GetForProfile(profile_);
  if (helper && session) {
    session->AddTabExtraData(helper->window_id(), helper->session_id(),
                             ProjectTabHelper::kSessionKey, PageId(tab));
  }
}

}  // namespace yee::projects
