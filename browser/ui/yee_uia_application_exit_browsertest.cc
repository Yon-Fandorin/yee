// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <string>
#include <tuple>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/accessibility/browser_test_with_ax_client.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/accessibility/platform/ax_platform_node_win.h"

namespace {

class YeeUiaApplicationExitBrowserTest : public BrowserTestWithAxClient {
 private:
  ClientApi GetClientApi() const override { return ClientApi::kUiAutomation; }
};

IN_PROC_BROWSER_TEST_F(YeeUiaApplicationExitBrowserTest,
                       ClosingOneWindowKeepsAccessibilityAvailable) {
  BrowserWindowInterface* remaining = CreateBrowser(browser()->GetProfile());
  ASSERT_HRESULT_SUCCEEDED(InitializeClient(remaining));
  ASSERT_HRESULT_SUCCEEDED(FindAll());

  CloseBrowserSynchronously(browser());

  EXPECT_TRUE(ui::AXPlatform::GetInstance().IsUiaProviderEnabled());
  EXPECT_GT(ui::AXPlatformNodeWin::GetCounts().live_nodes, 0U);
  EXPECT_HRESULT_SUCCEEDED(FindAll());
}

IN_PROC_BROWSER_TEST_F(YeeUiaApplicationExitBrowserTest,
                       ApplicationExitReleasesClientReferences) {
  ASSERT_HRESULT_SUCCEEDED(InitializeClient(browser()));
  ASSERT_HRESULT_SUCCEEDED(FindAll());
  ASSERT_GT(ui::AXPlatformNodeWin::GetCounts().live_nodes, 0U);

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&chrome::AttemptExit));
  RunUntilBrowserProcessQuits();

  EXPECT_FALSE(ui::AXPlatform::GetInstance().IsUiaProviderEnabled());
  WaitForNodeCounts({0U, 0U, 0U, 0U});
}

IN_PROC_BROWSER_TEST_F(YeeUiaApplicationExitBrowserTest,
                       ExitAfterLastWindowReleasesClientReferences) {
  ASSERT_HRESULT_SUCCEEDED(InitializeClient(browser()));
  ASSERT_HRESULT_SUCCEEDED(FindAll());
  CloseBrowserSynchronously(browser());

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&chrome::AttemptExit));
  RunUntilBrowserProcessQuits();

  EXPECT_FALSE(ui::AXPlatform::GetInstance().IsUiaProviderEnabled());
  WaitForNodeCounts({0U, 0U, 0U, 0U});
}

}  // namespace

#endif  // BUILDFLAG(IS_WIN)
