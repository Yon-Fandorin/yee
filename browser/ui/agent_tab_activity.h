// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_TAB_ACTIVITY_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_TAB_ACTIVITY_H_

#include <optional>

#include "components/tabs/public/tab_alert.h"

namespace yee {

// Presentation state for a Context Tab. This is deliberately separate from an
// Agent Task and from a user-created tab group: a task may touch multiple tabs
// and a tab may be used by multiple tasks over its lifetime.
enum class AgentTabActivity {
  kNone,
  kWorking,
  kNeedsInput,
};

AgentTabActivity ResolveAgentTabActivity(std::optional<tabs::TabAlert> alert);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_AGENT_TAB_ACTIVITY_H_
