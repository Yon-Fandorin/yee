// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/test_switches.h"
#include "build/build_config.h"
#include "chrome/test/base/chrome_test_launcher.h"
#include "chrome/test/base/chrome_test_suite.h"
#include "content/public/common/content_switches.h"
#include "ui/compositor/compositor_switches.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/views/yee/background_browser_test_support.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "base/win/dark_mode_support.h"
#include "base/win/win_util.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#endif

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  size_t parallel_jobs = base::NumParallelJobs(/*cores_per_job=*/2);
  if (parallel_jobs == 0U) {
    return 1;
  }

#if BUILDFLAG(IS_WIN)
  base::win::PinUser32();
  base::win::EnableHighDPISupport();
  base::win::IsDarkModeAvailable();
#endif

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->GetSwitchValueASCII("yee-test-tier") == "background") {
    command_line->AppendSwitch("headless");
#if BUILDFLAG(IS_MAC)
    yee::test::SuppressBackgroundBrowserActivation();
#endif
  }

#if BUILDFLAG(IS_WIN)
  yee::test::ScopedLauncherForegroundPermission foreground;
  if (!foreground.ready()) {
    return 1;
  }
#endif

  // foreground의 옵션 처리와 launcher는 upstream browser_tests와 같다.
  if (command_line->HasSwitch(switches::kTestLauncherInteractive)) {
    command_line->AppendSwitch(switches::kEnablePixelOutputInTests);
#if BUILDFLAG(IS_WIN)
    command_line->AppendSwitch(switches::kDisableGpu);
#endif
  }

  ChromeTestSuiteRunner runner;
  ChromeTestLauncherDelegate delegate(&runner);
  return LaunchChromeTests(parallel_jobs, &delegate, argc, argv);
}
