// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_content_blocking/content_blocking_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/yee_content_blocking/content_blocking_service.h"

namespace yee::content_blocking {

ContentBlockingServiceFactory::ContentBlockingServiceFactory()
    : ProfileKeyedServiceFactory(
          "YeeContentBlockingService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {}

ContentBlockingServiceFactory::~ContentBlockingServiceFactory() = default;

// static
ContentBlockingService* ContentBlockingServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  auto* service = static_cast<ContentBlockingService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
  // A different profile may have owned the coordinator when this service was
  // created. Reclaim it on use after that profile has closed.
  if (service && profile->IsRegularProfile() && !profile->IsGuestSession())
    service->StartBaselineListUpdates();
  return service;
}

// static
ContentBlockingServiceFactory* ContentBlockingServiceFactory::GetInstance() {
  static base::NoDestructor<ContentBlockingServiceFactory> instance;
  return instance.get();
}

std::unique_ptr<KeyedService>
ContentBlockingServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  auto service = std::make_unique<ContentBlockingService>(
      profile->GetPrefs(), profile->IsOffTheRecord());
  if (profile->IsRegularProfile() && !profile->IsGuestSession())
    service->StartBaselineListUpdates();
  return service;
}

void ContentBlockingServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  ContentBlockingService::RegisterProfilePrefs(registry);
}

}  // namespace yee::content_blocking
