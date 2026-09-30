// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_FACTORY_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace yee::content_blocking {

class ContentBlockingService;

class ContentBlockingServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ContentBlockingService* GetForProfile(Profile* profile);
  static ContentBlockingServiceFactory* GetInstance();

  ContentBlockingServiceFactory(const ContentBlockingServiceFactory&) = delete;
  ContentBlockingServiceFactory& operator=(
      const ContentBlockingServiceFactory&) = delete;

 private:
  friend base::NoDestructor<ContentBlockingServiceFactory>;

  ContentBlockingServiceFactory();
  ~ContentBlockingServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

}  // namespace yee::content_blocking

#endif  // CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_SERVICE_FACTORY_H_
