// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_projects/project_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/yee_projects/project_service.h"
#include "components/pref_registry/pref_registry_syncable.h"

namespace yee::projects {

ProjectServiceFactory::ProjectServiceFactory()
    : ProfileKeyedServiceFactory(
          "YeeProjects",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {}

ProjectServiceFactory::~ProjectServiceFactory() = default;

ProjectServiceFactory* ProjectServiceFactory::GetInstance() {
  static base::NoDestructor<ProjectServiceFactory> instance;
  return instance.get();
}

ProjectService* ProjectServiceFactory::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      profile->IsGuestSession()) {
    return nullptr;
  }
  return static_cast<ProjectService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

std::unique_ptr<KeyedService>
ProjectServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<ProjectService>(
      Profile::FromBrowserContext(context)->GetPrefs());
}

void ProjectServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  ProjectService::RegisterProfilePrefs(registry);
}

}  // namespace yee::projects
