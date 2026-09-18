// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_tab_activity.h"

namespace yee {

AgentTabActivity ResolveAgentTabActivity(std::optional<tabs::TabAlert> alert) {
  if (!alert.has_value()) {
    return AgentTabActivity::kNone;
  }
  switch (*alert) {
    case tabs::TabAlert::kActorWaitingOnUser:
      return AgentTabActivity::kNeedsInput;
    case tabs::TabAlert::kActorAccessing:
    case tabs::TabAlert::kGlicAccessing:
      return AgentTabActivity::kWorking;
    case tabs::TabAlert::kGlicSharing:
    case tabs::TabAlert::kAudioPlaying:
    case tabs::TabAlert::kAudioMuting:
    case tabs::TabAlert::kMediaRecording:
    case tabs::TabAlert::kTabCapturing:
    case tabs::TabAlert::kDesktopCapturing:
    case tabs::TabAlert::kPipPlaying:
    case tabs::TabAlert::kVideoRecording:
    case tabs::TabAlert::kAudioRecording:
    case tabs::TabAlert::kBluetoothConnected:
    case tabs::TabAlert::kBluetoothScanActive:
    case tabs::TabAlert::kUsbConnected:
    case tabs::TabAlert::kHidConnected:
    case tabs::TabAlert::kSerialConnected:
    case tabs::TabAlert::kVrPresentingInHeadset:
      return AgentTabActivity::kNone;
  }
  return AgentTabActivity::kNone;
}

}  // namespace yee
