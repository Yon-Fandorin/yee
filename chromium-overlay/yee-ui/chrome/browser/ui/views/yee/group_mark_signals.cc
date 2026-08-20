// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/group_mark_signals.h"

#include <string>

#include "base/notreached.h"
#include "net/base/url_util.h"

namespace yee {

TabAlertKind ClassifyTabAlert(tabs::TabAlert alert) {
  switch (alert) {
    case tabs::TabAlert::kAudioPlaying:
    case tabs::TabAlert::kPipPlaying:
    case tabs::TabAlert::kMediaRecording:
    case tabs::TabAlert::kAudioRecording:
    case tabs::TabAlert::kVideoRecording:
    case tabs::TabAlert::kTabCapturing:
    case tabs::TabAlert::kDesktopCapturing:
      return TabAlertKind::kBrowserMedia;
    case tabs::TabAlert::kActorWaitingOnUser:
      return TabAlertKind::kAgentWaiting;
    case tabs::TabAlert::kActorAccessing:
    case tabs::TabAlert::kGlicAccessing:
    case tabs::TabAlert::kGlicSharing:
      return TabAlertKind::kAgentAccessing;
    case tabs::TabAlert::kAudioMuting:
    case tabs::TabAlert::kBluetoothConnected:
    case tabs::TabAlert::kBluetoothScanActive:
    case tabs::TabAlert::kUsbConnected:
    case tabs::TabAlert::kHidConnected:
    case tabs::TabAlert::kSerialConnected:
    case tabs::TabAlert::kVrPresentingInHeadset:
      return TabAlertKind::kIgnored;
  }
  NOTREACHED();
}

void ApplyFileDemoSignals(const GURL& url, GroupMarkSignals& signals) {
  if (!url.SchemeIsFile()) {
    return;
  }
  std::string signal;
  if (!net::GetValueForKeyInQuery(url, "yee-signal", &signal)) {
    return;
  }
  if (signal.find("audio") != std::string::npos) {
    signals.audio = true;
  }
  if (signal.find("input") != std::string::npos) {
    signals.needs_input = true;
  }
  if (signal.find("unread") != std::string::npos) {
    signals.unread = true;
  }
}

}  // namespace yee
