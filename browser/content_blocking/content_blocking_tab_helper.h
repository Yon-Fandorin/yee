// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_TAB_HELPER_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_TAB_HELPER_H_

#include <cstddef>

#include "base/callback_list.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class Page;
class WebContents;
}  // namespace content

namespace yee::content_blocking {

class ContentBlockingTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<ContentBlockingTabHelper> {
 public:
  ContentBlockingTabHelper(const ContentBlockingTabHelper&) = delete;
  ContentBlockingTabHelper& operator=(const ContentBlockingTabHelper&) = delete;
  ~ContentBlockingTabHelper() override;

  size_t blocked_count() const { return blocked_count_; }
  base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure callback);
  void RecordBlockedRequest();

 private:
  friend class content::WebContentsUserData<ContentBlockingTabHelper>;

  explicit ContentBlockingTabHelper(content::WebContents* web_contents);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  size_t blocked_count_ = 0;
  base::RepeatingClosureList changed_callbacks_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace yee::content_blocking

#endif  // CHROME_BROWSER_YEE_CONTENT_BLOCKING_CONTENT_BLOCKING_TAB_HELPER_H_
