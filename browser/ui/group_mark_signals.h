// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_GROUP_MARK_SIGNALS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_GROUP_MARK_SIGNALS_H_

#include "components/tabs/public/tab_alert.h"
#include "url/gurl.h"

namespace yee {

// Browser signals painted on the group color mark. Agent waiting/accessing
// are classified separately and must not be copied here.
struct GroupMarkSignals {
  bool audio = false;
  bool needs_input = false;
  bool unread = false;
};

enum class TabAlertKind {
  kIgnored = 0,
  kBrowserMedia,
  kAgentWaiting,
  kAgentAccessing,
};

TabAlertKind ClassifyTabAlert(tabs::TabAlert alert);

// Local file fixtures may request browser mark states that Chromium tab
// alerts cannot raise, via `?yee-signal=audio,input,unread`.
void ApplyFileDemoSignals(const GURL& url, GroupMarkSignals& signals);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_GROUP_MARK_SIGNALS_H_
