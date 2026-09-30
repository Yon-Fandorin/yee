// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/yee_content_blocking/content_blocking_tab_helper.h"

#include "content/public/browser/web_contents.h"

namespace yee::content_blocking {

ContentBlockingTabHelper::ContentBlockingTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<ContentBlockingTabHelper>(*web_contents) {}

ContentBlockingTabHelper::~ContentBlockingTabHelper() = default;

base::CallbackListSubscription ContentBlockingTabHelper::AddChangedCallback(
    base::RepeatingClosure callback) {
  return changed_callbacks_.Add(std::move(callback));
}

void ContentBlockingTabHelper::RecordBlockedRequest() {
  ++blocked_count_;
  changed_callbacks_.Notify();
}

void ContentBlockingTabHelper::PrimaryPageChanged(content::Page& page) {
  if (blocked_count_ == 0) {
    return;
  }
  blocked_count_ = 0;
  changed_callbacks_.Notify();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ContentBlockingTabHelper);

}  // namespace yee::content_blocking
