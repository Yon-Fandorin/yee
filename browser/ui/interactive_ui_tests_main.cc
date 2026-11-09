// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/command_line.h"
#include "base/memory/discardable_memory_allocator.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/test/test_switches.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/preloading/preloading_features.h"
#include "chrome/browser/ssl/https_upgrades_navigation_throttle.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/test/base/chrome_test_launcher.h"
#include "chrome/test/base/chrome_test_suite.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "content/public/common/content_switches.h"
#include "gpu/ipc/service/image_transport_surface.h"
#include "ui/base/interaction/interactive_test_internal.h"
#include "ui/base/test/ui_controls.h"
#include "ui/compositor/compositor_switches.h"

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/views/yee/background_browser_test_support.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "ui/linux/display_server_utils.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/test/ui_controls_ash.h"
#endif

#if defined(USE_AURA) && BUILDFLAG(IS_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#include "ui/platform_window/common/platform_window_defaults.h"
#endif  // defined(USE_AURA) && BUILDFLAG(IS_OZONE)

#if BUILDFLAG(IS_WIN)
#include "base/functional/bind.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/win_util.h"
#include "ui/views/widget/desktop_aura/desktop_drag_drop_client_win.h"
#endif

namespace {

bool IsBackgroundTestRun() {
  return base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
             "yee-test-tier") == "background";
}

}  // namespace

// Kombucha의 PassKey가 이 upstream 클래스 이름을 요구한다.
class InteractiveUITestSuite : public ChromeTestSuite {
 public:
  InteractiveUITestSuite(int argc, char** argv) : ChromeTestSuite(argc, argv) {}
  ~InteractiveUITestSuite() override = default;

 protected:
  // ChromeTestSuite overrides:
  void Initialize() override {
    ChromeTestSuite::Initialize();

#if BUILDFLAG(IS_CHROMEOS)
    ash::test::EnableUIControlsAsh();
#elif BUILDFLAG(IS_WIN)
    com_initializer_ = std::make_unique<base::win::ScopedCOMInitializer>();
    yee::test::InstallOwnedWindowsUIControls();
    views::DesktopDragDropClientWin::SetOnDragStartedCallbackForTesting(
        base::BindRepeating(&yee::test::QueueOwnedDragUnblockNudge));
#elif BUILDFLAG(IS_OZONE)
    // Notifies the platform that test config is needed. For Wayland, for
    // example, makes it possible to use emulated input.
    ui::test::EnableTestConfigForPlatformWindows();

#if BUILDFLAG(IS_LINUX)
    ui::SetOzonePlatformForLinuxIfNeeded(
        *base::CommandLine::ForCurrentProcess());
#endif

    ui::OzonePlatform::InitParams params;
    params.single_process = true;
    ui::OzonePlatform::InitializeForUI(params);
    ui_controls::EnableUIControls();
#else
    ui_controls::EnableUIControls();
#endif
    // Allow interactive Kombucha test verbs in interactive UI tests.
    ui::test::internal::InteractiveTestPrivate::
        set_interactive_test_verbs_allowed(
            base::PassKey<InteractiveUITestSuite>());

    // TODO(crbug.com/40263135) Investigate why https upgrade causes
    // interactive_ui_tests to run longer.
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
    // Force the HTTPS-Upgrades timeout to zero.
    HttpsUpgradesNavigationThrottle::set_timeout_for_testing(base::TimeDelta());
#endif

    // Prewarming the default search engine background page causes significant
    // latency and timeouts in interactive UI tests (especially on Mac due to
    // Cocoa event queue processing). Disable it for interactive_ui_tests.
    scoped_feature_list_.InitAndDisableFeature(features::kPrewarm);

    base::DiscardableMemoryAllocator::SetInstance(
        &discardable_memory_allocator_);
  }

  void Shutdown() override {
#if BUILDFLAG(IS_WIN)
    views::DesktopDragDropClientWin::SetOnDragStartedCallbackForTesting(
        base::NullCallback());
    com_initializer_.reset();
#endif
  }

 private:
#if BUILDFLAG(IS_WIN)
  std::unique_ptr<base::win::ScopedCOMInitializer> com_initializer_;
#endif
  base::TestDiscardableMemoryAllocator discardable_memory_allocator_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

class InteractiveUITestLauncherDelegate : public ChromeTestLauncherDelegate {
 public:
  explicit InteractiveUITestLauncherDelegate(ChromeTestSuiteRunner* runner)
      : ChromeTestLauncherDelegate(runner) {}
  InteractiveUITestLauncherDelegate(const InteractiveUITestLauncherDelegate&) =
      delete;
  InteractiveUITestLauncherDelegate& operator=(
      const InteractiveUITestLauncherDelegate&) = delete;

 protected:
#if BUILDFLAG(IS_MAC)
  void PreRunTest() override {
    // Clear currently pressed modifier keys (if any) before the test starts.
    if (!IsBackgroundTestRun()) {
      ui_test_utils::ClearKeyEventModifiers();
    }
  }

  void PostRunTest(base::TestResult* test_result) override {
    // Clear currently pressed modifier keys (if any) after the test finishes
    // and report an error if there were some.
    bool had_hanging_modifiers =
        !IsBackgroundTestRun() && ui_test_utils::ClearKeyEventModifiers();
    if (had_hanging_modifiers &&
        test_result->status == base::TestResult::TEST_SUCCESS) {
      test_result->status = base::TestResult::TEST_FAILURE_ON_EXIT;
    }
    ChromeTestLauncherDelegate::PostRunTest(test_result);
  }
#endif  // BUILDFLAG(IS_MAC)
};

class InteractiveUITestSuiteRunner : public ChromeTestSuiteRunner {
 public:
  int RunTestSuite(int argc, char** argv) override {
    InteractiveUITestSuite test_suite(argc, argv);
    return RunTestSuiteInternal(&test_suite);
  }
};

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

#if BUILDFLAG(IS_CHROMEOS) && defined(MEMORY_SANITIZER)
  // Force software-gl. This is necessary for mus tests to avoid an msan warning
  // in gl init.
  command_line->AppendSwitch(switches::kOverrideUseSoftwareGLForTests);
#endif

  // Without this it's possible for the first browser to start up in the
  // background, generally because the last test did something that causes the
  // test to run in the background. Most interactive ui tests assume they are in
  // the foreground and fail in weird ways if they aren't (for example, clicks
  // go to the wrong place). This ensures the first browser is always in the
  // foreground.
  if (IsBackgroundTestRun()) {
    // 직접 실행할 때도 native 창 표시를 막는다.
    command_line->AppendSwitch("headless");
#if BUILDFLAG(IS_MAC)
    yee::test::SuppressBackgroundBrowserActivation();
#endif
  } else {
    InProcessBrowserTest::set_global_browser_set_up_function(
        &yee::test::BringBrowserWindowToFront);
  }

#if BUILDFLAG(IS_WIN)
  base::win::EnableHighDPISupport();
  yee::test::ScopedLauncherForegroundPermission foreground;
  if (!foreground.ready()) {
    return 1;
  }
#endif  // BUILDFLAG(IS_WIN)

  // For ash chrome, it's using multiple X11 windows to host the browser.
  // Also, {emulating|injecting} keyboard and mouse events happen at ozone
  // level, not OS level. So it is fine to run tests in parallel.
#if BUILDFLAG(IS_CHROMEOS)
  size_t parallel_jobs = base::NumParallelJobs(/*cores_per_job=*/2);
  if (parallel_jobs == 0) {
    parallel_jobs = 1;
  }
#else
  // Run interactive_ui_tests serially, they do not support running in parallel.
  size_t parallel_jobs = 1;
#endif

  // Adjust switches for interactive tests where the user is expected to
  // manually verify results.
  if (command_line->HasSwitch(switches::kTestLauncherInteractive)) {
    // Since the test is interactive, the invoker will want to have pixel output
    // to actually see the result.
    command_line->AppendSwitch(switches::kEnablePixelOutputInTests);
#if BUILDFLAG(IS_WIN)
    // Under Windows, dialogs (but not the browser window) created in the
    // spawned browser_test process are invisible for some unknown reason.
    // Pass in --disable-gpu to resolve this for now. See
    // http://crbug.com/40504416.
    command_line->AppendSwitch(switches::kDisableGpu);
#endif  // BUILDFLAG(IS_WIN)
  }

  InteractiveUITestSuiteRunner runner;
  InteractiveUITestLauncherDelegate delegate(&runner);
  return LaunchChromeTests(parallel_jobs, &delegate, argc, argv);
}
