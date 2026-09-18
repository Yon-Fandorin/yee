// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_tab_activity.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

TEST(AgentTabActivityTest, KeepsAgentStateOutOfBrowserMediaSignals) {
  EXPECT_EQ(AgentTabActivity::kNone, ResolveAgentTabActivity(std::nullopt));
  EXPECT_EQ(AgentTabActivity::kNone,
            ResolveAgentTabActivity(tabs::TabAlert::kAudioPlaying));
  EXPECT_EQ(AgentTabActivity::kWorking,
            ResolveAgentTabActivity(tabs::TabAlert::kActorAccessing));
  EXPECT_EQ(AgentTabActivity::kNeedsInput,
            ResolveAgentTabActivity(tabs::TabAlert::kActorWaitingOnUser));
  EXPECT_EQ(AgentTabActivity::kNone,
            ResolveAgentTabActivity(tabs::TabAlert::kGlicSharing));
  EXPECT_EQ(AgentTabActivity::kNone,
            ResolveAgentTabActivity(tabs::TabAlert::kDesktopCapturing));
}

}  // namespace
}  // namespace yee
