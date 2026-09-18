// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/group_mark_signals.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

TEST(GroupMarkSignalsTest, SeparatesBrowserMediaFromAgentState) {
  for (tabs::TabAlert alert : {
           tabs::TabAlert::kAudioPlaying,
           tabs::TabAlert::kPipPlaying,
           tabs::TabAlert::kMediaRecording,
           tabs::TabAlert::kAudioRecording,
           tabs::TabAlert::kVideoRecording,
           tabs::TabAlert::kTabCapturing,
           tabs::TabAlert::kDesktopCapturing,
       }) {
    EXPECT_EQ(ClassifyTabAlert(alert), TabAlertKind::kBrowserMedia);
  }

  EXPECT_EQ(ClassifyTabAlert(tabs::TabAlert::kActorWaitingOnUser),
            TabAlertKind::kAgentWaiting);
  for (tabs::TabAlert alert : {
           tabs::TabAlert::kActorAccessing,
           tabs::TabAlert::kGlicAccessing,
           tabs::TabAlert::kGlicSharing,
       }) {
    EXPECT_EQ(ClassifyTabAlert(alert), TabAlertKind::kAgentAccessing);
  }

  for (tabs::TabAlert alert : {
           tabs::TabAlert::kAudioMuting,
           tabs::TabAlert::kBluetoothConnected,
           tabs::TabAlert::kBluetoothScanActive,
           tabs::TabAlert::kUsbConnected,
           tabs::TabAlert::kHidConnected,
           tabs::TabAlert::kSerialConnected,
           tabs::TabAlert::kVrPresentingInHeadset,
       }) {
    EXPECT_EQ(ClassifyTabAlert(alert), TabAlertKind::kIgnored);
  }
}

TEST(GroupMarkSignalsTest, FileFixtureCanAddEveryBrowserDemoSignal) {
  GroupMarkSignals signals;
  ApplyFileDemoSignals(
      GURL("file:///tmp/sidebar.html?yee-signal=audio,input,unread"), signals);

  EXPECT_TRUE(signals.audio);
  EXPECT_TRUE(signals.needs_input);
  EXPECT_TRUE(signals.unread);
}

TEST(GroupMarkSignalsTest, NonFilePagesCannotInjectDemoSignals) {
  GroupMarkSignals signals;
  ApplyFileDemoSignals(
      GURL("https://example.test/?yee-signal=audio,input,unread"), signals);

  EXPECT_FALSE(signals.audio);
  EXPECT_FALSE(signals.needs_input);
  EXPECT_FALSE(signals.unread);
}

}  // namespace
}  // namespace yee
