// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/browser_native_widget_mac.h"

#import <Cocoa/Cocoa.h>

#include <initializer_list>
#include "base/i18n/test/scoped_rtl_for_testing.h"

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/run_loop.h"

#include <map>
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/browser/ui/views/yee/native_frame_applied_test_support_mac.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/browser_test.h"
#include "third_party/blink/public/mojom/frame/fullscreen.mojom.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/background.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/native_widget_mac.h"

namespace {
using namespace yee::test;

struct GlassViews {
  NSView* glass_view = nil;
  NSView* tint_view = nil;
  NSView* opaque_view = nil;
};

GlassViews GetGlassViews(NSView* content_view) {
  Class background_view_class = NSClassFromString(@"GlassFrameBackgroundView");
  Class opaque_view_class = NSClassFromString(@"OpaqueFrameBackgroundView");
  NSView* glass_view = nil;
  NSView* opaque_view = nil;
  for (NSView* subview in content_view.subviews) {
    if ([subview isKindOfClass:background_view_class]) {
      glass_view = subview;
    } else if ([subview isKindOfClass:opaque_view_class]) {
      opaque_view = subview;
    }
  }

  NSView* tint_view = nil;
  if (glass_view) {
    for (NSView* subview in glass_view.subviews) {
      if ([subview isMemberOfClass:[NSView class]]) {
        tint_view = subview;
        break;
      }
    }
  }
  return {glass_view, tint_view, opaque_view};
}

int GetExpectedTopChromeGlassHeight(BrowserView* browser_view) {
  int height = 0;
  if (browser_view->browser_widget() &&
      browser_view->browser_widget()->GetFrameView()) {
    height = browser_view->browser_widget()->GetFrameView()->GetTopInset(true);
  }
  const auto top_element_info = browser_view->GetFrameElementInfo();
  height += top_element_info.top_area_height();
  height += top_element_info.toolbar_preferred_height;
  return height;
}

}  // namespace

class TestNativeWidgetMac : public views::NativeWidgetMac {
 public:
  static views::NativeWidgetMacNSWindowHost* GetHost(
      views::NativeWidgetMac* widget) {
    return static_cast<TestNativeWidgetMac*>(widget)->GetNSWindowHost();
  }
};

class YeeBrowserNativeWidgetMacGlassTest : public InProcessBrowserTest {
 public:
  YeeBrowserNativeWidgetMacGlassTest() {
    scoped_feature_list_.InitAndEnableFeature(features::kGlassFrame);
  }

  void TearDownOnMainThread() override {
    frames_.clear();
    applied_.Stop();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  AppliedCondition Frame(BrowserWindowInterface* target,
                         NativeFrameMatches expected,
                         AppliedMatch match) {
    auto& observer = frames_[target];
    if (!observer)
      observer = std::make_unique<NativeFrameAppliedObserver>(
          applied_.source(), *BrowserView::GetBrowserViewForBrowser(target));
    return applied_.Condition(observer->owner(), expected, match);
  }
  testing::AssertionResult AwaitEligible(BrowserWindowInterface* target) {
    return applied_.Run(
        {Frame(target, {.glass_eligible = true},
               AppliedMatch::kNativeFrameGlassEligible)},
        [] {}, AppliedDomain::kNativeWindow, TestTimeouts::action_timeout(),
        true);
  }

 protected:
  AppliedTestContext<AppliedBrowserObservers> applied_;

 protected:
  std::map<BrowserWindowInterface*, std::unique_ptr<NativeFrameAppliedObserver>>
      frames_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       ActiveInactiveTintOpacity) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* widget = browser_view->GetWidget();
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content_view = [ns_window contentView];

  // Bring browser to front to ensure it is active.
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(widget->IsActive());

  auto [glass_view, tint_view, opaque_view] = GetGlassViews(content_view);

  // 실제 tint가 사용하는 Yee 보정색 또는 native 프레임 색을 비교한다.
  const ui::ColorProvider* color_provider = browser_view->GetColorProvider();
  const SkColor active_color =
      yee::IsShellEnabled()
          ? yee::ResolveShellBackgroundColor(*color_provider, true)
          : color_provider->GetColor(ui::kColorFrameActive);
  const SkColor inactive_color =
      yee::IsShellEnabled()
          ? yee::ResolveShellBackgroundColor(*color_provider, false)
          : color_provider->GetColor(ui::kColorFrameInactive);

  auto get_sk_color = [](CGColorRef cg_color) {
    if (!cg_color) {
      return SK_ColorTRANSPARENT;
    }
    NSColor* ns_color = [NSColor colorWithCGColor:cg_color];
    NSColor* srgb_color =
        [ns_color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return SkColorSetARGB(static_cast<U8CPU>(srgb_color.alphaComponent * 255),
                          static_cast<U8CPU>(srgb_color.redComponent * 255),
                          static_cast<U8CPU>(srgb_color.greenComponent * 255),
                          static_cast<U8CPU>(srgb_color.blueComponent * 255));
  };

  auto get_color_from_views = [&](NSView* t_view, NSView* g_view) {
    if (t_view) {
      return get_sk_color(t_view.layer.backgroundColor);
    }
    CHECK(g_view);
    NSColor* t_color = [(id)g_view tintColor];
    NSColor* srgb_color =
        [t_color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return SkColorSetARGB(static_cast<U8CPU>(srgb_color.alphaComponent * 255),
                          static_cast<U8CPU>(srgb_color.redComponent * 255),
                          static_cast<U8CPU>(srgb_color.greenComponent * 255),
                          static_cast<U8CPU>(srgb_color.blueComponent * 255));
  };

  // Initially active.
  SkColor initial_color = get_color_from_views(tint_view, glass_view);
  EXPECT_EQ(SkColorSetA(initial_color, 255), SkColorSetA(active_color, 255));

  // Deactivate the widget by simulating native key status change.
  views::NativeWidgetMac* native_widget =
      static_cast<views::NativeWidgetMac*>(widget->native_widget());
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*widget, {.active = false},
                       AppliedMatch::kWidgetActive)},
      [&] {
        static_cast<remote_cocoa::mojom::NativeWidgetNSWindowHost*>(
            TestNativeWidgetMac::GetHost(native_widget))
            ->OnWindowKeyStatusChanged(/*is_key=*/false,
                                       /*is_content_first_responder=*/false,
                                       /*full_keyboard_access_enabled=*/false);
      }));

  // Find views again.
  auto [glass_view_deactivated, tint_view_deactivated,
        opaque_view_deactivated] = GetGlassViews(content_view);

  SkColor deactivated_color =
      get_color_from_views(tint_view_deactivated, glass_view_deactivated);
  EXPECT_EQ(SkColorSetA(deactivated_color, 255),
            SkColorSetA(inactive_color, 255));
}

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       FullscreenWindowIsOpaque) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* widget = browser_view->GetWidget();
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content_view = [ns_window contentView];

  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));

  EXPECT_FALSE([ns_window isOpaque]);
  auto [glass_view, tint_view, opaque_view] = GetGlassViews(content_view);
  EXPECT_NE(glass_view, nil);
  EXPECT_NE(opaque_view, nil);

  const auto capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-fullscreen-capture-dir");
  if (!capture_directory.empty()) {
    tabs::VerticalTabStripStateController::From(browser())
        ->SetVerticalTabsEnabled(false);
    widget->LayoutRootViewIfNecessary();
  }
  ASSERT_TRUE(applied_.Run(
      {Frame(browser(), {.opaque = true}, AppliedMatch::kNativeFrameOpaque)},
      [&] { ui_test_utils::ToggleFullscreenModeAndWait(browser()); }));
  auto [fs_glass_view, fs_tint_view, fs_opaque_view] =
      GetGlassViews(content_view);
  EXPECT_EQ(fs_glass_view, nil);
  EXPECT_EQ(fs_tint_view, nil);
  if (yee::IsShellEnabled()) {
    // 전체화면에서는 native 유리 대신 Yee 바탕이 완전히 불투명해진다.
    ASSERT_TRUE(browser_view->background());
    gfx::Canvas canvas(browser_view->size(), 1.f, false);
    browser_view->background()->Paint(&canvas, browser_view);
    EXPECT_EQ(SkColorGetA(canvas.GetBitmap().getColor(0, 0)), SK_AlphaOPAQUE);
  }
  if (!capture_directory.empty()) {
    // overlay Widget의 원래 NSWindow는 화면 밖에 있다. 탭·Toolbar의 접합부를
    // 검수할 수 있도록 AppKit의 실제 호스트를 캡처한다.
    NSView* moved =
        (__bridge NSView*)browser_view->overlay_widget()
            ->GetNativeWindowProperty(
                views::NativeWidgetMacNSWindowHost::kMovedContentNSView);
    ASSERT_TRUE(moved);
    NSWindow* header_window = moved.window;
    ASSERT_TRUE(header_window);
    base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
    ui::GrabWindowSnapshotAsPNG(
        gfx::NativeWindow(header_window),
        gfx::Rect(NSWidth(header_window.frame), NSHeight(header_window.frame)),
        snapshot.GetCallback());
    auto png = snapshot.Take();
    ASSERT_TRUE(png && png->size());
    ASSERT_TRUE(
        base::WriteFile(capture_directory.AppendASCII("native-header.png"),
                        base::span<const uint8_t>(*png)));
  }
  EXPECT_EQ(fs_opaque_view, nil);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       GlassViewGeometryMatchesShellPolicy) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* widget = browser_view->GetWidget();
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content_view = [ns_window contentView];

  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));

  auto [glass_view, tint_view, opaque_view] = GetGlassViews(content_view);
  ASSERT_NE(glass_view, nil);
  ASSERT_NE(opaque_view, nil);

  if (yee::IsShellEnabled()) {
    auto* const controller =
        tabs::VerticalTabStripStateController::From(browser());
    ASSERT_NE(controller, nullptr);
    const gfx::Rect original_bounds = widget->GetWindowBoundsInScreen();
    gfx::Rect resized_bounds = original_bounds;
    resized_bounds.Inset(24);
    for (const gfx::Rect& bounds :
         {original_bounds, resized_bounds, original_bounds}) {
      SCOPED_TRACE(bounds.ToString());
      ASSERT_TRUE(applied_.Run({applied_.Widget(*widget, {.bounds = bounds},
                                                AppliedMatch::kWidgetSize)},
                               [&] { widget->SetBounds(bounds); }));
      for (bool vertical : {false, true, false}) {
        SCOPED_TRACE(vertical);
        controller->SetVerticalTabsEnabled(vertical);
        widget->LayoutRootViewIfNecessary();
        const auto current_views = GetGlassViews(content_view);
        ASSERT_NE(current_views.glass_view, nil);
        EXPECT_EQ(NSHeight(current_views.glass_view.frame),
                  NSHeight(content_view.bounds));
        EXPECT_EQ(NSWidth(current_views.glass_view.frame),
                  NSWidth(content_view.bounds));
        EXPECT_EQ(NSMinX(current_views.glass_view.frame),
                  NSMinX(content_view.bounds));
        EXPECT_EQ(NSMaxY(current_views.glass_view.frame),
                  NSMaxY(content_view.bounds));
        ASSERT_NE(current_views.opaque_view, nil);
        // 전체 창 유리 아래의 불투명 배경은 크기·방향이 바뀌어도 비어 있다.
        EXPECT_TRUE(NSIsEmptyRect(current_views.opaque_view.frame));
        EXPECT_EQ(current_views.opaque_view.autoresizingMask, 0u);
        ASSERT_TRUE(browser_view->background());
        gfx::Canvas canvas(browser_view->size(), 1.f, false);
        browser_view->background()->Paint(&canvas, browser_view);
        const SkAlpha shell_alpha =
            SkColorGetA(canvas.GetBitmap().getColor(0, 0));
        EXPECT_GT(shell_alpha, SK_AlphaTRANSPARENT);
        EXPECT_LT(shell_alpha, SK_AlphaOPAQUE);
        if (!vertical) {
          // 불투명 Header가 빈 수평 탭 행을 덮어 native glass를 가리면 안 된다.
          const gfx::Rect strip_bounds =
              browser_view->tab_strip_view()->GetBoundsInScreen();
          EXPECT_LE(
              strip_bounds.bottom() -
                  GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap),
              browser_view->top_container()->GetBoundsInScreen().y());
        }
      }
    }
    return;
  }

  const int expected_height = GetExpectedTopChromeGlassHeight(browser_view);

  EXPECT_EQ(NSHeight(glass_view.frame), expected_height);
  EXPECT_LT(NSHeight(glass_view.frame), NSHeight(content_view.bounds));
  EXPECT_EQ(NSMaxY(glass_view.frame), NSMaxY(content_view.bounds));
  EXPECT_EQ(NSWidth(glass_view.frame), NSWidth(content_view.bounds));

  EXPECT_EQ(NSMinX(opaque_view.frame), 0);
  EXPECT_EQ(NSMinY(opaque_view.frame), 0);
  EXPECT_EQ(NSWidth(opaque_view.frame), NSWidth(content_view.bounds));
  EXPECT_EQ(NSHeight(opaque_view.frame),
            NSHeight(content_view.bounds) - expected_height);
  EXPECT_TRUE(opaque_view.layer.opaque);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       MultipleProfilesTintOpacity) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  auto get_tint_alpha = [](BrowserWindowInterface* target_browser) {
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(target_browser);
    NSWindow* window = view->GetWidget()->GetNativeWindow().GetNativeNSWindow();
    auto [glass_view, tint_view, opaque_view] =
        GetGlassViews([window contentView]);
    CHECK(tint_view);
    CHECK(tint_view.layer.backgroundColor);
    NSColor* ns_color =
        [NSColor colorWithCGColor:tint_view.layer.backgroundColor];
    NSColor* srgb_color =
        [ns_color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return srgb_color.alphaComponent;
  };

  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  const CGFloat single_profile_alpha = get_tint_alpha(browser());

  // Opening a second window in the same profile keeps the original opacity.
  BrowserWindowInterface* same_profile_browser =
      CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(AwaitEligible(same_profile_browser));
  EXPECT_NEAR(get_tint_alpha(same_profile_browser), single_profile_alpha,
              0.001);
  frames_.erase(same_profile_browser);
  CloseBrowserSynchronously(same_profile_browser);
  ASSERT_TRUE(AwaitEligible(browser()));

  // Yee는 프로필 수와 관계없이 같은 tint를 유지한다.
  const double multiple_profile_opacity_increase =
      yee::IsShellEnabled() ? 0.0 : 0.05;
  ProfileManager* const profile_manager = g_browser_process->profile_manager();
  const base::FilePath new_path =
      profile_manager->GenerateNextProfileDirectoryPath();
  Profile& second_profile =
      profiles::testing::CreateProfileSync(profile_manager, new_path);
  BrowserWindowInterface* second_profile_browser =
      CreateBrowser(&second_profile);
  ASSERT_TRUE(AwaitEligible(second_profile_browser));
  EXPECT_NEAR(get_tint_alpha(second_profile_browser),
              single_profile_alpha + multiple_profile_opacity_increase, 0.001);

  // 첫 프로필로 돌아와도 같은 정책을 적용한다.
  browser()->GetWindow()->Activate();
  ASSERT_TRUE(AwaitEligible(browser()));
  EXPECT_NEAR(get_tint_alpha(browser()),
              single_profile_alpha + multiple_profile_opacity_increase, 0.001);

  // Closing the second profile window (while it is in the background and
  // ineligible) restores the original tint opacity on the remaining active
  // window.
  frames_.erase(second_profile_browser);
  CloseBrowserSynchronously(second_profile_browser);
  EXPECT_NEAR(get_tint_alpha(browser()), single_profile_alpha, 0.001);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       PaintAsActiveLockMaintainsActiveTint) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* widget = browser_view->GetWidget();
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content_view = [ns_window contentView];

  // Bring browser to front to ensure it is active.
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(widget->IsActive());

  auto [glass_view, tint_view, opaque_view] = GetGlassViews(content_view);

  // 실제 tint가 사용하는 Yee 보정색 또는 native 프레임 색을 비교한다.
  const ui::ColorProvider* color_provider = browser_view->GetColorProvider();
  const SkColor active_color =
      yee::IsShellEnabled()
          ? yee::ResolveShellBackgroundColor(*color_provider, true)
          : color_provider->GetColor(ui::kColorFrameActive);
  const SkColor inactive_color =
      yee::IsShellEnabled()
          ? yee::ResolveShellBackgroundColor(*color_provider, false)
          : color_provider->GetColor(ui::kColorFrameInactive);

  auto get_sk_color = [](CGColorRef cg_color) {
    if (!cg_color) {
      return SK_ColorTRANSPARENT;
    }
    NSColor* ns_color = [NSColor colorWithCGColor:cg_color];
    NSColor* srgb_color =
        [ns_color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return SkColorSetARGB(static_cast<U8CPU>(srgb_color.alphaComponent * 255),
                          static_cast<U8CPU>(srgb_color.redComponent * 255),
                          static_cast<U8CPU>(srgb_color.greenComponent * 255),
                          static_cast<U8CPU>(srgb_color.blueComponent * 255));
  };

  auto get_color_from_views = [&](NSView* t_view, NSView* g_view) {
    if (t_view) {
      return get_sk_color(t_view.layer.backgroundColor);
    }
    CHECK(g_view);
    NSColor* t_color = [(id)g_view tintColor];
    NSColor* srgb_color =
        [t_color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    return SkColorSetARGB(static_cast<U8CPU>(srgb_color.alphaComponent * 255),
                          static_cast<U8CPU>(srgb_color.redComponent * 255),
                          static_cast<U8CPU>(srgb_color.greenComponent * 255),
                          static_cast<U8CPU>(srgb_color.blueComponent * 255));
  };

  // Lock paint-as-active on the widget (e.g. as a child bubble/widget would).
  std::unique_ptr<views::Widget::PaintAsActiveLock> lock =
      widget->LockPaintAsActive();

  // Deactivate the native widget (simulating a child window becoming key).
  views::NativeWidgetMac* native_widget =
      static_cast<views::NativeWidgetMac*>(widget->native_widget());
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*widget, {.active = false},
                       AppliedMatch::kWidgetActive)},
      [&] {
        static_cast<remote_cocoa::mojom::NativeWidgetNSWindowHost*>(
            TestNativeWidgetMac::GetHost(native_widget))
            ->OnWindowKeyStatusChanged(/*is_key=*/false,
                                       /*is_content_first_responder=*/false,
                                       /*full_keyboard_access_enabled=*/false);
      }));

  // Widget is natively inactive, but ShouldPaintAsActive() is true due to the
  // lock.
  EXPECT_FALSE(widget->IsActive());
  EXPECT_TRUE(widget->ShouldPaintAsActive());

  auto [glass_view_locked, tint_view_locked, opaque_view_locked] =
      GetGlassViews(content_view);
  SkColor locked_color =
      get_color_from_views(tint_view_locked, glass_view_locked);
  EXPECT_EQ(SkColorSetA(locked_color, 255), SkColorSetA(active_color, 255));

  // Release the lock. The widget should now transition to inactive tint.
  ASSERT_TRUE(applied_.Run({applied_.Widget(*widget, {.paint_as_active = false},
                                            AppliedMatch::kWidgetPaintActive)},
                           [&] { lock.reset(); }));

  EXPECT_FALSE(widget->ShouldPaintAsActive());
  auto [glass_view_unlocked, tint_view_unlocked, opaque_view_unlocked] =
      GetGlassViews(content_view);
  SkColor unlocked_color =
      get_color_from_views(tint_view_unlocked, glass_view_unlocked);
  EXPECT_EQ(SkColorSetA(unlocked_color, 255), SkColorSetA(inactive_color, 255));
}

IN_PROC_BROWSER_TEST_F(YeeBrowserNativeWidgetMacGlassTest,
                       TabFullscreenWindowIsOpaque) {
  if (!features::IsGlassFrameEnabled()) {
    GTEST_SKIP() << "Glass frame feature is disabled.";
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::Widget* widget = browser_view->GetWidget();
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  NSView* content_view = [ns_window contentView];

  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));

  EXPECT_FALSE([ns_window isOpaque]);
  auto [glass_view, tint_view, opaque_view] = GetGlassViews(content_view);
  EXPECT_NE(glass_view, nil);
  EXPECT_NE(opaque_view, nil);

  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(applied_.Run(
      {Frame(browser(), {.opaque = true}, AppliedMatch::kNativeFrameOpaque)},
      [&] {
        web_contents->GetDelegate()->EnterFullscreenModeForTab(
            web_contents->GetPrimaryMainFrame(), {});
      }));
  auto [fs_glass_view, fs_tint_view, fs_opaque_view] =
      GetGlassViews(content_view);
  EXPECT_EQ(fs_glass_view, nil);
  EXPECT_EQ(fs_tint_view, nil);
  EXPECT_EQ(fs_opaque_view, nil);
}
