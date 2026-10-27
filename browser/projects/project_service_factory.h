// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_FACTORY_H_
#define CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace yee::projects {
class ProjectService;

class ProjectServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ProjectService* GetForProfile(Profile* profile);
  static ProjectServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<ProjectServiceFactory>;
  ProjectServiceFactory();
  ~ProjectServiceFactory() override;
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

}  // namespace yee::projects

#endif  // CHROME_BROWSER_YEE_PROJECTS_PROJECT_SERVICE_FACTORY_H_
