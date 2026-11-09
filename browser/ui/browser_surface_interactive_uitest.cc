// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "cc/trees/render_frame_metadata.h"
#include "chrome/browser/actor/ui/actor_overlay_web_view.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/enterprise/data_protection/data_protection_overlay_view.h"
#include "chrome/browser/glic/browser_ui/context_sharing_border_view.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/ai_overlay_dialog/ai_overlay_dialog_controller.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/immersive/immersive_mode_controller.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/read_anything/read_anything_immersive_overlay_view.h"
#include "chrome/browser/ui/side_panel/side_panel_entry.h"
#include "chrome/browser/ui/side_panel/side_panel_registry.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/animations/side_panel_animations.h"
#include "chrome/browser/ui/views/animations/tab_strip_animations.h"
#include "chrome/browser/ui/views/find_bar_host.h"
#include "chrome/browser/ui/views/find_bar_view.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/browser_widget.h"
#include "chrome/browser/ui/views/frame/contents_capture_border_view.h"
#include "chrome/browser/ui/views/frame/contents_container_outline.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_layout.h"
#include "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/tab_strip_region_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_background_blur_backdrop.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/infobars/confirm_infobar.h"
#include "chrome/browser/ui/views/infobars/infobar_container_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/side_panel/side_panel_animation_content_view.h"
#include "chrome/browser/ui/views/status_bubble_views.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_line_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/common/unpinned_tab_container_view.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/tabs/tab_container_impl.h"
#include "chrome/browser/ui/views/tabs/tab_group_header.h"
#include "chrome/browser/ui/views/tabs/tab_group_underline.h"
#include "chrome/browser/ui/views/tabs/z_orderable_tab_container_element.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"
#include "chrome/browser/ui/views/yee/applied_wait_kombucha.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"
#include "chrome/browser/ui/views/yee/native_browser_test_support.h"
#include "chrome/browser/ui/views/yee/page_viewport_geometry.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/browser/ui/views/yee/page_viewport_migration.h"
#include "chrome/browser/ui/views/yee/shell_features.h"
#include "chrome/browser/ui/views/yee/sidebar_footer.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/ui/views/yee/sidebar_presentation.h"
#include "chrome/browser/ui/views/yee/split_pane_controls.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/bookmarks/common/bookmark_bar_visibility_state.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/prefs/pref_service.h"
#include "components/search/ntp_features.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "components/user_prefs/user_prefs.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/viz/common/frame_timing_details.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/filename_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_specifier.h"
#include "ui/base/interaction/element_tracker.h"
#include "ui/base/interaction/interactive_test_definitions.h"
#include "ui/base/pointer/touch_ui_controller.h"
#include "ui/base/test/ui_controls.h"
#include "ui/base/unowned_user_data/user_data_factory.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/paint_context.h"
#include "ui/compositor/test/test_utils.h"
#include "ui/display/display.h"
#include "ui/display/display_switches.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_container.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/native_theme/mock_os_settings_provider.h"
#include "ui/snapshot/snapshot.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/bounds_animator.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_host.h"
#include "ui/views/background.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/paint_info.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/root_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_WIN)
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/ui/tabs/split_view_layout_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/views/frame/browser_caption_button_container_win.h"
#include "chrome/browser/ui/views/frame/browser_frame_view_win.h"
#include "chrome/browser/ui/views/frame/windows_caption_button.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/isolated_world_ids.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/views/frame/immersive_mode_controller_mac.h"
#include "ui/base/cocoa/nswindow_test_util.h"
#endif

namespace {

views::View* GetTabAnchorForModelIndex(BrowserView* view, int index) {
  return view->tab_strip_view()->GetTabAnchorView(
      view->browser()->GetTabStripModel()->GetTabAtIndex(index)->GetHandle());
}

void ExpectWebViewNativeClip(views::WebView* web_view,
                             const gfx::Rect& expected_clip) {
  ASSERT_TRUE(web_view);
#if BUILDFLAG(IS_WIN)
  if (!web_view->holder()->native_view()) {
    // 아직 renderer가 없는 DevTools/overlay에는 native clip 대상도 없다.
    // Views layer가 있는 경우 그 clip은 따로 검증하고, 실제 native surface의
    // 생성 전후 계약은 YeeWindowsNativeClipSurvivesRendererAttachment로
    // 검증한다.
    EXPECT_FALSE(web_view->web_contents());
    EXPECT_TRUE(web_view->holder()->GetNativeViewClipRect().IsEmpty());
    if (web_view->layer()) {
      EXPECT_EQ(expected_clip, web_view->layer()->clip_rect());
    }
    return;
  }
#endif
  EXPECT_EQ(expected_clip, web_view->holder()->GetNativeViewClipRect());
}

// 실제 compositor의 선택적 시각 자료다. 동작 assertion으로 합성 화면의
// 시각 검수를 대체하지 않는다.
void CaptureYeeCompositorFrame(views::Widget* widget,
                               const base::FilePath& path) {
  auto* const compositor = widget->GetCompositor();
  compositor->ScheduleFullRedraw();
  yee::test::AppliedTestContext<yee::test::AppliedViewsObservers> frame;
  ASSERT_TRUE(frame.WaitForPresentedFrame(*widget));
  base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      readback.GetCallback());
  // 전체 root layer를 읽는다. 물리 픽셀인 compositor 크기를 area로 넘기면
  // layer 좌표 변환에서 배율이 한 번 더 적용된다.
  compositor->root_layer()->RequestCopyOfOutput(std::move(request));
  compositor->ScheduleFullRedraw();
  auto result = readback.Take();
  ASSERT_TRUE(result && !result->IsEmpty());
  auto pixels = result->ScopedAccessSkBitmap();
  const SkBitmap& bitmap = pixels.GetOutScopedBitmap();
  const auto png =
      gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
  ASSERT_TRUE(png);
  ASSERT_TRUE(base::WriteFile(path, *png));
  // 전체 layer 출력은 앞서 읽은 compositor 크기와 다를 수 있다. 결과를
  // 편집하지 않고 readback 결과와 같은 시점의 크기를 함께 남긴다.
  const std::string capture_metrics = base::StringPrintf(
      "result_rect=%s bitmap_px=%dx%d compositor_px=%s root_dip=%s dsf=%.6f\n",
      result->rect().ToString().c_str(), bitmap.width(), bitmap.height(),
      compositor->size().ToString().c_str(),
      widget->GetRootView()->size().ToString().c_str(),
      compositor->device_scale_factor());
  ASSERT_TRUE(
      base::WriteFile(path.AddExtensionASCII("capture.txt"), capture_metrics));
}

// Windows 창 버튼은 Views 합성 화면에 포함된다. macOS 재질은 compositor
// 밖의 native 창에서만 얻을 수 있다.
void CaptureYeeWindowFrame(views::Widget* widget, const base::FilePath& path) {
#if BUILDFLAG(IS_WIN)
  CaptureYeeCompositorFrame(widget, path);
#else
  auto* const compositor = widget->GetCompositor();
  compositor->ScheduleFullRedraw();
  yee::test::AppliedTestContext<yee::test::AppliedViewsObservers> frame;
  ASSERT_TRUE(frame.WaitForPresentedFrame(*widget));
  base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
  ui::GrabWindowSnapshotAsPNG(
      widget->GetNativeWindow(),
      gfx::Rect(widget->GetWindowBoundsInScreen().size()),
      snapshot.GetCallback());
  auto png = snapshot.Take();
  ASSERT_TRUE(png && png->size());
  ASSERT_TRUE(
      base::WriteFile(path, static_cast<base::span<const uint8_t>>(*png)));
#endif
}

}  // namespace

namespace {

class YeeGeometryConfirmInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  explicit YeeGeometryConfirmInfoBarDelegate(
      infobars::InfoBarDelegate::InfobarPriority priority =
          infobars::InfoBarDelegate::InfobarPriority::kDefault)
      : priority_(priority) {}
  ~YeeGeometryConfirmInfoBarDelegate() override = default;

  infobars::InfoBarDelegate::InfoBarIdentifier GetIdentifier() const override {
    return infobars::InfoBarDelegate::TEST_INFOBAR;
  }
  infobars::InfoBarDelegate::InfobarPriority GetPriority() const override {
    return priority_;
  }
  std::u16string GetMessageText() const override {
    return u"A long native notice whose text may elide while actions remain";
  }
  int GetButtons() const override { return BUTTON_OK; }
  bool EqualsDelegate(infobars::InfoBarDelegate* delegate) const override {
    return false;
  }

 private:
  const infobars::InfoBarDelegate::InfobarPriority priority_;
};

// 두 surface fixture가 동일한 관측/요청 수명을 사용한다.
class YeeAppliedSurfaceUiTest
    : public yee::test::AppliedWaitInteractiveTestMixin<
          InteractiveBrowserTest> {
 protected:
  void TearDownOnMainThread() override {
    applied_.Stop();
    yee::test::AppliedWaitInteractiveTestMixin<
        InteractiveBrowserTest>::TearDownOnMainThread();
  }

  yee::test::AppliedTestContext<yee::test::AppliedBrowserObservers> applied_;

  void ApplyScheduledLayout() {
    // 기존 fixture의 명시적 layout 자극이며 idle drain을 반복하지 않는다.
    BrowserView::GetBrowserViewForBrowser(browser())
        ->GetWidget()
        ->LayoutRootViewIfNecessary();
  }

  yee::test::AppliedConditions PageConditions(bool fresh = false) {
    using namespace yee::test;
    auto* view = BrowserView::GetBrowserViewForBrowser(browser());
    AppliedConditions conditions;
    if (yee::IsShellEnabled()) {
      for (const auto& container : view->GetContentsContainerViews()) {
        if (!container->GetVisible() ||
            !container->contents_view()->web_contents())
          continue;
        auto* clip = container->GetViewByID(yee::kPageViewportClipHostViewId);
        if (!clip)
          continue;
        const uint64_t handle = tabs::TabInterface::GetFromContents(
                                    container->contents_view()->web_contents())
                                    ->GetHandle()
                                    .raw_value();
        conditions.push_back(applied_.Condition(
            applied_.source().ExpectPageHosts(*browser(),
                                            *view->multi_contents_view(),
                                            handle, handle, fresh),
            PageHostsApplied{.pane_id = handle, .tab_handle = handle},
            AppliedMatch::kPageIdentity,
            fresh ? AppliedFreshness::kNewApply
                  : AppliedFreshness::kCurrentAfterDispatch));
        conditions.back().coordinates = GetAppliedViewCoordinates(
            *clip, AppliedCoordinates::Space::kParent);
      }
    }
    if (conditions.empty())
      conditions.push_back(applied_.View(*view,
                                         {.bounds = gfx::Rect(0, 0, 1, 1)},
                                         AppliedMatch::kViewNonEmpty));
    return conditions;
  }

  yee::test::AppliedCondition VerticalIdle() {
    using namespace yee::test;
    auto* view = BrowserView::GetBrowserViewForBrowser(browser());
    auto owner = applied_.source().ObserveBrowserAnimation(
        *view, *BrowserAnimationController::From(browser()),
        TabStripAnimations::kVerticalTabStrip,
        TabStripAnimations::kTabStripWidth, 1);
    return applied_.Condition(
        owner, AnimationMatches{.group_id = 1, .running = false});
  }

  yee::test::AppliedCondition SidebarHover(VerticalTabStripRegionView& region,
                                           bool expanded) {
    using namespace yee::test;
    auto owner = applied_.source().ObserveUiState(
        region, AppliedBrowserObservers::UiProbe::kSidebarHover);
    return applied_.Condition(owner,
                              ViewStateMatches{.expanded_on_hover = expanded},
                              AppliedMatch::kSidebarHover);
  }

  yee::test::AppliedConditions GroupAnimations(views::View* heading,
                                               bool running = false) {
    using namespace yee::test;
    AppliedConditions conditions;
    if (auto* legacy =
            views::AsViewClass<TabContainerImpl>(heading->parent())) {
      auto owner = applied_.source().ObserveTabContainerAnimation(*legacy, 10);
      conditions.push_back(applied_.Condition(
          owner, AnimationMatches{.group_id = 10, .running = running}));
    } else {
      uint64_t id = 10;
      for (auto* parent = heading->parent(); parent;
           parent = parent->parent()) {
        if ((views::IsViewClass<TabGroupView>(parent) ||
             views::IsViewClass<UnpinnedTabContainerView>(parent)) &&
            parent->GetLayoutManager()) {
          auto owner = applied_.source().ObserveUnifiedAnimation(
              *parent,
              *static_cast<TabCollectionAnimatingLayoutManager*>(
                  parent->GetLayoutManager()),
              ++id);
          conditions.push_back(applied_.Condition(
              owner, AnimationMatches{.group_id = id, .running = running}));
        }
      }
    }
    return conditions;
  }

  testing::AssertionResult Await(
      yee::test::AppliedConditions conditions,
      base::TimeDelta budget = TestTimeouts::action_timeout()) {
    return applied_.Run(
        std::move(conditions), [] {}, yee::test::AppliedDomain::kLayout, budget,
        true);
  }

  testing::AssertionResult RendererReady(content::WebContents& contents,
                                         views::View& viewport) {
    const double scale =
        viewport.GetWidget()->GetCompositor()->device_scale_factor();
    const auto expected = gfx::ScaleToCeiledSize(viewport.size(), scale);
    return Await({applied_.source().ExpectRendererViewport(contents, expected,
                                                         scale, false, true)});
  }

  testing::AssertionResult RendererPresented(content::WebContents& contents,
                                             views::View& viewport) {
    const double scale =
        viewport.GetWidget()->GetCompositor()->device_scale_factor();
    const auto pixels = gfx::ScaleToCeiledSize(viewport.size(), scale);
    yee::test::AppliedConditions conditions{
        applied_.source().ExpectRendererViewport(contents, pixels, scale, false,
                                                 true),
        applied_.View(viewport, {.bounds = viewport.bounds()},
                      yee::test::AppliedMatch::kViewBounds)};
    const auto presented =
        applied_.views().ObservePresentedAfterApply(viewport, conditions);
    conditions.push_back(presented);
    return Await(std::move(conditions));
  }

  yee::test::AppliedCondition SurfaceTransition(BrowserView& view,
                                                bool fullscreen,
                                                bool toolbar) {
    const auto owner = applied_.source().ObserveSurfaceEpoch(view);
    const auto before = applied_.source().Read(
        {owner, yee::test::AppliedConditionKind::kSurfaceEpochCommitted});
    auto expected = std::get<yee::test::SurfaceEpochCommitted>(before->value);
    using Destination = yee::test::SurfaceEpochCommitted::Destination;
    expected.destination = !fullscreen ? Destination::kWindowed
                           : toolbar   ? Destination::kImmersive
                                       : Destination::kFullscreen;
    // 현재 epoch를 baseline으로 보관한다. 다음 제품 generation을 추측하지
    // 않는다.
    return applied_.Condition(owner, expected,
                              yee::test::AppliedMatch::kSurfaceDestination,
                              yee::test::AppliedFreshness::kNewApply);
  }
  yee::test::AppliedConditions WindowedConditions(BrowserView& view) {
    auto conditions = PageConditions(true);
    conditions.push_back(SurfaceTransition(view, false, false));
    conditions.push_back(applied_.View(
        *view.top_container(), {.parent = applied_.views().ObserveView(view)},
        yee::test::AppliedMatch::kViewParent));
    conditions.push_back(applied_.View(*view.toolbar(), {.drawn = true},
                                       yee::test::AppliedMatch::kViewDrawn));
    conditions.push_back(applied_.View(
        *view.toolbar(),
        {.bounds = gfx::Rect(0, 0, 0, yee::kSidebarMetrics.toolbar_height)},
        yee::test::AppliedMatch::kViewHeight));
    conditions.push_back(applied_.View(
        *view.top_container(),
        {.bounds = gfx::Rect(
             0, 0, 0, yee::kSidebarMetrics.browser_surface_header_height())},
        yee::test::AppliedMatch::kViewHeight));
    // immersive overlay는 다른 모니터의 DSF를 쓸 수 있다. 복귀 대상인
    // 브라우저의 좌표계로 reparent·DIP 높이를 검사한다.
    const auto destination = yee::test::GetAppliedViewCoordinates(
        view, yee::test::AppliedCoordinates::Space::kParent);
    for (auto& condition : conditions) {
      if (std::holds_alternative<yee::test::ViewStateMatches>(
              condition.expected))
        condition.coordinates = destination;
    }
    const auto native = applied_.views().ObserveNativeWindow(*view.GetWidget());
    conditions.push_back(applied_.Condition(
        native, yee::test::NativeWindowMatches{.fullscreen = false},
        yee::test::AppliedMatch::kNativeFullscreen));
    return conditions;
  }
  yee::test::AppliedConditions ResizeConditions(BrowserView& view,
                                                gfx::Rect bounds) {
    auto conditions = PageConditions(true);
    conditions.push_back(
        applied_.Widget(*view.GetWidget(), {.bounds = bounds},
                        yee::test::AppliedMatch::kWidgetSizeNear));
    conditions.push_back(applied_.View(*view.toolbar(), {.drawn = true},
                                       yee::test::AppliedMatch::kViewDrawn));
    return conditions;
  }
  yee::test::AppliedCondition FooterMenuState(views::View& footer,
                                              bool visible) {
    return applied_.Condition(
        applied_.source().ObserveUiState(
            footer, yee::test::AppliedBrowserObservers::UiProbe::kFooterMenu),
        yee::test::ViewStateMatches{.visible = visible},
        yee::test::AppliedMatch::kFooterMenu);
  }
  yee::test::AppliedCondition FooterInkHidden(views::View& footer) {
    return applied_.Condition(
        applied_.source().ObserveUiState(
            footer,
            yee::test::AppliedBrowserObservers::UiProbe::kFooterInkHidden),
        yee::test::ViewStateMatches{.visible = true},
        yee::test::AppliedMatch::kViewVisible);
  }
  yee::test::AppliedOwnerToken ObserveDom(
      content::WebContents& contents,
      yee::test::WebUiAppliedSelectors::Probe probe) {
    base::test::TestFuture<bool> installed;
    auto owner = applied_.source().InstallWebUiObserver(
        *contents.GetPrimaryMainFrame(), {.root = "html", .probe = probe},
        installed.GetCallback());
    // observer 설치 ACK 뒤에만 DOM 요청을 arm한다.
    EXPECT_TRUE(installed.Get());
    return owner;
  }
  yee::test::AppliedCondition DomCondition(yee::test::AppliedOwnerToken owner,
                                           yee::test::WebUiStateMatches value,
                                           yee::test::AppliedMatch match) {
    value.document = owner;
    return applied_.Condition(owner, value, match);
  }
  testing::AssertionResult PresentedFrame(views::Widget& widget) {
    return applied_.WaitForPresentedFrame(widget);
  }
};

class YeeBrowserViewTabbedLayoutImplUiTest : public YeeAppliedSurfaceUiTest {
 public:
  static constexpr char kBaselineCl[] = "7635098";

  explicit YeeBrowserViewTabbedLayoutImplUiTest(
      bool disable_animations = true,
      bool enable_ai_overlay_dialog = false,
      bool enable_vertical_tabs = true,
      bool enable_indigo_overlay = false)
      : render_mode_resetter_(gfx::AnimationTestApi::SetRichAnimationRenderMode(
            disable_animations
                ? gfx::Animation::RichAnimationRenderMode::FORCE_DISABLED
                : gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED)),
        enable_ai_overlay_dialog_(enable_ai_overlay_dialog),
        enable_vertical_tabs_(enable_vertical_tabs),
        enable_indigo_overlay_(enable_indigo_overlay) {
    if (!enable_vertical_tabs_) {
      // 157에서 없어진 기능 플래그 대신 controller 부재를 명시적으로 주입한다.
      vertical_tabs_override_ =
          BrowserWindowFeatures::GetUserDataFactoryForTesting()
              .AddOverrideForTesting(
                  base::BindRepeating([](BrowserWindowInterface&) {
                    return std::unique_ptr<
                        tabs::VerticalTabStripStateController>();
                  }));
    }
  }

  ~YeeBrowserViewTabbedLayoutImplUiTest() override = default;

  DECLARE_CLASS_ELEMENT_IDENTIFIER_VALUE(kSidePanelTestElement);

  void SetUp() override {
    if (!enable_vertical_tabs_) {
      feature_list_.InitWithFeatures({features::kSidePanelFlyoverAnimation},
                                     {features::kAiOverlayDialog});
    } else if (enable_ai_overlay_dialog_) {
      feature_list_.InitWithFeatures(
          {features::kSidePanelFlyoverAnimation, features::kAiOverlayDialog},
          {});
    } else if (enable_indigo_overlay_) {
      feature_list_.InitWithFeatures(
          {features::kSidePanelFlyoverAnimation, features::kIndigo}, {});
    } else {
      feature_list_.InitWithFeatures({features::kSidePanelFlyoverAnimation},
                                     {});
    }
    set_open_about_blank_on_browser_launch(true);
    InteractiveBrowserTest::SetUp();
  }

  void TearDown() override {
    gfx::Animation::SetPrefersReducedMotionForTesting(false);
    InteractiveBrowserTest::TearDown();
  }

  void SetUpOnMainThread() override {
    InteractiveBrowserTest::SetUpOnMainThread();
    auto* const vertical_tabs =
        tabs::VerticalTabStripStateController::From(browser());
    if (enable_vertical_tabs_) {
      ASSERT_TRUE(vertical_tabs);
      vertical_tabs->SetVerticalTabsEnabled(true);
    } else {
      ASSERT_FALSE(vertical_tabs);
    }
    chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  }

  auto ReplaceAndShowSidePanel(
      SidePanelType type,
      std::optional<int> force_preferred_width = std::nullopt,
      gfx::AnimationContainer* animation_clock = nullptr) {
    return Steps(
        Do([this, type, animation_clock]() {
          // 브라우저와 탭에 각각 registry가 있으므로 현재 탭의 것을 쓴다.
          auto* const registry =
              SidePanelRegistry::From(browser()->GetActiveTabInterface());
          // 일반 패널 배치는 맞춤설정 전용 Header 정책과 구분한다.
          const auto key =
              SidePanelEntry::Key(SidePanelEntry::Id::kReadingList);
          CHECK(!registry->GetEntryForKey(key) || registry->Deregister(key));
          CHECK(registry->Register(std::make_unique<SidePanelEntry>(
              type, key, base::BindRepeating([](SidePanelEntryScope&) {
                auto content_view = std::make_unique<views::View>();
                content_view->SetLayoutManager(
                    std::make_unique<views::FillLayout>());
                auto label =
                    std::make_unique<views::Label>(u"Side Panel Ready");
                label->SetProperty(views::kElementIdentifierKey,
                                   kSidePanelTestElement);
                content_view->AddChildView(std::move(label));
                return content_view;
              }),
              /*default_content_width_callback=*/base::NullCallback())));
          SidePanelUI::From(browser())->Show(SidePanelEntry::Id::kReadingList);
          // 새 모션은 runner를 다시 시작하므로 이벤트 루프에 돌아가기 전에
          // 수동 시계를 멈춘다.
          if (animation_clock) {
            gfx::AnimationContainerTestApi pause(animation_clock);
          }
        }),
        WaitForShow(kSidePanelElementId),
        If(
            [force_preferred_width] {
              return force_preferred_width.has_value();
            },
            Then(WithView(kSidePanelElementId,
                          [force_preferred_width](SidePanel* side_panel) {
                            side_panel->SetPanelWidth(*force_preferred_width);
                            side_panel->InvalidateLayout();
                          }),
                 Do([this] { ApplyScheduledLayout(); }))),
        // 패널 준비 표식이 나타날 때까지 기다린다.
        WaitForShow(kSidePanelTestElement),
        // 브라우저 전체 레이아웃을 적용한다.
        Do([this] { ApplyScheduledLayout(); }));
  }

  int GetSidePanelGrowHeadroomInDips() {
    auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
    auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
        view->GetBrowserViewLayoutForTesting());
    CHECK(layout->last_native_layout_for_testing());
    const auto& plan =
        layout->last_native_layout_for_testing()->horizontal_plan;
    EXPECT_TRUE(plan.has_side_panel());
    EXPECT_TRUE(
        plan.row_mode ==
            browser_view_layout::NativeSidePanelRowMode::kSharedRow ||
        plan.row_mode ==
            browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel);
    // Header가 위에 있어도 content column의 패널 배분식은 같다.
    // Yee rail은 column 밖에 있다. Native가 정한 column과 최소 폭을 읽는다.
    return plan.content_column_params.visual_client_area.width() -
           plan.min_content_width - plan.side_panel_padding -
           view->side_panel()->width();
  }

  auto ToggleVerticalTabStripCollapsed(bool should_be_collapsed) {
    auto steps = Steps(
        PressButton(kVerticalTabStripCollapseButtonElementId),
        WaitForEvent(kTabStripRegionElementId,
                     VerticalTabStripRegionView::kAnimationCompletedEvent),
        Do([this]() { ApplyScheduledLayout(); }),
        CheckView(
            kTabStripRegionElementId,
            [](VerticalTabStripRegionView* region) {
              return region->GetTabStripCollectionController()
                  ->GetStateController()
                  ->IsCollapsed();
            },
            should_be_collapsed));
    AddDescriptionPrefix(
        steps, base::StringPrintf("ToggleVerticalTabStripCollapsed(%s)",
                                  should_be_collapsed ? "true" : "false"));
    return steps;
  }

  auto SaveBounds(ElementSpecifier spec,
                  TemporaryIdentifier<gfx::Rect> temp_id) {
    return WithElement(spec, [this, temp_id](ui::TrackedElement* el) {
      SetTemporaryValue(temp_id, el->GetScreenBounds());
    });
  }

  auto CheckContains(TemporaryIdentifier<gfx::Rect> rect,
                     ElementSpecifier spec) {
    return CheckElement(
               spec,
               [=, this](ui::TrackedElement* el) {
                 return GetTemporaryValue(rect).Contains(el->GetScreenBounds());
               })
        .AddDescriptionPrefix(base::StringPrintf("CheckContains(%s)",
                                                 rect.identifier().GetName()));
  }

  auto VerifyLayout() {
    INTERACTIVE_TEST_TEMPORARY_VALUE(gfx::Rect, kBrowserBounds);
    INTERACTIVE_TEST_TEMPORARY_VALUE(gfx::Rect, kSidePanelBounds);
    return Steps(
        SaveBounds(kBrowserViewElementId, kBrowserBounds),
        SaveBounds(kSidePanelElementId, kSidePanelBounds),
        CheckContains(kBrowserBounds, kSidePanelElementId),
        CheckContains(kBrowserBounds, ToolbarView::kToolbarElementId),
        CheckContains(kBrowserBounds, kMultiContentsViewElementId),
        CheckElement(ToolbarView::kToolbarElementId,
                     [=, this](ui::TrackedElement* el) {
                       return !GetTemporaryValue(kSidePanelBounds)
                                   .Intersects(el->GetScreenBounds());
                     }),
        CheckView(ToolbarView::kToolbarElementId, [=](ToolbarView* toolbar) {
          LOG(INFO) << "Toolbar minimum size is "
                    << toolbar->GetMinimumSize().ToString();
          return toolbar->width() >= toolbar->GetMinimumSize().width();
        }).SetDescription("Toolbar가 최소 크기 이상이다"));
  }

  auto CloseSidePanel() {
    return Steps(PressButton(kSidePanelCloseButtonElementId),
                 WaitForHide(kSidePanelElementId),
                 Do([this]() { ApplyScheduledLayout(); }));
  }

  static int GetCornerScreenshotSize() {
    return GetLayoutConstant(LayoutConstant::kToolbarCornerRadius);
  }

  template <typename F>
  auto ScreenshotSubregion(ui::ElementSpecifier spec,
                           const std::string& name,
                           F&& func) {
    INTERACTIVE_TEST_TEMPORARY_VALUE(gfx::Rect, kSubregion);
    return Steps(
        WithElement(spec,
                    [=, this](ui::TrackedElement* el) {
                      SetTemporaryValue(kSubregion, el->GetScreenBounds());
                    })
            .AddDescriptionPrefix("요소 경계 읽기"),
        // 본문 영역을 캡처할 때 세로 탭바와 겹치는 부분은 제외한다.
        // 본문 바탕을 탭바 아래로 밀지 않게 되면 이 보정도 없앨 수 있다.
        IfView(
            kBrowserViewElementId,
            [spec](const BrowserView* browser_view) {
              return spec == BrowserViewLayoutViews::
                                 kMainBackgroundRegionElementId &&
                     browser_view->ShouldDrawVerticalTabStrip();
            },
            Then(WithElement(
                kTabStripRegionElementId,
                [=, this](ui::TrackedElement* el) {
                  gfx::Rect rect = GetTemporaryValue(kSubregion);
                  const gfx::Rect tab_strip_bounds = el->GetScreenBounds();
                  rect.Inset(gfx::Insets::TLBR(
                      0, std::max(0, tab_strip_bounds.right() - rect.x()), 0,
                      0));
                  SetTemporaryValue(kSubregion, rect);
                }))),
        WithView(
            kBrowserViewElementId,
            [=, this,
             func = std::forward<F>(func)](BrowserView* browser_view) mutable {
              gfx::Rect bounds = GetTemporaryValue(kSubregion);
              bounds = std::move(
                           ui::test::internal::MaybeBind(std::forward<F>(func)))
                           .Run(bounds);
              bounds.Intersect(browser_view->GetBoundsInScreen());
              SetTemporaryValue(kSubregion, views::View::ConvertRectFromScreen(
                                                browser_view, bounds));
            })
            .AddDescriptionPrefix("clip 함수 적용"),
        // 캡처 영역이 요소 경계를 벗어날 수 있으므로 BrowserView를 대상으로
        // 한다.
        Screenshot(kBrowserViewElementId, name, kBaselineCl,
                   [=, this]() { return GetTemporaryValue(kSubregion); }));
  }

  auto ScreenshotLeft(ui::ElementSpecifier spec,
                      const std::string& name,
                      int outset) {
    return ScreenshotSubregion(spec, name, [outset](gfx::Rect rect) {
      rect.set_width(GetCornerScreenshotSize());
      rect.Outset(outset);
      return rect;
    });
  }

  auto ScreenshotRight(ui::ElementSpecifier spec,
                       const std::string& name,
                       int outset) {
    return ScreenshotSubregion(spec, name, [outset](gfx::Rect rect) {
      rect.set_x(rect.right() - GetCornerScreenshotSize());
      rect.set_width(GetCornerScreenshotSize());
      rect.Outset(outset);
      return rect;
    });
  }

  auto ScreenshotTop(ui::ElementSpecifier spec,
                     const std::string& name,
                     int outset) {
    return ScreenshotSubregion(spec, name, [outset](gfx::Rect rect) {
      rect.set_height(GetCornerScreenshotSize());
      rect.Outset(outset);
      return rect;
    });
  }

  auto ScreenshotBottom(ui::ElementSpecifier spec,
                        const std::string& name,
                        int outset) {
    return ScreenshotSubregion(spec, name, [outset](gfx::Rect rect) {
      rect.set_y(rect.bottom() - GetCornerScreenshotSize());
      rect.set_height(GetCornerScreenshotSize());
      rect.Outset(outset);
      return rect;
    });
  }

  auto ScreenshotAround(ui::ElementSpecifier spec,
                        const std::string& name,
                        int overlap) {
    return ScreenshotSubregion(spec, name, [overlap](gfx::Rect rect) {
      rect.Outset(overlap);
      return rect;
    });
  }

  auto ScreenshotUpperLeft(ui::ElementSpecifier spec,
                           const std::string& name,
                           int overlap) {
    return ScreenshotSubregion(spec, name, [overlap](gfx::Rect rect) {
      rect.Offset(-overlap, -overlap);
      const int size = GetCornerScreenshotSize() + overlap;
      rect.set_size(gfx::Size(size, size));
      return rect;
    });
  }

  auto ScreenshotUpperRight(ui::ElementSpecifier spec,
                            const std::string& name,
                            int overlap) {
    return ScreenshotSubregion(spec, name, [overlap](gfx::Rect rect) {
      rect.set_x(rect.right() - GetCornerScreenshotSize());
      rect.set_y(rect.y() - overlap);
      const int size = GetCornerScreenshotSize() + overlap;
      rect.set_size(gfx::Size(size, size));
      return rect;
    });
  }

  auto ScreenshotLowerRight(ui::ElementSpecifier spec,
                            const std::string& name,
                            int overlap) {
    return ScreenshotSubregion(spec, name, [overlap](gfx::Rect rect) {
      rect.set_x(rect.right() - GetCornerScreenshotSize());
      rect.set_y(rect.bottom() - GetCornerScreenshotSize());
      const int size = GetCornerScreenshotSize() + overlap;
      rect.set_size(gfx::Size(size, size));
      return rect;
    });
  }

  auto ScreenshotLowerLeft(ui::ElementSpecifier spec,
                           const std::string& name,
                           int overlap) {
    return ScreenshotSubregion(spec, name, [overlap](gfx::Rect rect) {
      rect.set_x(rect.x() - overlap);
      rect.set_y(rect.bottom() - GetCornerScreenshotSize());
      const int size = GetCornerScreenshotSize() + overlap;
      rect.set_size(gfx::Size(size, size));
      return rect;
    });
  }

  auto ResizeToMinimumWidth() {
    auto steps = Steps(
        WithView(kBrowserViewElementId,
                 [=](BrowserView* browser_view) {
                   auto* const widget = browser_view->GetWidget();
                   widget->SetSize(gfx::Size(widget->GetMinimumSize().width(),
                                             widget->GetSize().height()));
                 }),
        Do([this]() { ApplyScheduledLayout(); }));
    AddDescriptionPrefix(steps, "ResizeToMinimumWidth");
    return steps;
  }

  void ExpectOneVisibleVerticalTabStripToggle() {
    BrowserView* const browser_view =
        BrowserView::GetBrowserViewForBrowser(browser());
    ASSERT_TRUE(browser_view);
    const auto matching_views =
        views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
            kVerticalTabStripCollapseButtonElementId,
            views::ElementTrackerViews::GetContextForView(browser_view),
            /*require_visible=*/true);
    auto* const active =
        browser_view->multi_contents_view()->GetActiveContentsContainerView();
    EXPECT_EQ(1u, matching_views.size())
        << "popup="
        << browser_view->GetLocationBarView()
               ->GetOmniboxController()
               ->IsPopupOpen()
        << ", active header visible=" << active->mini_toolbar()->GetVisible()
        << ", active header drawn=" << active->mini_toolbar()->IsDrawn();
  }

  void SetBookmarkBarVisible(bool visible = true) {
    PrefService* const prefs = user_prefs::UserPrefs::Get(GetProfile());
    if (base::FeatureList::IsEnabled(
            ntp_features::kNtpSimplificationBookmarkBar)) {
      prefs->SetInteger(
          bookmarks::prefs::kBookmarkBarVisibilityState,
          static_cast<int>(
              visible ? bookmarks::BookmarkBarVisibilityState::kAlwaysShow
                      : bookmarks::BookmarkBarVisibilityState::kAlwaysHide));
    } else {
      prefs->SetBoolean(bookmarks::prefs::kShowBookmarkBar, visible);
    }
  }

 private:
  ui::UserDataFactory::ScopedOverride vertical_tabs_override_;
  base::test::ScopedFeatureList feature_list_;
  const gfx::AnimationTestApi::RenderModeResetter render_mode_resetter_;
  const bool enable_ai_overlay_dialog_;
  const bool enable_vertical_tabs_;
  const bool enable_indigo_overlay_;
};

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(YeeBrowserViewTabbedLayoutImplUiTest,
                                      kSidePanelTestElement);

class YeeBrowserViewTabbedLayoutImplFeatureOffUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest {
 public:
  YeeBrowserViewTabbedLayoutImplFeatureOffUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(
            /*disable_animations=*/true,
            /*enable_ai_overlay_dialog=*/false,
            /*enable_vertical_tabs=*/false) {}
};

class YeeBrowserViewTabbedLayoutImplNativeAnimationUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest {
 public:
  YeeBrowserViewTabbedLayoutImplNativeAnimationUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(
            /*disable_animations=*/false,
            /*enable_ai_overlay_dialog=*/false,
            /*enable_vertical_tabs=*/false) {}
};

class YeeBrowserViewTabbedLayoutImplTabAnimationUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest {
 public:
  YeeBrowserViewTabbedLayoutImplTabAnimationUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(/*disable_animations=*/false) {}
};

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplFeatureOffUiTest,
                       YeeShellWithoutVerticalTabsUsesNativeToolbar) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  EXPECT_FALSE(tabs::VerticalTabStripStateController::From(browser()));
  EXPECT_FALSE(browser_view->ShouldDrawVerticalTabStrip());
  EXPECT_TRUE(browser_view->toolbar()->GetVisible());
  EXPECT_EQ(browser_view->toolbar(),
            browser_view->toolbar()->location_bar_view()->parent());

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  EXPECT_EQ(gfx::Insets::TLBR(0, MultiContentsView::kSplitViewContentInset,
                              MultiContentsView::kSplitViewContentInset,
                              MultiContentsView::kSplitViewContentInset),
            multi_contents_view->split_view_insets_for_testing());

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();
  ASSERT_TRUE(multi_contents_view->IsInSplitView());
  EXPECT_EQ(browser_view->toolbar(),
            browser_view->toolbar()->location_bar_view()->parent());
  for (int index = 0; index < 2; ++index) {
    MultiContentsViewMiniToolbar* const mini_toolbar =
        multi_contents_view->mini_toolbar_for_testing(index);
    ASSERT_TRUE(mini_toolbar);
    EXPECT_FALSE(mini_toolbar->has_yee_navigation_controls_for_testing());
    EXPECT_FALSE(mini_toolbar->yee_sidebar_button_for_testing());
  }
  MultiContentsResizeArea* const resize_area =
      multi_contents_view->resize_area_for_testing();
  ASSERT_TRUE(resize_area);
  ASSERT_EQ(1u, resize_area->children().size());
  ASSERT_TRUE(resize_area->children().front()->layer());
  EXPECT_EQ(1.0f, resize_area->children().front()->layer()->opacity());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplFeatureOffUiTest,
                       NativeSidePanelRowModesMatchAppliedLayout) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);

  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar));
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& shared = *layout->last_native_layout_for_testing();
  EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kSharedRow,
            shared.horizontal_plan.row_mode);
  EXPECT_EQ(1, shared.top_container_calculation_count);
  EXPECT_EQ(browser_view->side_panel()->bounds(),
            shared.side_panel.current_bounds);
  EXPECT_EQ(browser_view->top_container()->bounds(),
            shared.top_container_bounds);
  EXPECT_EQ(browser_view->multi_contents_view()->bounds(),
            shared.multi_contents_bounds);
  EXPECT_EQ(browser_view->side_panel()->clip_to_visible_area_for_testing(),
            shared.side_panel.local_clip_bounds);

  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kContent));
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& forced = *layout->last_native_layout_for_testing();
  EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel,
            forced.horizontal_plan.row_mode);
  EXPECT_EQ(1, forced.top_container_calculation_count);
  EXPECT_EQ(browser_view->side_panel()->bounds(),
            forced.side_panel.current_bounds);
  EXPECT_EQ(browser_view->top_container()->bounds(),
            forced.top_container_bounds);
  EXPECT_EQ(browser_view->multi_contents_view()->bounds(),
            forced.multi_contents_bounds);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplNativeAnimationUiTest,
                       MidAnimationTypeAndWidthSwitchUseFreshPlans) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);

  auto* const animation_controller =
      BrowserAnimationController::From(browser());
  auto animation_container = base::MakeRefCounted<gfx::AnimationContainer>();
  animation_controller->SetAnimationContainerForTesting(
      SidePanelAnimations::kSidePanel, animation_container.get());

  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar, std::nullopt,
                                          animation_container.get()));
  gfx::AnimationContainerTestApi(animation_container.get())
      .IncrementTime(base::Milliseconds(100));
  ApplyScheduledLayout();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kSharedRow,
            layout->last_native_layout_for_testing()->horizontal_plan.row_mode);
  const int shared_panel_x =
      layout->last_native_layout_for_testing()->side_panel.current_bounds.x();

  browser_view->side_panel()->SetCurrentEntryType(SidePanelType::kContent);
  browser_view->InvalidateLayout();
  ApplyScheduledLayout();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel,
            layout->last_native_layout_for_testing()->horizontal_plan.row_mode);
  EXPECT_EQ(
      browser_view->side_panel()->bounds(),
      layout->last_native_layout_for_testing()->side_panel.current_bounds);
  EXPECT_EQ(1, layout->last_native_layout_for_testing()
                   ->top_container_calculation_count);

  browser_view->side_panel()->SetCurrentEntryType(SidePanelType::kToolbar);
  RunTestSequence(ResizeToMinimumWidth());
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  // 패널 애니메이션 중에는 native 최소 크기가 행 전환 임계값의 어느 쪽에도
  // 놓일 수 있다. 정확한 임계값은 planner 단위 검사에 맡기고 새 폭 계획의
  // 적용 여부를 확인한다.
  EXPECT_EQ(
      browser_view->side_panel()->bounds(),
      layout->last_native_layout_for_testing()->side_panel.current_bounds);
  EXPECT_NE(
      shared_panel_x,
      layout->last_native_layout_for_testing()->side_panel.current_bounds.x());
}

// 패널을 연 뒤 최소 창 크기로 줄여도 content-height 모드로 바뀌지 않는다.
// 이 모드는 패널을 여는 시점에 이미 창이 너무 작을 때만 사용한다.
// 배경: https://crbug.com/491484034

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       TestMinimumWindowSizeWhenSidePanelIsOpen) {
  gfx::Rect toolbar_bounds_in_screen;
  RunTestSequence(
      SelectTab(kBrowserViewElementId, 0),
      ReplaceAndShowSidePanel(SidePanelType::kToolbar), ResizeToMinimumWidth(),
      WithView(ToolbarView::kToolbarElementId,
               [&](views::View* toolbar) {
                 toolbar_bounds_in_screen = toolbar->GetBoundsInScreen();
               }),
      CheckView(kSidePanelElementId, [&](views::View* side_panel) {
        return side_panel->GetBoundsInScreen().y() <
               toolbar_bounds_in_screen.bottom();
      }));
}

#if BUILDFLAG(IS_WIN)
class YeeWindowsLivePlaybackUiTest : public YeeAppliedSurfaceUiTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    YeeAppliedSurfaceUiTest::SetUpInProcessBrowserTestFixture();
    base::CommandLine::ForCurrentProcess()->RemoveSwitch(
        switches::kDomAutomationController);
  }

  void SetUpOnMainThread() override {
    YeeAppliedSurfaceUiTest::SetUpOnMainThread();
    host_resolver()->AllowDirectLookup("*");
  }
};

IN_PROC_BROWSER_TEST_F(YeeWindowsLivePlaybackUiTest,
                       DISABLED_PublicLivePlaybackShowsWholeWindow) {
  const auto capture = base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
      "yee-live-window-capture");
  ASSERT_FALSE(capture.empty());
  const GURL url("https://www.earthcam.com/usa/newyork/timessquare/?cam=tstwo_hd2");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* const contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(url, contents->GetLastCommittedURL());
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  constexpr int kPlaybackWorld = content::ISOLATED_WORLD_ID_CONTENT_END;

  // 같은 영상에서 재생 시각, 표시 프레임, 라이브 경계가 함께 진행해야 한다.
  const auto playback = content::EvalJs(contents, R"(
    new Promise(resolve => {
      const listeners = new AbortController();
      let source, mediaUrl, frame;
      let startTime, startFrames, startEdge, lastEdge, edgeUpdates;
      const finish = result => {
        listeners.abort();
        if (source) source.cancelVideoFrameCallback(frame);
        resolve(result);
      };
      const sample = () => {
        if (!source.paused && !source.error && source.seekable.length &&
            source.currentSrc === mediaUrl && document.visibilityState === 'visible') {
          const frames = source.getVideoPlaybackQuality();
          const presented = frames.totalVideoFrames - frames.droppedVideoFrames;
          const edge = source.seekable.end(source.seekable.length - 1);
          if (startTime === undefined) {
            startTime = source.currentTime;
            startFrames = presented;
            startEdge = lastEdge = edge;
            edgeUpdates = 0;
          }
          if (edge > lastEdge + 0.5) {
            ++edgeUpdates;
            lastEdge = edge;
          }
          const seconds = source.currentTime - startTime;
          const advancedFrames = presented - startFrames;
          if (seconds >= 1 && advancedFrames >= 15 && edgeUpdates >= 2) {
            globalThis.yeeLiveTestVideo = source;
            finish({mediaUrl, seconds,
                    frames: advancedFrames, edgeAdvance: edge - startEdge,
                    edgeUpdates, time: source.currentTime, presented, edge});
            return;
          }
        }
        frame = source.requestVideoFrameCallback(sample);
      };
      const observe = () => {
        const video = [...document.querySelectorAll('video')].find(candidate => {
          const bounds = candidate.getBoundingClientRect();
          return candidate.currentSrc && !candidate.error &&
              candidate.readyState >= 2 && candidate.videoWidth > 0 &&
              candidate.videoHeight > 0 && bounds.width > 0 && bounds.height > 0 &&
              bounds.right > 0 && bounds.bottom > 0 && bounds.top < innerHeight &&
              bounds.left < innerWidth;
        });
        if (!video || (video === source && video.currentSrc === mediaUrl)) return;
        if (source) source.cancelVideoFrameCallback(frame);
        source = video;
        mediaUrl = video.currentSrc;
        startTime = undefined;
        frame = video.requestVideoFrameCallback(sample);
        if (video.paused) video.play().catch(() => {});
      };
      for (const event of ['loadeddata', 'playing', 'resize', 'durationchange']) {
        document.addEventListener(event, observe,
                                  {capture: true, signal: listeners.signal});
      }
      AbortSignal.timeout(40000).addEventListener('abort', () => finish(null),
                                                   {signal: listeners.signal});
      observe();
    })
  )", content::EXECUTE_SCRIPT_NO_USER_GESTURE, kPlaybackWorld);
  ASSERT_TRUE(playback.is_dict()) << playback;
  const auto& progress = playback.ExtractDict();
  ASSERT_TRUE(progress.FindDouble("seconds") && progress.FindInt("frames") &&
              progress.FindDouble("edgeAdvance") &&
              progress.FindInt("edgeUpdates") &&
              progress.FindString("mediaUrl") && progress.FindDouble("time") &&
              progress.FindInt("presented") && progress.FindDouble("edge"));
  RecordProperty("LivePlaybackSeconds",
                 base::StringPrintf("%.3f", *progress.FindDouble("seconds")));
  RecordProperty("LivePresentedFrames", progress.FindInt("frames").value());
  RecordProperty("LiveEdgeAdvanceSeconds",
                 base::StringPrintf("%.3f", *progress.FindDouble("edgeAdvance")));
  RecordProperty("LiveEdgeUpdates", progress.FindInt("edgeUpdates").value());

  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  ASSERT_TRUE(widget->IsActive());
  ASSERT_TRUE(PresentedFrame(*widget));
  auto* const page = view->multi_contents_view()->GetActiveContentsView();
  ASSERT_EQ(contents, page->web_contents());
  auto* const address = view->GetLocationBarView();
  const auto root = widget->GetRootView()->GetBoundsInScreen();
  for (auto* part : {view->tab_strip_view()->GetTabStripView(),
                     static_cast<views::View*>(address),
                     static_cast<views::View*>(page)}) {
    ASSERT_TRUE(part && part->IsDrawn());
    const auto bounds = part->GetBoundsInScreen();
    EXPECT_FALSE(bounds.IsEmpty());
    EXPECT_TRUE(root.Contains(bounds));
  }
  EXPECT_FALSE(address->GetBoundsInScreen().Intersects(page->GetBoundsInScreen()));
  ASSERT_NO_FATAL_FAILURE(CaptureYeeWindowFrame(widget, capture));
  const auto bytes = base::ReadFileToBytes(capture);
  ASSERT_TRUE(bytes);
  const auto bitmap = gfx::PNGCodec::Decode(*bytes);
  const auto window_pixels = widget->GetCompositor()->size();
  ASSERT_FALSE(window_pixels.IsEmpty());
  EXPECT_GE(bitmap.width(), window_pixels.width());
  EXPECT_GE(bitmap.height(), window_pixels.height());
  const auto video_bounds = content::EvalJs(contents, content::JsReplace(R"(
    (() => {
      const video = globalThis.yeeLiveTestVideo;
      if (!video?.isConnected || video.currentSrc !== $1 || video.paused || video.error ||
          video.readyState < 2 || !video.seekable.length ||
          document.visibilityState !== 'visible') return null;
      const frames = video.getVideoPlaybackQuality();
      if (video.currentTime < $2 ||
          frames.totalVideoFrames - frames.droppedVideoFrames < $3 ||
          video.seekable.end(video.seekable.length - 1) < $4) return null;
      const bounds = video.getBoundingClientRect();
      const left = Math.max(bounds.left, 0), top = Math.max(bounds.top, 0);
      const width = Math.min(bounds.right, innerWidth) - left;
      const height = Math.min(bounds.bottom, innerHeight) - top;
      if (width <= 0 || height <= 0) return null;
      const scale = devicePixelRatio;
      return [Math.ceil((left + width * 0.2) * scale),
              Math.ceil((top + height * 0.2) * scale),
              Math.floor(width * 0.6 * scale), Math.floor(height * 0.6 * scale)];
    })()
  )", *progress.FindString("mediaUrl"),
      *progress.FindDouble("time"), *progress.FindInt("presented"),
      *progress.FindDouble("edge")),
      content::EXECUTE_SCRIPT_NO_USER_GESTURE, kPlaybackWorld);
  ASSERT_TRUE(video_bounds.is_list()) << video_bounds;
  const auto& region = video_bounds.ExtractList();
  ASSERT_EQ(4u, region.size());
  gfx::Rect page_rect = page->GetBoundsInScreen();
  page_rect.Offset(-root.x(), -root.y());
  const gfx::Rect page_pixels = gfx::ScaleToEnclosedRect(
      page_rect, widget->GetCompositor()->device_scale_factor());
  const gfx::Rect video_pixels(page_pixels.x() + region[0].GetInt(),
                              page_pixels.y() + region[1].GetInt(),
                              region[2].GetInt(), region[3].GetInt());
  ASSERT_GT(video_pixels.width(), 4);
  ASSERT_GT(video_pixels.height(), 4);
  ASSERT_TRUE(gfx::Rect(bitmap.width(), bitmap.height()).Contains(video_pixels));
  // 캡처에 검은 비디오 자리만 남는 경우를 걸러낸다.
  const SkColor first = bitmap.getColor(video_pixels.x(), video_pixels.y());
  bool has_image = false;
  for (int y = 1; y <= 4; ++y) {
    for (int x = 1; x <= 4; ++x) {
      const int sample_x = video_pixels.x() + (video_pixels.width() - 1) * x / 4;
      const int sample_y = video_pixels.y() + (video_pixels.height() - 1) * y / 4;
      has_image |= bitmap.getColor(sample_x, sample_y) != first;
    }
  }
  EXPECT_TRUE(has_image);
}

class YeeWindowsStartupUiTest : public YeeAppliedSurfaceUiTest {
 public:
  YeeWindowsStartupUiTest() { set_open_about_blank_on_browser_launch(false); }

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    YeeAppliedSurfaceUiTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(startup_dir_.CreateUniqueTempDir());
    const auto page = startup_dir_.GetPath().AppendASCII("yee-startup.html");
    ASSERT_TRUE(base::WriteFile(
        page, "<!doctype html><title>Yee Startup</title><h1>Yee Startup</h1>"));
    startup_url_ = net::FilePathToFileURL(page);
    command_line->AppendArg(startup_url_.spec());
  }

  GURL startup_url_;

 private:
  base::ScopedTempDir startup_dir_;
};

IN_PROC_BROWSER_TEST_F(YeeWindowsStartupUiTest,
                       StartupAndHorizontalTabRoundTrip) {
  using namespace yee::test;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  auto* const model = browser()->tab_strip_model();
  const auto profile_root =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kUserDataDir);
  ASSERT_FALSE(profile_root.empty());
  EXPECT_EQ(profile_root, browser()->GetProfile()->GetPath().DirName());
  ASSERT_EQ(1, model->count());
  auto* const original_contents = model->GetActiveWebContents();
  ASSERT_TRUE(original_contents);
  ASSERT_TRUE(content::WaitForLoadStop(original_contents));
  ASSERT_EQ(startup_url_, original_contents->GetLastCommittedURL());
  EXPECT_EQ(u"Yee Startup", original_contents->GetTitle());
  ASSERT_TRUE(widget->IsVisibleOnScreen());
  ASSERT_TRUE(BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(PresentedFrame(*widget));
  auto* const startup_tabs = view->tab_strip_view()->GetTabStripView();
  auto* const address = view->GetLocationBarView();
  auto* const contents = view->multi_contents_view()->GetActiveContentsView();
  const gfx::Rect root_bounds = widget->GetRootView()->GetBoundsInScreen();
  for (auto* part : {startup_tabs, static_cast<views::View*>(address),
                     static_cast<views::View*>(contents)}) {
    ASSERT_TRUE(part && part->IsDrawn());
    const gfx::Rect bounds = part->GetBoundsInScreen();
    EXPECT_FALSE(bounds.IsEmpty());
    EXPECT_TRUE(root_bounds.Contains(bounds));
  }
  EXPECT_FALSE(
      address->GetBoundsInScreen().Intersects(contents->GetBoundsInScreen()));
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-tabbar-capture-dir");
  if (!captures.empty()) {
    ASSERT_TRUE(base::CreateDirectory(captures));
    CaptureYeeWindowFrame(widget, captures.AppendASCII("startup.png"));
  }

  auto* const layout = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(layout);
  if (view->ShouldDrawVerticalTabStrip()) {
    ASSERT_TRUE(applied_.Run({VerticalIdle()}, [&] {
      layout->SetVerticalTabsEnabled(false);
      ApplyScheduledLayout();
    }));
  }
  ASSERT_FALSE(view->ShouldDrawVerticalTabStrip());
  auto* const strip = view->tab_strip_view()->GetTabStripView();
  ASSERT_TRUE(strip && strip->IsDrawn());
  EXPECT_FALSE(
      strip->GetBoundsInScreen().Intersects(contents->GetBoundsInScreen()));

  const auto owner = applied_.source().ObserveTabModel(*model);
  const auto initial =
      applied_.source().Read({owner, AppliedConditionKind::kTabModelMatches});
  ASSERT_TRUE(initial);
  const auto& original = std::get<TabModelMatches>(initial->value);
  ASSERT_EQ(1u, original.tabs.size());
  auto* const first_tab = GetTabAnchorForModelIndex(view, 0);
  ASSERT_TRUE(first_tab);
  AppliedOwnerToken tab_layout_owner;
  if (auto* legacy =
          views::AsViewClass<TabContainerImpl>(first_tab->parent())) {
    tab_layout_owner =
        applied_.source().ObserveTabContainerAnimation(*legacy, 1);
  } else {
    auto* const unified =
        views::AsViewClass<UnpinnedTabContainerView>(first_tab->parent());
    ASSERT_TRUE(unified && unified->GetLayoutManager());
    tab_layout_owner = applied_.source().ObserveUnifiedAnimation(
        *unified,
        *static_cast<TabCollectionAnimatingLayoutManager*>(
            unified->GetLayoutManager()),
        1);
  }
  const auto tab_layout_idle = applied_.Condition(
      tab_layout_owner, AnimationMatches{.group_id = 1, .running = false});
  const auto click = [&](views::View* target) {
    ASSERT_TRUE(target);
    views::ViewTracker tracker(target);
    ApplyScheduledLayout();
    target = tracker.view();
    ASSERT_TRUE(target && target->IsDrawn() && target->GetWidget() == widget);
    const gfx::Rect bounds = target->GetBoundsInScreen();
    ASSERT_FALSE(bounds.IsEmpty());
    auto point = bounds.CenterPoint();
    ASSERT_TRUE(SendMouseMoveSync(point, widget->GetNativeWindow()));
    target = tracker.view();
    ASSERT_TRUE(target && target->GetWidget() == widget);
    auto* const root = widget->GetRootView();
    views::View::ConvertPointFromScreen(root, &point);
    auto* const hit = root->GetEventHandlerForPoint(point);
    ASSERT_TRUE(target->Contains(hit))
        << "tab or button: " << target->GetClassName()
        << ", target bounds: " << target->GetBoundsInScreen().ToString()
        << ", root point: " << point.ToString()
        << ", hit: " << (hit ? hit->GetClassName() : "none");
    ASSERT_TRUE(SendLeftMouseClickSync(widget->GetNativeWindow()));
    ApplyScheduledLayout();
  };
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(owner, original, AppliedMatch::kNewTabActive),
       tab_layout_idle},
      [&] {
        click(views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
            kNewTabButtonElementId,
            views::ElementTrackerViews::GetContextForWidget(widget), true));
      }));
  ASSERT_EQ(2, model->count());
  auto* const new_tab = model->GetActiveTab();
  const uint64_t new_handle = new_tab->GetHandle().raw_value();
  ASSERT_NE(original.active_tab, new_handle);
  auto* const new_contents = new_tab->GetContents();
  const auto active = [&](uint64_t handle) {
    return applied_.Condition(owner, TabModelMatches{.active_tab = handle},
                              AppliedMatch::kTabActive);
  };
  ASSERT_TRUE(applied_.Run({active(original.active_tab), tab_layout_idle},
                           [&] { click(GetTabAnchorForModelIndex(view, 0)); }));
  EXPECT_EQ(
      original_contents,
      view->multi_contents_view()->GetActiveContentsView()->web_contents());
  ASSERT_TRUE(applied_.Run({active(new_handle), tab_layout_idle},
                           [&] { click(GetTabAnchorForModelIndex(view, 1)); }));
  EXPECT_EQ(
      new_contents,
      view->multi_contents_view()->GetActiveContentsView()->web_contents());
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(owner, original, AppliedMatch::kTabOrder),
       active(original.active_tab)},
      [&] {
        ASSERT_TRUE(
            SendKeyPressSync(browser(), ui::VKEY_W, true, false, false, false));
        ApplyScheduledLayout();
      }));
  ASSERT_EQ(1, model->count());
  EXPECT_EQ(original_contents, model->GetActiveWebContents());
  EXPECT_EQ(startup_url_, original_contents->GetLastCommittedURL());
  EXPECT_EQ(
      original_contents,
      view->multi_contents_view()->GetActiveContentsView()->web_contents());
}

// 각 표시 조합에 새 창을 사용해 앞선 최대화·테마 전환의 입력 상태를 격리한다.
class YeeWindowsCaptionInputUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest,
      public testing::WithParamInterface<std::tuple<bool, bool, bool>> {};

IN_PROC_BROWSER_TEST_P(YeeWindowsCaptionInputUiTest,
                       HoverUsesVisibleControlBounds) {
  const auto [vertical, dark, maximized] = GetParam();
  gfx::ScopedAnimationDurationScaleMode animation_duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(state);
  const auto original_z_order = widget->GetZOrderLevel();
  widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  state->SetVerticalTabsEnabled(vertical);
  theme->SetBrowserColorScheme(dark ? ThemeService::BrowserColorScheme::kDark
                                    : ThemeService::BrowserColorScheme::kLight);
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(
          *widget,
          {.show_state =
               maximized ? yee::test::WidgetStateMatches::ShowState::kMaximized
                         : yee::test::WidgetStateMatches::ShowState::kNormal},
          yee::test::AppliedMatch::kWidgetShowState)},
      [&] {
        if (maximized) {
          widget->Maximize();
        } else {
          widget->Restore();
        }
      }));
  ASSERT_TRUE(([&]() { return widget->IsMaximized() == maximized; })());
  ApplyScheduledLayout();
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  const auto displays = display::Screen::Get()->GetAllDisplays();
  ASSERT_FALSE(displays.empty());
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-caption-input-capture-dir");
  if (!captures.empty()) {
    ASSERT_TRUE(base::CreateDirectory(captures));
  }
  // 배치·테마 전환이 frame view를 교체할 수 있으므로 현재 소유자를
  // 조회한다.
  auto* const frame =
      static_cast<BrowserFrameViewWin*>(view->browser_widget()->GetFrameView());
  ASSERT_TRUE(frame->UsesYeeCaptionColors());
  const auto* const controls = frame->caption_button_container_for_testing();
  const int control_inset =
      yee::kSidebarMetrics.content_gutter +
      (vertical ? yee::kSidebarMetrics.browser_surface_header_control_edge_inset
                : 0);
  EXPECT_EQ(frame->width() - control_inset, controls->bounds().right());
  const auto params = frame->GetBrowserLayoutParams();
  EXPECT_EQ(vertical
                ? yee::kSidebarMetrics.browser_surface_header_control_edge_inset
                : 0,
            params.trailing_exclusion.horizontal_padding);
  for (const auto [id, hit] :
       {std::pair{VIEW_ID_MINIMIZE_BUTTON, HTMINBUTTON},
        std::pair{maximized ? VIEW_ID_RESTORE_BUTTON : VIEW_ID_MAXIMIZE_BUTTON,
                  HTMAXBUTTON},
        std::pair{VIEW_ID_CLOSE_BUTTON, HTCLOSE}}) {
    auto* const button = views::AsViewClass<WindowsCaptionButton>(
        widget->GetRootView()->GetViewByID(id));
    SCOPED_TRACE(base::StringPrintf("vertical=%d dark=%d maximized=%d id=%d",
                                    vertical, dark, maximized, id));
    ASSERT_TRUE(button && button->IsDrawn());
    EXPECT_EQ(gfx::Size(yee::kSidebarMetrics.shell_control_size,
                        yee::kSidebarMetrics.shell_control_size),
              button->size());
    const gfx::Rect bounds = button->GetBoundsInScreen();
    const auto hit_at = [&](gfx::Point point) {
      views::View::ConvertPointFromScreen(frame->parent(), &point);
      return frame->NonClientHitTest(point);
    };
    const auto window = widget->GetNativeWindow();
    const gfx::Point center = bounds.CenterPoint();
    // 중앙과 양쪽 끝까지 보이는 버튼 경계 안에서 실제 입력으로 확인한다.
    for (const gfx::Point inside :
         {center, gfx::Point(bounds.x() + 1, center.y()),
          gfx::Point(bounds.right() - 2, center.y())}) {
      SCOPED_TRACE("inside=" + inside.ToString());
      EXPECT_EQ(hit, hit_at(inside));
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*button, {.hovered = true},
                         yee::test::AppliedMatch::kViewHovered)},
          [&] {
            ASSERT_TRUE(yee::test::SendMouseMoveSync(inside, window));
          },
          yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() {
        return button->GetState() == views::Button::STATE_HOVERED;
      })());
    }
    if (!captures.empty()) {
      CaptureYeeWindowFrame(
          widget,
          captures.AppendASCII(base::StringPrintf(
              "%s-%s-%s-%d-hover.png", vertical ? "sidebar" : "horizontal",
              dark ? "dark" : "light", maximized ? "maximized" : "restored",
              id)));
    }
    // 요소에 이벤트를 직송하지 않고 여백의 실제 좌표로 hit와 hover를
    // 확인한다.
    for (const gfx::Point padding : {gfx::Point(center.x(), bounds.y() - 1),
                                     gfx::Point(center.x(), bounds.bottom()),
                                     gfx::Point(bounds.x() - 1, center.y()),
                                     gfx::Point(bounds.right(), center.y())}) {
      SCOPED_TRACE("padding=" + padding.ToString() + " button=" +
                   bounds.ToString() + " frame=" + frame->bounds().ToString());
      const int component = hit_at(padding);
      EXPECT_NE(HTMINBUTTON, component);
      EXPECT_NE(HTMAXBUTTON, component);
      EXPECT_NE(HTCLOSE, component);
      // 정수 DIP 입력이 소수 배율에서 경계 안쪽 픽셀로 반올림되지 않게
      // trailing 쪽도 1 DIP 밖에서 실제 hover를 검사한다. 위의 정확한 경계
      // hit-test는 그대로 유지한다.
      gfx::Point mouse_padding = padding;
      if (padding.x() == bounds.right()) {
        mouse_padding.Offset(1, 0);
      }
      if (padding.y() == bounds.bottom()) {
        mouse_padding.Offset(0, 1);
      }
      // 최대화된 창 위쪽처럼 모니터 밖인 좌표는 OS가 화면 안으로 보정한다.
      // 논리 hit-test는 유지하고 실제 마우스는 도달 가능한 화면점만 검사한다.
      if (!std::ranges::any_of(displays, [&](const display::Display& display) {
            return display.bounds().Contains(mouse_padding);
          })) {
        continue;
      }
      ASSERT_TRUE(
          applied_.Run({applied_.View(*button, {.hovered = false},
                                      yee::test::AppliedMatch::kViewHovered)},
                       [&] {
                         ASSERT_TRUE(yee::test::SendMouseMoveSync(
                             mouse_padding, window));
                       }));
      ASSERT_TRUE(([&]() {
        return button->GetState() == views::Button::STATE_NORMAL;
      })());
    }
  }
  widget->Restore();
  theme->SetBrowserColorScheme(original_scheme);
  widget->SetZOrderLevel(original_z_order);
  ApplyScheduledLayout();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsCaptionButtonsChangeWindowState) {
  using yee::test::AppliedDomain;
  using yee::test::AppliedMatch;
  using ShowState = yee::test::WidgetStateMatches::ShowState;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  const auto native_window = widget->GetNativeWindow();
  ASSERT_TRUE(state);
  ASSERT_FALSE(widget->IsMaximized());
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));

  for (bool vertical : {true, false}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    ApplyScheduledLayout();
    ASSERT_EQ(vertical, view->ShouldDrawVerticalTabStrip());
    for (const auto [id, expected] :
         {std::pair{VIEW_ID_MAXIMIZE_BUTTON, ShowState::kMaximized},
          std::pair{VIEW_ID_RESTORE_BUTTON, ShowState::kNormal},
          std::pair{VIEW_ID_MINIMIZE_BUTTON, ShowState::kMinimized}}) {
      SCOPED_TRACE(id);
      ApplyScheduledLayout();
      auto* const button = widget->GetRootView()->GetViewByID(id);
      ASSERT_TRUE(button && button->IsDrawn());
      ASSERT_TRUE(applied_.Run(
          {applied_.Widget(*widget, {.show_state = expected},
                           AppliedMatch::kWidgetShowState)},
          [&] {
            ASSERT_TRUE(yee::test::SendMouseMoveSync(
                button->GetBoundsInScreen().CenterPoint(), native_window));
            ASSERT_TRUE(yee::test::SendLeftMouseClickSync(native_window));
          },
          AppliedDomain::kWidget));
    }
    // 작업 표시줄을 조작하지 않고 같은 테스트 창을 복귀시킨다.
    ASSERT_TRUE(applied_.Run(
        {applied_.Widget(*widget, {.show_state = ShowState::kNormal},
                         AppliedMatch::kWidgetShowState),
         applied_.Widget(*widget, {}, AppliedMatch::kWidgetReady)},
        [&] {
          widget->Restore();
          ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
        },
        AppliedDomain::kWidget));
  }
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsTaskbarRestoresMinimizedWindow) {
  using namespace yee::test;
  using ShowState = WidgetStateMatches::ShowState;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  const auto window = widget->GetNativeWindow();
  const auto profile_root =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          switches::kUserDataDir);
  ASSERT_FALSE(profile_root.empty());
  ASSERT_EQ(profile_root, browser()->GetProfile()->GetPath().DirName());
  const std::string marker =
      "YeeTaskbarSmoke-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto* const contents = browser()->tab_strip_model()->GetActiveWebContents();
  content::TitleWatcher title(contents, base::ASCIIToUTF16(marker));
  ASSERT_TRUE(content::ExecJs(contents,
                              content::JsReplace("document.title = $1", marker)));
  EXPECT_EQ(base::ASCIIToUTF16(marker), title.WaitAndGetTitle());
  view->UpdateTitleBar();
  ASSERT_TRUE(NameTaskbarTestWindow(window, base::ASCIIToWide(marker)));
  ASSERT_TRUE(BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(PresentedFrame(*widget));
  ApplyScheduledLayout();
  auto* const minimize =
      widget->GetRootView()->GetViewByID(VIEW_ID_MINIMIZE_BUTTON);
  ASSERT_TRUE(minimize && minimize->IsDrawn());
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*widget, {.show_state = ShowState::kMinimized},
                       AppliedMatch::kWidgetShowState)},
      [&] {
        ASSERT_TRUE(SendMouseMoveSync(minimize->GetBoundsInScreen().CenterPoint(),
                                      window));
        ASSERT_TRUE(SendLeftMouseClickSync(window));
      },
      AppliedDomain::kWidget));
  ASSERT_TRUE(applied_.Run(
      {applied_.Widget(*widget, {.show_state = ShowState::kNormal},
                       AppliedMatch::kWidgetShowState),
       applied_.Widget(*widget, {.active = true}, AppliedMatch::kWidgetActive),
       applied_.Widget(*widget, {}, AppliedMatch::kWidgetReady)},
      [&] { ASSERT_TRUE(RestoreFromTaskbar(window, base::ASCIIToWide(marker))); },
      AppliedDomain::kWidget));
  EXPECT_EQ(window, widget->GetNativeWindow());
  EXPECT_EQ(contents, browser()->tab_strip_model()->GetActiveWebContents());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsTabContextMenuCreatesGroup) {
  using namespace yee::test;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const model = browser()->tab_strip_model();
  ASSERT_EQ(2, model->count());
  auto* const other = model->GetTabAtIndex(0);
  auto* const tab = model->GetTabAtIndex(1);
  ASSERT_TRUE(model->group_model()->ListTabGroups().empty());
  ASSERT_TRUE(BringBrowserWindowToFront(browser()));
  ApplyScheduledLayout();
  const auto owner = applied_.source().ObserveTabModel(*model);
  TabModelMatches tabs;
  tabs.tabs.resize(model->count());
  const auto menu = TabMenuModel::kAddToNewGroupItemIdentifier;

  ASSERT_TRUE(RunTestSequence(
      NameView("TabToGroup", GetTabAnchorForModelIndex(view, 1)),
      CheckView("TabToGroup", [](views::View* target) {
        return SendContextMenuClickSync(*target, NativeContextMenuAction::kOpen);
      }),
      WaitForShow(menu),
      ApplyStepsAndWait(
          applied_,
          [&] {
            return AppliedConditions{applied_.Condition(
                owner, tabs, AppliedMatch::kTabCount,
                AppliedFreshness::kNewApply)};
          },
          Steps(CheckView(menu, [](views::View* target) {
                  return SendContextMenuClickSync(
                      *target, NativeContextMenuAction::kSelect);
                }).SetMustRemainVisible(false))),
      WaitForShow(kTabGroupHeaderElementId)));

  ASSERT_TRUE(tab->GetGroup());
  EXPECT_THAT(model->group_model()->ListTabGroups(),
              testing::ElementsAre(*tab->GetGroup()));
  EXPECT_FALSE(other->GetGroup());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsTabContextMenuCreatesSplit) {
  using namespace yee::test;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const model = browser()->tab_strip_model();
  auto* const panes = view->multi_contents_view();
  ASSERT_EQ(2, model->count());
  auto* const peer = model->GetTabAtIndex(0);
  auto* const active = model->GetActiveTab();
  ASSERT_NE(active, peer);
  ASSERT_FALSE(panes->IsInSplitView());
  ASSERT_TRUE(BringBrowserWindowToFront(browser()));
  ApplyScheduledLayout();
  const auto menu = TabMenuModel::kSplitTabsMenuItem;
  const auto layout = SplitViewLayoutMenuModel::kVerticalMenuItem;
  const auto select = [](views::View* target) {
    return SendContextMenuClickSync(*target, NativeContextMenuAction::kSelect);
  };
  auto select_split =
      tabs::IsSplitViewHorizontalDirectAccessEnabledForTab()
          ? Steps(CheckView(menu, [](views::View* target) {
                    return target->IsDrawn() && target->GetWidget() &&
                           SendMouseMoveSync(
                               target->GetBoundsInScreen().CenterPoint(),
                               target->GetWidget()->GetNativeWindow());
                  }),
                  WaitForShow(layout),
                  CheckView(layout, select).SetMustRemainVisible(false))
          : Steps(CheckView(menu, select).SetMustRemainVisible(false));

  ASSERT_TRUE(RunTestSequence(
      NameView("TabToSplit", GetTabAnchorForModelIndex(view, 0)),
      CheckView("TabToSplit", [](views::View* target) {
        return SendContextMenuClickSync(*target, NativeContextMenuAction::kOpen);
      }),
      WaitForShow(menu),
      ApplyStepsAndWait(
          applied_,
          [&] {
            return AppliedConditions{applied_.View(
                *panes->resize_area_for_testing(), {.visible = true},
                AppliedMatch::kViewVisible)};
          },
          std::move(select_split))));

  ASSERT_TRUE(active->GetSplit());
  EXPECT_EQ(active->GetSplit(), peer->GetSplit());
  EXPECT_TRUE(panes->IsInSplitView());
  EXPECT_EQ(2, model->count());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsSidebarGroupEditorAndFirstTabClose) {
  using namespace yee::test;
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const model = browser()->tab_strip_model();
  const auto native_window = view->GetWidget()->GetNativeWindow();
  ASSERT_EQ(2, model->count());
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  const auto group = model->AddToNewGroup({0, 1, 2});
  model->ChangeTabGroupVisuals(
      group, tab_groups::TabGroupVisualData(
                 u"검수 그룹", tab_groups::TabGroupColorId::kBlue, false));
  model->ActivateTabAt(0);
  ApplyScheduledLayout();

  auto* const tracker = views::ElementTrackerViews::GetInstance();
  const auto context = views::ElementTrackerViews::GetContextForView(view);
  const auto group_header = [&] {
    return tracker->GetFirstMatchingView(kTabGroupHeaderElementId, context,
                                         true);
  };
  auto* const header = views::AsViewClass<TabGroupHeaderView>(group_header());
  ASSERT_TRUE(header && header->group() == group);
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  const auto parking =
      view->multi_contents_view()->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(yee::test::SendMouseMoveSync(parking, native_window));
  const auto visible_editors = [&] {
    return tracker->GetAllMatchingViews(kTabGroupEditorBubbleId, context, true);
  };
  EXPECT_TRUE(visible_editors().empty());
  auto* const editor_button = header->editor_bubble_button();
  ASSERT_TRUE(editor_button);
  const auto title_center =
      header->title_label_for_testing()->GetBoundsInScreen().CenterPoint();
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*editor_button, {.visible = true},
                     AppliedMatch::kViewVisible),
       applied_.View(*editor_button, {}, AppliedMatch::kViewCenterVisible)},
      [&] {
        ASSERT_TRUE(yee::test::SendMouseMoveSync(title_center, native_window));
        ApplyScheduledLayout();
      }));
  ASSERT_TRUE(editor_button->IsDrawn());
  // 창의 활성 상태는 bubble의 표시 여부와 독립적이다. 포인터가 Header를
  // 떠난 뒤에도 편집 버튼과 실제 bubble이 표시되는지 확인한다.
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*header, {.hovered = false}, AppliedMatch::kViewHovered),
       applied_.View(*editor_button, {.visible = true},
                     AppliedMatch::kViewVisible)},
      [&] {
        const auto center = editor_button->GetBoundsInScreen().CenterPoint();
        ASSERT_TRUE(yee::test::SendMouseMoveSync(center, native_window));
        auto* const root = view->GetWidget()->GetRootView();
        auto root_point = center;
        views::View::ConvertPointFromScreen(root, &root_point);
        ASSERT_TRUE(
            editor_button->Contains(root->GetEventHandlerForPoint(root_point)));
        ASSERT_TRUE(yee::test::SendLeftMouseClickSync(native_window));
        ASSERT_TRUE(yee::test::SendMouseMoveSync(parking, native_window));
      }));
  const auto editors = visible_editors();
  ASSERT_EQ(1u, editors.size());
  ASSERT_TRUE(editors.front()->IsDrawn());
  auto* const editor_widget = editors.front()->GetWidget();
  ASSERT_TRUE(editor_widget && editor_widget->IsVisible());
  const auto editor = editor_widget->GetWeakPtr();
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*editor_button, {.visible = false},
                     AppliedMatch::kViewVisible)},
      [&] {
        ASSERT_TRUE(editor && editor->IsVisible());
        ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
            editor->GetNativeWindow(), ui::VKEY_ESCAPE, false, false, false,
            false));
      },
      AppliedDomain::kWidget));
  EXPECT_TRUE(visible_editors().empty());

  const auto owner = applied_.source().ObserveTabModel(*model);
  const auto before =
      applied_.source().Read({owner, AppliedConditionKind::kTabModelMatches});
  ASSERT_TRUE(before);
  auto remaining = std::get<TabModelMatches>(before->value);
  ASSERT_EQ(3u, remaining.tabs.size());
  remaining.tabs.erase(remaining.tabs.begin());
  ASSERT_TRUE(applied_.Run(
      {applied_.Condition(owner, remaining, AppliedMatch::kTabOrder)}, [&] {
        ASSERT_TRUE(yee::test::SendKeyPressSync(browser(), ui::VKEY_W, true,
                                                false, false, false));
      }));
  ASSERT_EQ(2, model->count());
  for (int index = 0; index < model->count(); ++index) {
    EXPECT_EQ(group, model->GetTabGroupForTab(index));
  }
  ApplyScheduledLayout();
  ASSERT_TRUE(group_header());
  EXPECT_TRUE(group_header()->IsDrawn());
}

INSTANTIATE_TEST_SUITE_P(
    CaptionPresentation,
    YeeWindowsCaptionInputUiTest,
    testing::Combine(testing::Bool(), testing::Bool(), testing::Bool()),
    ([](const testing::TestParamInfo<std::tuple<bool, bool, bool>>& info) {
      const auto [vertical, dark, maximized] = info.param;
      return std::string(vertical ? "Sidebar" : "Horizontal") +
             (dark ? "Dark" : "Light") + (maximized ? "Maximized" : "Restored");
    }));

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsSplitThemeChangesKeepCaptionReservations) {
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-split-tab-capture-dir");
  if (!captures.empty()) {
    ASSERT_TRUE(base::CreateDirectory(captures));
    const auto measurements = captures.AppendASCII("viewport-measurements.log");
    ASSERT_FALSE(base::PathExists(measurements));
    ASSERT_TRUE(base::WriteFile(measurements, std::string()));
    const auto header_measurements =
        captures.AppendASCII("header-measurements.log");
    ASSERT_FALSE(base::PathExists(header_measurements));
    ASSERT_TRUE(base::WriteFile(header_measurements, std::string()));
  }
  gfx::Rect window_bounds = view->GetWidget()->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(836, 631));
  view->GetWidget()->SetBounds(window_bounds);
  const GURL pane_a_url(
      "data:text/html,<title>Pane A</title><body style='margin:0;"
      "padding:32px;box-sizing:border-box;background:rgb(242,245,250);"
      "color:rgb(25,55,90);font:20px sans-serif'><h1>Pane A</h1>"
      "<p>Left / top page</p><hr><p>Page content remains inside "
      "its own rounded card during theme changes.</p>"
      "<i style='position:fixed;left:8px;top:8px;width:8px;"
      "height:8px;background:rgb(255,0,255)'></i>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), pane_a_url));
  auto* const pane_a_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(pane_a_contents);
  ASSERT_EQ(pane_a_url, pane_a_contents->GetLastCommittedURL());
  ASSERT_EQ(u"Pane A", pane_a_contents->GetTitle());
  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  const GURL pane_b_url(
      "data:text/html,<title>Pane B</title><body style='margin:0;"
      "padding:32px;box-sizing:border-box;background:rgb(21,24,32);"
      "color:rgb(225,232,245);font:20px sans-serif'><h1>Pane B</h1>"
      "<p>Right / bottom page</p><hr><p>Independent page colors "
      "make clipping and overlap visible.</p>"
      "<i style='position:fixed;left:8px;top:8px;width:8px;"
      "height:8px;background:rgb(0,255,255)'></i>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), pane_b_url));
  auto* const pane_b_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(pane_b_contents);
  ASSERT_NE(pane_a_contents, pane_b_contents);
  ASSERT_EQ(pane_b_url, pane_b_contents->GetLastCommittedURL());
  ASSERT_EQ(u"Pane B", pane_b_contents->GetTitle());
  ApplyScheduledLayout();
  ASSERT_TRUE(view->multi_contents_view()->IsInSplitView());
  const double saved_ratio = view->multi_contents_view()->GetSplitRatio();
  const SkColor pane_a_light = SkColorSetRGB(242, 245, 250);
  const SkColor pane_a_dark = SkColorSetRGB(36, 40, 48);
  const SkColor pane_b_dark = SkColorSetRGB(21, 24, 32);
  const auto pane_a_background =
      applied_.source().ObserveRendererBackground(*pane_a_contents);
  const auto check_header = [&](ContentsContainerView* container,
                                const gfx::Rect& controls) {
    auto* const header = container->mini_toolbar();
    const bool active = container->contents_view() ==
                        view->multi_contents_view()->GetActiveContentsView();
    const auto metrics = header->GetYeeHeaderMetrics(
        active, *view->toolbar()->location_bar_view());
    const auto* const flex =
        static_cast<views::FlexLayout*>(header->GetLayoutManager());
    std::string required;
    for (size_t i = 0; i < metrics.controls.size(); ++i) {
      const auto& control = metrics.controls[i];
      required += base::StringPrintf(
          "slot%zu=%d margin=%s padding=%s; ", i, control.width,
          control.margins ? control.margins->ToString().c_str() : "default",
          control.internal_padding.ToString().c_str());
    }
    const std::string measurement = base::StringPrintf(
        "active=%d header=%s minimum=%s actual_margin=%s "
        "intrinsic_default=%d intrinsic_compact=%d controls=%s\n",
        active, header->GetBoundsInScreen().ToString().c_str(),
        header->GetMinimumSize().ToString().c_str(),
        flex->interior_margin().ToString().c_str(),
        yee::MeasureSplitPaneHeaderMinimumWidth(
            metrics, yee::ResolveSplitPaneHeaderSpacing(active, false)),
        yee::MeasureSplitPaneHeaderMinimumWidth(
            metrics, yee::ResolveSplitPaneHeaderSpacing(active, true)),
        required.c_str());
    SCOPED_TRACE(measurement);
    if (!captures.empty()) {
      EXPECT_TRUE(base::AppendToFile(
          captures.AppendASCII("header-measurements.log"), measurement));
    }
    EXPECT_GE(header->width(), header->GetMinimumSize().width());
    for (views::View* child : header->children()) {
      if (views::AsViewClass<ToolbarButton>(child) ||
          child == header->close_button_for_testing()) {
        const bool sidebar = child->GetProperty(views::kElementIdentifierKey) ==
                             kVerticalTabStripCollapseButtonElementId;
        EXPECT_EQ(!sidebar || active, child->IsDrawn());
      }
      if (!child->IsDrawn()) {
        continue;
      }
      EXPECT_TRUE(header->GetLocalBounds().Contains(child->bounds()));
      const gfx::Rect bounds = views::View::ConvertRectToTarget(
          child, view, child->GetLocalBounds());
      EXPECT_FALSE(bounds.Intersects(controls)) << bounds.ToString();
    }
    EXPECT_DOUBLE_EQ(saved_ratio, view->multi_contents_view()->GetSplitRatio());
  };

  for (bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    for (auto split_layout : {split_tabs::SplitTabLayout::kSideBySide,
                              split_tabs::SplitTabLayout::kStacked}) {
      SCOPED_TRACE(static_cast<int>(split_layout));
      view->multi_contents_view()->UpdateSplitVisualData(
          split_tabs::SplitTabVisualData(split_layout));
      ApplyScheduledLayout();
      for (bool dark : {false, true}) {
        SCOPED_TRACE(dark);
        const SkColor pane_a_color = dark ? pane_a_dark : pane_a_light;
        // 실제 문서의 배경을 바꾸고 renderer가 새 색을 알린 뒤 합성한다.
        ASSERT_TRUE(applied_.Run(
            {applied_.Condition(
                pane_a_background,
                yee::test::PresentationMatches{.use_native_theme = dark},
                yee::test::AppliedMatch::kPresentationDark)},
            [&] {
              ASSERT_TRUE(content::ExecJs(
                  pane_a_contents,
                  content::JsReplace(
                      "Object.assign(document.body.style, "
                      "{backgroundColor: $1, color: $2});",
                      color_utils::SkColorToRgbaString(pane_a_color),
                      dark ? "#e1e8f5" : "#19375a")));
            },
            yee::test::AppliedDomain::kFrame));
        // 테마가 frame을 다시 만들면 소유 창이 전면에 표시된 뒤 합성한다.
        ASSERT_TRUE(applied_.Run(
            {applied_.Widget(*view->GetWidget(), {},
                             yee::test::AppliedMatch::kWidgetReady)},
            [&] {
              theme->SetBrowserColorScheme(
                  dark ? ThemeService::BrowserColorScheme::kDark
                       : ThemeService::BrowserColorScheme::kLight);
              ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
            },
            yee::test::AppliedDomain::kWidget));
        ApplyScheduledLayout();
        const BrowserLayoutParams frame_params =
            view->browser_widget()->GetFrameView()->GetBrowserLayoutParams();
        ASSERT_FALSE(frame_params.IsEmpty());
        ASSERT_TRUE(frame_params.visual_client_area.Contains(view->bounds()));
        const auto params = frame_params.InLocalCoordinates(view->bounds());
        const auto exclusion = params.trailing_exclusion.ContentWithPadding();
        // Native frame의 논리 exclusion을 실제 Header 자식과 같은 물리 좌표로
        // 비교한다.
        const gfx::Rect controls = view->GetMirroredRect(gfx::Rect(
            params.visual_client_area.right() -
                base::ClampCeil(exclusion.width()),
            params.visual_client_area.y(), base::ClampCeil(exclusion.width()),
            base::ClampCeil(exclusion.height())));
        auto* const frame = static_cast<BrowserFrameViewWin*>(
            view->browser_widget()->GetFrameView());
        const auto* const caption =
            frame->caption_button_container_for_testing();
        ASSERT_TRUE(caption && caption->IsDrawn());
        EXPECT_TRUE(controls.Contains(views::View::ConvertRectToTarget(
            caption, view, caption->GetLocalBounds())));
        const SkColor caption_color =
            frame->GetCaptionColor(BrowserFrameActiveState::kUseCurrent);
        SkColor caption_background = yee::ResolveShellBackgroundColor(
            *frame->GetColorProvider(),
            view->GetWidget()->ShouldPaintAsActive());
        if (vertical) {
          // 논리 bounds 변환을 재사용하지 않고 실제 화면 좌표로 색상 소유자를
          // 찾는다. RTL에서는 창 버튼 아래의 Pane Header가 달라진다.
          const gfx::Point caption_point =
              caption->GetBoundsInScreen().CenterPoint();
          bool caption_header_found = false;
          for (const auto& container : view->GetContentsContainerViews()) {
            if (!container->mini_toolbar()->GetBoundsInScreen().Contains(
                    caption_point)) {
              continue;
            }
            EXPECT_FALSE(caption_header_found);
            caption_header_found = true;
            const auto* const binding =
                container->GetYeeBrowserSurfacePresentationBinding();
            ASSERT_TRUE(binding && binding->GetPresentation().has_value());
            EXPECT_EQ(caption_color, binding->GetPresentation()->primary);
            caption_background = binding->GetPresentation()->surface;
          }
          EXPECT_TRUE(caption_header_found);
        }
        EXPECT_GE(
            color_utils::GetContrastRatio(caption_color, caption_background),
            color_utils::kMinimumVisibleContrastRatio);
        for (const auto& container : view->GetContentsContainerViews()) {
          auto* const header = container->mini_toolbar();
          ASSERT_TRUE(header->IsDrawn());
          EXPECT_TRUE(container->GetLocalBounds().Contains(
              container->GetPageViewportGeometry()
                  .current_viewport_in_container));
          auto* const web_view = container->contents_view();
          auto* const contents = web_view->web_contents();
          ASSERT_TRUE(contents);
          ASSERT_TRUE(contents == pane_a_contents ||
                      contents == pane_b_contents);
          const bool is_pane_a = contents == pane_a_contents;
          ASSERT_EQ(is_pane_a ? pane_a_url : pane_b_url,
                    contents->GetLastCommittedURL());
          ASSERT_EQ(is_pane_a ? u"Pane A" : u"Pane B", contents->GetTitle());
          // DOM visualViewport는 스크롤바를 제외하므로 전체 native viewport와
          // 다를 수 있다. 실제 활성화된 renderer 프레임의 전체 크기를 비교한다.
          content::RenderFrameSubmissionObserver frame_observer(contents);
          gfx::Size expected_pixels;
          gfx::Size renderer_pixels;
          ASSERT_TRUE(RendererReady(*contents, *web_view));
          const bool renderer_size_matches = ([&]() {
            expected_pixels = gfx::ScaleToCeiledSize(
                web_view->size(),
                view->GetWidget()->GetCompositor()->device_scale_factor());
            renderer_pixels = frame_observer.LastRenderFrameMetadata()
                                  .viewport_size_in_pixels;
            return renderer_pixels == expected_pixels;
          })();
          const std::string measurement = base::StringPrintf(
              "vertical=%d split_layout=%d dark=%d matched=%d "
              "web_view=%s holder=%s expected_px=%s renderer_frame_px=%s "
              "dsf=%.6f renderer_frame_dsf=%.6f widget_outer=%s "
              "root_dip=%s compositor_px=%s\n",
              vertical, static_cast<int>(split_layout), dark,
              renderer_size_matches,
              web_view->GetBoundsInScreen().ToString().c_str(),
              web_view->holder()->GetBoundsInScreen().ToString().c_str(),
              expected_pixels.ToString().c_str(),
              renderer_pixels.ToString().c_str(),
              view->GetWidget()->GetCompositor()->device_scale_factor(),
              frame_observer.LastRenderFrameMetadata().device_scale_factor,
              view->GetWidget()->GetWindowBoundsInScreen().ToString().c_str(),
              view->GetWidget()->GetRootView()->size().ToString().c_str(),
              view->GetWidget()->GetCompositor()->size().ToString().c_str());
          if (!captures.empty()) {
            EXPECT_TRUE(base::AppendToFile(
                captures.AppendASCII("viewport-measurements.log"),
                measurement));
          }
          ASSERT_TRUE(renderer_size_matches) << measurement;
          content::WaitForCopyableViewInFrame(contents->GetPrimaryMainFrame());
          ASSERT_TRUE(RendererPresented(*contents, *web_view));
          EXPECT_EQ(web_view->GetBoundsInScreen(),
                    contents->GetRenderWidgetHostView()->GetViewBounds());
          check_header(container, controls);
        }
        auto* const location_bar = view->toolbar()->location_bar_view();
        ASSERT_TRUE(location_bar->IsDrawn());
        EXPECT_GE(location_bar->width(),
                  location_bar->GetMinimumSize().width());
        auto* const star =
            views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
                kBookmarkStarViewElementId,
                views::ElementTrackerViews::GetContextForView(view), true);
        for (views::View* control :
             {static_cast<views::View*>(location_bar->location_icon_view()),
              star}) {
          if (control && control->IsDrawn()) {
            EXPECT_TRUE(location_bar->GetLocalBounds().Contains(
                views::View::ConvertRectToTarget(control, location_bar,
                                                 control->GetLocalBounds())));
          }
        }
        // 단색 페이지의 중앙만 검사하면 잘못 이동한 native layer도 통과한다.
        // 각 viewport 원점에 고정한 실제 renderer 표식을 합성 결과에서 읽는다.
        auto* const compositor = view->GetWidget()->GetCompositor();
        // Renderer 크기 확인 뒤에도 이전 native 합성 프레임은 아직 표시될 수
        // 있다.
        compositor->ScheduleFullRedraw();
        ASSERT_TRUE(applied_.WaitForPresentedFrame(*view->GetWidget()));
        base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
        auto request = std::make_unique<viz::CopyOutputRequest>(
            viz::CopyOutputRequest::ResultFormat::RGBA,
            viz::CopyOutputRequest::ResultDestination::kSystemMemory,
            readback.GetCallback());
        compositor->root_layer()->RequestCopyOfOutput(std::move(request));
        compositor->ScheduleFullRedraw();
        auto result = readback.Take();
        ASSERT_TRUE(result);
        ASSERT_FALSE(result->IsEmpty());
        auto pixels = result->ScopedAccessSkBitmap();
        const SkBitmap bitmap = pixels.GetOutScopedBitmap();
        const gfx::Point root_origin =
            view->GetWidget()->GetRootView()->GetBoundsInScreen().origin();
        for (const auto& container : view->GetContentsContainerViews()) {
          auto* const web_view = container->contents_view();
          const auto viewport_origin = web_view->GetBoundsInScreen().origin() -
                                       root_origin.OffsetFromOrigin();
          const auto pixel_at = [&](gfx::Vector2d offset) {
            return gfx::ScaleToFlooredPoint(viewport_origin + offset,
                                            compositor->device_scale_factor());
          };
          const gfx::Rect image_bounds(bitmap.width(), bitmap.height());
          const auto marker = pixel_at(gfx::Vector2d(12, 12));
          const auto background = pixel_at(gfx::Vector2d(24, 24));
          ASSERT_TRUE(image_bounds.Contains(marker));
          ASSERT_TRUE(image_bounds.Contains(background));
          const bool pane_a = web_view->web_contents() == pane_a_contents;
          const SkColor expected =
              pane_a ? SkColorSetRGB(255, 0, 255) : SkColorSetRGB(0, 255, 255);
          EXPECT_EQ(expected, bitmap.getColor(marker.x(), marker.y()))
              << "viewport=" << web_view->GetBoundsInScreen().ToString()
              << " marker_px=" << marker.ToString()
              << " readback_px=" << result->rect().ToString();
          EXPECT_EQ(pane_a ? pane_a_color : pane_b_dark,
                    bitmap.getColor(background.x(), background.y()))
              << "page_background_px=" << background.ToString();
        }
        if (!captures.empty()) {
          CaptureYeeWindowFrame(
              view->GetWidget(),
              captures.AppendASCII(base::StringPrintf(
                  "theme-%s-%s-%s.png", vertical ? "sidebar" : "horizontal",
                  split_layout == split_tabs::SplitTabLayout::kStacked
                      ? "stacked"
                      : "side-by-side",
                  dark ? "dark" : "light")));
        }
      }
    }
  }
  // 같은 좁은 canvas에서 창 버튼을 가진 Pane가 활성/비활성인 양쪽을 검증한다.
  state->SetVerticalTabsEnabled(true);
  view->multi_contents_view()->UpdateSplitVisualData(
      split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide));
  for (const auto& selected : view->GetContentsContainerViews()) {
    auto* const contents = selected->contents_view()->web_contents();
    ASSERT_TRUE(contents && contents->GetDelegate());
    contents->GetDelegate()->ActivateContents(contents);
    ApplyScheduledLayout();
    const auto frame_params =
        view->browser_widget()->GetFrameView()->GetBrowserLayoutParams();
    ASSERT_FALSE(frame_params.IsEmpty());
    const auto params = frame_params.InLocalCoordinates(view->bounds());
    const auto exclusion = params.trailing_exclusion.ContentWithPadding();
    const gfx::Rect controls = view->GetMirroredRect(gfx::Rect(
        params.visual_client_area.right() - base::ClampCeil(exclusion.width()),
        params.visual_client_area.y(), base::ClampCeil(exclusion.width()),
        base::ClampCeil(exclusion.height())));
    for (const auto& container : view->GetContentsContainerViews()) {
      auto* const web_view = container->contents_view();
      content::RenderFrameSubmissionObserver observer(web_view->web_contents());
      ASSERT_TRUE(RendererReady(*web_view->web_contents(), *web_view));
      ASSERT_TRUE(([&]() {
        return observer.LastRenderFrameMetadata().viewport_size_in_pixels ==
               gfx::ScaleToCeiledSize(
                   web_view->size(),
                   view->GetWidget()->GetCompositor()->device_scale_factor());
      })());
      check_header(container, controls);
    }
    EXPECT_GE(view->toolbar()->location_bar_view()->width(),
              view->toolbar()->location_bar_view()->GetMinimumSize().width());
  }
  theme->SetBrowserColorScheme(original_scheme);
  ApplyScheduledLayout();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsHorizontalTabsConnectPageSurfaceToHeader) {
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = view->GetWidget();
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  auto* const model = browser()->tab_strip_model();
  auto* const theme =
      ThemeServiceFactory::GetForProfile(browser()->GetProfile());
  const auto original_scheme = theme->GetBrowserColorScheme();
  state->SetVerticalTabsEnabled(false);
  auto* const source =
      yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
          model->GetActiveWebContents());
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      view->GetBrowserViewLayoutForTesting());
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-tabbar-capture-dir");

  for (bool maximized : {false, true}) {
    SCOPED_TRACE(maximized);
    ASSERT_TRUE(applied_.Run(
        {applied_.Widget(
            *widget,
            {.show_state =
                 maximized
                     ? yee::test::WidgetStateMatches::ShowState::kMaximized
                     : yee::test::WidgetStateMatches::ShowState::kNormal},
            yee::test::AppliedMatch::kWidgetShowState)},
        [&] {
          if (maximized) {
            widget->Maximize();
          } else if (widget->IsMaximized()) {
            widget->Restore();
          }
        }));
    ASSERT_TRUE(([&]() { return widget->IsMaximized() == maximized; })());
    for (bool dark : {false, true}) {
      SCOPED_TRACE(dark);
      theme->SetBrowserColorScheme(
          dark ? ThemeService::BrowserColorScheme::kDark
               : ThemeService::BrowserColorScheme::kLight);
      // 탭 행의 shell 바탕과 페이지 표면을 다르게 해서 얇은 틈도 드러낸다.
      const SkColor surface =
          dark ? SkColorSetRGB(242, 245, 250) : SkColorSetRGB(21, 24, 32);
      source->SetPageSurfaceColorForTesting(surface);
      ApplyScheduledLayout();
      // 모드 전환 뒤 새 collection의 실제 탭이 표시될 때까지 기다린다.
      ASSERT_TRUE(Await(PageConditions()));
      ASSERT_TRUE(([&]() {
        ApplyScheduledLayout();
        auto* active = GetTabAnchorForModelIndex(view, model->active_index());
        return active && active->IsDrawn() &&
               !active->GetVisibleBounds().IsEmpty();
      })());
      auto* const tab = GetTabAnchorForModelIndex(view, model->active_index());
      ASSERT_TRUE(tab);
      ASSERT_TRUE(tab->IsDrawn());
      ASSERT_TRUE(layout->last_native_layout_for_testing());
      const gfx::Rect tab_bounds = tab->GetBoundsInScreen();
      const gfx::Rect header_bounds =
          layout->last_native_layout_for_testing()->header_bounds +
          view->GetBoundsInScreen().OffsetFromOrigin();
      // region의 할당 높이가 아닌 실제 선택 탭의 아래쪽을 비교한다.
      EXPECT_EQ(tab_bounds.bottom() -
                    GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap),
                header_bounds.y());

      content::WaitForCopyableViewInFrame(
          model->GetActiveWebContents()->GetPrimaryMainFrame());
      auto* const compositor = widget->GetCompositor();
      base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
      auto request = std::make_unique<viz::CopyOutputRequest>(
          viz::CopyOutputRequest::ResultFormat::RGBA,
          viz::CopyOutputRequest::ResultDestination::kSystemMemory,
          readback.GetCallback());
      compositor->root_layer()->RequestCopyOfOutput(std::move(request));
      compositor->ScheduleFullRedraw();
      auto result = readback.Take();
      ASSERT_TRUE(result);
      ASSERT_FALSE(result->IsEmpty());
      auto access = result->ScopedAccessSkBitmap();
      const SkBitmap bitmap = access.GetOutScopedBitmap();
      const gfx::Rect root_bounds = widget->GetRootView()->GetBoundsInScreen();
      const int top = tab_bounds.bottom() - 2;
      const int seam_width = std::max(1, tab_bounds.width() / 2);
      const gfx::Rect seam_in_root(
          tab_bounds.CenterPoint().x() - root_bounds.x() - seam_width / 2,
          top - root_bounds.y(), seam_width, 5);
      const gfx::Rect seam_pixels = gfx::ToEnclosingRect(gfx::ScaleRect(
          gfx::RectF(seam_in_root), compositor->device_scale_factor()));
      ASSERT_TRUE(
          gfx::Rect(bitmap.width(), bitmap.height()).Contains(seam_pixels));
      // 기본 선택 탭은 Header의 페이지 면과 연결하고 native 테마는 보존한다.
      const bool native_theme = yee::ShouldUseNativeHorizontalTabTheme(
          *tab->GetNativeTheme(), tab->GetThemeProvider());
      const SkColor tab_surface =
          native_theme ? tab->GetColorProvider()->GetColor(
                             widget->ShouldPaintAsActive()
                                 ? kColorTabBackgroundActiveFrameActive
                                 : kColorTabBackgroundActiveFrameInactive)
                       : surface;
      size_t different_pixels = 0;
      // 두 표면 경계에 걸친 소수 배율의 한 픽셀은 제외하고 완전히 포함된
      // 실제 합성 픽셀을 각각 확인한다.
      for (bool above_header : {true, false}) {
        gfx::Rect sample = seam_in_root;
        // native Toolbar overlap 행은 탭이 위에서 그리므로 탭 표면에 속한다.
        sample.set_y(tab_bounds.bottom() - root_bounds.y() -
                     (above_header ? 2 : 0));
        sample.set_height(above_header ? 2 : 3);
        const gfx::Rect pixels = gfx::ToEnclosedRect(gfx::ScaleRect(
            gfx::RectF(sample), compositor->device_scale_factor()));
        ASSERT_FALSE(pixels.IsEmpty());
        const SkColor expected = above_header ? tab_surface : surface;
        for (int y = pixels.y(); y < pixels.bottom(); ++y) {
          for (int x = pixels.x(); x < pixels.right(); ++x) {
            if (bitmap.getColor(x, y) != expected) {
              ++different_pixels;
            }
          }
        }
      }
      EXPECT_EQ(0U, different_pixels)
          << "탭의 테마색과 Header의 페이지색 사이에 틈이나 잘림이 있음; tab="
          << tab_bounds.ToString() << ", header=" << header_bounds.ToString();
      if (!captures.empty()) {
        CaptureYeeWindowFrame(widget,
                              captures.AppendASCII(base::StringPrintf(
                                  "seam-%s-%s.png", dark ? "dark" : "light",
                                  maximized ? "maximized" : "restored")));
      }
    }
  }
  widget->Restore();
  theme->SetBrowserColorScheme(original_scheme);
  ApplyScheduledLayout();
}
#endif

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeHorizontalTabsPreserveRowsColorsAndTabIdentity) {
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  auto* const model = browser()->tab_strip_model();
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(state);
  ASSERT_TRUE(view);
  const auto capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-tabbar-capture-dir");
  const auto split_capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-split-tab-capture-dir");
  const auto group_marker_capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-group-marker-capture-dir");
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  model->SetTabPinned(0, true);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  const auto group = model->AddToNewGroup({1, 2, 3, 4});
  // 그룹 양쪽 경계를 확인할 수 있도록 뒤에도 비그룹 탭을 유지한다.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  model->ActivateTabAt(4);
  const auto active_handle = model->GetActiveTab()->GetHandle();
  const int count = model->count();
  state->SetVerticalTabsEnabled(false);
  ApplyScheduledLayout();
  ASSERT_TRUE(
      yee::UsesHorizontalTabGroupCapsule(*GetTabAnchorForModelIndex(view, 1)));

  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  gfx::Rect full_header_bounds =
      layout->last_native_layout_for_testing()->header_bounds;
#if BUILDFLAG(IS_WIN)
  gfx::Size full_header_window_size = view->GetWidget()->GetSize();
#endif
  const auto expect_rows = [&](bool has_panel) {
    ApplyScheduledLayout();
    ASSERT_FALSE(view->ShouldDrawVerticalTabStrip());
    EXPECT_EQ(yee::BrowserSurfaceMode::kConnectedTabs,
              yee::GetBrowserSurfaceMode(*browser()));
    ASSERT_TRUE(layout->last_native_layout_for_testing());
    const auto& applied = *layout->last_native_layout_for_testing();
    const gfx::Rect tabs = view->tab_strip_view()->GetBoundsInScreen();
    const gfx::Rect address =
        view->toolbar()->location_bar_view()->GetBoundsInScreen();
    EXPECT_GT(tabs.height(), 0);
    EXPECT_FALSE(tabs.Intersects(address));
    auto* sidebar =
        views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
            kVerticalTabStripCollapseButtonElementId,
            views::ElementTrackerViews::GetContextForView(view), true);
    ASSERT_TRUE(sidebar);
    const gfx::Rect sidebar_bounds = sidebar->GetBoundsInScreen();
    const gfx::Rect header = view->GetMirroredRect(applied.header_bounds) +
                             view->GetBoundsInScreen().OffsetFromOrigin();
    const auto check_group_marker = [&](auto&& self,
                                        views::View* child) -> void {
      if (auto* marker = views::AsViewClass<TabGroupUnderline>(child);
          marker && marker->IsDrawn()) {
        const auto bounds = marker->GetBoundsInScreen();
        EXPECT_TRUE(tabs.Contains(bounds));
        const gfx::Rect native_row =
            GetTabAnchorForModelIndex(view, 1)->GetBoundsInScreen();
        ASSERT_TRUE(yee::UsesHorizontalTabGroupCapsule(*marker));
        EXPECT_EQ(native_row.y() +
                      yee::kSidebarMetrics.horizontal_group_capsule_top_inset,
                  bounds.y());
        EXPECT_LE(bounds.bottom(), header.y());
        if (has_panel) {
          EXPECT_FALSE(
              bounds.Intersects(view->side_panel()->GetBoundsInScreen()));
        }
      }
      for (views::View* descendant : child->children()) {
        self(self, descendant);
      }
    };
    check_group_marker(check_group_marker, view->tab_strip_view());
    EXPECT_LE(base::i18n::IsRTL() ? header.right() - sidebar_bounds.right()
                                  : sidebar_bounds.x() - header.x(),
              yee::kSidebarMetrics.browser_surface_header_control_edge_inset);
    EXPECT_GE(
        view->toolbar()->GetBoundsInScreen().right(),
        header.right() -
            yee::kSidebarMetrics.browser_surface_header_control_edge_inset);
    EXPECT_EQ(applied.header_bounds.y() + view->GetBoundsInScreen().y(),
              tabs.bottom() -
                  GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap));
    EXPECT_GE(view->multi_contents_view()->GetBoundsInScreen().y(),
              address.bottom());
    EXPECT_EQ(count, model->count());
    EXPECT_EQ(active_handle, model->GetActiveTab()->GetHandle());
    EXPECT_TRUE(model->IsTabPinned(0));
    EXPECT_EQ(group, model->GetTabGroupForTab(1));
    if (has_panel) {
      // 연결된 수평 탭바는 전체 폭의 Header를 쓴다. 옆 패널이 선택 탭 아래의
      // Toolbar를 줄이면 안 된다.
      EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel,
                applied.horizontal_plan.row_mode);
      EXPECT_EQ(full_header_bounds, applied.header_bounds);
#if BUILDFLAG(IS_WIN)
      EXPECT_EQ(full_header_window_size, view->GetWidget()->GetSize());
#endif
      // 넓은 resize 입력 영역 뒤에도 공통 backing이 이어져야 한다.
      EXPECT_EQ(applied.header_bounds.x(), applied.main_background_bounds.x());
      EXPECT_EQ(applied.header_bounds.right(),
                applied.main_background_bounds.right());
      for (int id :
           {yee::kSidePanelResizeGutterViewId, yee::kSidePanelSurfaceViewId}) {
        auto* surface = view->GetViewByID(id);
        ASSERT_TRUE(surface);
        EXPECT_TRUE(applied.main_background_bounds.Contains(surface->bounds()));
      }
      EXPECT_GE(view->side_panel()->GetBoundsInScreen().y(), header.bottom());
      EXPECT_FALSE(view->toolbar()->GetBoundsInScreen().Intersects(
          view->side_panel()->GetBoundsInScreen()));
      // bounds·모서리 경로만 검사하면 꺼진 Header 배경을 놓친다.
      // 좁은 본문 바깥인 양쪽 끝에서 실제 native 배경 픽셀을 읽는다.
      auto* const top = view->top_container();
      EXPECT_FALSE(top->clip_path().contains(0.5f, 0.5f));
      EXPECT_FALSE(top->clip_path().contains(top->width() - 0.5f, 0.5f));
      EXPECT_TRUE(top->clip_path().contains(top->width() / 2.f, 0.5f));
      EXPECT_GE(top->clip_path().getBounds().bottom(), top->height());
      gfx::Canvas canvas(top->size(), 1.f, false);
      top->background()->Paint(&canvas, top);
      const SkBitmap bitmap = canvas.GetBitmap();
      ASSERT_TRUE(view->GetYeeHeaderPresentation());
      for (int x : {1, top->width() - 2}) {
        EXPECT_EQ(view->GetYeeHeaderPresentation()->surface,
                  bitmap.getColor(x, top->height() / 2));
        EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(bitmap.getColor(x, 0)));
      }
    }
  };
  expect_rows(false);

  const auto group_header = [&]() {
    return views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
        kTabGroupHeaderElementId,
        views::ElementTrackerViews::GetContextForView(view), true);
  };
  ASSERT_TRUE(group_header());
  const auto vertical_group_header = [&]() -> TabGroupHeaderView* {
    auto* header = views::AsViewClass<TabGroupHeaderView>(group_header());
    return header && header->group() == group ? header : nullptr;
  };
  const auto group_preview_target = [&]() -> views::View* {
    auto* header = vertical_group_header();
    if (!header) {
      return nullptr;
    }
    for (views::View* child : header->children()) {
      const auto icons =
          std::ranges::count_if(child->children(), [](views::View* item) {
            return views::IsViewClass<views::ImageView>(item);
          });
      if (icons == 3 && child->GetVisible()) {
        return child;
      }
    }
    return nullptr;
  };
  const auto group_title_target = [&]() -> views::View* {
    auto* header = vertical_group_header();
    return header ? header->title_label_for_testing() : nullptr;
  };
  const auto input_geometry_ready = [&](const auto& target,
                                        const auto& handler) {
    ApplyScheduledLayout();
    if (!Await(GroupAnimations(group_header())))
      return false;
    return ([&] {
      auto* area = target();
      auto* receiver = handler();
      if (!area || !receiver || !area->GetWidget() ||
          area->GetWidget() != receiver->GetWidget() || !area->IsDrawn() ||
          area->GetBoundsInScreen().IsEmpty() ||
          !area->GetVisibleBounds().Contains(
              area->GetLocalBounds().CenterPoint())) {
        return false;
      }
      for (auto* ancestor = area; ancestor; ancestor = ancestor->parent()) {
        if (TabCollectionAnimatingLayoutManager::IsViewAnimating(ancestor)) {
          return false;
        }
      }
      return true;
    })();
  };
  const auto pointer_matches =
      [&](views::View* area, views::View* receiver,
          const char* property) -> testing::AssertionResult {
    if (!area || !receiver || !area->GetWidget()) {
      return testing::AssertionFailure() << property << ": 현재 대상이 없다";
    }
    auto* const widget = area->GetWidget();
    auto* const root = widget->GetRootView();
    const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
    auto* const hit = root->GetEventHandlerForPoint(
        views::View::ConvertPointFromScreen(root, cursor));
    const std::string observation = base::StringPrintf(
        "cursor=%s area=%s receiver=%s hit=%s active=%d visible=%d",
        cursor.ToString().c_str(), area->GetBoundsInScreen().ToString().c_str(),
        receiver->GetClassName(), hit ? hit->GetClassName() : "null",
        widget->IsActive(), widget->IsVisible());
    RecordProperty(property, observation);
    if (!widget->IsActive() || !widget->IsVisible() || !area->IsDrawn() ||
        !area->GetBoundsInScreen().Contains(cursor) || hit != receiver) {
      return testing::AssertionFailure() << observation;
    }
    return testing::AssertionSuccess();
  };
  const auto group_marker = [&]() -> views::View* {
    auto* const parent = group_header()->parent();
    if (auto* legacy = views::AsViewClass<TabContainerImpl>(parent)) {
      return legacy->GetGroupViews(group)->underline();
    }
    auto* unified = views::AsViewClass<TabGroupView>(parent);
    return unified ? unified->group_line() : nullptr;
  };
  const auto tab_styling = [](views::View* tab) -> const TabStyleViews* {
    if (auto* legacy = views::AsViewClass<Tab>(tab)) {
      return legacy->tab_style_views();
    }
    auto* unified = views::AsViewClass<TabView>(tab);
    return unified ? unified->tab_styling() : nullptr;
  };
  // 현재 native 배경을 캡처하며 실제 레이어나 배치는 바꾸지 않는다.
  const auto capture_group = [&](const std::string& name) {
    if (group_marker_capture_directory.empty()) {
      return;
    }
    ASSERT_TRUE(group_marker());
    auto* const widget = group_marker()->GetWidget();
    const auto path = group_marker_capture_directory.AppendASCII(name + ".png");
    if (widget == view->GetWidget()) {
      CaptureYeeWindowFrame(widget, path);
    } else {
      CaptureYeeCompositorFrame(widget, path);
    }
  };
  const auto expect_boundary_gap = [&](int index, bool preceding) {
    auto* const tab = GetTabAnchorForModelIndex(view, index);
    const auto* styling = tab_styling(tab);
    ASSERT_TRUE(styling);
    ASSERT_TRUE(group_marker());
    // 경계 구분선은 hover 때 사라지므로 이전 입력 위치의 영향을 제거한다.
    ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
    const auto window_bounds = view->GetBoundsInScreen();
    const auto not_hovered = [](const TabStyleViewDelegate* item) {
      return !item ||
             (!item->IsHovering() && item->GetHoverAnimationValue() == 0.0f);
    };
    const auto* delegate = styling->delegate();
    ASSERT_TRUE(applied_.Run(
        {applied_.Condition(applied_.source().ObserveUiState(
                                *tab, yee::test::AppliedBrowserObservers::
                                          UiProbe::kTabAndAdjacentHover),
                            yee::test::ViewStateMatches{},
                            yee::test::AppliedMatch::kTabHoverIdle)},
        [&] {
          ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(gfx::Point(
              window_bounds.CenterPoint().x(), window_bounds.bottom() - 80)));
        }));
    ASSERT_TRUE(([&]() {
      return not_hovered(delegate) &&
             not_hovered(delegate->GetAdjacentTab(true)) &&
             not_hovered(delegate->GetAdjacentTab(false));
    })());
    SCOPED_TRACE(testing::Message()
                 << "index=" << index << " preceding=" << preceding
                 << " active=" << delegate->IsActive()
                 << " selected=" << delegate->IsSelected()
                 << " hovered=" << delegate->IsHovering()
                 << " hover_value=" << delegate->GetHoverAnimationValue());
    const float scale =
        tab->GetWidget()->GetCompositor()->device_scale_factor();
    gfx::Canvas canvas(tab->size(), scale, false);
    styling->PaintTab(&canvas);
    const auto pixels = canvas.GetBitmap();
    auto* const strip = view->tab_strip_view();
    const auto tab_bounds = views::View::ConvertRectToTarget(
        tab, strip, gfx::RectF(tab->GetLocalBounds()));
    const auto backing = views::View::ConvertRectToTarget(
        group_marker(), strip, gfx::RectF(group_marker()->GetLocalBounds()));
    const bool facing_left = preceding == base::i18n::IsRTL();
    int separator_pixels = 0;
    for (int x = 0; x < pixels.width(); ++x) {
      if ((x < pixels.width() / 2) != facing_left ||
          SkColorGetA(pixels.getColor(x, pixels.height() / 2)) ==
              SK_AlphaTRANSPARENT) {
        continue;
      }
      ++separator_pixels;
      const float gap = facing_left
                            ? tab_bounds.x() + x / scale - backing.right()
                            : backing.x() - tab_bounds.x() - (x + 1) / scale;
      EXPECT_GE(gap, yee::kSidebarMetrics.horizontal_group_capsule_padding);
    }
    EXPECT_GT(separator_pixels, 0);
    if (!preceding &&
        views::IsViewClass<TabGroupView>(group_header()->parent())) {
      const auto* grouped =
          tab_styling(GetTabAnchorForModelIndex(view, index - 1));
      ASSERT_TRUE(grouped);
      const auto opacities = grouped->GetSeparatorOpacitiesForTesting();
      EXPECT_FLOAT_EQ(0.0f,
                      base::i18n::IsRTL() ? opacities.left : opacities.right);
    }
  };
  auto* tab_group = model->group_model()->GetTabGroup(group);
  const auto group_color = tab_groups::TabGroupColorId::kGreen;
  const auto set_favicon = [&](int index, SkColor color) {
    auto* contents = model->GetWebContentsAt(index);
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    auto* entry = contents->GetController().GetLastCommittedEntry();
    ASSERT_TRUE(entry);
    SkBitmap bitmap;
    bitmap.allocN32Pixels(16, 16);
    bitmap.eraseColor(color);
    entry->GetFavicon().image = gfx::Image::CreateFrom1xBitmap(bitmap);
    entry->GetFavicon().valid = true;
    contents->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
  };
  set_favicon(1, SK_ColorRED);
  set_favicon(2, SK_ColorGREEN);
  set_favicon(3, SK_ColorBLUE);
  {
    ui::MockOsSettingsProvider os_settings;
    for (auto contrast : {ui::NativeTheme::PreferredContrast::kNoPreference,
                          ui::NativeTheme::PreferredContrast::kMore,
                          ui::NativeTheme::PreferredContrast::kNoPreference}) {
      SCOPED_TRACE(static_cast<int>(contrast));
      os_settings.SetPreferredContrast(contrast);
      ApplyScheduledLayout();
      ASSERT_EQ(contrast, view->GetNativeTheme()->preferred_contrast());
      const bool capsule =
          contrast != ui::NativeTheme::PreferredContrast::kMore;
      ASSERT_TRUE(group_marker());
      EXPECT_EQ(capsule, yee::UsesHorizontalTabGroupCapsule(*group_marker()));
      auto* const header = group_header();
      auto* const root = header->parent();
      if (views::IsViewClass<TabContainerImpl>(root)) {
        if (capsule) {
          EXPECT_EQ(yee::kSidebarMetrics.horizontal_group_capsule_height,
                    group_marker()->height());
        } else {
          EXPECT_EQ(TabGroupUnderline::kStrokeThickness,
                    group_marker()->height());
          EXPECT_EQ(
              header->bounds().bottom() -
                  GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap),
              group_marker()->bounds().bottom());
        }
      } else {
        EXPECT_EQ(capsule, group_marker()->GetVisible());
        auto* const unified_header =
            views::AsViewClass<TabGroupHeaderView>(header);
        ASSERT_TRUE(unified_header);
        const auto& margin = unified_header->GetInteriorMargin();
        EXPECT_EQ(yee::kSidebarMetrics.group_header_horizontal_inset,
                  margin.right());
        EXPECT_EQ(capsule, margin.left() > margin.right());
      }
      auto* const ring = views::FocusRing::Get(header);
      ASSERT_TRUE(ring);
      EXPECT_EQ(capsule, ring->GetColorId().has_value());
      // 실제 그리기로 cache를 채우고 테마 변경만으로 순서가 바뀌는지 확인한다.
      const float scale =
          root->GetWidget()->GetCompositor()->device_scale_factor();
      SkBitmap painted;
      {
        ui::CanvasPainter painter(&painted, root->parent()->size(), scale,
                                  SK_ColorTRANSPARENT, false);
        ui::PaintContext context(painter.context(),
                                 ui::PaintContext::CLONE_WITHOUT_INVALIDATION);
        root->Paint(views::PaintInfo::CreateRootPaintInfo(
            context, root->parent()->size()));
      }
      if (auto* legacy = views::AsViewClass<TabContainerImpl>(root)) {
        const auto& order = legacy->GetZOrderCacheForTesting();
        const auto position = [&](views::View* target) {
          return std::ranges::find_if(
              order, [&](const auto& item) { return item.view() == target; });
        };
        const auto backing = position(group_marker());
        const auto tab = position(GetTabAnchorForModelIndex(view, 1));
        ASSERT_NE(order.end(), backing);
        ASSERT_NE(order.end(), tab);
        EXPECT_EQ(capsule, backing < tab);
      }
    }
  }
  ApplyScheduledLayout();
  // 뒤쪽 경계는 그룹 전체를 비활성 상태로 두고 확인한다.
  model->ActivateTabAt(0);
  ApplyScheduledLayout();
  ASSERT_LT(tab_group->ListTabs().end(), static_cast<size_t>(model->count()));
  expect_boundary_gap(static_cast<int>(tab_group->ListTabs().end()), false);
  model->ActivateTabAt(4);
  ApplyScheduledLayout();
  const auto overflow_count = [&]() {
    int count = 0;
    const auto visit = [&](auto&& self, const views::View* child) -> void {
      if (const auto* label = views::AsViewClass<views::Label>(child)) {
        count += label->GetVisible() && label->GetText() == u"+1";
      }
      for (const views::View* descendant : child->children()) {
        self(self, descendant);
      }
    };
    visit(visit, group_header());
    return count;
  };
  views::View* visible_preview = nullptr;
  const auto expect_preview = [&](std::array<SkColor, 3> expected) {
    EXPECT_GT(yee::GetTabGroupPreviewWidth(*group_header()), 0);
    views::View* preview = nullptr;
    std::vector<views::ImageView*> icons;
    for (views::View* child : group_header()->children()) {
      icons.clear();
      for (views::View* grandchild : child->children()) {
        if (auto* icon = views::AsViewClass<views::ImageView>(grandchild)) {
          icons.push_back(icon);
        }
      }
      if (icons.size() == expected.size()) {
        preview = child;
        break;
      }
    }
    ASSERT_TRUE(preview);
    visible_preview = preview;
    EXPECT_TRUE(group_header()->GetLocalBounds().Contains(preview->bounds()));
    std::ranges::sort(icons, {}, &views::View::x);
    // Chromium은 favicon 갱신을 모아서 처리한다. layout 완료만으로 지연 작업이
    // 처리됐다고 가정하지 않고 실제 콜백을 기다린다.
    yee::test::AppliedConditions icon_conditions;
    for (size_t i = 0; i < icons.size(); ++i)
      icon_conditions.push_back(applied_.Condition(
          applied_.source().ObserveUiState(
              *icons[i],
              yee::test::AppliedBrowserObservers::UiProbe::kImageCenter),
          yee::test::PresentationMatches{.icon_id = expected[i]},
          yee::test::AppliedMatch::kPresentationIcon));
    ASSERT_TRUE(Await(icon_conditions));
    ASSERT_TRUE(([&]() {
      for (size_t i = 0; i < icons.size(); ++i) {
        const auto image = icons[i]->GetImage();
        if (image.isNull() || image.GetRepresentation(1.f).GetBitmap().getColor(
                                  8, 8) != expected[i]) {
          return false;
        }
      }
      return true;
    })());
    const gfx::Rect bounds = preview->bounds();
    preview->SetPosition(
        group_header()->GetMirroredRect(gfx::Rect(preview->size())).origin());
    SkBitmap painted;
    {
      ui::CanvasPainter painter(&painted, preview->size(), 1.f,
                                SK_ColorTRANSPARENT, false);
      preview->Paint(views::PaintInfo::CreateRootPaintInfo(painter.context(),
                                                           preview->size()));
    }
    preview->SetBoundsRect(bounds);
    for (size_t i = 0; i < icons.size(); ++i) {
      const auto bitmap =
          icons[i]->GetImage().GetRepresentation(1.f).GetBitmap();
      EXPECT_EQ(expected[i], bitmap.getColor(8, 8));
      if (i + 1 < icons.size()) {
        gfx::Rect overlap = preview->GetMirroredRect(icons[i]->bounds());
        overlap.Intersect(preview->GetMirroredRect(icons[i + 1]->bounds()));
        ASSERT_FALSE(overlap.IsEmpty());
        EXPECT_EQ(expected[i], painted.getColor(overlap.CenterPoint().x(),
                                                overlap.CenterPoint().y()));
        const gfx::Rect front = preview->GetMirroredRect(icons[i]->bounds());
        const int seam_x = base::i18n::IsRTL() ? front.x() : front.right() - 1;
        const int body_x = seam_x + (base::i18n::IsRTL() ? -4 : 4);
        const auto seam_alpha =
            SkColorGetA(painted.getColor(seam_x, front.CenterPoint().y()));
        // 뒤 favicon은 겹친 경계에서 흐려졌다가 원래 불투명도로 이어진다.
        // chip 색으로 덮지 않고 alpha mask를 써야 한다.
        EXPECT_GT(seam_alpha, SK_AlphaTRANSPARENT);
        EXPECT_LT(seam_alpha, SK_AlphaOPAQUE);
        EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(painted.getColor(
                                      body_x, front.CenterPoint().y())));
      }
    }
  };
  for (bool vertical : {false, true}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    model->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(u"Research", group_color, true));
    ApplyScheduledLayout();
    expect_preview({SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE});
    EXPECT_EQ(1, overflow_count());
    if (!vertical) {
      auto* theme = ThemeServiceFactory::GetForProfile(browser()->GetProfile());
      const auto original_scheme = theme->GetBrowserColorScheme();
      ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
      const auto native_window = view->GetWidget()->GetNativeWindow();
      for (bool dark : {false, true}) {
        SCOPED_TRACE(dark);
        theme->SetBrowserColorScheme(
            dark ? ThemeService::BrowserColorScheme::kDark
                 : ThemeService::BrowserColorScheme::kLight);
        ApplyScheduledLayout();
        auto* heading = group_header();
        views::Label* name = nullptr;
        const auto find_name = [&](auto&& self, views::View* item) -> void {
          if (auto* label = views::AsViewClass<views::Label>(item);
              label && label->GetText() == u"Research") {
            name = label;
          }
          for (views::View* child : item->children()) {
            self(self, child);
          }
        };
        find_name(find_name, heading);
        ASSERT_TRUE(name);
        views::View* chip = views::IsViewClass<TabGroupHeader>(heading)
                                ? name->parent()
                                : heading;
        const auto size = chip->size();
        const auto header_size = heading->size();
        // 실제 포인터 왕복은 색만 바꾸며 접힌 파비콘 영역을 재차 늘리지 않는다.
        for (bool hovered : {false, true, false, true, false}) {
          const auto target =
              hovered ? name->GetBoundsInScreen().CenterPoint()
                      : view->toolbar()->GetBoundsInScreen().CenterPoint();
          SCOPED_TRACE(testing::Message()
                       << "hovered=" << hovered
                       << ", header=" << heading->GetClassName());
          // macOS 입력 도구는 NSView 사이의 tracking-area exit를 만들지 않는다.
          // 같은 Chrome NSView의 주소줄로 이동하고 실제 paint 갱신까지
          // 확인한다.
          ASSERT_TRUE(applied_.Run(
              {applied_.View(*heading, {.hovered = hovered},
                             yee::test::AppliedMatch::kViewHovered),
               applied_.Condition(
                   applied_.source().ObserveUiState(
                       *chip, yee::test::AppliedBrowserObservers::UiProbe::
                                  kPaintedBackground),
                   yee::test::PresentationMatches{.background_color =
                                                      hovered ? 1u : 0u},
                   yee::test::AppliedMatch::kPresentationAlpha)},
              [&] {
                ASSERT_TRUE(
                    ui_test_utils::SendMouseMoveSync(target, native_window));
              }));
          ASSERT_TRUE(([&] {
            if (heading->IsMouseHovered() != hovered || !chip->background()) {
              return false;
            }
            gfx::Canvas frame(chip->size(), 1.f, false);
            chip->background()->Paint(&frame, chip);
            const auto alpha = SkColorGetA(frame.GetBitmap().getColor(
                chip->width() / 2, chip->height() / 2));
            const auto colors = yee::ResolveHorizontalTabGroupColors(
                *heading,
                heading->GetColorProvider()->GetColor(
                    GetTabGroupTabStripColorId(
                        group_color,
                        heading->GetWidget()->ShouldPaintAsActive())),
                hovered);
            return colors &&
                   color_utils::GetContrastRatio(name->GetEnabledColor(),
                                                 colors->background) >=
                       color_utils::kMinimumReadableContrastRatio &&
                   (hovered ? alpha > 0 && alpha < SK_AlphaOPAQUE : alpha == 0);
          })());
          ApplyScheduledLayout();
          EXPECT_EQ(size, chip->size());
          EXPECT_EQ(header_size, heading->size());
          ASSERT_TRUE(chip->background());
          gfx::Canvas canvas(chip->size(), 1.f, false);
          chip->background()->Paint(&canvas, chip);
          const auto alpha = SkColorGetA(canvas.GetBitmap().getColor(
              chip->width() / 2, chip->height() / 2));
          if (hovered) {
            EXPECT_GT(alpha, SK_AlphaTRANSPARENT);
            EXPECT_LT(alpha, SK_AlphaOPAQUE);
          } else {
            EXPECT_EQ(SK_AlphaTRANSPARENT, alpha);
          }
          const auto colors = yee::ResolveHorizontalTabGroupColors(
              *heading,
              heading->GetColorProvider()->GetColor(GetTabGroupTabStripColorId(
                  group_color, heading->GetWidget()->ShouldPaintAsActive())),
              hovered);
          ASSERT_TRUE(colors);
          // native가 해석한 실제 제목색의 최소 가독성 대비를 보장한다.
          EXPECT_GE(color_utils::GetContrastRatio(name->GetEnabledColor(),
                                                  colors->background),
                    color_utils::kMinimumReadableContrastRatio);
        }
      }
      theme->SetBrowserColorScheme(original_scheme);
      ApplyScheduledLayout();
    }
    if (vertical) {
      // 장식용 아이콘의 native hover·클릭은 그룹 행으로 전달돼야 한다.
      ASSERT_TRUE(visible_preview);
      ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
      const auto native_window = view->GetWidget()->GetNativeWindow();
      ASSERT_TRUE(
          input_geometry_ready(group_preview_target, vertical_group_header));
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          group_preview_target()->GetBoundsInScreen().CenterPoint(),
          native_window));
      ApplyScheduledLayout();
      ASSERT_TRUE(pointer_matches(group_preview_target(),
                                  vertical_group_header(),
                                  "yee_group_preview_pointer"));
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
          native_window));
      ASSERT_TRUE(
          ([&]() { return !tab_group->visual_data()->is_collapsed(); })());
      ApplyScheduledLayout();
      EXPECT_EQ(0, yee::GetTabGroupPreviewWidth(*group_header()));
      // 펼치면 미리보기가 사라진다. 현재 제목으로 이동해 편집 버튼을 피한다.
      ASSERT_TRUE(
          input_geometry_ready(group_title_target, vertical_group_header));
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          group_title_target()->GetBoundsInScreen().CenterPoint(),
          native_window));
      ApplyScheduledLayout();
      ASSERT_TRUE(pointer_matches(group_title_target(), vertical_group_header(),
                                  "yee_group_title_pointer"));
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
          native_window));
      ASSERT_TRUE(
          ([&]() { return tab_group->visual_data()->is_collapsed(); })());
      ApplyScheduledLayout();
      expect_preview({SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE});
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          view->toolbar()->GetBoundsInScreen().CenterPoint(), native_window));
    }
    // 기존 TabUIHelper 콜백이 접힌 그룹 요약도 갱신해야 한다.
    set_favicon(1, SK_ColorYELLOW);
    ApplyScheduledLayout();
    expect_preview({SK_ColorYELLOW, SK_ColorGREEN, SK_ColorBLUE});
    // 제목·URL이 같아도 순서를 바꾸면 favicon 순서가 바뀌어야 한다.
    model->MoveWebContentsAt(1, 3, false);
    ApplyScheduledLayout();
    expect_preview({SK_ColorGREEN, SK_ColorBLUE, SK_ColorYELLOW});
    model->MoveWebContentsAt(3, 1, false);
    set_favicon(1, SK_ColorRED);
    model->RemoveFromGroup({4});
    ApplyScheduledLayout();
    EXPECT_EQ(0, overflow_count());
    model->AddToExistingGroup({4}, group);
    ApplyScheduledLayout();
    EXPECT_EQ(4, tab_group->tab_count());
    // 통합 탭바는 멤버를 추가할 때 그룹을 펼친다.
    model->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(u"Research", group_color, true));
    ApplyScheduledLayout();
    expect_preview({SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE});
    EXPECT_EQ(1, overflow_count());
    model->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(u"Research", group_color, false));
    ApplyScheduledLayout();
    EXPECT_EQ(0, yee::GetTabGroupPreviewWidth(*group_header()));

    // 같은 그룹을 긴 이름·빈 이름·짧은 이름으로 바꾼다. 제목의 선호 크기나
    // tooltip만 보지 않고 실제 할당된 배치를 검사한다.
    const gfx::Rect original_bounds =
        view->GetWidget()->GetWindowBoundsInScreen();
    const int original_sidebar_width = state->GetUncollapsedWidth();
    auto* const sidebar_region =
        view->vertical_tab_strip_region_view_for_testing();
    constexpr struct {
      const char* name;
      std::u16string_view title;
    } title_cases[] = {
        {"long",
         u"기획과 리서치 — Product design research and implementation "
         u"notes for the upcoming browser release"},
        {"unnamed", u""},
        {"short", u"작업 도구"},
    };
    for (bool constrained : {false, true}) {
      SCOPED_TRACE(constrained);
      if (constrained) {
        if (vertical) {
          ASSERT_TRUE(sidebar_region);
          sidebar_region->OnResize(sidebar_region->GetMinimumSize().width() -
                                       sidebar_region->width(),
                                   /*done_resizing=*/true);
          ApplyScheduledLayout();
          EXPECT_EQ(sidebar_region->GetMinimumSize().width(),
                    sidebar_region->width());
          EXPECT_LT(sidebar_region->width(), original_sidebar_width);
        } else {
          RunTestSequence(ResizeToMinimumWidth());
          EXPECT_LT(view->GetWidget()->GetWindowBoundsInScreen().width(),
                    original_bounds.width());
        }
      }
      for (const auto& title_case : title_cases) {
        SCOPED_TRACE(title_case.name);
        for (bool collapsed : {true, false}) {
          SCOPED_TRACE(collapsed);
          model->ChangeTabGroupVisuals(
              group,
              tab_groups::TabGroupVisualData(std::u16string(title_case.title),
                                             group_color, collapsed));
          ApplyScheduledLayout();
          if (constrained && !vertical) {
            // 긴 이름과 접힘 상태에 따라 native 그룹의 고정 폭과 창 최소 폭도
            // 바뀐다. 각 상태의 현재 최소 폭에서 검사하고 이전 폭을 강제하지
            // 않는다.
            RunTestSequence(ResizeToMinimumWidth());
            EXPECT_GE(view->GetWidget()->GetSize().width(),
                      view->GetWidget()->GetMinimumSize().width());
            EXPECT_LT(view->GetWidget()->GetSize().width(),
                      original_bounds.width());
          }
          if (!vertical) {
            auto* const new_tab =
                views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
                    kNewTabButtonElementId,
                    views::ElementTrackerViews::GetContextForView(view), true);
            ASSERT_TRUE(new_tab);
            const gfx::Rect button_bounds = new_tab->GetBoundsInScreen();
            const gfx::Rect strip_bounds =
                view->tab_strip_view()->GetTabStripView()->GetBoundsInScreen();
            EXPECT_TRUE(view->GetBoundsInScreen().Contains(button_bounds));
            EXPECT_EQ(yee::kSidebarMetrics.shell_control_size,
                      button_bounds.width());
            EXPECT_EQ(yee::kSidebarMetrics.shell_control_size,
                      button_bounds.height());
            EXPECT_EQ(gfx::Size(yee::kSidebarMetrics.shell_control_size,
                                yee::kSidebarMetrics.shell_control_size),
                      new_tab->GetContentsBounds().size());
            auto* generator =
                new_tab->GetProperty(views::kHighlightPathGeneratorKey);
            ASSERT_TRUE(generator);
            SkRRect highlight;
            ASSERT_TRUE(
                generator->GetHighlightPath(new_tab).isRRect(&highlight));
            EXPECT_EQ(yee::kSidebarMetrics.shell_control_corner_radius,
                      highlight.radii(SkRRect::kUpperLeft_Corner).x());
            EXPECT_FALSE(button_bounds.Intersects(strip_bounds))
                << "button=" << button_bounds.ToString()
                << ", strip=" << strip_bounds.ToString();
            if (constrained && collapsed &&
                std::string_view(title_case.name) == "short") {
              const auto tab_handles = [model]() {
                std::vector<tabs::TabHandle> handles;
                for (int index = 0; index < model->count(); ++index) {
                  handles.push_back(model->GetTabAtIndex(index)->GetHandle());
                }
                return handles;
              };
              const auto handles_before = tab_handles();
              const int count_before = model->count();
              const auto active_before = model->GetActiveTab()->GetHandle();
              const auto current_new_tab = [&]() {
                return views::ElementTrackerViews::GetInstance()
                    ->GetFirstMatchingView(
                        kNewTabButtonElementId,
                        views::ElementTrackerViews::GetContextForView(view),
                        true);
              };
              ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
              ASSERT_TRUE(
                  input_geometry_ready(current_new_tab, current_new_tab));
              const auto native_window = view->GetWidget()->GetNativeWindow();
              ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
                  current_new_tab()->GetBoundsInScreen().CenterPoint(),
                  native_window));
              // 이동 통지 뒤에도 실제 포인터와 현재 hit target을 다시 읽는다.
              ApplyScheduledLayout();
              ASSERT_TRUE(pointer_matches(current_new_tab(), current_new_tab(),
                                          "yee_new_tab_pointer"));
              ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
                  ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP,
                  native_window));
              // 생성·선택이 완료된 새 handle을 찾고 그 탭만 닫는다.
              ASSERT_TRUE(([&] {
                if (model->count() != count_before + 1) {
                  return false;
                }
                const auto created = model->GetActiveTab()->GetHandle();
                if (std::ranges::find(handles_before, created) !=
                    handles_before.end()) {
                  return false;
                }
                auto previous = tab_handles();
                std::erase(previous, created);
                return previous == handles_before;
              })());
              const auto created = model->GetActiveTab()->GetHandle();
              RecordProperty("yee_new_tab_created_handle", created.raw_value());
              ASSERT_TRUE(created.Get());
              model->CloseWebContents(
                  created.Get()->GetContents(),
                  TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
              ASSERT_TRUE(([&] {
                return model->count() == count_before &&
                       tab_handles() == handles_before;
              })());
              ASSERT_TRUE(active_before.Get());
              model->ActivateTab(active_before.Get());
              ASSERT_TRUE(([&] {
                return model->GetActiveTab()->GetHandle() == active_before;
              })());
              EXPECT_EQ(handles_before, tab_handles());
              EXPECT_EQ(count_before, model->count());
              EXPECT_EQ(active_before, model->GetActiveTab()->GetHandle());
              // native 새 탭 생성으로 펼쳐진 그룹을 이번 검사 상태로 복원한다.
              model->ChangeTabGroupVisuals(
                  group, tab_groups::TabGroupVisualData(
                             std::u16string(title_case.title), group_color,
                             collapsed));
              ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
                  view->toolbar()->GetBoundsInScreen().CenterPoint(),
                  native_window));
              ApplyScheduledLayout();
            }
          }
          auto* const header = group_header();
          if (!header && !capture_directory.empty()) {
            CaptureYeeWindowFrame(
                view->GetWidget(),
                capture_directory.AppendASCII("missing-group-header.png"));
          }
          ASSERT_TRUE(header)
              << "vertical=" << vertical << ", sidebar width="
              << (sidebar_region ? sidebar_region->width() : -1) << ", minimum="
              << (sidebar_region ? sidebar_region->GetMinimumSize().width()
                                 : -1)
              << ", collapsed=" << state->IsCollapsed() << ", window="
              << view->GetWidget()->GetWindowBoundsInScreen().ToString()
              << ", tabs="
              << view->tab_strip_view()->GetBoundsInScreen().ToString()
              << ", capture enabled=" << !capture_directory.empty();
          ASSERT_TRUE(visible_preview);
          EXPECT_EQ(collapsed, visible_preview->GetVisible());
          EXPECT_EQ(collapsed, yee::GetTabGroupPreviewWidth(*header) > 0);
          if (collapsed) {
            EXPECT_TRUE(
                header->GetLocalBounds().Contains(visible_preview->bounds()));
            EXPECT_EQ(visible_preview->GetPreferredSize().width(),
                      visible_preview->width());
            EXPECT_EQ(1, overflow_count());
          }

          views::Label* title_label = nullptr;
          const auto inspect = [&](auto&& self, views::View* child) -> void {
            if (child == visible_preview) {
              return;
            }
            if (auto* label = views::AsViewClass<views::Label>(child);
                !title_label && label && label->GetText() == title_case.title) {
              title_label = label;
            }
            if (child->IsDrawn() && !child->bounds().IsEmpty() &&
                (views::AsViewClass<views::ImageView>(child) ||
                 (views::AsViewClass<views::Label>(child) &&
                  !title_case.title.empty()))) {
              EXPECT_TRUE(header->GetBoundsInScreen().Contains(
                  child->GetBoundsInScreen()))
                  << child->GetClassName()
                  << " child=" << child->GetBoundsInScreen().ToString()
                  << " header=" << header->GetBoundsInScreen().ToString();
              if (collapsed) {
                EXPECT_FALSE(child->GetBoundsInScreen().Intersects(
                    visible_preview->GetBoundsInScreen()));
              }
            }
            for (views::View* descendant : child->children()) {
              self(self, descendant);
            }
          };
          inspect(inspect, header);
          ASSERT_TRUE(title_label);
          EXPECT_FALSE(title_label->GetMultiLine());
          EXPECT_EQ(gfx::FADE_TAIL, title_label->GetElideBehavior());
          if (!vertical && !header->IsMouseHovered()) {
            auto* chip = views::IsViewClass<TabGroupHeader>(header)
                             ? title_label->parent()
                             : header;
            ASSERT_TRUE(chip->background());
            const float scale =
                chip->GetWidget()->GetCompositor()->device_scale_factor();
            gfx::Canvas canvas(chip->size(), scale, false);
            chip->background()->Paint(&canvas, chip);
            const auto pixels = canvas.GetBitmap();
            EXPECT_EQ(SK_AlphaTRANSPARENT,
                      SkColorGetA(pixels.getColor(pixels.width() / 2,
                                                  pixels.height() / 2)));
            ASSERT_TRUE(group_marker());
            EXPECT_TRUE(group_marker()->IsDrawn());
            gfx::Canvas capsule_canvas(group_marker()->size(), scale, false);
            if (auto* underline =
                    views::AsViewClass<TabGroupUnderline>(group_marker())) {
              underline->OnPaint(&capsule_canvas);
            } else {
              ASSERT_TRUE(group_marker()->background());
              group_marker()->background()->Paint(&capsule_canvas,
                                                  group_marker());
            }
            const auto capsule_pixels = capsule_canvas.GetBitmap();
            const auto alpha = SkColorGetA(capsule_pixels.getColor(
                capsule_pixels.width() / 2, capsule_pixels.height() / 2));
            EXPECT_GT(alpha, SK_AlphaTRANSPARENT);
            EXPECT_LT(alpha, SK_AlphaOPAQUE);
          }
          const auto& accessible_name =
              header->GetViewAccessibility().GetCachedName();
          EXPECT_FALSE(accessible_name.empty());
          if (!title_case.title.empty()) {
            EXPECT_NE(std::u16string::npos,
                      accessible_name.find(title_case.title));
            if (!constrained) {
              EXPECT_GT(title_label->width(), 0);
            }
          }
          if (title_case.title == title_cases[0].title) {
            EXPECT_GT(title_label->GetPreferredSize().width(),
                      title_label->width());
          }
          if (!vertical) {
            ASSERT_TRUE(group_marker());
            gfx::Rect interior = group_marker()->GetBoundsInScreen();
            interior.Inset(gfx::Insets::VH(
                0, yee::kSidebarMetrics.group_header_horizontal_inset));
            if (!title_case.title.empty()) {
              EXPECT_TRUE(interior.Contains(title_label->GetBoundsInScreen()))
                  << "title=" << title_label->GetBoundsInScreen().ToString()
                  << ", group interior=" << interior.ToString();
            }
            // 접힌 요약과 펼친 실제 탭의 16 DIP glyph를 모두 확인한다.
            // 사각형 경계를 잘라 넘친 부분을 숨기는 것으로 대체하지 않는다.
            const auto expect_glyphs = [&](auto&& self,
                                           views::View* current) -> void {
              if (current->IsDrawn() &&
                  ((collapsed &&
                    views::IsViewClass<views::ImageView>(current)) ||
                   views::IsViewClass<TabIcon>(current))) {
                gfx::Rect glyph = current->GetContentsBounds();
                if (!glyph.IsEmpty()) {
                  views::View::ConvertRectToScreen(current, &glyph);
                  EXPECT_TRUE(interior.Contains(glyph))
                      << current->GetClassName()
                      << " glyph=" << glyph.ToString()
                      << ", group interior=" << interior.ToString();
                }
              }
              for (views::View* child : current->children()) {
                self(self, child);
              }
            };
            if (collapsed) {
              expect_glyphs(expect_glyphs, visible_preview);
            } else {
              for (int index : tab_group->ListTabs().ToIntVector()) {
                expect_glyphs(expect_glyphs,
                              GetTabAnchorForModelIndex(view, index));
              }
            }
          }
          if (constrained && !capture_directory.empty()) {
            CaptureYeeWindowFrame(
                view->GetWidget(),
                capture_directory.AppendASCII(base::StringPrintf(
                    "title-%s-%s-%s.png", vertical ? "sidebar" : "horizontal",
                    title_case.name, collapsed ? "collapsed" : "expanded")));
          }
          if (constrained && !vertical) {
            // 긴 그룹 뒤의 탭은 native가 가릴 수 있다. 그룹 앞에 인접한
            // 비활성 일반 탭 두 개를 두어 실제 보이는 아이콘 간격을 검사한다.
            chrome::AddTabAt(browser(), GURL("about:blank"), 1, false);
            chrome::AddTabAt(browser(), GURL("about:blank"), 2, false);
            auto* const first_gap_tab = model->GetTabAtIndex(1);
            auto* const second_gap_tab = model->GetTabAtIndex(2);
            set_favicon(1, SK_ColorCYAN);
            set_favicon(2, SK_ColorMAGENTA);
            RunTestSequence(ResizeToMinimumWidth());
            ApplyScheduledLayout();
            if (auto* unified = views::AsViewClass<TabStripView>(
                    view->tab_strip_view()->GetTabStripView())) {
              // 스크롤 요청은 여러 post-layout callback에 걸쳐 완료된다.
              // native 소유자가 두 추적 대상을 해제한 뒤 가시 영역을 읽는다.
              ASSERT_TRUE(applied_.Run(
                  {applied_.Condition(
                      applied_.source().ObserveUiState(
                          *unified, yee::test::AppliedBrowserObservers::
                                        UiProbe::kScrollTargets),
                      yee::test::ViewStateMatches{},
                      yee::test::AppliedMatch::kViewScrollIdle)},
                  [&] {
                    unified->ScrollToFitViews(
                        GetTabAnchorForModelIndex(view, 1),
                        GetTabAnchorForModelIndex(view, 2));
                    unified->GetUnpinnedTabsScrollViewForTesting()
                        ->InvalidateLayout();
                  }));
              ASSERT_TRUE(([&] {
                ApplyScheduledLayout();
                return !unified->GetPrimaryScrollTargetForTesting() &&
                       !unified->GetSecondaryScrollTargetForTesting();
              })());
              ApplyScheduledLayout();
            }
            for (int index : {1, 2}) {
              EXPECT_FALSE(model->IsTabPinned(index));
              EXPECT_FALSE(model->GetTabGroupForTab(index));
              EXPECT_NE(model->active_index(), index);
              EXPECT_TRUE(GetTabAnchorForModelIndex(view, index)->IsDrawn());
            }
            std::vector<std::pair<int, gfx::Rect>> favicons;
            for (int index = 0; index < model->count(); ++index) {
              if (model->GetTabGroupForTab(index)) {
                continue;
              }
              const size_t icons_before = favicons.size();
              const auto inspect_icon = [&](auto&& self,
                                            views::View* current) -> void {
                if (auto* icon = views::AsViewClass<TabIcon>(current);
                    icon && icon->IsDrawn()) {
                  gfx::Rect bounds = icon->GetContentsBounds();
                  EXPECT_FALSE(bounds.IsEmpty());
                  const gfx::Rect visible = icon->GetVisibleBounds();
                  if (!capture_directory.empty()) {
                    LOG(INFO)
                        << "간격 검사 favicon: index=" << index
                        << ", contents=" << bounds.ToString()
                        << ", visible=" << visible.ToString()
                        << ", screen=" << icon->GetBoundsInScreen().ToString();
                  }
                  if (index == 1 || index == 2) {
                    EXPECT_TRUE(visible.Contains(bounds))
                        << "index=" << index
                        << ", favicon=" << bounds.ToString()
                        << ", visible=" << visible.ToString();
                  }
                  // IsDrawn은 스크롤 viewport 밖에서도 참이다. 두 명시적
                  // 탭은 전부 보여야 하며 나머지는 실제 보이는 부분을 비교한다.
                  bounds.Intersect(visible);
                  if (bounds.IsEmpty()) {
                    return;
                  }
                  views::View::ConvertRectToScreen(icon, &bounds);
                  for (const auto& [other_index, other] : favicons) {
                    EXPECT_FALSE(bounds.Intersects(other))
                        << "index=" << index
                        << ", favicon=" << bounds.ToString()
                        << ", adjacent index=" << other_index
                        << ", adjacent=" << other.ToString();
                  }
                  favicons.emplace_back(index, bounds);
                }
                for (views::View* child : current->children()) {
                  self(self, child);
                }
              };
              inspect_icon(inspect_icon,
                           GetTabAnchorForModelIndex(view, index));
              if (index == 1 || index == 2) {
                EXPECT_EQ(icons_before + 1, favicons.size());
              }
            }
            EXPECT_GE(favicons.size(), 2u);
            if (!capture_directory.empty()) {
              CaptureYeeWindowFrame(
                  view->GetWidget(),
                  capture_directory.AppendASCII(base::StringPrintf(
                      "gap-%s-%s.png", title_case.name,
                      collapsed ? "collapsed" : "expanded")));
            }
            model->CloseWebContentsAt(model->GetIndexOfTab(second_gap_tab),
                                      TabCloseTypes::CLOSE_NONE);
            model->CloseWebContentsAt(model->GetIndexOfTab(first_gap_tab),
                                      TabCloseTypes::CLOSE_NONE);
            RunTestSequence(ResizeToMinimumWidth());
            ApplyScheduledLayout();
            EXPECT_EQ(count, model->count());
            EXPECT_EQ(active_handle, model->GetActiveTab()->GetHandle());
          }
          if (!vertical && !collapsed) {
            capture_group(base::StringPrintf("title-%s-%s", title_case.name,
                                             constrained ? "narrow" : "wide"));
          }
        }
      }
    }
    if (vertical) {
      sidebar_region->OnResize(original_sidebar_width - sidebar_region->width(),
                               /*done_resizing=*/true);
    }
    view->GetWidget()->SetBounds(original_bounds);
    model->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(u"Research", group_color, false));
    ApplyScheduledLayout();
  }
  state->SetVerticalTabsEnabled(false);
  // 접힌 그룹을 클릭하면 비그룹 탭이 선택될 수 있다. 멤버를 다시 선택해
  // 이후 paint 검사가 실제 활성 그룹 스타일을 확인하게 한다.
  model->ActivateTabAt(4);
  expect_rows(false);
#if BUILDFLAG(IS_WIN)
  // fractional scale의 Win32 외곽 복원은 DIP 크기를 보정할 수 있다.
  // 패널을 열기 직전 실제 창을 기준으로 잡고 이후 두 크기를 정확히 비교한다.
  full_header_bounds = layout->last_native_layout_for_testing()->header_bounds;
  full_header_window_size = view->GetWidget()->GetSize();
#endif

  auto* const source = yee::BrowserSurfaceColorController::FromWebContents(
      model->GetActiveWebContents());
  ASSERT_TRUE(source);
  const auto expect_colors = [&]() {
    ASSERT_EQ(group, model->GetTabGroupForTab(model->active_index()));
    views::View* const tab =
        GetTabAnchorForModelIndex(view, model->active_index());
    ASSERT_TRUE(tab);
    const auto expect_title = [&](SkColor foreground) {
      bool matching_title = false;
      std::string title_diagnostics;
      const auto visit = [&](auto&& self, views::View* child) -> void {
        if (auto* label = views::AsViewClass<views::Label>(child)) {
          title_diagnostics += base::StringPrintf(
              " visible=%d drawn=%d bounds=%s color=%s;", label->GetVisible(),
              label->IsDrawn(), label->bounds().ToString().c_str(),
              color_utils::SkColorToRgbaString(label->GetEnabledColor())
                  .c_str());
          matching_title |= label->GetVisible() && !label->bounds().IsEmpty() &&
                            label->GetEnabledColor() == foreground;
        }
        for (views::View* descendant : child->children()) {
          self(self, descendant);
        }
      };
      visit(visit, tab);
      EXPECT_TRUE(matching_title)
          << "탭 제목 관측: tab=" << tab->GetBoundsInScreen().ToString()
          << " window="
          << view->GetWidget()->GetWindowBoundsInScreen().ToString()
          << " expected=" << color_utils::SkColorToRgbaString(foreground)
          << title_diagnostics;
    };
    ASSERT_TRUE(view->GetYeeHeaderPresentation());
#if BUILDFLAG(IS_WIN)
    const auto expect_caption_contrast = [&]() {
      // 수평 모드의 창 버튼 아래에는 페이지 Header가 아니라 shell 탭 행이 있다.
      // 페이지·테마가 바뀌어도 실제 창 바탕에 대한 glyph 대비를 유지해야 한다.
      auto* const frame = static_cast<BrowserFrameViewWin*>(
          view->browser_widget()->GetFrameView());
      const SkColor caption_background = yee::ResolveShellBackgroundColor(
          *frame->GetColorProvider(), view->GetWidget()->ShouldPaintAsActive());
      EXPECT_GE(
          color_utils::GetContrastRatio(
              frame->GetCaptionColor(BrowserFrameActiveState::kUseCurrent),
              caption_background),
          color_utils::kMinimumVisibleContrastRatio)
          << "header surface=" << view->GetYeeHeaderPresentation()->surface
          << ", caption background=" << caption_background;
    };
    expect_caption_contrast();
#endif
    const auto current_tab = yee::ResolveHorizontalTabColors(
        *tab->GetColorProvider(), true, true, 0.0,
        view->GetWidget()->ShouldPaintAsActive(), std::nullopt, false,
        view->GetYeeHeaderPresentation()->surface);
    // 연결된 선택 탭은 페이지 Header와 같은 면과 대비로 전환한다.
    expect_title(current_tab.foreground);
    const auto* styling = tab_styling(tab);
    ASSERT_TRUE(styling);
    ASSERT_TRUE(model->IsTabPinned(0));
    ASSERT_NE(0, model->active_index());
    const auto* pinned_styling =
        tab_styling(GetTabAnchorForModelIndex(view, 0));
    ASSERT_TRUE(pinned_styling);
    // 통합 호스트의 비활성 고정 탭도 같은 hover 곡률을 사용한다.
    for (const auto* shape_style : {styling, pinned_styling}) {
      SkRRect hover_shape;
      ASSERT_TRUE(shape_style->GetPath(TabStyle::PathType::kHighlight, 1.f, {})
                      .isRRect(&hover_shape));
      for (SkRRect::Corner corner :
           {SkRRect::kUpperLeft_Corner, SkRRect::kUpperRight_Corner,
            SkRRect::kLowerRight_Corner, SkRRect::kLowerLeft_Corner}) {
        const float radius = hover_shape.radii(corner).x();
        EXPECT_TRUE(radius == 0 ||
                    radius == yee::kSidebarMetrics.content_corner_radius);
      }
    }
    // 계산된 tint만 검사하면 native 기본 paint가 비활성 멤버를 생략해도 모른다.
    // hover 없이 실제 배경 paint를 검사한다.
    auto* const member = GetTabAnchorForModelIndex(view, 1);
    ASSERT_TRUE(member);
    const auto* member_styling = tab_styling(member);
    ASSERT_TRUE(member_styling);
    gfx::Canvas member_canvas(member->size(), 1.f, false);
    member_styling->PaintTab(&member_canvas);
    const SkColor member_pixel = member_canvas.GetBitmap().getColor(
        member->width() / 2, member->height() / 2);
    ASSERT_TRUE(yee::UsesHorizontalTabGroupCapsule(*member));
    // 공통 배경이 평상시 채움을 소유하므로 멤버 타일에 이음매를 그리지 않는다.
    EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(member_pixel));
    // 그룹에 속한다고 연결된 활성 탭의 색 윤곽선이 되살아나면 안 된다.
    // 그룹 표식과 키보드 focus ring은 별도 역할이다.
    EXPECT_EQ(0, styling->GetStrokeThickness());
    const SkPath original_path =
        styling->GetPath(TabStyle::PathType::kActiveTab, 1.f, {});
    if (yee::UsesHorizontalTabGroupCapsule(*tab)) {
      EXPECT_FALSE(
          original_path.contains(tab->width() / 2.f, tab->height() - 0.5f));
      EXPECT_TRUE(styling->GetPath(TabStyle::PathType::kHitTest, 1.f, {})
                      .contains(tab->width() / 2.f, tab->height() - 0.5f));
    }
    ASSERT_TRUE(view->tab_strip_view()->SetPaneFocus(tab));
    auto* const focus_ring = views::FocusRing::Get(tab);
    ASSERT_TRUE(focus_ring);
    EXPECT_TRUE(focus_ring->ShouldPaintForTesting());
    for (SkColor surface :
         {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x82, 0x82, 0x82),
          SkColorSetRGB(0x13, 0x4E, 0x4A)}) {
      source->SetPageSurfaceColorForTesting(surface);
      ASSERT_TRUE(view->GetYeeHeaderPresentation());
      const auto presentation = *view->GetYeeHeaderPresentation();
      EXPECT_EQ(surface, presentation.surface);
      const auto page_tab = yee::ResolveHorizontalTabColors(
          *tab->GetColorProvider(), true, true, 0.0,
          view->GetWidget()->ShouldPaintAsActive(), std::nullopt, false,
          surface);
      EXPECT_EQ(original_path,
                styling->GetPath(TabStyle::PathType::kActiveTab, 1.f, {}));
      expect_title(page_tab.foreground);
      EXPECT_EQ(surface, styling->CalculateTargetColors().background_color);
      EXPECT_EQ(page_tab.foreground,
                styling->CalculateTargetColors().foreground_color);
#if BUILDFLAG(IS_WIN)
      expect_caption_contrast();
#endif
      ASSERT_TRUE(focus_ring->GetColorId());
      EXPECT_GE(color_utils::GetContrastRatio(tab->GetColorProvider()->GetColor(
                                                  *focus_ring->GetColorId()),
                                              surface),
                color_utils::kMinimumVisibleContrastRatio);
    }
  };
  expect_colors();

  for (SidePanelType type :
       {SidePanelType::kToolbar, SidePanelType::kContent}) {
    RunTestSequence(ReplaceAndShowSidePanel(type));
    for (bool right_aligned : {false, true}) {
      browser()->GetProfile()->GetPrefs()->SetBoolean(
          prefs::kSidePanelHorizontalAlignment, right_aligned);
      expect_rows(true);
      expect_colors();
    }
    RunTestSequence(CloseSidePanel());
    expect_rows(false);
  }

  if (!capture_directory.empty()) {
    // 선택적 캡처는 실제 native 패널 배치와 그룹 배경의 합성을 검수한다.
    // 일반 실행은 필요한 통합 검사만 유지한다.
    auto* const theme =
        ThemeServiceFactory::GetForProfile(browser()->GetProfile());
    const auto original_scheme = theme->GetBrowserColorScheme();
    view->GetWidget()->GetFocusManager()->ClearFocus();
    for (bool dark : {false, true}) {
      theme->SetBrowserColorScheme(
          dark ? ThemeService::BrowserColorScheme::kDark
               : ThemeService::BrowserColorScheme::kLight);
      for (bool vertical : {false, true}) {
        state->SetVerticalTabsEnabled(vertical);
        const auto capture_group_states =
            [&](const char* panel, std::optional<SidePanelType> type) {
              for (bool collapsed : {false, true}) {
                model->ChangeTabGroupVisuals(
                    group, tab_groups::TabGroupVisualData(u"Research",
                                                          group_color, false));
                // 접힌 그룹에 숨겨진 멤버가 선택된 채 남으면 안 된다.
                // 새로 선택한 탭에서 contextual panel을 다시 연다.
                model->ActivateTabAt(collapsed ? model->count() - 1 : 4);
                auto* const contents = model->GetActiveWebContents();
                // 앞의 색상 경계 검사는 의도적으로 색을 주입했다.
                // 시각 자료는 실제 페이지 배경에 맞춰야 한다.
                ASSERT_TRUE(Await({applied_.Condition(
                    applied_.source().ObserveRendererBackground(*contents),
                    yee::test::PresentationMatches{.use_native_theme = dark},
                    yee::test::AppliedMatch::kPresentationDark)}));
                ASSERT_TRUE(([&]() {
                  const auto background = contents->GetBackgroundColor();
                  return background && color_utils::IsDark(*background) == dark;
                })());
                auto* const page_source =
                    yee::BrowserSurfaceColorController::FromWebContents(
                        contents);
                ASSERT_TRUE(page_source);
                page_source->SetPageSurfaceColorForTesting(
                    *contents->GetBackgroundColor());
                model->ChangeTabGroupVisuals(
                    group, tab_groups::TabGroupVisualData(
                               u"Research", group_color, collapsed));
                if (type) {
                  RunTestSequence(ReplaceAndShowSidePanel(*type));
                }
                ApplyScheduledLayout();
                const auto name = base::StringPrintf(
                    "%s-%s-%s-%s.png", dark ? "dark" : "light",
                    vertical ? "sidebar" : "horizontal", panel,
                    collapsed ? "collapsed" : "expanded");
                CaptureYeeWindowFrame(view->GetWidget(),
                                      capture_directory.AppendASCII(name));
                if (type) {
                  RunTestSequence(CloseSidePanel());
                }
              }
              model->ChangeTabGroupVisuals(
                  group, tab_groups::TabGroupVisualData(u"Research",
                                                        group_color, false));
              model->ActivateTabAt(4);
              ApplyScheduledLayout();
            };
        capture_group_states("closed", std::nullopt);
        for (SidePanelType type :
             {SidePanelType::kToolbar, SidePanelType::kContent}) {
          for (bool right_aligned : {false, true}) {
            browser()->GetProfile()->GetPrefs()->SetBoolean(
                prefs::kSidePanelHorizontalAlignment, right_aligned);
            const auto panel = base::StringPrintf(
                "%s-%s",
                type == SidePanelType::kToolbar ? "toolbar" : "content",
                right_aligned ? "right" : "left");
            capture_group_states(panel.c_str(), type);
          }
        }
      }
    }
    theme->SetBrowserColorScheme(original_scheme);
    state->SetVerticalTabsEnabled(false);
    ApplyScheduledLayout();
  }

  // 실제 보이는 전환 버튼이 수평 모드에서 접기 명령을 실행하지 않는다.
  // 실제 탭 포커스를 유지한 채 hierarchy를 정리한다. 늦은 blur가 정리된
  // collection node에 hover card 갱신을 요청하면 안 된다.
  auto* const focused_tab =
      GetTabAnchorForModelIndex(view, model->active_index());
  ASSERT_TRUE(view->tab_strip_view()->SetPaneFocus(focused_tab));
  EXPECT_TRUE(focused_tab->HasFocus());
  RunTestSequence(PressButton(kVerticalTabStripCollapseButtonElementId));
  ApplyScheduledLayout();
  EXPECT_TRUE(state->ShouldDisplayVerticalTabs());
  EXPECT_EQ(yee::BrowserSurfaceMode::kCard,
            yee::GetBrowserSurfaceMode(*browser()));
  EXPECT_TRUE(view->top_container()->clip_path().isRect(nullptr));
  EXPECT_FALSE(view->top_container()
                   ->background()
                   ->AsA<CustomCornersBackground>()
                   ->is_visible());
  EXPECT_EQ(count, model->count());
  const gfx::Rect original_window =
      view->GetWidget()->GetWindowBoundsInScreen();
  const int vertical_header_width = view->top_container()->width();
  gfx::Rect wider_window = original_window;
  wider_window.set_width(wider_window.width() + 32);
  view->GetWidget()->SetBounds(wider_window);
  ApplyScheduledLayout();
  EXPECT_GT(view->top_container()->width(), vertical_header_width);
  EXPECT_EQ(view->top_container()->width(),
            view->top_container()->clip_path().getBounds().width());
  view->GetWidget()->SetBounds(original_window);
  ApplyScheduledLayout();
  state->SetVerticalTabsEnabled(false);
  expect_rows(false);
  expect_colors();

#if BUILDFLAG(IS_MAC)
  PrefService* const pref_service = browser()->GetProfile()->GetPrefs();
  const bool always_show =
      pref_service->GetBoolean(prefs::kShowFullscreenToolbar);
  ui::NSWindowFullscreenNotificationWaiter native_fullscreen(
      view->GetWidget()->GetNativeWindow());
  const auto wait_for_header_frames = [&]() {
    ApplyScheduledLayout();
    std::vector<views::Widget*> widgets;
    for (views::View* host :
         {static_cast<views::View*>(view),
          static_cast<views::View*>(view->top_container()), group_header()}) {
      ASSERT_TRUE(host);
      if (host->IsDrawn() && !host->GetLocalBounds().IsEmpty() &&
          std::ranges::find(widgets, host->GetWidget()) == widgets.end()) {
        widgets.push_back(host->GetWidget());
      }
    }
    for (auto* widget : widgets) {
      auto* const compositor = widget->GetCompositor();
      ASSERT_TRUE(compositor);
      SCOPED_TRACE(testing::Message()
                   << "widget=" << widget->GetName() << ", role="
                   << (widget == view->GetWidget()        ? "body"
                       : widget == view->overlay_widget() ? "toolbar-overlay"
                       : widget == view->tab_overlay_widget() ? "tab-overlay"
                                                              : "other")
                   << ", bounds="
                   << widget->GetWindowBoundsInScreen().ToString()
                   << ", visible=" << widget->IsVisible()
                   << ", active=" << widget->IsActive()
                   << ", compositor_visible=" << compositor->IsVisible()
                   << ", compositor_size=" << compositor->size().ToString());
      base::test::ScopedRunLoopTimeout frame_timeout(FROM_HERE,
                                                     base::Seconds(5));
      base::test::TestFuture<viz::FrameTimingDetails> presented;
      // 표시 콜백과 readback을 같은 commit에 실어 새 출력의 표시를 기다린다.
      compositor->RequestSuccessfulPresentationTimeForNextFrame(
          presented.GetCallback<const viz::FrameTimingDetails&>());
      base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
      auto request = std::make_unique<viz::CopyOutputRequest>(
          viz::CopyOutputRequest::ResultFormat::RGBA,
          viz::CopyOutputRequest::ResultDestination::kSystemMemory,
          readback.GetCallback());
      request->set_area(gfx::Rect(1, 1));
      compositor->root_layer()->RequestCopyOfOutput(std::move(request));
      compositor->ScheduleFullRedraw();
      ASSERT_TRUE(presented.Wait());
      ASSERT_TRUE(readback.Wait());
      auto result = readback.Take();
      ASSERT_TRUE(result && !result->IsEmpty());
    }
  };
  pref_service->SetBoolean(prefs::kShowFullscreenToolbar, true);
  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kContent));
  // 앞선 탭 포커스가 전체화면의 헤더 표시 잠금을 만들지 않게 한다.
  ASSERT_TRUE(view->GetActiveContentsWebView());
  view->GetActiveContentsWebView()->RequestFocus();
  ASSERT_TRUE(view->GetActiveContentsWebView()->HasFocus());
  auto enter_conditions = PageConditions(true);
  enter_conditions.push_back(SurfaceTransition(*view, true, true));
  enter_conditions.push_back(applied_.View(
      *view->top_container(), {.parent = applied_.views().ObserveView(*view)},
      yee::test::AppliedMatch::kViewParentDifferent));
  enter_conditions.push_back(applied_.Condition(
      applied_.views().ObserveNativeWindow(*view->GetWidget()),
      yee::test::NativeWindowMatches{.fullscreen = true},
      yee::test::AppliedMatch::kNativeFullscreen));
  ASSERT_TRUE(applied_.Run(
      enter_conditions,
      [&] {
        ui_test_utils::ToggleFullscreenModeAndWait(browser());
        // Browser 상태와 별도로 AppKit 전환과 표시 중인 native 호스트를
        // 기다린다.
        native_fullscreen.WaitForEnterAndExitCount(1, 0);
      },
      yee::test::AppliedDomain::kNativeWindow));
  ASSERT_TRUE(([&]() { return view->top_container()->parent() != view; })());
  ASSERT_NO_FATAL_FAILURE(wait_for_header_frames());
  // immersive Header는 BrowserView 밖에 있다. 연결된 Surface 정책을 유지해
  // 별도 카드 윤곽을 그리지 않아야 한다.
  EXPECT_EQ(yee::BrowserSurfaceMode::kConnectedTabs,
            yee::GetBrowserSurfaceMode(*browser()));
  auto* const immersive_header = view->top_container();
  auto* const immersive_background =
      immersive_header->background()->AsA<CustomCornersBackground>();
  ASSERT_TRUE(immersive_background);
  EXPECT_TRUE(immersive_background->is_visible());
  EXPECT_FALSE(immersive_header->clip_path().contains(0.5f, 0.5f));
  EXPECT_FALSE(immersive_header->clip_path().contains(
      immersive_header->width() - 0.5f, 0.5f));
  EXPECT_TRUE(immersive_header->clip_path().contains(
      immersive_header->width() / 2.f, 0.5f));
  const SkPath header_clip = immersive_header->clip_path();
  const SkPath header_background_path =
      immersive_background->GetBackgroundPath();
  ASSERT_TRUE(view->GetYeeHeaderPresentation());
  const auto recolored_header =
      yee::ResolveBrowserSurfacePresentation(SK_ColorBLACK, 1, 1, 1);
  yee::UpdateHorizontalTabHeaderBackground(*immersive_header, recolored_header);
  EXPECT_EQ(header_clip, immersive_header->clip_path());
  EXPECT_EQ(header_background_path, immersive_background->GetBackgroundPath());
  EXPECT_TRUE(immersive_background->is_visible());
  gfx::Canvas header_canvas(immersive_header->size(), 1.f, false);
  immersive_background->Paint(&header_canvas, immersive_header);
  const SkBitmap header_pixels = header_canvas.GetBitmap();
  for (int x : {1, immersive_header->width() - 2}) {
    EXPECT_EQ(recolored_header.surface,
              header_pixels.getColor(x, immersive_header->height() / 2));
    EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(header_pixels.getColor(x, 0)));
  }
  yee::UpdateHorizontalTabHeaderBackground(*immersive_header,
                                           view->GetYeeHeaderPresentation());
  auto* const immersive_outline = view->top_container()->parent()->GetViewByID(
      yee::kCombinedSurfaceOutlineViewId);
  ASSERT_TRUE(immersive_outline);
  const gfx::Rect immersive_outline_bounds = immersive_outline->bounds();
  immersive_outline->SetPosition(gfx::Point());
  SkBitmap immersive_border;
  {
    ui::CanvasPainter painter(&immersive_border, immersive_outline->size(), 1.f,
                              SK_ColorTRANSPARENT, false);
    immersive_outline->Paint(views::PaintInfo::CreateRootPaintInfo(
        painter.context(), immersive_outline->size()));
  }
  immersive_outline->SetBoundsRect(immersive_outline_bounds);
  ASSERT_GT(immersive_border.width(), 0);
  EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(immersive_border.getColor(
                                     immersive_border.width() / 2, 0)));
  EXPECT_EQ(yee::kSidebarMetrics.browser_surface_header_height(),
            view->top_container()->height());
  {
    const gfx::Rect toolbar = view->toolbar()->GetBoundsInScreen();
    const gfx::Rect body = view->multi_contents_view()->GetBoundsInScreen();
    auto* const immersive = static_cast<ImmersiveModeControllerMac*>(
        ImmersiveModeController::From(browser()));
    const auto* const fullscreen_focus =
        view->GetFocusManager()->GetFocusedView();
    const auto* const fullscreen_stored_focus =
        view->GetFocusManager()->GetStoredFocusView();
    SCOPED_TRACE(testing::Message()
                 << "전체화면 메뉴 노출="
                 << immersive->menu_bar_reveal_amount_for_testing()
                 << ", reveal=" << immersive->IsRevealed() << ", focus="
                 << (fullscreen_focus ? fullscreen_focus->GetClassName()
                                      : "null")
                 << ", stored focus="
                 << (fullscreen_stored_focus
                         ? fullscreen_stored_focus->GetClassName()
                         : "null")
                 << ", client="
                 << view->GetWidget()->GetClientAreaBoundsInScreen().ToString()
                 << ", header client="
                 << view->top_container()
                        ->GetWidget()
                        ->GetClientAreaBoundsInScreen()
                        .ToString());
    EXPECT_LE(toolbar.x() - body.x(),
              yee::kSidebarMetrics.browser_surface_header_control_edge_inset);
    EXPECT_GE(
        toolbar.right(),
        body.right() -
            yee::kSidebarMetrics.browser_surface_header_control_edge_inset);
    EXPECT_GE(body.y(), toolbar.bottom())
        << "body=" << body.ToString() << ", toolbar=" << toolbar.ToString();
    EXPECT_LE(body.y(), toolbar.bottom() + yee::kSidebarMetrics.content_gutter);
    EXPECT_FALSE(
        view->tab_strip_view()->GetBoundsInScreen().Intersects(toolbar));
    EXPECT_GE(view->side_panel()->GetBoundsInScreen().y(), toolbar.bottom());
    EXPECT_EQ(active_handle, model->GetActiveTab()->GetHandle());
  }
  if (!capture_directory.empty()) {
    // 두 캡처는 별도 native 호스트의 자료다. 앞의 직접 View paint는 각 호스트의
    // 로컬 정책을 증명하며 최종 합성 접합부까지 증명하지는 않는다.
    CaptureYeeWindowFrame(view->GetWidget(), capture_directory.AppendASCII(
                                                 "fullscreen-header-join.png"));
    CaptureYeeCompositorFrame(
        immersive_header->GetWidget(),
        capture_directory.AppendASCII("fullscreen-header-join-header.png"));
  }
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  native_fullscreen.WaitForEnterAndExitCount(1, 1);
  ASSERT_NO_FATAL_FAILURE(wait_for_header_frames());
  pref_service->SetBoolean(prefs::kShowFullscreenToolbar, always_show);
  expect_rows(true);
  EXPECT_EQ(view, view->top_container()->parent());
  EXPECT_TRUE(view->top_container()
                  ->background()
                  ->AsA<CustomCornersBackground>()
                  ->is_visible());
  RunTestSequence(CloseSidePanel());
  expect_rows(false);
#endif

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();
  ASSERT_TRUE(view->multi_contents_view()->IsInSplitView());
  EXPECT_TRUE(view->tab_strip_view()->GetVisible());
  EXPECT_LE(view->tab_strip_view()->GetBoundsInScreen().bottom() -
                GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap),
            view->multi_contents_view()->GetBoundsInScreen().y());
  EXPECT_NE(view->toolbar(), view->toolbar()->location_bar_view()->parent());

  const std::array<int, 2> split_indices = {4, model->active_index()};
  const auto expect_split_capsules = [&]() {
    for (int index : split_indices) {
      SCOPED_TRACE(index);
      auto* const tab = GetTabAnchorForModelIndex(view, index);
      ASSERT_TRUE(tab);
      const auto* styling = tab_styling(tab);
      ASSERT_TRUE(styling);
      const SkPath paint =
          styling->GetPath(TabStyle::PathType::kActiveTab, 1.f, {});
      const auto hit = styling->GetPath(TabStyle::PathType::kHitTest, 1.f, {});
      // 캡슐은 독립 Pane Header 위에 여유를 둔다.
      // native 입력 영역은 탭 행 하단까지 유지한다.
      const float center = tab->width() / 2.f;
      EXPECT_FALSE(paint.contains(center, tab->height() - 1.f));
      EXPECT_TRUE(paint.contains(center, tab->height() / 2.f));
      EXPECT_TRUE(hit.contains(center, tab->height() - 1.f));
      views::View* icon = nullptr;
      const auto find_icon = [&](auto&& self, views::View* child) -> void {
        if (views::IsViewClass<TabIcon>(child)) {
          icon = child;
        }
        for (views::View* descendant : child->children()) {
          self(self, descendant);
        }
      };
      find_icon(find_icon, tab);
      ASSERT_TRUE(icon);
      ASSERT_TRUE(icon->GetVisible());
      gfx::Point icon_center = icon->GetLocalBounds().CenterPoint();
      views::View::ConvertPointToTarget(icon, tab, &icon_center);
      // 양쪽 바깥 모서리를 포함해 식별용 glyph 전체 공간을 보장한다.
      for (float dx : {-7.5f, 7.5f}) {
        for (float dy : {-7.5f, 7.5f}) {
          EXPECT_TRUE(
              paint.contains(icon_center.x() + dx, icon_center.y() + dy));
        }
      }
      const auto colors = styling->CalculateTargetColors();
      EXPECT_GE(color_utils::GetContrastRatio(colors.foreground_color,
                                              colors.background_color),
                color_utils::kMinimumReadableContrastRatio);
    }
  };
  expect_split_capsules();
  // 수평 배치는 유지해도 분할에서는 공용 Header가 없다. 양쪽 Side Panel은
  // 독립 카드의 둥근 상단과 윤곽을 실제 backing에 적용해야 한다.
  const bool panel_was_right_aligned =
      browser()->GetProfile()->GetPrefs()->GetBoolean(
          prefs::kSidePanelHorizontalAlignment);
  for (bool right_aligned : {false, true}) {
    browser()->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kSidePanelHorizontalAlignment, right_aligned);
    RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar));
    ApplyScheduledLayout();
    EXPECT_TRUE(
        layout->last_native_layout_for_testing()->header_bounds.IsEmpty());
    auto* const panel = view->GetViewByID(yee::kSidePanelSurfaceViewId);
    ASSERT_TRUE(panel);
    ASSERT_TRUE(panel->IsDrawn());
    const gfx::Rect panel_bounds = panel->bounds();
    panel->SetPosition(gfx::Point());
    SkBitmap panel_paint;
    {
      ui::CanvasPainter painter(&panel_paint, panel->size(), 1.f,
                                SK_ColorTRANSPARENT, false);
      panel->Paint(views::PaintInfo::CreateRootPaintInfo(painter.context(),
                                                         panel->size()));
    }
    panel->SetBoundsRect(panel_bounds);
    EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(panel_paint.getColor(0, 0)));
    EXPECT_EQ(SK_AlphaTRANSPARENT,
              SkColorGetA(panel_paint.getColor(panel_paint.width() - 1, 0)));
    EXPECT_GT(SkColorGetA(panel_paint.getColor(0, panel_paint.height() / 2)),
              0u);
    RunTestSequence(CloseSidePanel());
  }
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kSidePanelHorizontalAlignment, panel_was_right_aligned);
  const gfx::Rect split_window = view->GetWidget()->GetWindowBoundsInScreen();
  gfx::Rect narrow_split_window = split_window;
  narrow_split_window.set_width(700);
  view->GetWidget()->SetBounds(narrow_split_window);
  ApplyScheduledLayout();
  expect_split_capsules();
  view->GetWidget()->SetBounds(split_window);
  ApplyScheduledLayout();

  if (!split_capture_directory.empty()) {
    auto* const theme =
        ThemeServiceFactory::GetForProfile(browser()->GetProfile());
    const auto scheme = theme->GetBrowserColorScheme();
    view->GetWidget()->GetFocusManager()->ClearFocus();
    for (int index : split_indices) {
      auto* const contents = model->GetWebContentsAt(index);
      if (const auto background = contents->GetBackgroundColor()) {
        auto* const member_source =
            yee::BrowserSurfaceColorController::FromWebContents(contents);
        ASSERT_TRUE(member_source);
        member_source->SetPageSurfaceColorForTesting(*background);
      }
    }
    for (bool dark : {false, true}) {
      theme->SetBrowserColorScheme(
          dark ? ThemeService::BrowserColorScheme::kDark
               : ThemeService::BrowserColorScheme::kLight);
      model->ActivateTabAt(split_indices.back());
      auto* const contents = model->GetActiveWebContents();
      ASSERT_TRUE(Await({applied_.Condition(
          applied_.source().ObserveRendererBackground(*contents),
          yee::test::PresentationMatches{.use_native_theme = dark},
          yee::test::AppliedMatch::kPresentationDark)}));
      ASSERT_TRUE(([&]() {
        const auto color = contents->GetBackgroundColor();
        return color && color_utils::IsDark(*color) == dark;
      })());
      auto* const capture_source =
          yee::BrowserSurfaceColorController::FromWebContents(contents);
      ASSERT_TRUE(capture_source);
      capture_source->SetPageSurfaceColorForTesting(
          *contents->GetBackgroundColor());
      for (bool narrow : {false, true}) {
        view->GetWidget()->SetBounds(narrow ? narrow_split_window
                                            : split_window);
        ApplyScheduledLayout();
        expect_split_capsules();
        CaptureYeeWindowFrame(
            view->GetWidget(),
            split_capture_directory.AppendASCII(
                base::StringPrintf("split-%s-%s.png", dark ? "dark" : "light",
                                   narrow ? "narrow" : "wide")));
        capture_group(base::StringPrintf("split-%s-%s", dark ? "dark" : "light",
                                         narrow ? "narrow" : "wide"));
      }
      view->GetWidget()->SetBounds(split_window);
      ApplyScheduledLayout();
      if (!dark) {
        // native 분할 클릭은 마지막 포커스 pane을 복원한다. 다른 탭을 먼저
        // 선택해 실제 클릭으로 분할 묶음을 선택했는지 확인한다.
        model->ActivateTabAt(0);
        ApplyScheduledLayout();
        auto* const tab =
            GetTabAnchorForModelIndex(view, split_indices.front());
        ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
        const auto current_split_tab = [&]() {
          return GetTabAnchorForModelIndex(view, split_indices.front());
        };
        ASSERT_TRUE(input_geometry_ready(current_split_tab, current_split_tab));
        const auto window = tab->GetWidget()->GetNativeWindow();
        ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
            current_split_tab()->GetBoundsInScreen().CenterPoint(), window));
        CaptureYeeWindowFrame(
            view->GetWidget(),
            split_capture_directory.AppendASCII("split-light-hover.png"));
        ApplyScheduledLayout();
        ASSERT_TRUE(pointer_matches(current_split_tab(), current_split_tab(),
                                    "yee_split_tab_pointer_after_capture"));
        ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
            ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP, window));
        ASSERT_TRUE((
            [&]() { return model->active_index() == split_indices.back(); })());
        ASSERT_TRUE(view->tab_strip_view()->SetPaneFocus(tab));
        auto* const ring = views::FocusRing::Get(tab);
        ASSERT_TRUE(ring);
        EXPECT_TRUE(ring->ShouldPaintForTesting());
        CaptureYeeWindowFrame(
            view->GetWidget(),
            split_capture_directory.AppendASCII("split-light-focus.png"));
        view->GetWidget()->GetFocusManager()->ClearFocus();
        ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
            view->multi_contents_view()->GetBoundsInScreen().CenterPoint(),
            window));
      }
      model->ActivateTabAt(0);
      ApplyScheduledLayout();
      expect_split_capsules();
      CaptureYeeWindowFrame(
          view->GetWidget(),
          split_capture_directory.AppendASCII(base::StringPrintf(
              "split-%s-inactive.png", dark ? "dark" : "light")));
      capture_group(
          base::StringPrintf("split-%s-inactive", dark ? "dark" : "light"));
    }
    theme->SetBrowserColorScheme(scheme);
    model->ActivateTabAt(split_indices.back());
    ApplyScheduledLayout();
  }

#if BUILDFLAG(IS_MAC)
  // immersive 재부모화와 분할 rehosting을 함께 검사한다.
  // 이전에는 두 전환을 각각 확인했다.
  const auto split_id = model->GetSplitForTab(model->active_index());
  ASSERT_TRUE(split_id);
  ASSERT_EQ(split_id, model->GetSplitForTab(4));
  const auto inspect_split_group = [&](const char* name) {
    SCOPED_TRACE(name);
    expect_split_capsules();
    auto* const strip = view->tab_strip_view();
    int marker_count = 0;
    const auto visit = [&](auto&& self, views::View* child) -> void {
      views::View* marker = views::AsViewClass<TabGroupUnderline>(child);
      if (auto* unified = views::AsViewClass<TabGroupView>(child)) {
        marker = unified->group_line();
      }
      if (marker && marker->IsDrawn()) {
        ++marker_count;
        const gfx::RectF local = views::View::ConvertRectToTarget(
            marker, strip, gfx::RectF(marker->GetLocalBounds()));
        EXPECT_TRUE(gfx::RectF(strip->GetLocalBounds()).Contains(local));
        ASSERT_TRUE(yee::UsesHorizontalTabGroupCapsule(*marker));
        {
          EXPECT_GT(local.height(),
                    yee::kSidebarMetrics.horizontal_group_chip_height);
          const float scale =
              marker->GetWidget()->GetCompositor()->device_scale_factor();
          gfx::Canvas group_canvas(marker->size(), scale, false);
          if (auto* legacy = views::AsViewClass<TabGroupUnderline>(marker)) {
            legacy->OnPaint(&group_canvas);
          } else {
            ASSERT_TRUE(marker->background());
            marker->background()->Paint(&group_canvas, marker);
          }
          const SkBitmap pixels = group_canvas.GetBitmap();
          const auto capsule_paint =
              pixels.getColor(pixels.width() / 2, pixels.height() / 2);
          EXPECT_GT(SkColorGetA(capsule_paint), SK_AlphaTRANSPARENT);
          EXPECT_LT(SkColorGetA(capsule_paint), SK_AlphaOPAQUE);
          const SkColor surface = color_utils::GetResultingPaintColor(
              capsule_paint, yee::ResolveShellBackgroundColor(
                                 *marker->GetColorProvider(),
                                 marker->GetWidget()->ShouldPaintAsActive()));
          EXPECT_EQ(SK_AlphaTRANSPARENT, SkColorGetA(pixels.getColor(0, 0)));
          const auto inspect_title = [&](auto&& self,
                                         views::View* child) -> void {
            if (auto* label = views::AsViewClass<views::Label>(child);
                label && label->IsDrawn() && !label->GetText().empty() &&
                !label->bounds().IsEmpty()) {
              const auto title = views::View::ConvertRectToTarget(
                  label, strip, gfx::RectF(label->GetLocalBounds()));
              EXPECT_TRUE(local.Contains(title))
                  << "capsule=" << local.ToString()
                  << ", title=" << title.ToString();
              EXPECT_GE(color_utils::GetContrastRatio(label->GetEnabledColor(),
                                                      surface),
                        color_utils::kMinimumReadableContrastRatio);
            }
            for (views::View* descendant : child->children()) {
              self(self, descendant);
            }
          };
          inspect_title(inspect_title, group_header());
          // 실제 배율의 raster로 경계 간격과 모든 멤버의 포함 범위를 확인한다.
          expect_boundary_gap(0, true);
          for (int index : tab_group->ListTabs().ToIntVector()) {
            auto* const member =
                strip->GetTabAnchorView(view->browser()
                                            ->GetTabStripModel()
                                            ->GetTabAtIndex(index)
                                            ->GetHandle());
            const auto* styling = tab_styling(member);
            ASSERT_TRUE(styling);
            const SkRect paint =
                styling->GetPath(TabStyle::PathType::kActiveTab, scale, {})
                    .getBounds();
            const gfx::RectF painted = views::View::ConvertRectToTarget(
                member, strip,
                gfx::RectF(paint.x() / scale, paint.y() / scale,
                           paint.width() / scale, paint.height() / scale));
            EXPECT_TRUE(local.Contains(painted));
          }
        }
        // unified Header는 행 중앙의 chip이다. 캡슐은 실제 탭 행에 정렬한다.
        auto* const member =
            GetTabAnchorForModelIndex(view, tab_group->ListTabs().start());
        ASSERT_TRUE(member);
        const gfx::RectF native_row = views::View::ConvertRectToTarget(
            member, strip, gfx::RectF(member->GetLocalBounds()));
        EXPECT_EQ(native_row.y() +
                      yee::kSidebarMetrics.horizontal_group_capsule_top_inset,
                  local.y());
        EXPECT_NE(marker, strip->GetEventHandlerForPoint(
                              gfx::ToRoundedRect(local).CenterPoint()));
        EXPECT_LE(
            local.bottom(),
            strip->height() -
                GetLayoutConstant(LayoutConstant::kTabstripToolbarOverlap));
      }
      for (views::View* descendant : child->children()) {
        self(self, descendant);
      }
    };
    visit(visit, view->tab_strip_view());
    EXPECT_GT(marker_count, 0);
    EXPECT_TRUE(view->multi_contents_view()->IsInSplitView());
    capture_group(name);
    const auto& split_captures = split_capture_directory.empty()
                                     ? capture_directory
                                     : split_capture_directory;
    if (!split_captures.empty()) {
      CaptureYeeWindowFrame(
          view->GetWidget(),
          split_captures.AppendASCII(base::StringPrintf("%s.png", name)));
      if (group_header()->GetWidget() != view->GetWidget()) {
        // AppKit은 옮겨진 NSView로 immersive accessory를 표시한다.
        // 화면 밖의 원래 NSWindow는 SCK가 공유할 수 있는 창이 아니다.
        CaptureYeeCompositorFrame(group_header()->GetWidget(),
                                  split_captures.AppendASCII(base::StringPrintf(
                                      "%s-header.png", name)));
      }
    }
  };
  inspect_split_group("split-windowed");
  ASSERT_TRUE(view->GetActiveContentsWebView());
  view->GetActiveContentsWebView()->RequestFocus();
  ASSERT_TRUE(view->GetActiveContentsWebView()->HasFocus());
  pref_service->SetBoolean(prefs::kShowFullscreenToolbar, true);
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  native_fullscreen.WaitForEnterAndExitCount(2, 1);
  ASSERT_NO_FATAL_FAILURE(wait_for_header_frames());
  inspect_split_group("split-fullscreen-second");
  model->ActivateTabAt(4);
  ApplyScheduledLayout();
  inspect_split_group("split-fullscreen-first");
  ui_test_utils::ToggleFullscreenModeAndWait(browser());
  native_fullscreen.WaitForEnterAndExitCount(2, 2);
  ASSERT_NO_FATAL_FAILURE(wait_for_header_frames());
  pref_service->SetBoolean(prefs::kShowFullscreenToolbar, always_show);
  ApplyScheduledLayout();
  inspect_split_group("split-fullscreen-exit");
#endif

  // 분할로 재호스팅된 실제 버튼도 같은 방향 전환 명령을 사용한다.
  RunTestSequence(PressButton(kVerticalTabStripCollapseButtonElementId));
  ApplyScheduledLayout();
  EXPECT_TRUE(state->ShouldDisplayVerticalTabs());
  EXPECT_TRUE(view->multi_contents_view()->IsInSplitView());
  state->SetVerticalTabsEnabled(false);
  ApplyScheduledLayout();
  EXPECT_TRUE(view->multi_contents_view()->IsInSplitView());
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(
    YeeBrowserViewTabbedLayoutImplTabAnimationUiTest,
    YeeMacHorizontalFullscreenExitRestoresToolbarWithoutFocus) {
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(view);
  browser()->tab_strip_model()->AddToNewGroup({0});
  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>Mac fullscreen toolbar</title>"
                      "<style>html,body{background:gray}</style>"
                      "<p>Keep page focus</p>")));
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(false);
  ApplyScheduledLayout();
  auto* const pref_service = browser()->GetProfile()->GetPrefs();
  const bool original = pref_service->GetBoolean(prefs::kShowFullscreenToolbar);
  for (bool always_show : {false, true}) {
    SCOPED_TRACE(always_show);
    const auto started = base::TimeTicks::Now();
    const std::string prefix = always_show ? "toolbar_" : "autohide_";
    const auto record_window_state = [&](const std::string& phase) {
      auto* const widget = view->GetWidget();
      const std::string key = prefix + phase;
      RecordProperty(
          key + "_ms",
          std::to_string((base::TimeTicks::Now() - started).InMilliseconds()));
      RecordProperty(key + "_fullscreen", view->IsFullscreen());
      RecordProperty(key + "_active", widget->IsActive());
      RecordProperty(key + "_paint_active", widget->ShouldPaintAsActive());
      RecordProperty(key + "_visible", widget->IsVisible());
      RecordProperty(key + "_visible_on_screen", widget->IsVisibleOnScreen());
      RecordProperty(key + "_page_focus",
                     view->GetActiveContentsWebView()->HasFocus());
      auto* const manager = widget->GetFocusManager();
      auto* const focused = manager->GetFocusedView();
      auto* const stored = manager->GetStoredFocusView();
      RecordProperty(key + "_focused_view",
                     std::string(focused ? focused->GetClassName() : "none"));
      RecordProperty(key + "_stored_view",
                     std::string(stored ? stored->GetClassName() : "none"));
    };
    const auto native_window_ready = [&]() {
      auto* const widget = view->GetWidget();
      return widget->IsVisibleOnScreen() && widget->IsActive() &&
             widget->ShouldPaintAsActive();
    };
    pref_service->SetBoolean(prefs::kShowFullscreenToolbar, always_show);
    ApplyScheduledLayout();
    // Space 복귀가 끝나기 전의 key-window 알림만으로 다음 키를 보내지 않는다.
    {
      base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(5));
      ASSERT_TRUE(applied_.Run(
          {applied_.Widget(*view->GetWidget(), {},
                           yee::test::AppliedMatch::kWidgetReady)},
          [&] { ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser())); },
          yee::test::AppliedDomain::kNativeWindow, base::Seconds(5)));
      ASSERT_TRUE((native_window_ready)());
    }
    view->GetActiveContentsWebView()->RequestFocus();
    ASSERT_TRUE(view->GetActiveContentsWebView()->HasFocus());
    ui::NSWindowFullscreenNotificationWaiter native_fullscreen(
        view->GetWidget()->GetNativeWindow());
    record_window_state("before_enter");
    auto fullscreen_conditions = PageConditions(true);
    fullscreen_conditions.push_back(
        SurfaceTransition(*view, true, always_show));
    fullscreen_conditions.push_back(applied_.Condition(
        applied_.views().ObserveNativeWindow(*view->GetWidget()),
        yee::test::NativeWindowMatches{.fullscreen = true},
        yee::test::AppliedMatch::kNativeFullscreen));
    ASSERT_TRUE(applied_.Run(
        fullscreen_conditions,
        [&] {
          ASSERT_TRUE(yee::test::SendKeyPressSync(
              browser(), ui::VKEY_F, true, false, false, true));
          native_fullscreen.WaitForEnterAndExitCount(1, 0);
        },
        yee::test::AppliedDomain::kNativeWindow, base::Seconds(5)));
    record_window_state("after_enter");
    ASSERT_TRUE(view->IsFullscreen());
    base::test::TestFuture<viz::FrameTimingDetails> exit_presented;
    view->GetWidget()
        ->GetCompositor()
        ->RequestSuccessfulPresentationTimeForNextFrame(
            exit_presented.GetCallback<const viz::FrameTimingDetails&>());
    auto exit_conditions = WindowedConditions(*view);
    exit_conditions.push_back(applied_.Widget(
        *view->GetWidget(), {}, yee::test::AppliedMatch::kWidgetReady));
    exit_conditions.push_back(
        applied_.Focus(*view->GetActiveContentsWebView()));
    // 실제 적용된 geometry 뒤 성공한 frame도 같은 요청에 연결한다.
    exit_conditions.push_back(applied_.views().ObservePresentedAfterApply(
        *view->GetWidget()->GetRootView(), exit_conditions));
    // 키보드 초점이나 강제 layout 없이 실제 종료 알림만으로 복원돼야 한다.
    base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(5));
    ASSERT_TRUE(applied_.Run(
        exit_conditions,
        [&] {
          ASSERT_TRUE(yee::test::SendKeyPressSync(
              browser(), ui::VKEY_F, true, false, false, true));
          native_fullscreen.WaitForEnterAndExitCount(1, 1);
          record_window_state("after_exit");
        },
        yee::test::AppliedDomain::kNativeWindow, base::Seconds(5)));
    ASSERT_TRUE(([&]() {
      return !view->IsFullscreen() && view->top_container()->parent() == view &&
             view->toolbar()->IsDrawn() &&
             view->toolbar()->height() == yee::kSidebarMetrics.toolbar_height &&
             view->top_container()->height() ==
                 yee::kSidebarMetrics.browser_surface_header_height() &&
             view->multi_contents_view()->GetBoundsInScreen().y() >=
                 view->toolbar()->GetBoundsInScreen().bottom();
    })())
        << "top=" << view->top_container()->bounds().ToString()
        << ", toolbar=" << view->toolbar()->bounds().ToString()
        << ", visible=" << view->toolbar()->GetVisible() << ", immersive="
        << ImmersiveModeController::From(browser())->IsEnabled();
    ASSERT_TRUE(exit_presented.Wait());
    auto* const layer = view->top_container()->layer();
    ASSERT_TRUE(layer);
    EXPECT_TRUE(layer->visible());
    EXPECT_TRUE(layer->GetTargetVisibility());
    EXPECT_GT(layer->opacity(), 0.f);
    EXPECT_GT(layer->GetTargetOpacity(), 0.f);
    EXPECT_EQ(view->GetWidget()->GetCompositor(), layer->GetCompositor());
    ui::AXNodeData toolbar_ax;
    view->toolbar()->GetViewAccessibility().GetAccessibleNodeData(&toolbar_ax);
    EXPECT_FALSE(toolbar_ax.HasState(ax::mojom::State::kInvisible));
    const auto captures =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "yee-tabbar-capture-dir");
    if (!captures.empty()) {
      // native 현재 출력을 읽기 전에 layout·redraw를 요청하지 않는다.
      base::test::TestFuture<scoped_refptr<base::RefCountedMemory>> snapshot;
      ui::GrabWindowSnapshotAsPNG(
          view->GetWidget()->GetNativeWindow(),
          gfx::Rect(view->GetWidget()->GetWindowBoundsInScreen().size()),
          snapshot.GetCallback());
      auto png = snapshot.Take();
      ASSERT_TRUE(png && png->size());
      ASSERT_TRUE(base::WriteFile(
          captures.AppendASCII(always_show ? "natural-exit-toolbar.png"
                                           : "natural-exit-autohide.png"),
          static_cast<base::span<const uint8_t>>(*png)));
    }
    // 활성화가 늦게 복원돼도 초점을 강제로 옮기지 않고 자연 복귀를 검사한다.
    record_window_state("before_focus_wait");

    EXPECT_TRUE(([&]() {
      return native_window_ready() &&
             view->GetActiveContentsWebView()->HasFocus();
    })());
    record_window_state("after_focus_wait");
  }
  pref_service->SetBoolean(prefs::kShowFullscreenToolbar, original);
}
// native 창 크기 변경의 회귀 검사다. 실제 창 메뉴·화면 이동은 다루지 않는다.
// resize 뒤 layout/redraw를 강제 호출하지 않고 chrome 배치를 확인한다.
IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplTabAnimationUiTest,
                       YeeMacNativeResizeKeepsWindowedChromeGeometry) {
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(view);
  auto* const widget = view->GetWidget();
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(widget && state);
  ASSERT_FALSE(view->IsFullscreen());
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  const gfx::Rect original = widget->GetWindowBoundsInScreen();
  gfx::Rect work_area = display::Screen::Get()
                            ->GetDisplayNearestWindow(widget->GetNativeWindow())
                            .work_area();
  work_area.Inset(8);
  const int tab_count = browser()->tab_strip_model()->count();
  auto* const active_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active_contents);

  for (bool vertical : {true, false}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    ApplyScheduledLayout();
    const gfx::Size minimum = widget->GetMinimumSize();
    if (minimum.width() > work_area.width() ||
        minimum.height() > work_area.height()) {
      GTEST_SKIP() << "화면이 native 창의 최소 크기보다 작음";
    }
    const gfx::Size compact_size(
        std::max(minimum.width(), std::min(720, work_area.width() - 32)),
        std::max(minimum.height(), std::min(1000, work_area.height() - 32)));
    if (compact_size == work_area.size()) {
      GTEST_SKIP() << "서로 다른 유효 native 창 크기를 만들 수 없음";
    }
    const gfx::Rect compact(work_area.origin(), compact_size);
    for (const auto& [phase, bounds] :
         {std::pair{"compact", compact}, std::pair{"fill-bounds", work_area},
          std::pair{"restore", original}}) {
      SCOPED_TRACE(phase);
      if (widget->GetWindowBoundsInScreen().size() == bounds.size()) {
        continue;
      }
      base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(5));
      base::test::TestFuture<viz::FrameTimingDetails> presented;
      widget->GetCompositor()->RequestSuccessfulPresentationTimeForNextFrame(
          presented.GetCallback<const viz::FrameTimingDetails&>());
      ASSERT_TRUE(applied_.Run(
          ResizeConditions(*view, bounds), [&] { widget->SetBounds(bounds); },
          yee::test::AppliedDomain::kLayout, base::Seconds(5)));
      ASSERT_TRUE(([&]() {
        const gfx::Rect actual = widget->GetWindowBoundsInScreen();
        auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
            view->GetBrowserViewLayoutForTesting());
        views::View* const strip =
            vertical ? static_cast<views::View*>(
                           view->vertical_tab_strip_region_view_for_testing())
                     : static_cast<views::View*>(view->tab_strip_view());
        return std::abs(actual.width() - bounds.width()) <= 1 &&
               std::abs(actual.height() - bounds.height()) <= 1 &&
               layout->last_native_layout_for_testing() &&
               !layout->last_native_layout_for_testing()
                    ->header_bounds.IsEmpty() &&
               layout->last_native_layout_for_testing()
                       ->multi_contents_bounds ==
                   view->multi_contents_view()->bounds() &&
               view->toolbar()->IsDrawn() &&
               !view->toolbar()->GetVisibleBounds().IsEmpty() && strip &&
               strip->IsDrawn() && !strip->GetVisibleBounds().IsEmpty();
      })())
          << "requested=" << bounds.ToString()
          << ", actual=" << widget->GetWindowBoundsInScreen().ToString();
      ASSERT_TRUE(presented.Wait());
      auto* const layer = view->top_container()->layer();
      ASSERT_TRUE(layer);
      EXPECT_TRUE(layer->visible());
      EXPECT_TRUE(layer->GetTargetVisibility());
      EXPECT_GT(layer->opacity(), 0.f);
      EXPECT_EQ(widget->GetCompositor(), layer->GetCompositor());
      EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
      EXPECT_EQ(active_contents,
                browser()->tab_strip_model()->GetActiveWebContents());
      RecordProperty(std::string(vertical ? "sidebar_" : "horizontal_") + phase,
                     widget->GetWindowBoundsInScreen().ToString());
    }
  }
}
#endif

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeNativePlannerResultMatchesAppliedLayout) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar));

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);

  const auto logical_bounds_in_browser =
      [browser_view](views::View* view) -> gfx::Rect {
    if (!view) {
      return gfx::Rect();
    }
    gfx::Rect bounds = view->bounds();
    for (views::View* parent = view->parent(); parent && parent != browser_view;
         parent = parent->parent()) {
      bounds.Offset(parent->x(), parent->y());
    }
    return bounds;
  };
  const auto expect_applied_result = [&]() {
    ASSERT_TRUE(layout->last_native_layout_for_testing());
    const auto& result = *layout->last_native_layout_for_testing();
    EXPECT_EQ(1, result.top_container_calculation_count);
    EXPECT_EQ(result.header_bounds, result.top_container_input_bounds);
    EXPECT_EQ(result.top_container_bounds,
              logical_bounds_in_browser(browser_view->top_container()));
    EXPECT_EQ(result.toolbar_bounds,
              logical_bounds_in_browser(browser_view->toolbar()));
    EXPECT_EQ(result.side_panel.visible,
              browser_view->side_panel()->GetVisible());
    if (result.side_panel.visible) {
      EXPECT_EQ(result.side_panel.current_bounds,
                logical_bounds_in_browser(browser_view->side_panel()));
      EXPECT_EQ(result.side_panel.local_clip_bounds,
                browser_view->side_panel()->clip_to_visible_area_for_testing());

      views::View* const side_panel_surface =
          browser_view->GetViewByID(yee::kSidePanelSurfaceViewId);
      ASSERT_TRUE(side_panel_surface);
      gfx::Rect expected_surface_bounds = result.side_panel.current_bounds;
      if (result.side_panel.local_clip_bounds) {
        expected_surface_bounds = *result.side_panel.local_clip_bounds;
        expected_surface_bounds.Intersect(
            gfx::Rect(result.side_panel.current_bounds.size()));
        expected_surface_bounds.Offset(
            result.side_panel.current_bounds.OffsetFromOrigin());
      }
      EXPECT_EQ(expected_surface_bounds,
                logical_bounds_in_browser(side_panel_surface));
      views::View* const resize_gutter =
          browser_view->GetViewByID(yee::kSidePanelResizeGutterViewId);
      ASSERT_TRUE(resize_gutter);
      EXPECT_EQ(yee::GetSidePanelResizeGutterBounds(expected_surface_bounds,
                                                    result.side_panel.leading),
                logical_bounds_in_browser(resize_gutter));
      EXPECT_FALSE(
          browser_view->side_panel()->resize_area_for_testing()->GetVisible());
      EXPECT_FALSE(logical_bounds_in_browser(resize_gutter)
                       .Intersects(expected_surface_bounds));
      EXPECT_EQ(result.side_panel.visible_width > 0 &&
                    !expected_surface_bounds.IsEmpty(),
                side_panel_surface->GetVisible());
      views::View* const surface_outline =
          browser_view->GetViewByID(yee::kCombinedSurfaceOutlineViewId);
      ASSERT_TRUE(surface_outline);
      EXPECT_EQ(result.combined_surface_outline_bounds,
                logical_bounds_in_browser(surface_outline));
      EXPECT_EQ(
          gfx::RoundedCornersF(
              0.0f, 0.0f, yee::kSidebarMetrics.split_pane_inner_corner_radius,
              yee::kSidebarMetrics.split_pane_inner_corner_radius),
          browser_view->side_panel()->background_radii());
    }
    EXPECT_EQ(result.main_shadow_bounds,
              logical_bounds_in_browser(browser_view->main_shadow_overlay()));
    EXPECT_EQ(result.main_shadow_visible,
              browser_view->main_shadow_overlay()->GetVisible());
    EXPECT_EQ(result.main_background_bounds,
              logical_bounds_in_browser(
                  browser_view->main_background_region_for_testing()));
    ASSERT_TRUE(browser_view->main_background_region_for_testing()->layer());
    EXPECT_FALSE(browser_view->main_background_region_for_testing()
                     ->layer()
                     ->fills_bounds_opaquely());
    EXPECT_EQ(result.main_background_normal_visibility ||
                  result.main_background_forced_for_clip,
              browser_view->main_background_region_for_testing()->GetVisible());
    EXPECT_EQ(result.multi_contents_bounds,
              logical_bounds_in_browser(browser_view->multi_contents_view()));
    EXPECT_EQ(result.native_content_animating,
              browser_view->multi_contents_view()
                  ->is_animating_content_for_testing());
    if (browser_view->multi_contents_view()->IsInSplitView()) {
      EXPECT_EQ(
          result.split_view_insets,
          browser_view->multi_contents_view()->split_view_insets_for_testing());
    }
    if (result.target_content_size) {
      ASSERT_TRUE(browser_view->multi_contents_view()
                      ->target_content_bounds_for_testing());
      EXPECT_EQ(*result.target_content_size,
                browser_view->multi_contents_view()
                    ->target_content_bounds_for_testing()
                    ->actual_size);
      EXPECT_EQ(result.target_content_clip_insets,
                browser_view->multi_contents_view()
                    ->target_content_bounds_for_testing()
                    ->clipped_area);
    } else {
      EXPECT_FALSE(browser_view->multi_contents_view()
                       ->target_content_bounds_for_testing());
    }
  };

  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kSharedRow,
            layout->last_native_layout_for_testing()->horizontal_plan.row_mode);
  EXPECT_FALSE(
      layout->last_native_layout_for_testing()->header_bounds.IsEmpty());
  EXPECT_EQ(layout->last_native_layout_for_testing()->header_bounds.right(),
            layout->last_native_layout_for_testing()
                ->combined_surface_outline_bounds.right());
  EXPECT_LE(
      logical_bounds_in_browser(browser_view->GetLocationBarView()).right(),
      layout->last_native_layout_for_testing()->header_bounds.right());
  expect_applied_result();

  // 작은 작업 영역에서는 실제 패널이 이미 상한에 있을 수 있다.
  const bool collapse_for_resize = GetSidePanelGrowHeadroomInDips() < 50;
  if (collapse_for_resize) {
    RunTestSequence(ToggleVerticalTabStripCollapsed(true));
    expect_applied_result();
  }
  ASSERT_GE(GetSidePanelGrowHeadroomInDips(), 50);
  views::View* const external_resize =
      browser_view->GetViewByID(yee::kSidePanelResizeGutterViewId);
  ASSERT_TRUE(external_resize);
  EXPECT_EQ(browser_view, external_resize->parent());
  gfx::Point gutter_center = external_resize->GetBoundsInScreen().CenterPoint();
  views::View::ConvertPointFromScreen(browser_view, &gutter_center);
  EXPECT_EQ(external_resize,
            browser_view->GetEventHandlerForPoint(gutter_center));
  const int panel_width_before = browser_view->side_panel()->width();
  ui::KeyEvent grow(ui::EventType::kKeyPressed,
                    browser_view->side_panel()->IsRightAligned()
                        ? ui::VKEY_LEFT
                        : ui::VKEY_RIGHT,
                    ui::EF_NONE);
  EXPECT_TRUE(external_resize->OnKeyPressed(grow));
  ApplyScheduledLayout();
  EXPECT_EQ(panel_width_before + 50, browser_view->side_panel()->width());
  expect_applied_result();
  if (collapse_for_resize) {
    RunTestSequence(ToggleVerticalTabStripCollapsed(false));
    expect_applied_result();
  }

  views::View* const resize_area =
      browser_view->side_panel()->resize_area_for_testing();
  ASSERT_TRUE(resize_area);
  EXPECT_EQ(browser_view->side_panel(), resize_area->parent());
  EXPECT_LT(browser_view->GetIndexOf(browser_view->multi_contents_view()),
            browser_view->GetIndexOf(browser_view->main_shadow_overlay()));
  EXPECT_LT(browser_view->GetIndexOf(browser_view->main_shadow_overlay()),
            browser_view->GetIndexOf(browser_view->side_panel()));
  views::View* const side_panel_surface =
      browser_view->GetViewByID(yee::kSidePanelSurfaceViewId);
  ASSERT_TRUE(side_panel_surface);
  EXPECT_LT(browser_view->GetIndexOf(browser_view->main_shadow_overlay()),
            browser_view->GetIndexOf(side_panel_surface));
  EXPECT_LT(browser_view->GetIndexOf(side_panel_surface),
            browser_view->GetIndexOf(browser_view->side_panel()));

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();
  ASSERT_TRUE(browser_view->multi_contents_view()->IsInSplitView());
  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar));
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& split_result = *layout->last_native_layout_for_testing();
  EXPECT_TRUE(split_result.header_bounds.IsEmpty());
  EXPECT_TRUE(split_result.top_container_bounds.IsEmpty());
  EXPECT_EQ(yee::kSidebarMetrics.content_gutter,
            split_result.side_panel.final_bounds.y() -
                split_result.body.notice_flow_allocation.y());
  EXPECT_EQ(split_result.side_panel.final_bounds.y(),
            split_result.combined_surface_outline_bounds.y());
  expect_applied_result();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeSidebarFlyoutKeepsExpandedControlsAndPageBounds) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "yee-sidebar-capture-dir")) {
    const auto capture_url =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            "yee-sidebar-capture-url");
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(),
        GURL(capture_url.empty() ? "chrome://downloads/" : capture_url)));
    if (!capture_url.empty()) {
      auto* const contents =
          browser()->tab_strip_model()->GetActiveWebContents();
      const auto theme_color = contents->GetThemeColor();
      auto* const source =
          yee::BrowserSurfaceColorController::FromWebContents(contents);
      ASSERT_TRUE(source);
      // 재현 페이지의 색 전환이 끝난 뒤 같은 배경의 전후 픽셀을 비교한다.
      if (theme_color) {
        ASSERT_TRUE(Await({applied_.Condition(
            applied_.source().ObserveSurfaceColor(*contents),
            yee::test::PresentationMatches{.source_id = source->source_id(),
                                           .background_color = *theme_color},
            yee::test::AppliedMatch::kPresentationColor)}));
        ASSERT_TRUE(([&]() { return source->GetColor() == theme_color; })());
      }
    }
  }
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  state->SetVerticalTabsEnabled(true);
  state->SetExpandOnHoverEnabled(true);
  state->SetUncollapsedWidth(tabs::kVerticalTabStripUncollapsedMinWidth);
  state->RequestCollapse(false);
  ApplyScheduledLayout();

  auto* const browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(region);
  views::View* const create =
      region->GetViewByID(yee::kSidebarHeaderCreateViewId);
  views::View* const agent =
      region->GetViewByID(yee::kSidebarHeaderAgentViewId);
  views::View* const footer = region->GetViewByID(yee::kSidebarFooterViewId);
  ASSERT_TRUE(create);
  ASSERT_TRUE(agent);
  ASSERT_TRUE(footer);
  EXPECT_TRUE(create->GetVisible());
  EXPECT_TRUE(agent->GetVisible());
  EXPECT_TRUE(footer->GetVisible());
  const gfx::Rect create_bounds = create->GetBoundsInScreen();
  const gfx::Rect agent_bounds = agent->GetBoundsInScreen();
  EXPECT_GE(region->width(),
            region->sidebar_header_actions()->GetMinimumSize().width());
  EXPECT_EQ(tabs::kVerticalTabStripUncollapsedMinWidth,
            state->GetUncollapsedWidth());
  views::View* toggle = nullptr;
  for (views::View* child : browser_view->toolbar()->children()) {
    if (child->GetProperty(views::kElementIdentifierKey) ==
        kVerticalTabStripCollapseButtonElementId) {
      toggle = child;
      break;
    }
  }
  ASSERT_TRUE(toggle);
  EXPECT_EQ(create_bounds.CenterPoint().y(),
            toggle->GetBoundsInScreen().CenterPoint().y());
  // Create·Agent는 오른쪽 조작 열에 속한다. 왼쪽 접기 버튼과는 창 버튼의
  // 수직 기준선을 공유하며 가로 위치는 독립적이다.
  EXPECT_EQ(region->width() -
                yee::kSidebarMetrics.tab_strip_horizontal_padding -
                yee::kSidebarMetrics.tab_content_horizontal_padding -
                yee::kSidebarMetrics.tab_icon_design_width / 2,
            agent->bounds().CenterPoint().x());

  state->RequestCollapse(true);
  ApplyScheduledLayout();
  ASSERT_TRUE(state->IsCollapsed());
  EXPECT_EQ(yee::kSidebarMetrics.titlebar_height, region->y());
  EXPECT_FALSE(create->GetVisible());
  EXPECT_FALSE(agent->GetVisible());
  EXPECT_FALSE(footer->GetVisible());
  EXPECT_EQ(create_bounds.CenterPoint().y(),
            toggle->GetBoundsInScreen().CenterPoint().y());
  const gfx::Rect page_bounds =
      browser_view->multi_contents_view()->GetBoundsInScreen();
  auto* const container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  const gfx::Rect viewport =
      container->GetPageViewportGeometry().current_viewport_in_screen;

  // hover flag는 animation 배치보다 먼저 바뀐다. 같은 대기에서 위치·크기와
  // 표시 opacity도 확인해 뒤의 Flyout assertion보다 일찍 완료되지 않게 한다.
  const auto sidebar_hover_layout = [&](bool expanded) {
    using namespace yee::test;
    const int top = yee::kSidebarMetrics.titlebar_height +
                    (expanded ? yee::kSidebarMetrics.content_gutter : 0);
    const int bottom = expanded ? yee::kSidebarMetrics.sidebar_flyout_inset
                                : yee::kSidebarMetrics.content_gutter;
    const int width = expanded ? region->uncollapsed_width()
                               : yee::kSidebarMetrics.collapsed_width;
    return AppliedConditions{
        SidebarHover(*region, expanded),
        applied_.View(
            *region,
            {.bounds = gfx::Rect(0, top, width,
                                 browser_view->height() - top - bottom)},
            AppliedMatch::kViewBounds),
        applied_.View(*region, {.opacity = expanded ? 1.0 : 0.0},
                      AppliedMatch::kViewOpacity)};
  };

  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(browser_view->GetWidget()->IsActive());
  ASSERT_TRUE(browser_view->GetWidget()->ShouldPaintAsActive());
  const auto native_window = browser_view->GetWidget()->GetNativeWindow();
  const auto capture_window = [&](views::Widget* widget,
                                  std::string_view name) {
    const auto directory =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "yee-sidebar-capture-dir");
    if (directory.empty()) {
      return;
    }
    CaptureYeeWindowFrame(widget,
                          directory.AppendASCII(std::string(name) + ".png"));
  };
  // 대상 창의 hitTest로 실제 NSView에 이동을 전달한다. macOS 앱 큐를
  // 통하는 mouseMoved와 창 안의 직접 전달 경로는 서로 다르다.
  ASSERT_TRUE(
      ui_test_utils::SendMouseMoveSync(viewport.CenterPoint(), native_window));
  // 필요할 때만 실제 compositor 프레임을 저장한다. 이미지 자체의 합격 여부는
  // 기능 assertion으로 대신하지 않고 사람이 실제 렌더링을 검수한다.
  const auto capture = [&](std::string_view name,
                           views::Widget* widget = nullptr) {
    const auto directory =
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            "yee-sidebar-capture-dir");
    if (directory.empty()) {
      return;
    }
    CaptureYeeCompositorFrame(
        widget ? widget : browser_view->GetWidget(),
        directory.AppendASCII(std::string(name) + ".png"));
  };
  capture("collapsed");
  if (!browser_view->GetWidget()->IsActive()) {
    // 캡처 중 다른 창으로 포커스가 이동했다면 실제 입력 직전에 한 번만
    // 활성화를 복구한다. 이후 hover·팝업 검사는 실제 창 상태를 그대로 따른다.
    ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
    capture("collapsed");
  }
  ASSERT_TRUE(applied_.Run(
      sidebar_hover_layout(true),
      [&] {
        ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
            region->GetBoundsInScreen().CenterPoint(), native_window));
      },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() { return region->is_expanded_on_hover(); })())
      << "bounds=" << region->GetBoundsInScreen().ToString()
      << ", mouse_inside=" << region->IsMouseHovered()
      << ", active=" << browser_view->GetWidget()->IsActive()
      << ", paint_active=" << browser_view->GetWidget()->ShouldPaintAsActive()
      << ", opacity=" << region->layer()->opacity();
  ApplyScheduledLayout();
  const auto expect_revealed = [&]() {
    EXPECT_TRUE(region->is_expanded_on_hover());
    EXPECT_EQ(region->uncollapsed_width(), region->width());
    EXPECT_FLOAT_EQ(1.0f, region->layer()->opacity());
    for (const auto* control : {create, agent, footer}) {
      EXPECT_TRUE(control->GetVisible());
      EXPECT_EQ(control->GetLocalBounds(), control->GetVisibleBounds());
    }
    EXPECT_EQ(page_bounds,
              browser_view->multi_contents_view()->GetBoundsInScreen());
    EXPECT_EQ(viewport,
              container->GetPageViewportGeometry().current_viewport_in_screen);
    EXPECT_EQ(yee::kSidebarMetrics.titlebar_height +
                  yee::kSidebarMetrics.content_gutter,
              region->y());
    EXPECT_EQ(yee::kSidebarMetrics.sidebar_flyout_inset,
              browser_view->height() - region->bounds().bottom());
    EXPECT_EQ(base::i18n::IsRTL() ? -yee::kSidebarMetrics.sidebar_flyout_inset
                                  : yee::kSidebarMetrics.sidebar_flyout_inset,
              region->GetTransform().rc(0, 3));
    const int edge_x =
        base::i18n::IsRTL()
            ? region->width() + yee::kSidebarMetrics.sidebar_flyout_inset - 1
            : 1 - yee::kSidebarMetrics.sidebar_flyout_inset;
    EXPECT_TRUE(region->HitTestPoint(
        gfx::Point(edge_x, 1 - yee::kSidebarMetrics.content_gutter)));
    EXPECT_FALSE(region->HitTestPoint(gfx::Point(
        region->width() / 2,
        yee::kSidebarMetrics.browser_surface_header_center_y() - region->y())));
    const int bottom_bridge = yee::kSidebarMetrics.sidebar_flyout_inset -
                              yee::kSidebarMetrics.content_gutter;
    EXPECT_TRUE(region->HitTestPoint(
        gfx::Point(edge_x, region->height() + bottom_bridge - 1)));
    EXPECT_GE(create->y(),
              yee::kSidebarMetrics.sidebar_header_control_top_padding);
    EXPECT_EQ(create->bounds().CenterPoint().y(),
              agent->bounds().CenterPoint().y());
    EXPECT_EQ(region->width() -
                  yee::kSidebarMetrics.tab_strip_horizontal_padding -
                  yee::kSidebarMetrics.tab_content_horizontal_padding -
                  yee::kSidebarMetrics.tab_icon_design_width / 2,
              agent->bounds().CenterPoint().x());
    EXPECT_EQ(agent->GetBoundsInScreen().CenterPoint().x(),
              footer->GetViewByID(yee::kSidebarFooterChevronViewId)
                  ->GetBoundsInScreen()
                  .CenterPoint()
                  .x());
  };
  expect_revealed();
  gfx::Rect revealed_create_bounds = create_bounds;
  gfx::Rect revealed_agent_bounds = agent_bounds;
  const int offset = base::i18n::IsRTL()
                         ? -yee::kSidebarMetrics.sidebar_flyout_inset
                         : yee::kSidebarMetrics.sidebar_flyout_inset;
  revealed_create_bounds.Offset(offset, yee::kSidebarMetrics.titlebar_height);
  revealed_agent_bounds.Offset(offset, yee::kSidebarMetrics.titlebar_height);
  EXPECT_EQ(revealed_create_bounds, create->GetBoundsInScreen());
  EXPECT_EQ(revealed_agent_bounds, agent->GetBoundsInScreen());
  EXPECT_EQ(yee::kSidebarMetrics.sidebar_footer_row_height, footer->height());
  EXPECT_EQ(page_bounds,
            browser_view->multi_contents_view()->GetBoundsInScreen());
  auto* const background = region->background()->AsA<CustomCornersBackground>();
  ASSERT_TRUE(background);
  ASSERT_TRUE(std::holds_alternative<CustomCorners::ColorResolver>(
      background->primary_color().color));
  EXPECT_EQ(yee::ResolveSidebarFlyoutColor(
                *region->GetColorProvider(),
                region->GetWidget()->ShouldPaintAsActive()),
            std::get<CustomCorners::ColorResolver>(
                background->primary_color().color)(*region));
  EXPECT_FLOAT_EQ(1.0f, background->primary_color().opacity);
  EXPECT_FLOAT_EQ(0.0f, region->layer()->background_blur());
  for (const views::View* child : browser_view->children()) {
    if (const auto* backdrop =
            views::AsViewClass<VerticalTabStripBackgroundBlurBackdrop>(child)) {
      EXPECT_FALSE(backdrop->GetVisible());
    }
  }
  EXPECT_EQ(yee::kSidebarMetrics.content_corner_radius,
            yee::ResolveSidebarFlyoutPresentation(1.0).corner_radius);

  const auto capture_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-sidebar-capture-dir");
  if (!capture_directory.empty()) {
    const gfx::Rect panel = views::View::ConvertRectToTarget(
        region, browser_view, region->GetLocalBounds());
    const std::string geometry = base::StringPrintf(
        "scale=%f\npanel_x=%d\npanel_y=%d\npanel_width=%d\npanel_height=%d\n"
        "radius=%d\n",
        browser_view->GetWidget()->GetCompositor()->device_scale_factor(),
        panel.x(), panel.y(), panel.width(), panel.height(),
        yee::kSidebarMetrics.content_corner_radius);
    ASSERT_TRUE(base::WriteFile(capture_directory.AppendASCII("geometry.txt"),
                                geometry));
  }

  capture("hover");
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      agent->GetBoundsInScreen().CenterPoint(), native_window));
  capture("button-hover");

  // 포인터를 페이지에 둔 채 키보드로 연다. 팝업이 포커스를 가져가도
  // 테스트 잠금 없이 사이드바와 anchor가 유지되어야 한다.
  // Mac의 논리 focus만으로는 native 키 전달을 보장하지 않으므로 입력 전에
  // 실제 창을 활성화한다. 팝업을 연 뒤에는 활성화 상태를 복구하지 않는다.
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  footer->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(footer->HasFocus())
      << "focusable=" << footer->IsFocusable()
      << ", drawn=" << footer->IsDrawn()
      << ", active=" << browser_view->GetWidget()->IsActive()
      << ", expanded=" << region->is_expanded_on_hover();
  capture("keyboard-focus");
  ASSERT_TRUE(
      ui_test_utils::SendMouseMoveSync(viewport.CenterPoint(), native_window));
  ASSERT_TRUE(footer->HasFocus())
      << "active=" << browser_view->GetWidget()->IsActive()
      << ", expanded=" << region->is_expanded_on_hover();
  ASSERT_TRUE(browser_view->GetWidget()->IsActive());
  ASSERT_TRUE(browser_view->GetWidget()->ShouldPaintAsActive());
  ASSERT_TRUE(region->is_expanded_on_hover());
  ASSERT_TRUE(footer->IsDrawn());
  ASSERT_TRUE(applied_.Run(
      {FooterMenuState(*footer, true)},
      [&] {
        ASSERT_TRUE(yee::test::SendKeyPressSync(
            browser(), ui::VKEY_SPACE, false, false, false, false));
      },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() {
    views::View* menu = yee::GetSidebarFooterMenuForTesting(*footer);
    return menu && menu->GetWidget()->IsActive();
  })())
      << "menu=" << yee::GetSidebarFooterMenuForTesting(*footer)
      << ", footer_focus=" << footer->HasFocus()
      << ", browser_active=" << browser_view->GetWidget()->IsActive()
      << ", expanded=" << region->is_expanded_on_hover();
  ApplyScheduledLayout();
  EXPECT_FALSE(
      region->Contains(browser_view->GetFocusManager()->GetFocusedView()));
  expect_revealed();
  capture("popup-open");
  // 실제 native 키 전달에서도 하위 화면의 Escape는 전체 팝업을 닫지 않는다.
  auto* const menu = yee::GetSidebarFooterMenuForTesting(*footer);
  ASSERT_TRUE(menu);
  const auto menu_window = menu->GetWidget()->GetNativeWindow();
  ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
      menu_window, ui::VKEY_RETURN, false, false, false, false));
  ASSERT_TRUE(([&]() {
    return yee::GetSidebarFooterMenuForTesting(*footer) == menu &&
           !menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId);
  })());
  ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
      menu_window, ui::VKEY_ESCAPE, false, false, false, false));
  ASSERT_TRUE(([&]() {
    return yee::GetSidebarFooterMenuForTesting(*footer) == menu &&
           menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId);
  })())
      << "same menu=" << (yee::GetSidebarFooterMenuForTesting(*footer) == menu)
      << ", current menu=" << yee::GetSidebarFooterMenuForTesting(*footer)
      << ", browser active=" << browser_view->GetWidget()->IsActive()
      << ", expanded=" << region->is_expanded_on_hover();
  auto* const tools =
      menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId);
#if BUILDFLAG(IS_WIN)
  // 화면을 다시 만든 메뉴의 배치를 마친 뒤 native 입력 좌표를 읽는다.
  menu->GetWidget()->LayoutRootViewIfNecessary();
  // keyup까지 처리한 뒤에도 focus와 activation이 메뉴에 남아야 한다.
  ASSERT_TRUE(menu->GetWidget()->IsActive());
  ASSERT_TRUE(tools->HasFocus());
  ASSERT_TRUE(tools->IsDrawn());
#endif
  const gfx::Point row_center = tools->GetBoundsInScreen().CenterPoint();
#if BUILDFLAG(IS_WIN)
  RecordProperty("YeeFlyoutPointerReturnInitialCenter", row_center.ToString());
  RecordProperty("YeeFlyoutPointerReturnInitialBounds",
                 tools->GetBoundsInScreen().ToString());
#endif
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(row_center, menu_window));
  ASSERT_TRUE(tools->HasFocus());
  ASSERT_TRUE(menu->GetWidget()->IsActive());
  // macOS의 사용자 Home 키 바인딩과 별개인 native Tab 순회를 검수한다.
  ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
      menu_window, ui::VKEY_TAB, false, false, false, false));
  EXPECT_TRUE(
      menu->GetViewByID(yee::kSidebarFooterRootMemoryViewId)->HasFocus());
  ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
      menu_window, ui::VKEY_TAB, false, true, false, false));
  EXPECT_TRUE(tools->HasFocus());
  EXPECT_EQ(views::FocusManager::FocusChangeReason::kFocusTraversal,
            menu->GetFocusManager()->focus_change_reason());
#if BUILDFLAG(IS_WIN)
  // Tab 순회 뒤 현재 배치에서 좌표를 다시 읽는다. 125%에서도 실제 이동이
  // 발생하도록 같은 행 안의 충분히 다른 위치를 사용한다.
  menu->GetWidget()->LayoutRootViewIfNecessary();
  const gfx::Point pointer_return = tools->GetBoundsInScreen().CenterPoint() +
                                    gfx::Vector2d(tools->width() / 4, 0);
  const auto record_row = [&](const char* phase) {
    auto* const current_menu = yee::GetSidebarFooterMenuForTesting(*footer);
    auto* const current_tools =
        current_menu == menu
            ? menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId)
            : nullptr;
    if (current_tools != tools) {
      RecordProperty(phase, "menu or row changed");
      return;
    }
    auto* const widget = menu->GetWidget();
    auto* const root = widget->GetRootView();
    const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
    auto* const hit = root->GetEventHandlerForPoint(
        views::View::ConvertPointFromScreen(root, cursor));
    RecordProperty(
        phase,
        base::StringPrintf(
            "bounds=%s visible=%s cursor=%s hit=%s "
            "active=%d drawn=%d focused=%d background=%d "
            "reason=%d",
            tools->GetBoundsInScreen().ToString().c_str(),
            tools->GetVisibleBounds().ToString().c_str(),
            cursor.ToString().c_str(), hit ? hit->GetClassName() : "null",
            widget->IsActive(), tools->IsDrawn(), tools->HasFocus(),
            tools->background() != nullptr,
            static_cast<int>(menu->GetFocusManager()->focus_change_reason())));
  };
  yee::test::ScopedNativeInputTrace native_trace(menu->GetWidget());
  native_trace.Snapshot("before-pointer-return", pointer_return);
  record_row("YeeFlyoutPointerReturnReady");
  RecordProperty("YeeFlyoutPointerReturnBounds",
                 tools->GetBoundsInScreen().ToString());
  RecordProperty("YeeFlyoutPointerReturnVisibleBounds",
                 tools->GetVisibleBounds().ToString());
  RecordProperty("YeeFlyoutPointerReturnTarget", pointer_return.ToString());
  RecordProperty("YeeFlyoutPointerReturnNativeInput", native_trace.ToJson());
  ASSERT_TRUE(tools->IsDrawn());
  ASSERT_TRUE(tools->GetVisibleBounds().Contains(
      views::View::ConvertPointFromScreen(tools, pointer_return)));
  auto* const menu_root = menu->GetWidget()->GetRootView();
  ASSERT_EQ(tools, menu_root->GetEventHandlerForPoint(
                       views::View::ConvertPointFromScreen(menu_root,
                                                           pointer_return)));
  // Windows 입력 ACK와 행의 실제 pointer focus 적용을 각각 관찰한다.
  ASSERT_TRUE(applied_.Run(
      {applied_.Focus(*tools),
       applied_.View(
           *tools,
           {.focus_change_reason = static_cast<int>(
                views::FocusManager::FocusChangeReason::kDirectFocusChange)},
           yee::test::AppliedMatch::kViewPointerFocus)},
      [&] {
        const bool pointer_move_sent =
            ui_test_utils::SendMouseMoveSync(pointer_return, menu_window);
        native_trace.Snapshot("after-pointer-return", pointer_return);
        record_row("YeeFlyoutPointerReturnSent");
        RecordProperty("YeeFlyoutPointerReturnNativeInput",
                       native_trace.ToJson());
        ASSERT_TRUE(pointer_move_sent);
      },
      yee::test::AppliedDomain::kFocus));
  const bool pointer_state_observed = ([&] {
    return yee::GetSidebarFooterMenuForTesting(*footer) == menu &&
           menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId) ==
               tools &&
           tools->HasFocus() && tools->background() &&
           menu->GetFocusManager()->focus_change_reason() ==
               views::FocusManager::FocusChangeReason::kDirectFocusChange;
  })();
  native_trace.Snapshot("after-pointer-state-observation", pointer_return);
  record_row("YeeFlyoutPointerReturnObservedState");
  RecordProperty("YeeFlyoutPointerReturnNativeInput", native_trace.ToJson());
  RecordProperty("YeeFlyoutPointerReturnObserved",
                 pointer_state_observed ? 1 : 0);
  EXPECT_TRUE(pointer_state_observed);
  ASSERT_EQ(menu, yee::GetSidebarFooterMenuForTesting(*footer));
  ASSERT_EQ(tools,
            menu->GetViewByID(yee::kSidebarFooterRootBrowserToolsViewId));
#else
  const gfx::Point pointer_return = row_center + gfx::Vector2d(1, 0);
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(pointer_return, menu_window));
#endif
  EXPECT_TRUE(tools->HasFocus());
  EXPECT_TRUE(tools->background());
  EXPECT_EQ(views::FocusManager::FocusChangeReason::kDirectFocusChange,
            menu->GetFocusManager()->focus_change_reason());
  ASSERT_TRUE(
      ui_test_utils::SendMouseMoveSync(viewport.CenterPoint(), native_window));
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "yee-sidebar-capture-dir")) {
    // 누름 ripple이 끝난 실제 열린 상태도 별도로 검수한다.
    ASSERT_TRUE(Await({FooterInkHidden(*footer)}));
    ASSERT_TRUE(([&]() {
      return views::InkDrop::Get(footer)
                 ->GetInkDrop()
                 ->GetTargetInkDropState() == views::InkDropState::HIDDEN;
    })());
    capture("popup-settled");
    auto* const popup = yee::GetSidebarFooterMenuForTesting(*footer);
    ASSERT_TRUE(popup);
    capture("footer-popup", popup->GetWidget());
  }

  // 페이지를 실제로 누르면 팝업 잠금이 해제되어 원래 접힘 상태로 돌아간다.
  auto close_popup_conditions = sidebar_hover_layout(false);
  close_popup_conditions.push_back(FooterMenuState(*footer, false));
  ASSERT_TRUE(applied_.Run(
      std::move(close_popup_conditions),
      [&] {
        ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
            ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
      },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(
      ([&]() { return !yee::GetSidebarFooterMenuForTesting(*footer); })());
  ASSERT_TRUE(([&]() { return !region->is_expanded_on_hover(); })())
      << "mouse_inside=" << region->IsMouseHovered()
      << ", footer_focus=" << footer->HasFocus() << ", sidebar_focus="
      << region->Contains(browser_view->GetFocusManager()->GetFocusedView());
  ApplyScheduledLayout();
  EXPECT_EQ(yee::kSidebarMetrics.collapsed_width, region->width());
  EXPECT_FLOAT_EQ(0.0f, region->layer()->opacity());
  EXPECT_FALSE(create->GetVisible());
  EXPECT_FALSE(agent->GetVisible());
  EXPECT_FALSE(footer->GetVisible());
  EXPECT_EQ(page_bounds,
            browser_view->multi_contents_view()->GetBoundsInScreen());
  EXPECT_EQ(viewport,
            container->GetPageViewportGeometry().current_viewport_in_screen);
  state->RequestCollapse(false);
  ApplyScheduledLayout();
  EXPECT_TRUE(create->GetVisible());
  EXPECT_TRUE(agent->GetVisible());
  EXPECT_TRUE(footer->GetVisible());
#if BUILDFLAG(IS_WIN)
  // 하위 화면의 keyup 소비가 루트의 정상 Escape 닫기와 focus 복귀를
  // 막지 않는지도 같은 native 입력 경로로 확인한다.
  footer->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(applied_.Run(
      {FooterMenuState(*footer, true)},
      [&] {
        ASSERT_TRUE(yee::test::SendKeyPressSync(
            browser(), ui::VKEY_SPACE, false, false, false, false));
      },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() {
    const auto* root_menu = yee::GetSidebarFooterMenuForTesting(*footer);
    return root_menu && root_menu->GetWidget()->IsActive();
  })());
  ASSERT_TRUE(applied_.Run(
      {FooterMenuState(*footer, false), applied_.Focus(*footer),
       applied_.Widget(*browser_view->GetWidget(), {.active = true},
                       yee::test::AppliedMatch::kWidgetActive)},
      [&] {
        ASSERT_TRUE(yee::test::SendKeyPressToWindowSync(
            yee::GetSidebarFooterMenuForTesting(*footer)
                ->GetWidget()
                ->GetNativeWindow(),
            ui::VKEY_ESCAPE, false, false, false, false));
      }));
  ASSERT_TRUE(([&]() {
    return !yee::GetSidebarFooterMenuForTesting(*footer) &&
           footer->HasFocus() && browser_view->GetWidget()->IsActive();
  })());
#endif
  // 탭 드래그처럼 펼침 잠금을 가진 채 고정 상태를 접어도 완료 콜백에 재진입하지
  // 않고 hover로 인계된다. 위의 실제 입력·팝업 검사는 이 잠금 없이 수행한다.
  footer->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(footer->HasFocus());
  auto expand_lock =
      region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  ASSERT_TRUE(applied_.Run(
      sidebar_hover_layout(true), [&] { state->RequestCollapse(true); },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() {
    return region->is_expanded_on_hover() &&
           region->y() == yee::kSidebarMetrics.titlebar_height +
                              yee::kSidebarMetrics.content_gutter;
  })());
  ApplyScheduledLayout();
  const gfx::Rect locked_page_bounds =
      browser_view->multi_contents_view()->GetBoundsInScreen();
  const gfx::Rect locked_viewport =
      container->GetPageViewportGeometry().current_viewport_in_screen;
  expect_revealed();
  browser_view->GetFocusManager()->ClearFocus();
  ASSERT_TRUE(applied_.Run(
      sidebar_hover_layout(false), [&] { expand_lock.reset(); },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() { return !region->is_expanded_on_hover(); })());
  ApplyScheduledLayout();
  EXPECT_FLOAT_EQ(0.0f, region->layer()->opacity());
  EXPECT_EQ(locked_page_bounds,
            browser_view->multi_contents_view()->GetBoundsInScreen());
  EXPECT_EQ(locked_viewport,
            container->GetPageViewportGeometry().current_viewport_in_screen);
  if (!capture_directory.empty()) {
    // macOS 창 촬영은 포커스를 바꿀 수 있으므로 실제 입력 검사를 모두
    // 마친 뒤 수행한다. 촬영용 잠금은 위의 hover·팝업 검증에 관여하지 않는다.
    auto photo_lock =
        region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
    auto paint_lock = browser_view->GetWidget()->LockPaintAsActive();
    ApplyScheduledLayout();
    capture_window(browser_view->GetWidget(), "window");
  }
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeFullscreenPreservesSurfaceWithoutStaleChromeRows) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);
  const auto relayout_browser = [&]() {
    browser_view->InvalidateLayout();
    ApplyScheduledLayout();
  };
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_FALSE(
      layout->last_native_layout_for_testing()->header_bounds.IsEmpty());
  yee::SurfaceGeometryTransitionEpoch* const epoch =
      yee::GetSurfaceGeometryTransition(*browser_view);
  ASSERT_TRUE(epoch);

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  multi_contents_view->SetTargetContentBounds(
      MultiContentsView::TargetContentBounds(multi_contents_view->size(),
                                             gfx::Insets::TLBR(7, 11, 13, 17)));
  multi_contents_view->DeprecatedLayoutImmediately();
  ASSERT_TRUE(multi_contents_view->target_content_bounds_for_testing());

  const yee::BrowserSurfaceChromeState hidden_fullscreen{
      yee::SurfaceGeometryStableState::kFullscreenHiddenChrome,
      /*toolbar_participates=*/true,
      /*sidebar_participates=*/true};
  yee::BeginSurfaceGeometryTransition(
      *epoch, hidden_fullscreen,
      yee::SurfaceGeometryPlatformPath::kMacNativeAsync);
  relayout_browser();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(
      yee::SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination,
      layout->last_native_layout_for_testing()->surface_transition_phase);
  EXPECT_FALSE(
      layout->last_native_layout_for_testing()->header_bounds.IsEmpty());
  EXPECT_FALSE(multi_contents_view->target_content_bounds_for_testing());

  // 전환 시작 시 취소한 target outset을 뒤늦은 Sidebar/Side Panel
  // 애니메이션 레이아웃이 되살리지 않아야 한다.
  multi_contents_view->SetTargetContentBounds(
      MultiContentsView::TargetContentBounds(multi_contents_view->size(),
                                             gfx::Insets::TLBR(3, 5, 7, 9)));
  multi_contents_view->DeprecatedLayoutImmediately();
  ASSERT_TRUE(multi_contents_view->target_content_bounds_for_testing());
  relayout_browser();
  EXPECT_FALSE(multi_contents_view->target_content_bounds_for_testing());

  ASSERT_TRUE(yee::CommitSurfaceGeometryTransition(*epoch, hidden_fullscreen));
  const yee::BrowserSurfaceChromeState always_toolbar{
      yee::SurfaceGeometryStableState::kFullscreenWithToolbar,
      /*toolbar_participates=*/true,
      /*sidebar_participates=*/true};
  yee::BeginSurfaceGeometryTransition(
      *epoch, always_toolbar,
      yee::SurfaceGeometryPlatformPath::kMacNativeAsync);
  relayout_browser();

  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto hidden_snapshot = *layout->last_native_layout_for_testing();
  EXPECT_EQ(yee::SurfaceGeometryStableState::kFullscreenHiddenChrome,
            hidden_snapshot.surface_chrome_state.stable_state);
  EXPECT_EQ(
      yee::SurfaceGeometryTransitionPhase::kAwaitingAuthoritativeDestination,
      hidden_snapshot.surface_transition_phase);
  EXPECT_GT(hidden_snapshot.surface_transition_generation,
            hidden_snapshot.surface_committed_generation);
  EXPECT_FALSE(
      hidden_snapshot.surface_chrome_participation.header_participates);
  EXPECT_TRUE(hidden_snapshot.header_bounds.IsEmpty());
  EXPECT_EQ(yee::kSidebarMetrics.content_gutter,
            hidden_snapshot.multi_contents_bounds.y());
  EXPECT_FALSE(multi_contents_view->target_content_bounds_for_testing());

  ContentsContainerView* const container =
      multi_contents_view->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  EXPECT_EQ(gfx::RoundedCornersF(yee::kSidebarMetrics.content_corner_radius),
            container->contents_view()->GetBackgroundRadii());
  const yee::PageViewportGeometry hidden_viewport =
      container->GetPageViewportGeometry();
  const gfx::Rect expected_current_clip(
      gfx::Point(), hidden_viewport.current_viewport_in_container.size());
  EXPECT_EQ(expected_current_clip,
            container->contents_view()->holder()->GetNativeViewClipRect());
  ASSERT_TRUE(container->contents_view()->layer());
  EXPECT_EQ(expected_current_clip,
            container->contents_view()->layer()->clip_rect());

  // 실제 창은 immersive reveal처럼 Toolbar를 계속 표시한다.
  // 확정된 숨김 상태가 아래 Surface를 바꾸지 않아야 한다.
  const gfx::Rect hidden_bounds = hidden_snapshot.multi_contents_bounds;
  relayout_browser();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_TRUE(
      layout->last_native_layout_for_testing()->header_bounds.IsEmpty());
  EXPECT_EQ(hidden_bounds,
            layout->last_native_layout_for_testing()->multi_contents_bounds);

  // immersive의 Toolbar capability가 true여도 숨긴 Header를 예약하지 않는다.
  auto* const sidebar = tabs::VerticalTabStripStateController::From(browser());
  auto* const region =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(region);
  sidebar->SetExpandOnHoverEnabled(true);
  sidebar->RequestCollapse(true);
  ApplyScheduledLayout();
  auto hover_lock =
      region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  ApplyScheduledLayout();
  EXPECT_EQ(yee::kSidebarMetrics.sidebar_flyout_inset, region->y());
  hover_lock.reset();
  sidebar->RequestCollapse(false);
  ApplyScheduledLayout();

  ASSERT_TRUE(yee::CommitSurfaceGeometryTransition(*epoch, always_toolbar));
  const yee::BrowserSurfaceChromeState windowed{
      yee::SurfaceGeometryStableState::kWindowed,
      /*toolbar_participates=*/true,
      /*sidebar_participates=*/true};
  yee::BeginSurfaceGeometryTransition(
      *epoch, windowed, yee::SurfaceGeometryPlatformPath::kMacNativeAsync);
  relayout_browser();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(yee::SurfaceGeometryStableState::kFullscreenWithToolbar,
            layout->last_native_layout_for_testing()
                ->surface_chrome_state.stable_state);
  EXPECT_TRUE(layout->last_native_layout_for_testing()
                  ->surface_chrome_participation.header_participates);
  EXPECT_FALSE(
      layout->last_native_layout_for_testing()->header_bounds.IsEmpty());

  ASSERT_TRUE(yee::CommitSurfaceGeometryTransition(*epoch, windowed));
  relayout_browser();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeSplitTargetLayoutUsesHandedInInsets) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();
  ASSERT_TRUE(multi_contents_view->IsInSplitView());

  const gfx::Insets browser_resolved_insets =
      multi_contents_view->split_view_insets_for_testing();
  const gfx::Insets handed_in_insets = gfx::Insets::TLBR(
      browser_resolved_insets.top() + 3, browser_resolved_insets.left() + 24,
      browser_resolved_insets.bottom() + 20,
      browser_resolved_insets.right() + 28);
  multi_contents_view->SetSplitViewInsets(handed_in_insets);
  multi_contents_view->DeprecatedLayoutImmediately();
  ASSERT_EQ(handed_in_insets,
            multi_contents_view->split_view_insets_for_testing());

  std::vector<gfx::Rect> current_contents_bounds;
  ASSERT_EQ(2U, multi_contents_view->contents_container_views().size());
  for (ContentsContainerView* const contents :
       multi_contents_view->contents_container_views()) {
    contents->DeprecatedLayoutImmediately();
    current_contents_bounds.push_back(contents->contents_view()->bounds());
  }

  // 현재 host와 target host 크기가 같으면 pane 폭과 렌더링 경계도 같아야 한다.
  // Yee inset을 여기서 다시 계산하면 target outset과 폭 차이가 생긴다.
  multi_contents_view->SetTargetContentBounds(
      MultiContentsView::TargetContentBounds(multi_contents_view->size(),
                                             gfx::Insets()));
  multi_contents_view->DeprecatedLayoutImmediately();

  for (size_t i = 0; i < multi_contents_view->contents_container_views().size();
       ++i) {
    ContentsContainerView* const contents =
        multi_contents_view->contents_container_views()[i];
    contents->DeprecatedLayoutImmediately();
    EXPECT_EQ(current_contents_bounds[i], contents->contents_view()->bounds());
  }

  multi_contents_view->SetTargetContentBounds(std::nullopt);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeePageViewportGeometryMatchesAppliedDevToolsEdges) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ContentsContainerView* const container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);

  auto expect_applied_geometry =
      [&](ContentsContainerView::DevToolsDockedPlacement expected_placement) {
        const yee::PageViewportGeometry geometry =
            container->GetPageViewportGeometry();
        EXPECT_GT(geometry.generation, 0U);
        EXPECT_EQ(expected_placement, container->devtools_docked_placement());
        EXPECT_EQ(geometry.content_and_footer_in_container,
                  container->GetContentsViewBounds());
        EXPECT_EQ(geometry.current_viewport_in_screen,
                  container->contents_view()->GetBoundsInScreen());
        EXPECT_EQ(geometry.current_viewport_in_container,
                  geometry.target_stack_in_container);
        EXPECT_TRUE(geometry.current_clip_in_target.IsEmpty());

        views::View* const clip_host =
            container->GetViewByID(yee::kPageViewportClipHostViewId);
        CHECK(clip_host);
        views::View& target_host = yee::GetPageTargetHost(*clip_host);
        views::View& overlay_host = yee::GetViewportOverlayHost(*clip_host);
        EXPECT_EQ(container, clip_host->parent());
        EXPECT_EQ(geometry.current_viewport_in_container, clip_host->bounds());
        EXPECT_EQ(clip_host, target_host.parent());
        EXPECT_EQ(clip_host, overlay_host.parent());
        EXPECT_EQ(clip_host->GetLocalBounds(), target_host.bounds());
        EXPECT_EQ(clip_host->GetLocalBounds(), overlay_host.bounds());
        EXPECT_EQ(target_host.GetLocalBounds(),
                  container->contents_view()->bounds());
        for (const views::View* target_child : target_host.children()) {
          EXPECT_EQ(target_host.GetLocalBounds(), target_child->bounds());
        }
        EXPECT_EQ(&overlay_host, container->GetToastAnchorView()->parent());
        EXPECT_EQ(gfx::Rect(0, 0, overlay_host.width(), 0),
                  container->GetToastAnchorView()->bounds());
        EXPECT_TRUE(clip_host->GetViewAccessibility().GetIsIgnored());
        EXPECT_TRUE(target_host.GetViewAccessibility().GetIsIgnored());
        EXPECT_TRUE(overlay_host.GetViewAccessibility().GetIsIgnored());
        EXPECT_EQ(views::View::FocusBehavior::NEVER,
                  clip_host->GetFocusBehavior());
        EXPECT_TRUE(clip_host->layer());
        EXPECT_TRUE(clip_host->layer()->GetMasksToBounds());

        // 투명 overlay host가 페이지 전체를 덮어도 빈 픽셀의 입력은 renderer로
        // 보낸다. 보이는 overlay 자식만 입력을 받을 수 있다.
        const gfx::Point viewport_center =
            overlay_host.GetLocalBounds().CenterPoint();
        EXPECT_FALSE(overlay_host.HitTestPoint(viewport_center));
        views::View* const page_handler =
            clip_host->GetEventHandlerForPoint(viewport_center);
        EXPECT_TRUE(page_handler);
        if (page_handler) {
          EXPECT_TRUE(target_host.Contains(page_handler));
        }

        const auto expect_ledger_parent =
            [&](yee::PageViewportChildFamily family, const views::View* child) {
              ASSERT_TRUE(child);
              const yee::PageViewportChildPolicy policy =
                  yee::GetPageViewportChildPolicy(family);
              const views::View* expected_parent = container;
              if (policy.host == yee::PageViewportChildHost::kPageTargetHost) {
                expected_parent = &target_host;
              } else if (policy.host ==
                         yee::PageViewportChildHost::kViewportOverlayHost) {
                expected_parent = &overlay_host;
              }
              EXPECT_EQ(expected_parent, child->parent());
            };

        expect_ledger_parent(yee::PageViewportChildFamily::kContentsWebView,
                             container->contents_view());
        expect_ledger_parent(yee::PageViewportChildFamily::kDevToolsWebView,
                             container->devtools_web_view());
        expect_ledger_parent(yee::PageViewportChildFamily::kDevToolsScrim,
                             container->devtools_scrim_view());
        expect_ledger_parent(
            yee::PageViewportChildFamily::kDataProtectionOverlay,
            container->data_protection_overlay_view());
        expect_ledger_parent(yee::PageViewportChildFamily::kContentsScrim,
                             container->contents_scrim_view());
        const views::View* glic_selection = nullptr;
        for (const views::View* child : overlay_host.children()) {
          if (child->GetProperty(views::kElementIdentifierKey) ==
              kGlicSelectionOverlayViewElementId) {
            glic_selection = child;
            break;
          }
        }
        expect_ledger_parent(yee::PageViewportChildFamily::kGlicSelection,
                             glic_selection);
        expect_ledger_parent(yee::PageViewportChildFamily::kToastAnchor,
                             container->GetToastAnchorView());
        expect_ledger_parent(yee::PageViewportChildFamily::kPaneHeader,
                             container->mini_toolbar());
        expect_ledger_parent(
            yee::PageViewportChildFamily::kPaneEmphasis,
            container->GetViewByID(yee::kSplitPaneEmphasisViewId));
        expect_ledger_parent(yee::PageViewportChildFamily::kContainerOutline,
                             container->contents_outline_view());
        expect_ledger_parent(yee::PageViewportChildFamily::kCaptureBorder,
                             container->capture_contents_border_view());
        if (container->new_tab_footer_view()) {
          expect_ledger_parent(yee::PageViewportChildFamily::kNtpFooter,
                               container->new_tab_footer_view());
        }
        if (container->indigo_overlay_view()) {
          expect_ledger_parent(yee::PageViewportChildFamily::kIndigoOverlay,
                               container->indigo_overlay_view());
        }
        if (container->read_anything_immersive_overlay_view()) {
          expect_ledger_parent(
              yee::PageViewportChildFamily::kReadAnythingImmersive,
              container->read_anything_immersive_overlay_view());
        }
        if (container->actor_overlay_web_view()) {
          expect_ledger_parent(yee::PageViewportChildFamily::kActorOverlay,
                               container->actor_overlay_web_view());
        }
        if (container->glic_border_view()) {
          expect_ledger_parent(yee::PageViewportChildFamily::kGlicContextBorder,
                               container->glic_border_view());
        }

        const views::View* footer_separator = nullptr;
        for (const views::View* child : container->children()) {
          if (child->GetProperty(views::kElementIdentifierKey) ==
              kFooterWebViewSeparatorElementId) {
            EXPECT_EQ(nullptr, footer_separator);
            footer_separator = child;
          }
        }
        EXPECT_EQ(container->new_tab_footer_view() != nullptr,
                  footer_separator != nullptr);
        if (footer_separator) {
          expect_ledger_parent(yee::PageViewportChildFamily::kNtpFooter,
                               footer_separator);
        }

        std::vector<const views::View*> expected_direct_children = {
            container->devtools_web_view(),
            container->devtools_scrim_view(),
            clip_host,
            container->contents_scrim_view(),
            container->mini_toolbar(),
            container->GetViewByID(yee::kSplitPaneEmphasisViewId),
            container->contents_outline_view(),
            container->capture_contents_border_view(),
        };
        if (container->new_tab_footer_view()) {
          expected_direct_children.push_back(footer_separator);
          expected_direct_children.push_back(container->new_tab_footer_view());
        }
        if (container->glic_border_view()) {
          expected_direct_children.push_back(container->glic_border_view());
        }
        std::vector<const views::View*> actual_direct_children(
            container->children().begin(), container->children().end());
        EXPECT_THAT(actual_direct_children, testing::UnorderedElementsAreArray(
                                                expected_direct_children));

        std::vector<const views::View*> expected_target_children = {
            container->contents_view(),
            container->data_protection_overlay_view(),
        };
        if (container->indigo_overlay_view()) {
          expected_target_children.push_back(container->indigo_overlay_view());
        }
        if (container->read_anything_immersive_overlay_view()) {
          expected_target_children.push_back(
              container->read_anything_immersive_overlay_view());
        }
        if (container->actor_overlay_web_view()) {
          expected_target_children.push_back(
              container->actor_overlay_web_view());
        }
        std::vector<const views::View*> actual_target_children(
            target_host.children().begin(), target_host.children().end());
        EXPECT_THAT(actual_target_children, testing::UnorderedElementsAreArray(
                                                expected_target_children));

        std::vector<const views::View*> expected_overlay_children = {
            container->GetToastAnchorView(), glic_selection};
        if (container->ai_overlay_dialog_view()) {
          expected_overlay_children.push_back(
              container->ai_overlay_dialog_view());
        }
        std::vector<const views::View*> actual_overlay_children(
            overlay_host.children().begin(), overlay_host.children().end());
        EXPECT_THAT(actual_overlay_children, testing::UnorderedElementsAreArray(
                                                 expected_overlay_children));

        const auto expect_direct_layer_clip =
            [](const views::View* child,
               const gfx::Rect& visible_bounds_in_container) {
              ASSERT_TRUE(child);
              ASSERT_TRUE(child->layer());
              EXPECT_EQ(yee::ResolveDirectChildClipInLocalSpace(
                            child->bounds(), visible_bounds_in_container),
                        child->layer()->clip_rect());
            };
        const auto expect_direct_web_view_clip =
            [&](views::WebView* web_view,
                const gfx::Rect& visible_bounds_in_container) {
              ASSERT_TRUE(web_view);
              if (web_view->layer()) {
                expect_direct_layer_clip(web_view, visible_bounds_in_container);
              }
              ExpectWebViewNativeClip(
                  web_view,
                  yee::ResolveDirectChildClipInLocalSpace(
                      web_view->bounds(), visible_bounds_in_container));
            };
        const auto expect_local_layer_clip =
            [](const views::View* child, const gfx::Rect& expected_local_clip) {
              ASSERT_TRUE(child);
              ASSERT_TRUE(child->layer());
              EXPECT_EQ(expected_local_clip, child->layer()->clip_rect());
            };
        const auto expect_local_web_view_clip =
            [&](views::WebView* web_view,
                const gfx::Rect& expected_local_clip) {
              ASSERT_TRUE(web_view);
              if (web_view->layer()) {
                expect_local_layer_clip(web_view, expected_local_clip);
              }
              ExpectWebViewNativeClip(web_view, expected_local_clip);
            };

        gfx::Rect target_clip = geometry.current_clip_in_target;
        if (target_clip.IsEmpty()) {
          target_clip = target_host.GetLocalBounds();
        }

        expect_local_web_view_clip(container->contents_view(), target_clip);
        expect_direct_web_view_clip(container->devtools_web_view(),
                                    geometry.page_environment_in_container);
        expect_direct_layer_clip(container->devtools_scrim_view(),
                                 geometry.page_environment_in_container);
        expect_local_layer_clip(container->data_protection_overlay_view(),
                                target_clip);
        expect_direct_layer_clip(container->contents_scrim_view(),
                                 geometry.page_environment_in_container);
        expect_local_layer_clip(
            glic_selection,
            yee::ResolveDirectChildClipInLocalSpace(
                glic_selection->bounds(), overlay_host.GetLocalBounds()));
        if (container->new_tab_footer_view()) {
          expect_direct_web_view_clip(container->new_tab_footer_view(),
                                      geometry.content_and_footer_in_container);
        }
        if (container->indigo_overlay_view()) {
          expect_local_layer_clip(container->indigo_overlay_view(),
                                  target_clip);
        }
        if (container->read_anything_immersive_overlay_view()) {
          expect_local_layer_clip(
              container->read_anything_immersive_overlay_view(), target_clip);
        }
        if (container->actor_overlay_web_view()) {
          expect_local_web_view_clip(container->actor_overlay_web_view(),
                                     target_clip);
        }
        if (container->glic_border_view() &&
            container->glic_border_view()->layer()) {
          expect_direct_layer_clip(container->glic_border_view(),
                                   geometry.current_viewport_in_container);
        }
        return geometry;
      };

  yee::PageViewportGeometry geometry = expect_applied_geometry(
      ContentsContainerView::DevToolsDockedPlacement::kNone);
  const gfx::Rect page_environment = geometry.page_environment_in_container;
  ASSERT_GT(page_environment.width(), 300);
  ASSERT_GT(page_environment.height(), 100);

  const auto set_strategy = [&](devtools::DockSide side,
                                const gfx::Rect& bounds) {
    const DevToolsContentsResizingStrategy strategy(side, bounds);
    container->SetContentsResizingStrategy(strategy);
    container->DeprecatedLayoutImmediately();
  };

  set_strategy(devtools::DockSide::kLeft,
               gfx::Rect(250, 0, page_environment.width() - 250,
                         page_environment.height()));
  geometry = expect_applied_geometry(
      ContentsContainerView::DevToolsDockedPlacement::kLeft);
  EXPECT_EQ(page_environment, geometry.page_environment_in_container);
  EXPECT_GT(geometry.content_and_footer_in_container.x(), page_environment.x());

  set_strategy(devtools::DockSide::kRight,
               gfx::Rect(0, 0, page_environment.width() - 250,
                         page_environment.height()));
  geometry = expect_applied_geometry(
      ContentsContainerView::DevToolsDockedPlacement::kRight);
  EXPECT_EQ(page_environment, geometry.page_environment_in_container);
  EXPECT_EQ(page_environment.origin(),
            geometry.content_and_footer_in_container.origin());

  set_strategy(devtools::DockSide::kBottom,
               gfx::Rect(0, 0, page_environment.width(),
                         page_environment.height() - 100));
  geometry = expect_applied_geometry(
      ContentsContainerView::DevToolsDockedPlacement::kBottom);
  EXPECT_EQ(page_environment, geometry.page_environment_in_container);
  EXPECT_EQ(page_environment.width(),
            geometry.content_and_footer_in_container.width());

  const DevToolsContentsResizingStrategy no_dock_strategy;
  container->SetContentsResizingStrategy(no_dock_strategy);
  container->DeprecatedLayoutImmediately();
  geometry = expect_applied_geometry(
      ContentsContainerView::DevToolsDockedPlacement::kNone);

  ContentsCaptureBorderView* const capture_border =
      container->capture_contents_border_view();
  ASSERT_TRUE(capture_border);
  const gfx::Rect current_viewport = geometry.current_viewport_in_container;
  container->SetCaptureContentsBorderLocation(gfx::Rect(
      -10, -10, current_viewport.width() + 20, current_viewport.height() + 20));
  container->ShowCaptureContentsBorder();
  container->DeprecatedLayoutImmediately();
  EXPECT_EQ(current_viewport, capture_border->bounds());
  container->SetCaptureContentsBorderLocation(std::nullopt);
  container->HideCaptureContentsBorder();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeePageRemainsPhysicallyClickableWithSidePanelOpen) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  content::WebContents* const web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);
  ASSERT_TRUE(content::ExecJs(web_contents, R"(
    window.yeePhysicalClickCount = 0;
    window.addEventListener('click', () => ++window.yeePhysicalClickCount);
  )"));

  ApplyScheduledLayout();
  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar));
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ContentsContainerView* const container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  const gfx::Point click_point = container->GetPageViewportGeometry()
                                     .current_viewport_in_screen.CenterPoint();
  const auto input_dom = ObserveDom(
      *web_contents, yee::test::WebUiAppliedSelectors::Probe::kInput);
  ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(click_point));
  ASSERT_TRUE(applied_.Run(
      {DomCondition(input_dom, {.click_count = 1},
                    yee::test::AppliedMatch::kWebUiClickCount)},
      [&] {
        ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
            ui_controls::LEFT, ui_controls::DOWN | ui_controls::UP));
      },
      yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
  ASSERT_TRUE(([&]() {
    return content::EvalJs(web_contents, "window.yeePhysicalClickCount")
               .ExtractInt() == 1;
  })());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeSidePanelGutterPhysicallyResizesBesideWebContents) {
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  browser_view->GetWidget()->SetColorModeOverride(
      ui::ColorProviderKey::ColorMode::kDark);
  auto* const surface_source =
      yee::BrowserSurfaceColorController::FromWebContents(
          browser()->tab_strip_model()->GetActiveWebContents());
  ASSERT_TRUE(surface_source);
  surface_source->SetPageSurfaceColorForTesting(SK_ColorWHITE);
  gfx::Rect window_bounds =
      browser_view->GetWidget()->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(1200, 800));
  browser_view->GetWidget()->SetBounds(window_bounds);
  // macOS hover는 native tracking area를 따른다. window hint로 입력 helper가
  // 이동을 보내기 전에 native WebView를 hit-test하게 한다.
  const gfx::NativeWindow native_window =
      browser_view->GetWidget()->GetNativeWindow();

  // 투명 native 페이지 host가 gutter 입력을 가로채는지 확인하려면 native
  // WebView가 필요하다. Label 패널과 Views hit-test만으로는 잡을 수 없다.
  auto* const registry =
      SidePanelRegistry::From(browser()->GetActiveTabInterface());
  const auto key = SidePanelEntry::Key(SidePanelEntry::Id::kReadingList);
  ASSERT_TRUE(!registry->GetEntryForKey(key) || registry->Deregister(key));
  ASSERT_TRUE(registry->Register(std::make_unique<SidePanelEntry>(
      SidePanelType::kToolbar, key,
      base::BindRepeating(
          [](Profile* profile,
             SidePanelEntryScope&) -> std::unique_ptr<views::View> {
            auto web_view = std::make_unique<views::WebView>(profile);
            web_view->SetProperty(views::kElementIdentifierKey,
                                  kSidePanelTestElement);
            web_view->LoadInitialURL(GURL("about:blank"));
            return web_view;
          },
          base::Unretained(browser_view->GetProfile())),
      /*default_content_width_callback=*/base::NullCallback())));
  SidePanelUI::From(browser())->Show(SidePanelEntry::Id::kReadingList);
  RunTestSequence(WaitForShow(kSidePanelTestElement));
  RunTestSequence(WithView(kSidePanelTestElement, [](views::WebView* view) {
    ASSERT_TRUE(content::WaitForLoadStop(view->GetWebContents()));
  }));
  ApplyScheduledLayout();

  for (bool vertical : {true, false}) {
    SCOPED_TRACE(vertical);
    state->SetVerticalTabsEnabled(vertical);
    ApplyScheduledLayout();
    for (bool right_aligned : {true, false}) {
      SCOPED_TRACE(right_aligned);
      browser_view->GetProfile()->GetPrefs()->SetBoolean(
          prefs::kSidePanelHorizontalAlignment, right_aligned);
      browser_view->side_panel()->SetPanelWidth(360);
      ApplyScheduledLayout();
      if (vertical && GetSidePanelGrowHeadroomInDips() < 64 &&
          !state->IsCollapsed()) {
        RunTestSequence(ToggleVerticalTabStripCollapsed(true));
      }
      ASSERT_GE(GetSidePanelGrowHeadroomInDips(), 64);
      views::View* const gutter =
          browser_view->GetViewByID(yee::kSidePanelResizeGutterViewId);
      ASSERT_TRUE(gutter);
      ASSERT_TRUE(gutter->IsDrawn());
      ASSERT_TRUE(yee::test::BringBrowserWindowToFront(browser()));
      browser_view->GetWidget()->GetCompositor()->ScheduleFullRedraw();
      ASSERT_TRUE(applied_.WaitForPresentedFrame(*browser_view->GetWidget()));
      views::View* const marker = gutter->children().front();
      ASSERT_TRUE(marker->layer());
      browser_view->GetFocusManager()->ClearFocus();
      const auto move_away = [&]() {
        return ui_test_utils::SendMouseMoveSync(
            browser_view->toolbar()->GetBoundsInScreen().CenterPoint(),
            native_window);
      };
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*marker, {.opacity = 0},
                         yee::test::AppliedMatch::kViewOpacity)},
          [&] { ASSERT_TRUE(move_away()); }, yee::test::AppliedDomain::kLayout,
          TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() { return marker->layer()->opacity() == 0.f; })());
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*marker, {},
                         yee::test::AppliedMatch::kViewOpacityPositive)},
          [&] {
            gutter->RequestFocus();
            ASSERT_TRUE(gutter->HasFocus());
          },
          yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() { return marker->layer()->opacity() > 0.f; })());
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*marker, {.opacity = 0},
                         yee::test::AppliedMatch::kViewOpacity)},
          [&] { browser_view->GetFocusManager()->ClearFocus(); },
          yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() { return marker->layer()->opacity() == 0.f; })());
      const int starting_width = browser_view->side_panel()->width();
      const int toolbar_minimum_width =
          browser_view->toolbar()->GetMinimumSize().width();
      const gfx::Rect panel_bounds =
          browser_view->side_panel()->GetBoundsInScreen();
      const gfx::Point start = gutter->GetBoundsInScreen().CenterPoint();
      EXPECT_FALSE(panel_bounds.Contains(start));
      const int grow_delta =
          start.x() < panel_bounds.CenterPoint().x() ? -64 : 64;
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*marker, {},
                         yee::test::AppliedMatch::kViewOpacityPositive)},
          [&] {
            ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(start, native_window));
          },
          yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() { return marker->layer()->opacity() > 0.f; })());
      ASSERT_TRUE(marker->background());
      gfx::Canvas marker_canvas(marker->size(), 1.f, false);
      marker->background()->Paint(&marker_canvas, marker);
      const auto center = marker->GetLocalBounds().CenterPoint();
      const SkColor foreground =
          marker_canvas.GetBitmap().getColor(center.x(), center.y());
      if (vertical) {
        EXPECT_EQ(gutter->GetColorProvider()->GetColor(ui::kColorSysOnSurface),
                  foreground);
      } else {
        EXPECT_GE(color_utils::GetContrastRatio(
                      color_utils::AlphaBlend(foreground, SK_ColorWHITE,
                                              marker->layer()->opacity()),
                      SK_ColorWHITE),
                  color_utils::kMinimumVisibleContrastRatio);
      }
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::DOWN, native_window));
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          start + gfx::Vector2d(grow_delta, 0), native_window));
      const bool received_drag =
          static_cast<views::ResizeArea*>(gutter)->is_resizing();
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::UP, native_window));
      ApplyScheduledLayout();
      EXPECT_NEAR(starting_width + 64, browser_view->side_panel()->width(), 2)
          << "received_drag=" << received_drag << " preferred="
          << browser_view->side_panel()->GetPreferredSize().width()
          << " start=" << start.ToString()
          << " panel=" << panel_bounds.ToString()
          << " gutter=" << gutter->GetBoundsInScreen().ToString()
          << " delta=" << grow_delta;
      EXPECT_EQ(toolbar_minimum_width,
                browser_view->toolbar()->GetMinimumSize().width());

      const gfx::Point resized_start =
          gutter->GetBoundsInScreen().CenterPoint();
      ASSERT_TRUE(
          ui_test_utils::SendMouseMoveSync(resized_start, native_window));
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::DOWN, native_window));
      ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
          resized_start - gfx::Vector2d(grow_delta, 0), native_window));
      ASSERT_TRUE(ui_test_utils::SendMouseEventsSync(
          ui_controls::LEFT, ui_controls::UP, native_window));
      ApplyScheduledLayout();
      EXPECT_NEAR(starting_width, browser_view->side_panel()->width(), 2);
      ASSERT_TRUE(applied_.Run(
          {applied_.View(*marker, {.opacity = 0},
                         yee::test::AppliedMatch::kViewOpacity)},
          [&] { ASSERT_TRUE(move_away()); }, yee::test::AppliedDomain::kLayout,
          TestTimeouts::action_timeout()));
      ASSERT_TRUE(([&]() { return marker->layer()->opacity() == 0.f; })());
    }
  }
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeePageHostsPreserveSplitFocusAndContainerReuse) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  views::FocusManager* const focus_manager = browser_view->GetFocusManager();
  ASSERT_TRUE(focus_manager);

  const auto enter_split = [&]() {
    ASSERT_FALSE(multi_contents_view->IsInSplitView());
    // 포커스 순회는 NTP의 비동기 Omnibox 강조와 분리한다. 팝업을 여는 동안
    // 강조가 Pane Header를 일시적으로 숨길 수 있으므로 안정된 split 구조에서
    // 검사한다.
    chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), /*index=*/1,
                     /*foreground=*/true);
    browser()->tab_strip_model()->AddToNewSplit(
        {0}, split_tabs::SplitTabVisualData(),
        split_tabs::SplitTabCreatedSource::kToolbarButton);
    ApplyScheduledLayout();
    ASSERT_TRUE(multi_contents_view->IsInSplitView());
    ASSERT_EQ(2U, multi_contents_view->contents_container_views().size());
  };

  const auto expect_focus_order = [&]() {
    const auto& containers = multi_contents_view->contents_container_views();
    ASSERT_EQ(2U, containers.size());
    ContentsContainerView* const first = containers[0];
    ContentsContainerView* const second = containers[1];
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    const auto is_within = [](const views::View* ancestor,
                              const views::View* view) {
      return ancestor == view || (ancestor && view && ancestor->Contains(view));
    };
    const auto expect_not_a_host = [&](const views::View* focused) {
      ASSERT_TRUE(focused);
      for (ContentsContainerView* container : {first, second}) {
        views::View* const clip_host =
            container->GetViewByID(yee::kPageViewportClipHostViewId);
        ASSERT_TRUE(clip_host);
        EXPECT_NE(clip_host, focused);
        EXPECT_NE(&yee::GetPageTargetHost(*clip_host), focused);
        EXPECT_NE(&yee::GetViewportOverlayHost(*clip_host), focused);
      }
    };
    const auto advance_through = [&](views::View* region, bool reverse) {
      int focus_stop_count = 0;
      while (is_within(region, focus_manager->GetFocusedView())) {
        expect_not_a_host(focus_manager->GetFocusedView());
        ASSERT_LT(focus_stop_count++, 32)
            << "Focus traversal did not leave " << region->GetClassName();
        focus_manager->AdvanceFocus(reverse);
        ASSERT_TRUE(focus_manager->GetFocusedView());
      }
      EXPECT_GT(focus_stop_count, 0);
    };

    focus_manager->SetFocusedView(first->contents_view());
    focus_manager->AdvanceFocus(/*reverse=*/true);
    views::View* const start_view = focus_manager->GetFocusedView();
    ASSERT_TRUE(start_view);
    ASSERT_FALSE(is_within(browser_view->contents_container(), start_view));

    focus_manager->AdvanceFocus(/*reverse=*/false);
    EXPECT_EQ(first->contents_view(), focus_manager->GetFocusedView());
    EXPECT_EQ(0, multi_contents_view->GetActiveIndex());
    expect_not_a_host(focus_manager->GetFocusedView());

    focus_manager->AdvanceFocus(/*reverse=*/false);
    ASSERT_TRUE(
        is_within(first->mini_toolbar(), focus_manager->GetFocusedView()));
    advance_through(first->mini_toolbar(), /*reverse=*/false);
    EXPECT_TRUE(is_within(multi_contents_view->resize_area_for_testing(),
                          focus_manager->GetFocusedView()));
    EXPECT_EQ(0, multi_contents_view->GetActiveIndex());

    focus_manager->AdvanceFocus(/*reverse=*/false);
    EXPECT_EQ(second->contents_view(), focus_manager->GetFocusedView());
    EXPECT_EQ(1, multi_contents_view->GetActiveIndex());
    expect_not_a_host(focus_manager->GetFocusedView());

    focus_manager->AdvanceFocus(/*reverse=*/false);
    ASSERT_TRUE(
        is_within(second->mini_toolbar(), focus_manager->GetFocusedView()));
    advance_through(second->mini_toolbar(), /*reverse=*/false);
    ASSERT_FALSE(is_within(browser_view->contents_container(),
                           focus_manager->GetFocusedView()));
    EXPECT_EQ(1, multi_contents_view->GetActiveIndex());

    focus_manager->AdvanceFocus(/*reverse=*/true);
    ASSERT_TRUE(
        is_within(second->mini_toolbar(), focus_manager->GetFocusedView()));
    advance_through(second->mini_toolbar(), /*reverse=*/true);
    EXPECT_EQ(second->contents_view(), focus_manager->GetFocusedView());
    EXPECT_EQ(1, multi_contents_view->GetActiveIndex());
    expect_not_a_host(focus_manager->GetFocusedView());

    focus_manager->AdvanceFocus(/*reverse=*/true);
    EXPECT_TRUE(is_within(multi_contents_view->resize_area_for_testing(),
                          focus_manager->GetFocusedView()));
    focus_manager->AdvanceFocus(/*reverse=*/true);
    ASSERT_TRUE(
        is_within(first->mini_toolbar(), focus_manager->GetFocusedView()));
    advance_through(first->mini_toolbar(), /*reverse=*/true);
    EXPECT_EQ(first->contents_view(), focus_manager->GetFocusedView());
    EXPECT_EQ(0, multi_contents_view->GetActiveIndex());
    expect_not_a_host(focus_manager->GetFocusedView());

    focus_manager->AdvanceFocus(/*reverse=*/true);
    EXPECT_EQ(start_view, focus_manager->GetFocusedView());
  };

  enter_split();
  expect_focus_order();

  browser()->tab_strip_model()->CloseWebContentsAt(
      browser()->tab_strip_model()->active_index(), TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(([&]() { return !multi_contents_view->IsInSplitView(); })());
  ApplyScheduledLayout();
  ASSERT_FALSE(multi_contents_view->IsInSplitView());
  ASSERT_EQ(2U, multi_contents_view->contents_container_views().size());
  EXPECT_EQ(
      1U, std::ranges::count_if(multi_contents_view->contents_container_views(),
                                [](const ContentsContainerView* container) {
                                  return container->GetVisible();
                                }));

  enter_split();
  expect_focus_order();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeInfoBarFollowsExactSplitPaneIdentity) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  ASSERT_TRUE(multi_contents_view->IsInSplitView());
  content::WebContents* const manager_contents =
      multi_contents_view->GetActiveContentsView()->web_contents();
  ASSERT_TRUE(manager_contents);
  auto* const manager =
      infobars::ContentInfoBarManager::FromWebContents(manager_contents);
  ASSERT_TRUE(manager);
  auto* const infobar =
      static_cast<ConfirmInfoBar*>(manager->AddInfoBar(ConfirmInfoBar::Create(
          std::make_unique<YeeGeometryConfirmInfoBarDelegate>())));
  ASSERT_TRUE(infobar);
  infobar->SetBackground(
      views::CreateSolidBackground(SkColorSetRGB(255, 0, 255)));
  ApplyScheduledLayout();

  InfoBarContainerView* const infobar_container =
      browser_view->infobar_container();
  ASSERT_TRUE(infobar_container);
  const auto expect_exact_active_pane_flow = [&]() {
    ContentsContainerView* const active_container =
        multi_contents_view->GetActiveContentsContainerView();
    CHECK(active_container);
    const gfx::Rect infobar_bounds = infobar_container->GetBoundsInScreen();
    const gfx::Rect active_container_bounds =
        active_container->GetBoundsInScreen();
    const gfx::Rect active_body_bounds =
        multi_contents_view->GetActiveContentsView()->GetBoundsInScreen();
    const gfx::Rect inactive_body_bounds =
        multi_contents_view->GetInactiveContentsView()->GetBoundsInScreen();
    const InfoBarFlowMetrics metrics =
        infobar_container->GetFlowMetricsForLayout();

    EXPECT_TRUE(infobar_container->GetVisible());
    EXPECT_EQ(manager_contents, infobar_container->GetWebContentsForLayout());
    EXPECT_TRUE(active_container_bounds.Contains(infobar_bounds));
    EXPECT_EQ(active_body_bounds.x(), infobar_bounds.x());
    EXPECT_EQ(active_body_bounds.width(), infobar_bounds.width());
    EXPECT_EQ(active_body_bounds.y(), infobar_bounds.bottom());
    EXPECT_EQ(inactive_body_bounds.y(), infobar_bounds.y());
    EXPECT_FALSE(inactive_body_bounds.Intersects(infobar_bounds));
    EXPECT_EQ(metrics.semantic_stack_height, infobar_bounds.height());
    EXPECT_LE(metrics.rigid_control_min_width, infobar_bounds.width());
    EXPECT_EQ(1U, metrics.visible_bar_count);

    const yee::PageViewportGeometry geometry =
        active_container->GetPageViewportGeometry();
    gfx::Rect page_environment_in_screen =
        geometry.page_environment_in_container;
    views::View::ConvertRectToScreen(active_container,
                                     &page_environment_in_screen);
    EXPECT_EQ(infobar_bounds.bottom(), page_environment_in_screen.y());
    EXPECT_EQ(active_body_bounds, geometry.current_viewport_in_screen);
    views::View* const clip_host =
        active_container->GetViewByID(yee::kPageViewportClipHostViewId);
    CHECK(clip_host);
    EXPECT_EQ(&yee::GetPageTargetHost(*clip_host),
              active_container->contents_view()->parent());
    EXPECT_EQ(yee::GetPageTargetHost(*clip_host).GetLocalBounds(),
              active_container->contents_view()->bounds());
    EXPECT_EQ(active_container->GetContentsViewBounds(),
              geometry.content_and_footer_in_container);
    EXPECT_GT(geometry.generation, 0U);
    return infobar_bounds;
  };
  views::Widget* const widget = browser_view->GetWidget();
  ASSERT_TRUE(widget);
  ui::Compositor* const compositor = widget->GetCompositor();
  ASSERT_TRUE(compositor);
  const gfx::Rect root_in_screen = widget->GetRootView()->GetBoundsInScreen();
  const float device_scale_factor = compositor->device_scale_factor();
  ASSERT_GT(device_scale_factor, 0.0f);
  const auto to_device_pixels = [&](const gfx::Rect& screen_dip_bounds) {
    gfx::Rect root_dip_bounds = screen_dip_bounds;
    root_dip_bounds.Offset(-root_in_screen.x(), -root_in_screen.y());
    return gfx::ToEnclosingRect(
        gfx::ScaleRect(gfx::RectF(root_dip_bounds), device_scale_factor));
  };
  const auto expect_sentinel_pixels_contained =
      [&](const gfx::Rect& infobar_bounds) {
        base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
        auto request = std::make_unique<viz::CopyOutputRequest>(
            viz::CopyOutputRequest::ResultFormat::RGBA,
            viz::CopyOutputRequest::ResultDestination::kSystemMemory,
            readback.GetCallback());
        request->set_area(gfx::Rect(compositor->size()));
        compositor->root_layer()->RequestCopyOfOutput(std::move(request));
        compositor->ScheduleFullRedraw();
        std::unique_ptr<viz::CopyOutputResult> result = readback.Take();
        ASSERT_TRUE(result);
        ASSERT_FALSE(result->IsEmpty());
        auto scoped_bitmap = result->ScopedAccessSkBitmap();
        const SkBitmap bitmap = scoped_bitmap.GetOutScopedBitmap();
        ASSERT_TRUE(bitmap.readyToDraw());

        const gfx::Rect expected_pixels = to_device_pixels(infobar_bounds);
        size_t sentinel_inside = 0;
        size_t sentinel_outside = 0;
        for (int y = 0; y < bitmap.height(); ++y) {
          for (int x = 0; x < bitmap.width(); ++x) {
            const SkColor color = bitmap.getColor(x, y);
            const bool is_sentinel = SkColorGetR(color) >= 240 &&
                                     SkColorGetG(color) <= 15 &&
                                     SkColorGetB(color) >= 240;
            if (!is_sentinel) {
              continue;
            }
            if (expected_pixels.Contains(x, y)) {
              ++sentinel_inside;
            } else {
              ++sentinel_outside;
            }
          }
        }
        EXPECT_GT(sentinel_inside, 100U);
        EXPECT_EQ(0U, sentinel_outside);
      };

  const gfx::Rect initial_infobar_bounds = expect_exact_active_pane_flow();
  expect_sentinel_pixels_contained(initial_infobar_bounds);
  TabStripModel* const tab_strip_model = browser()->tab_strip_model();
  const split_tabs::SplitTabId split_id =
      tab_strip_model->GetActiveTab()->GetSplit().value();
  tab_strip_model->ReverseTabsInSplit(split_id);
  ApplyScheduledLayout();
  EXPECT_EQ(manager_contents,
            multi_contents_view->GetActiveContentsView()->web_contents());
  const gfx::Rect reversed_infobar_bounds = expect_exact_active_pane_flow();
  EXPECT_NE(initial_infobar_bounds.x(), reversed_infobar_bounds.x());
  expect_sentinel_pixels_contained(reversed_infobar_bounds);

  content::WebContents* const other_contents =
      multi_contents_view->GetInactiveContentsView()->web_contents();
  ASSERT_TRUE(other_contents);
  tab_strip_model->ActivateTabAt(
      tab_strip_model->GetIndexOfWebContents(other_contents));
  ApplyScheduledLayout();
  EXPECT_FALSE(infobar_container->GetVisible());
  EXPECT_EQ(
      multi_contents_view->GetActiveContentsView()->GetBoundsInScreen().y(),
      multi_contents_view->GetInactiveContentsView()->GetBoundsInScreen().y());

  tab_strip_model->ActivateTabAt(
      tab_strip_model->GetIndexOfWebContents(manager_contents));
  ApplyScheduledLayout();
  expect_exact_active_pane_flow();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeSurfaceDecorationAndZOrderUseResolvedFrame) {
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ApplyScheduledLayout();

  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& snapshot = *layout->last_native_layout_for_testing();
  views::View* const surface_outline =
      browser_view->GetViewByID(yee::kCombinedSurfaceOutlineViewId);
  views::View* const side_panel_surface =
      browser_view->GetViewByID(yee::kSidePanelSurfaceViewId);
  ASSERT_TRUE(surface_outline);
  ASSERT_TRUE(side_panel_surface);
  views::View* const resize_gutter =
      browser_view->GetViewByID(yee::kSidePanelResizeGutterViewId);
  ASSERT_TRUE(resize_gutter);
  EXPECT_EQ(snapshot.combined_surface_outline_bounds,
            surface_outline->bounds());
  EXPECT_EQ(snapshot.combined_surface_outline_visible,
            surface_outline->GetVisible());
  EXPECT_GT(snapshot.combined_header_separator_offset, 0);
  EXPECT_EQ(0.0f, snapshot.infobar_top_corner_radius);
  EXPECT_TRUE(browser_view->infobar_container()
                  ->presentation_for_testing()
                  .contain_horizontal_shadow);
  EXPECT_EQ(0.0f, browser_view->infobar_container()
                      ->presentation_for_testing()
                      .top_corner_radius);

  const auto expect_before = [&](const views::View* first,
                                 const views::View* second) {
    const std::optional<size_t> first_index = browser_view->GetIndexOf(first);
    const std::optional<size_t> second_index = browser_view->GetIndexOf(second);
    ASSERT_TRUE(first_index);
    ASSERT_TRUE(second_index);
    EXPECT_LT(*first_index, *second_index);
  };
  expect_before(browser_view->multi_contents_view(),
                browser_view->infobar_container());
  expect_before(browser_view->infobar_container(),
                browser_view->main_shadow_overlay());
  expect_before(browser_view->main_shadow_overlay(), side_panel_surface);
  expect_before(side_panel_surface, browser_view->side_panel());
  expect_before(browser_view->side_panel(), surface_outline);
  auto* const sidebar =
      browser_view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(sidebar);
  const auto expect_surface_controls_order = [&]() {
    expect_before(surface_outline, resize_gutter);
    expect_before(resize_gutter, sidebar);
    expect_before(resize_gutter, browser_view->find_bar_host_view());
    expect_before(resize_gutter, browser_view->window_scrim_view());
    ASSERT_TRUE(surface_outline->layer());
    ASSERT_TRUE(resize_gutter->layer());
    ASSERT_TRUE(sidebar->layer());
    ui::Layer* const parent = surface_outline->layer()->parent();
    ASSERT_TRUE(parent);
    ASSERT_EQ(parent, resize_gutter->layer()->parent());
    ASSERT_EQ(parent, sidebar->layer()->parent());
    const auto& layers = parent->children();
    EXPECT_LT(std::find(layers.begin(), layers.end(), surface_outline->layer()),
              std::find(layers.begin(), layers.end(), resize_gutter->layer()));
    EXPECT_LT(std::find(layers.begin(), layers.end(), resize_gutter->layer()),
              std::find(layers.begin(), layers.end(), sidebar->layer()));
  };
  expect_surface_controls_order();
  expect_before(surface_outline, browser_view->find_bar_host_view());
  expect_before(surface_outline, browser_view->window_scrim_view());

  auto* const animation_content_ptr =
      browser_view->SetSidePanelAnimationContent(
          std::make_unique<SidePanelAnimationContentView>());
  views::ViewTracker animation_content_tracker(animation_content_ptr);
  expect_before(animation_content_ptr, surface_outline);
  expect_surface_controls_order();

  browser_view->SetSidePanelAnimationContent(nullptr);
  EXPECT_FALSE(animation_content_tracker.view());
  EXPECT_EQ(nullptr, browser_view->GetSidePanelAnimationContent());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeInfoBarPresentationRoundsOnlyTheTopVisibleBar) {
  content::WebContents* const active_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(active_contents);
  auto* const manager =
      infobars::ContentInfoBarManager::FromWebContents(active_contents);
  ASSERT_TRUE(manager);
  constexpr auto kStackedPriority =
      infobars::InfoBarDelegate::InfobarPriority::kCriticalSecurity;
  ASSERT_TRUE(manager->AddInfoBar(ConfirmInfoBar::Create(
      std::make_unique<YeeGeometryConfirmInfoBarDelegate>(kStackedPriority))));
  ASSERT_TRUE(manager->AddInfoBar(ConfirmInfoBar::Create(
      std::make_unique<YeeGeometryConfirmInfoBarDelegate>(kStackedPriority))));
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  InfoBarContainerView* const infobar_container =
      browser_view->infobar_container();
  ASSERT_TRUE(infobar_container);
  ASSERT_GE(infobar_container->children().size(), 3U);
  auto* const first_bar =
      views::AsViewClass<InfoBarView>(infobar_container->children()[0]);
  auto* const second_bar =
      views::AsViewClass<InfoBarView>(infobar_container->children()[1]);
  ASSERT_TRUE(first_bar);
  ASSERT_TRUE(second_bar);

  constexpr float kTopCornerRadius = 11.0f;
  infobar_container->SetPresentationForLayout(InfoBarContainerPresentation{
      /*contain_horizontal_shadow=*/true, kTopCornerRadius});
  EXPECT_EQ(
      gfx::RoundedCornersF(kTopCornerRadius, kTopCornerRadius, 0.0f, 0.0f),
      first_bar->layer()->rounded_corner_radii());
  EXPECT_TRUE(second_bar->layer()->rounded_corner_radii().IsEmpty());
  ASSERT_TRUE(infobar_container->background());
  ASSERT_TRUE(
      infobar_container->background()->GetRoundedCornerRadii().has_value());
  EXPECT_EQ(
      gfx::RoundedCornersF(kTopCornerRadius, kTopCornerRadius, 0.0f, 0.0f),
      *infobar_container->background()->GetRoundedCornerRadii());

  infobar_container->SetPresentationForLayout(
      InfoBarContainerPresentation{/*contain_horizontal_shadow=*/true,
                                   /*top_corner_radius=*/0.0f});
  EXPECT_TRUE(first_bar->layer()->rounded_corner_radii().IsEmpty());
  EXPECT_TRUE(second_bar->layer()->rounded_corner_radii().IsEmpty());
}

enum class TopContainerBackgroundTestMode {
  kDefault,
  kTouch,
  kImmersive,
};

class YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest,
      public testing::WithParamInterface<TopContainerBackgroundTestMode> {
 public:
  YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest() = default;
  ~YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest() override =
      default;
};

IN_PROC_BROWSER_TEST_P(
    YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest,
    TopContainerBackground) {
  std::optional<ui::TouchUiController::TouchUiScoperForTesting> touch_ui_scoper;
  switch (GetParam()) {
    case TopContainerBackgroundTestMode::kTouch:
      touch_ui_scoper.emplace(true);
      break;
    case TopContainerBackgroundTestMode::kImmersive:
      ImmersiveModeController::From(browser())->SetEnabled(true);
      break;
    case TopContainerBackgroundTestMode::kDefault:
      break;
  }
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(1, layout->last_native_layout_for_testing()
                   ->top_container_calculation_count);

  auto* const top_container = browser_view->top_container();
  auto* const background =
      top_container->background()->AsA<CustomCornersBackground>();
  ASSERT_NE(nullptr, background);
  if (browser_view->UsesYeeBrowserSurfaceGeometry()) {
    EXPECT_FALSE(background->is_visible());
    return;
  }
  CustomCornersBackground::ColorChoice expected;
  switch (GetParam()) {
    case TopContainerBackgroundTestMode::kDefault:
      expected = CustomCornersBackground::ToolbarTheme();
      break;
    case TopContainerBackgroundTestMode::kTouch:
    case TopContainerBackgroundTestMode::kImmersive:
#if BUILDFLAG(IS_CHROMEOS)
      // ChromeOS의 이 모드에서는 top container가 탭바를 포함하므로 frame 색을
      // 쓴다.
      expected = ui::kColorFrameActive;
#else
      expected = CustomCornersBackground::ToolbarTheme();
#endif
      break;
  }
  EXPECT_EQ(expected, background->primary_color().color);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest,
    testing::Values(TopContainerBackgroundTestMode::kDefault,
                    TopContainerBackgroundTestMode::kTouch
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
                    ,
                    TopContainerBackgroundTestMode::kImmersive
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)

                    ));

// flyover 애니메이션을 켠 상태에서 WebContents 크기 변경 횟수를 제한한다.
class YeeBrowserViewTabbedLayoutImplContentLayoutUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest,
      public views::ViewObserver {
 public:
  YeeBrowserViewTabbedLayoutImplContentLayoutUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(/*disable_animations=*/false) {}
  ~YeeBrowserViewTabbedLayoutImplContentLayoutUiTest() override = default;

  void SetUpOnMainThread() override {
    YeeBrowserViewTabbedLayoutImplUiTest::SetUpOnMainThread();
    const auto& container_views = GetContentsContainers();
    for (ContentsContainerView* container : container_views) {
      auto* const contents = container->contents_view();
      resize_data_.emplace(contents, ResizeData());
      observation_.AddObservation(contents);
    }
  }

  void TearDownOnMainThread() override {
    observation_.RemoveAllObservations();
    resize_data_.clear();
    YeeBrowserViewTabbedLayoutImplUiTest::TearDownOnMainThread();
  }

  auto OpenSidePanel() {
    return Steps(
        InParallel(
            RunSubsequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar)),
            RunSubsequence(WaitForEvent(kSidePanelElementId,
                                        SidePanel::kOpenAnimationCompletedEvent)
                               .SetMustBeVisibleAtStart(false))),
        Do([this]() { ApplyScheduledLayout(); }));
  }

  auto ClearResizeCounts() {
    return Do([this]() {
      const auto& container_views = GetContentsContainers();
      for (ContentsContainerView* container : container_views) {
        auto* contents = container->contents_view();
        auto& data = resize_data_[contents];
        data.last_size = contents->size();
        data.count = 0U;
      }
    });
  }

  template <typename... Args>
  auto CheckResizeCounts(Args&&... args) {
    return CheckResult(
               [this]() {
                 const auto& container_views = GetContentsContainers();
                 std::vector<size_t> counts;
                 for (ContentsContainerView* container : container_views) {
                   counts.push_back(
                       resize_data_[container->contents_view()].count);
                 }
                 return counts;
               },
               testing::ElementsAre(
                   testing::Matcher<size_t>(std::forward<Args>(args))...))
        .SetDescription("CheckResizeCounts()");
  }

  auto EnterSplitView() {
    return Steps(
        Do([this]() {
          chrome::NewSplitTab(
              browser(), split_tabs::SplitTabLayout::kSideBySide,
              split_tabs::SplitTabCreatedSource::kToolbarButton);
        }),
        WaitForShow(MultiContentsResizeArea::kMultiContentsResizeAreaElementId),
        Do([this]() { ApplyScheduledLayout(); }));
  }

  void ExpectCombinedViewportContainment() {
    ApplyScheduledLayout();
    auto* const browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    auto* const multi = browser_view->multi_contents_view();
    for (ContentsContainerView* container : multi->contents_container_views()) {
      if (!container->GetVisible()) {
        continue;
      }
      EXPECT_TRUE(
          multi->GetBoundsInScreen().Contains(container->GetBoundsInScreen()));
      const auto geometry = container->GetPageViewportGeometry();
      EXPECT_FALSE(geometry.current_viewport_in_container.IsEmpty())
          << "container=" << container->bounds().ToString() << ", environment="
          << geometry.page_environment_in_container.ToString() << ", devtools="
          << container->devtools_web_view()->bounds().ToString() << ", window="
          << browser_view->GetWidget()->GetWindowBoundsInScreen().ToString();
      EXPECT_TRUE(container->GetLocalBounds().Contains(
          geometry.page_environment_in_container));
      EXPECT_TRUE(geometry.page_environment_in_container.Contains(
          geometry.current_viewport_in_container));
      auto* const clip =
          container->GetViewByID(yee::kPageViewportClipHostViewId);
      ASSERT_TRUE(clip);
      EXPECT_EQ(geometry.current_viewport_in_container, clip->bounds());
      ASSERT_TRUE(clip->layer());
      EXPECT_TRUE(clip->layer()->GetMasksToBounds());
      auto& target = yee::GetPageTargetHost(*clip);
      const gfx::Rect native_clip = geometry.current_clip_in_target.IsEmpty()
                                        ? target.GetLocalBounds()
                                        : geometry.current_clip_in_target;
      EXPECT_EQ(native_clip,
                container->contents_view()->holder()->GetNativeViewClipRect());
      const gfx::Rect revealed_target =
          views::View::ConvertRectToTarget(&target, clip, native_clip);
      EXPECT_EQ(clip->GetLocalBounds(), revealed_target);
      EXPECT_EQ(target.GetLocalBounds(), container->contents_view()->bounds());
      EXPECT_EQ(clip->GetLocalBounds(),
                yee::GetViewportOverlayHost(*clip).bounds());
    }
  }

 private:
  const std::vector<raw_ptr<ContentsContainerView, DanglingUntriaged>>&
  GetContentsContainers() {
    return BrowserView::GetBrowserViewForBrowser(browser())
        ->multi_contents_view()
        ->contents_container_views();
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override {
    if (auto* webview = views::AsViewClass<views::WebView>(observed_view)) {
      auto& data = resize_data_[webview];
      if (data.last_size != observed_view->size()) {
        data.last_size = observed_view->size();
        ++data.count;
      }
    }
  }

  struct ResizeData {
    gfx::Size last_size;
    size_t count = 0U;
  };

  std::map<raw_ptr<const views::WebView>, ResizeData> resize_data_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      observation_{this};
};

#if BUILDFLAG(IS_MAC)
class YeeBrowserViewTabbedLayoutImplRtlFractionalDsfUiTest
    : public YeeBrowserViewTabbedLayoutImplContentLayoutUiTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    YeeBrowserViewTabbedLayoutImplContentLayoutUiTest::SetUpCommandLine(
        command_line);
    command_line->AppendSwitchASCII("force-ui-direction", "rtl");
    command_line->AppendSwitchASCII("force-device-scale-factor", "1.25");
  }
};
#endif  // BUILDFLAG(IS_MAC)

IN_PROC_BROWSER_TEST_F(
    YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
    YeeSidebarFlyoutPinAndReverseKeepContinuousPresentation) {
  auto* const sidebar = tabs::VerticalTabStripStateController::From(browser());
  sidebar->SetVerticalTabsEnabled(true);
  sidebar->SetExpandOnHoverEnabled(true);
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const region = view->vertical_tab_strip_region_view_for_testing();
  ASSERT_TRUE(region);
  auto* const controller = BrowserAnimationController::From(browser());
  auto clock = base::MakeRefCounted<gfx::AnimationContainer>();
  controller->SetAnimationContainerForTesting(
      TabStripAnimations::kVerticalTabStrip, clock.get());
  const auto advance_sidebar = [&](base::TimeDelta delta) {
    gfx::AnimationContainerTestApi time(clock.get());
    time.IncrementTime(delta);
    ApplyScheduledLayout();
  };
  sidebar->RequestCollapse(false);
  advance_sidebar(base::Seconds(2));
  ASSERT_TRUE(ui_test_utils::SendMouseMoveSync(
      view->multi_contents_view()->GetBoundsInScreen().CenterPoint()));
  sidebar->RequestCollapse(true);
  advance_sidebar(base::Seconds(2));
  ASSERT_TRUE(sidebar->IsCollapsed());
  EXPECT_FLOAT_EQ(0.0f, region->layer()->opacity());
  const gfx::Rect collapsed_page =
      view->multi_contents_view()->GetBoundsInScreen();

  // 이 검사는 애니메이션 인계만 고정된 시계로 관찰한다. 팝업 수명 검사는
  // 별도 실제 hover 경로에서 테스트 잠금 없이 수행한다.
  auto revealed =
      region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  auto* const create = region->GetViewByID(yee::kSidebarHeaderCreateViewId);
  auto* const agent = region->GetViewByID(yee::kSidebarHeaderAgentViewId);
  ASSERT_TRUE(create);
  ASSERT_TRUE(agent);
  const auto expect_complete_controls = [&]() {
    EXPECT_EQ(create->GetVisible(), agent->GetVisible());
    if (create->GetVisible()) {
      EXPECT_GE(agent->x(),
                create->bounds().right() +
                    2 * yee::kSidebarMetrics.shell_control_horizontal_margin);
    }
    for (const views::View* control : {create, agent}) {
      if (control->GetVisible()) {
        EXPECT_EQ(control->GetLocalBounds(), control->GetVisibleBounds());
      }
    }
  };
  bool saw_hidden = false;
  bool saw_visible = false;
  for (int step_ms : {16, 32, 32, 64}) {
    advance_sidebar(base::Milliseconds(step_ms));
    expect_complete_controls();
    saw_hidden |= !create->GetVisible();
    saw_visible |= create->GetVisible();
    EXPECT_EQ(collapsed_page, view->multi_contents_view()->GetBoundsInScreen());
  }
  advance_sidebar(base::Seconds(2));
  expect_complete_controls();
  saw_visible |= create->GetVisible();
  if (!saw_hidden || !saw_visible ||
      region->width() != region->uncollapsed_width() ||
      region->layer()->opacity() != 1.0f) {
    // 첫 펼침이 실패했을 때만 상태를 읽어 정상 실행의 타이밍을 보존한다.
    const auto motion =
        controller->GetCurrentMotion(TabStripAnimations::kVerticalTabStrip);
    LOG(ERROR)
        << "Yee flyout reveal: hidden=" << saw_hidden
        << ", visible=" << saw_visible
        << ", collapsed=" << sidebar->IsCollapsed()
        << ", hover=" << region->is_expanded_on_hover() << ", animating="
        << controller->IsAnimating(TabStripAnimations::kVerticalTabStrip)
        << ", running=" << clock->is_running()
        << ", collapse_motion=" << (motion == TabStripAnimations::kCollapse)
        << ", reveal_motion=" << (motion == TabStripAnimations::kExpandOnHover)
        << ", expand_motion=" << (motion == TabStripAnimations::kExpand)
        << ", hide_motion=" << (motion == TabStripAnimations::kCollapseOnHover)
        << ", width=" << region->width()
        << ", opacity=" << region->layer()->opacity() << ", width_track="
        << controller
               ->GetCurrentValue(TabStripAnimations::kVerticalTabStrip,
                                 TabStripAnimations::kTabStripWidth)
               .value_or(-1.0)
        << ", hover_track="
        << controller
               ->GetCurrentValue(TabStripAnimations::kVerticalTabStrip,
                                 TabStripAnimations::kTabStripHoverWidth)
               .value_or(-1.0)
        << ", clock_ahead_ms="
        << (clock->last_tick_time() - base::TimeTicks::Now()).InMillisecondsF();
  }
  EXPECT_TRUE(saw_hidden);
  EXPECT_TRUE(saw_visible);
  ASSERT_EQ(region->uncollapsed_width(), region->width());
  ASSERT_FLOAT_EQ(1.0f, region->layer()->opacity());
  EXPECT_EQ(collapsed_page, view->multi_contents_view()->GetBoundsInScreen());

  sidebar->RequestCollapse(false);
  revealed.reset();
  advance_sidebar(base::TimeDelta());
  EXPECT_EQ(region->uncollapsed_width(), region->width());
  EXPECT_FLOAT_EQ(1.0f, region->layer()->opacity());
  for (int step_ms : {32, 64, 32}) {
    advance_sidebar(base::Milliseconds(step_ms));
    expect_complete_controls();
    EXPECT_TRUE(controller->IsAnimating(TabStripAnimations::kVerticalTabStrip));
    EXPECT_EQ(region->uncollapsed_width(), region->width());
    EXPECT_FLOAT_EQ(1.0f, region->layer()->opacity());
  }

  // 두 폭 트랙이 교차하는 도중 반대로 전환해도 첫 프레임이 튀지 않는다.
  const int width_before_reversal = region->width();
  const float opacity_before_reversal = region->layer()->opacity();
  const gfx::Rect bounds_before_reversal = region->bounds();
  const gfx::Transform transform_before_reversal = region->GetTransform();
  sidebar->RequestCollapse(true);
  advance_sidebar(base::TimeDelta());
  EXPECT_NEAR(width_before_reversal, region->width(), 1);
  EXPECT_FLOAT_EQ(opacity_before_reversal, region->layer()->opacity());
  EXPECT_EQ(bounds_before_reversal, region->bounds());
  EXPECT_EQ(transform_before_reversal, region->GetTransform());
  advance_sidebar(base::Milliseconds(64));
  expect_complete_controls();
  EXPECT_LE(region->width(), width_before_reversal);
  EXPECT_LT(region->layer()->opacity(), opacity_before_reversal);
  advance_sidebar(base::Seconds(2));
  EXPECT_TRUE(sidebar->IsCollapsed());
  EXPECT_EQ(yee::kSidebarMetrics.collapsed_width, region->width());
  EXPECT_FLOAT_EQ(0.0f, region->layer()->opacity());
  EXPECT_EQ(collapsed_page, view->multi_contents_view()->GetBoundsInScreen());

  // 실제 animation에서도 종료 콜백이 끝난 뒤 펼침 잠금의 hover로 인계한다.
  sidebar->RequestCollapse(false);
  advance_sidebar(base::Seconds(2));
#if BUILDFLAG(IS_WIN)
  // ToolbarButton은 접근성 전용 focus이므로 실제 키보드 순회 모드로 진입한다.
  view->GetFocusManager()->SetKeyboardAccessible(true);
#endif
  create->RequestFocusWithReason(
      views::FocusManager::FocusChangeReason::kFocusTraversal);
  ASSERT_TRUE(create->HasFocus());
  auto expand_lock =
      region->GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepExpanded);
  ASSERT_TRUE(applied_.Run({SidebarHover(*region, true)}, [&] {
    sidebar->RequestCollapse(true);
    advance_sidebar(base::Seconds(2));
  }));
  ASSERT_TRUE(([&]() { return region->is_expanded_on_hover(); })());
  advance_sidebar(base::Seconds(2));
  expect_complete_controls();
  EXPECT_EQ(region->uncollapsed_width(), region->width());
  EXPECT_FLOAT_EQ(1.0f, region->layer()->opacity());
  EXPECT_EQ(collapsed_page, view->multi_contents_view()->GetBoundsInScreen());
  view->GetFocusManager()->ClearFocus();
  expand_lock.reset();
  advance_sidebar(base::Seconds(2));
  EXPECT_FLOAT_EQ(0.0f, region->layer()->opacity());
  EXPECT_EQ(collapsed_page, view->multi_contents_view()->GetBoundsInScreen());
}

IN_PROC_BROWSER_TEST_F(
    YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
    YeeCombinedPanelSidebarTransitionsKeepViewportContained) {
  auto* const sidebar = tabs::VerticalTabStripStateController::From(browser());
  sidebar->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const multi = view->multi_contents_view();
  auto* const controller = BrowserAnimationController::From(browser());
  auto panel_clock = base::MakeRefCounted<gfx::AnimationContainer>();
  auto sidebar_clock = base::MakeRefCounted<gfx::AnimationContainer>();
  controller->SetAnimationContainerForTesting(SidePanelAnimations::kSidePanel,
                                              panel_clock.get());
  controller->SetAnimationContainerForTesting(
      TabStripAnimations::kVerticalTabStrip, sidebar_clock.get());

  for (bool split : {false, true}) {
    SCOPED_TRACE(split);
    if (split) {
      RunTestSequence(EnterSplitView());
    }
    RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar,
                                            std::nullopt, panel_clock.get()));
    sidebar->RequestCollapse(true);
    gfx::AnimationContainerTestApi{sidebar_clock.get()};
    for (int frame = 0; frame < 3; ++frame) {
      SCOPED_TRACE(frame);
      gfx::AnimationContainerTestApi(panel_clock.get())
          .IncrementTime(base::Milliseconds(32));
      gfx::AnimationContainerTestApi(sidebar_clock.get())
          .IncrementTime(base::Milliseconds(32));
      ExpectCombinedViewportContainment();
    }
    EXPECT_TRUE(controller->IsAnimating(SidePanelAnimations::kSidePanel));
    EXPECT_TRUE(controller->IsAnimating(TabStripAnimations::kVerticalTabStrip));

    // 한쪽 전환을 반전하면서 다른 쪽 정렬과 창 크기를 바꿔도 이전 frame의
    // clip이 남지 않아야 한다.
    sidebar->RequestCollapse(false);
    gfx::AnimationContainerTestApi{sidebar_clock.get()};
    view->side_panel()->Close(/*animated=*/true);
    gfx::AnimationContainerTestApi{panel_clock.get()};
    view->GetProfile()->GetPrefs()->SetBoolean(
        prefs::kSidePanelHorizontalAlignment, split);
    gfx::Rect bounds = view->GetWidget()->GetWindowBoundsInScreen();
    bounds.set_size(split ? gfx::Size(1100, 800) : gfx::Size(1200, 850));
    view->GetWidget()->SetBounds(bounds);
    gfx::AnimationContainerTestApi(panel_clock.get())
        .IncrementTime(base::Milliseconds(32));
    gfx::AnimationContainerTestApi(sidebar_clock.get())
        .IncrementTime(base::Milliseconds(32));
    ExpectCombinedViewportContainment();
    view->side_panel()->Open(/*animated=*/true);
    gfx::AnimationContainerTestApi{panel_clock.get()};
    gfx::AnimationContainerTestApi(panel_clock.get())
        .IncrementTime(base::Seconds(2));
    ExpectCombinedViewportContainment();
    EXPECT_TRUE(controller->IsAnimating(TabStripAnimations::kVerticalTabStrip));
    EXPECT_TRUE(multi->target_content_bounds_for_testing());
    gfx::AnimationContainerTestApi(sidebar_clock.get())
        .IncrementTime(base::Seconds(2));
    ExpectCombinedViewportContainment();
    EXPECT_FALSE(multi->target_content_bounds_for_testing());

    // 두 애니메이션 시계가 뒤늦게 진행해도 fullscreen epoch는 target을
    // 취소한다. 여기서는 실제 레이아웃 소비자를 확인하며 OS 완료 시점은 플랫폼
    // 검사에 맡긴다.
    sidebar->RequestCollapse(true);
    gfx::AnimationContainerTestApi{sidebar_clock.get()};
    view->side_panel()->Close(/*animated=*/true);
    gfx::AnimationContainerTestApi{panel_clock.get()};
    gfx::AnimationContainerTestApi(panel_clock.get())
        .IncrementTime(base::Milliseconds(32));
    gfx::AnimationContainerTestApi(sidebar_clock.get())
        .IncrementTime(base::Milliseconds(32));
    ExpectCombinedViewportContainment();
    auto* const epoch = yee::GetSurfaceGeometryTransition(*view);
    ASSERT_TRUE(epoch);
    const auto original = epoch->committed;
    yee::BeginSurfaceGeometryTransition(
        *epoch,
        {yee::SurfaceGeometryStableState::kFullscreenHiddenChrome, false, true},
        yee::SurfaceGeometryPlatformPath::kMacNativeAsync);
    view->InvalidateLayout();
    ExpectCombinedViewportContainment();
    EXPECT_FALSE(multi->target_content_bounds_for_testing());
    gfx::AnimationContainerTestApi(panel_clock.get())
        .IncrementTime(base::Milliseconds(32));
    gfx::AnimationContainerTestApi(sidebar_clock.get())
        .IncrementTime(base::Milliseconds(32));
    ExpectCombinedViewportContainment();
    EXPECT_FALSE(multi->target_content_bounds_for_testing());
    yee::BeginSurfaceGeometryTransition(
        *epoch, original, yee::SurfaceGeometryPlatformPath::kMacNativeAsync);
    ASSERT_TRUE(yee::CommitSurfaceGeometryTransition(*epoch, original));
    sidebar->RequestCollapse(false);
    gfx::AnimationContainerTestApi{sidebar_clock.get()};
    gfx::AnimationContainerTestApi(panel_clock.get())
        .IncrementTime(base::Seconds(2));
    gfx::AnimationContainerTestApi(sidebar_clock.get())
        .IncrementTime(base::Seconds(2));
    ExpectCombinedViewportContainment();
    EXPECT_FALSE(multi->target_content_bounds_for_testing());
  }
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       YeeSplitPanelDevToolsNoticeKeepOwningPane) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  auto* const view = BrowserView::GetBrowserViewForBrowser(browser());
  // 창 크기 요청이 작업 영역에 제한돼도 실제 Sidebar를 접어 공간을 확보한다.
  gfx::Rect window_bounds = view->GetWidget()->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(1280, 900));
  view->GetWidget()->SetBounds(window_bounds);
  ApplyScheduledLayout();
  RunTestSequence(ToggleVerticalTabStripCollapsed(true), EnterSplitView(),
                  OpenSidePanel());
  auto* const multi = view->multi_contents_view();
  auto* const owner = multi->GetActiveContentsView()->web_contents();
  auto* const manager = infobars::ContentInfoBarManager::FromWebContents(owner);
  ASSERT_TRUE(manager);
  auto pending_notice = ConfirmInfoBar::Create(
      std::make_unique<YeeGeometryConfirmInfoBarDelegate>());
  const infobars::InfoBar* notice_bar = nullptr;
  auto notice_animation = applied_.Condition(
      applied_.source().ObserveInfoBarAnimation(*pending_notice, 40),
      yee::test::AnimationMatches{.group_id = 40, .running = false});
  // 추가 전 InfoBar는 창에 붙지 않아 배율·방향이 기본값이다. 붙을 container의
  // 좌표계로 기대값을 정해 RTL·125% 분기에서도 같은 값을 비교한다.
  notice_animation.coordinates =
      yee::test::GetAppliedViewCoordinates(*view->infobar_container());
  ASSERT_TRUE(applied_.Run(
      {applied_.View(*view->infobar_container(), {.visible = true},
                     yee::test::AppliedMatch::kViewVisible),
       notice_animation},
      [&] {
        notice_bar = manager->AddInfoBar(std::move(pending_notice));
        ASSERT_TRUE(notice_bar);
      }));
  ASSERT_TRUE(([&] {
    return view->infobar_container()->GetVisible() &&
           !notice_bar->animation().is_animating();
  })());

  for (auto axis : {split_tabs::SplitTabLayout::kSideBySide,
                    split_tabs::SplitTabLayout::kStacked}) {
    // 작은 RTL125 창에서도 도구가 있는 실제 소유 pane에 충분한 폭을 준다.
    const bool owner_is_start =
        multi->contents_container_views()[0]->contents_view()->web_contents() ==
        owner;
    const double ratio = axis == split_tabs::SplitTabLayout::kSideBySide
                             ? (owner_is_start ? 0.8 : 0.2)
                             : 0.5;
    multi->UpdateSplitVisualData(split_tabs::SplitTabVisualData(axis, ratio));
    for (auto dock : {devtools::DockSide::kLeft, devtools::DockSide::kRight,
                      devtools::DockSide::kBottom}) {
      SCOPED_TRACE(testing::Message() << "split=" << static_cast<int>(axis)
                                      << ", dock=" << static_cast<int>(dock));
      auto* const container = multi->GetActiveContentsContainerView();
      ApplyScheduledLayout();
      const auto environment =
          container->GetPageViewportGeometry().page_environment_in_container;
      const bool bottom = dock == devtools::DockSide::kBottom;
      const DevToolsContentsResizingStrategy strategy(
          dock, gfx::Rect(dock == devtools::DockSide::kLeft ? 80 : 0, 0,
                          environment.width() - (bottom ? 0 : 80),
                          environment.height() - (bottom ? 80 : 0)));
      gfx::Rect expected_devtools;
      gfx::Rect expected_page;
      ApplyDevToolsContentsResizingStrategy(strategy,
                                            gfx::Rect(environment.size()),
                                            &expected_devtools, &expected_page);
      ASSERT_FALSE(expected_page.IsEmpty())
          << "Native 도킹 전제: environment=" << environment.ToString()
          << " window="
          << view->GetWidget()->GetWindowBoundsInScreen().ToString();
      container->SetContentsResizingStrategy(strategy);
      ExpectCombinedViewportContainment();
      gfx::Rect page_environment =
          container->GetPageViewportGeometry().page_environment_in_container;
      views::View::ConvertRectToScreen(container, &page_environment);
      const auto notice = view->infobar_container()->GetBoundsInScreen();
      EXPECT_EQ(notice.bottom(), page_environment.y());
      EXPECT_EQ(notice.width(), page_environment.width());
      EXPECT_FALSE(notice.Intersects(
          multi->GetInactiveContentsContainerView()->GetBoundsInScreen()));
      ExpectWebViewNativeClip(container->devtools_web_view(),
                              yee::ResolveDirectChildClipInLocalSpace(
                                  container->devtools_web_view()->bounds(),
                                  container->GetPageViewportGeometry()
                                      .page_environment_in_container));
      container->SetContentsResizingStrategy(
          DevToolsContentsResizingStrategy());
    }
  }
  auto* const tabs = browser()->tab_strip_model();
  tabs->ReverseTabsInSplit(tabs->GetActiveTab()->GetSplit().value());
  ExpectCombinedViewportContainment();
  EXPECT_EQ(owner, multi->GetActiveContentsView()->web_contents());
  auto* const other = multi->GetInactiveContentsView()->web_contents();
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(other));
  ExpectCombinedViewportContainment();
  EXPECT_FALSE(view->infobar_container()->GetVisible());
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(owner));
  ExpectCombinedViewportContainment();
  EXPECT_TRUE(view->infobar_container()->GetVisible());
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       NativePlannerMatchesAppliedAnimationFramesAndReversal) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* const layout = static_cast<BrowserViewTabbedLayoutImpl*>(
      browser_view->GetBrowserViewLayoutForTesting());
  ASSERT_TRUE(layout);

  browser_view->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kSidePanelHorizontalAlignment, false);
  auto* const animation_controller =
      BrowserAnimationController::From(browser());
  auto animation_container = base::MakeRefCounted<gfx::AnimationContainer>();
  animation_controller->SetAnimationContainerForTesting(
      SidePanelAnimations::kSidePanel, animation_container.get());

  constexpr SkColor kHeaderSurface = SkColorSetRGB(0x18, 0x22, 0x34);
  yee::BrowserSurfaceColorController* const surface_controller =
      yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
          browser()->tab_strip_model()->GetActiveWebContents());
  ASSERT_TRUE(surface_controller);
  surface_controller->SetPageSurfaceColorForTesting(kHeaderSurface);
  const std::optional<yee::BrowserSurfacePresentation> header_presentation =
      surface_controller->GetPresentation();
  ASSERT_TRUE(header_presentation);
  ASSERT_EQ(kHeaderSurface, header_presentation->surface);

  ApplyScheduledLayout();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const auto& baseline_layout = *layout->last_native_layout_for_testing();
  views::Widget* const widget = browser_view->GetWidget();
  ASSERT_TRUE(widget);
  ui::Compositor* const compositor = widget->GetCompositor();
  ASSERT_TRUE(compositor);
  views::View* const surface_fill =
      browser_view->GetViewByID(yee::kBrowserSurfaceFillViewId);
  ASSERT_TRUE(surface_fill);
  views::View* const main_background_region =
      browser_view->main_background_region_for_testing();
  ASSERT_TRUE(main_background_region);
  ASSERT_EQ(main_background_region, surface_fill->parent());
  ASSERT_TRUE(main_background_region->layer());
  ASSERT_TRUE(browser_view->top_container()->layer());
  ASSERT_FALSE(surface_fill->layer());
  const std::optional<size_t> main_background_index =
      browser_view->GetIndexOf(main_background_region);
  const std::optional<size_t> header_index =
      browser_view->GetIndexOf(browser_view->top_container());
  ASSERT_TRUE(main_background_index);
  ASSERT_TRUE(header_index);
  EXPECT_LT(*main_background_index, *header_index);
  ui::Layer* const shared_layer_parent =
      main_background_region->layer()->parent();
  ASSERT_TRUE(shared_layer_parent);
  ASSERT_EQ(shared_layer_parent,
            browser_view->top_container()->layer()->parent());
  const auto main_layer_position = std::ranges::find(
      shared_layer_parent->children(), main_background_region->layer());
  const auto header_layer_position = std::ranges::find(
      shared_layer_parent->children(), browser_view->top_container()->layer());
  ASSERT_NE(shared_layer_parent->children().end(), main_layer_position);
  ASSERT_NE(shared_layer_parent->children().end(), header_layer_position);
  EXPECT_LT(main_layer_position, header_layer_position);
  const float device_scale_factor = compositor->device_scale_factor();
  ASSERT_GT(device_scale_factor, 0.0f);
  const gfx::Rect root_in_screen = widget->GetRootView()->GetBoundsInScreen();
  const auto view_bounds_in_browser = [browser_view](const views::View* view) {
    return views::View::ConvertRectToTarget(view, browser_view,
                                            view->GetLocalBounds());
  };
  ToolbarView* const toolbar = browser_view->toolbar();
  ASSERT_TRUE(toolbar);
  LocationBarView* const location_bar = toolbar->location_bar_view();
  ASSERT_TRUE(location_bar);
  const gfx::Rect baseline_location_bar_bounds =
      view_bounds_in_browser(location_bar);
  ASSERT_TRUE(
      baseline_layout.header_bounds.Contains(baseline_location_bar_bounds));
  const int baseline_non_location_bar_width =
      baseline_layout.header_bounds.width() -
      baseline_location_bar_bounds.width();
#if BUILDFLAG(IS_WIN)
  const int baseline_toolbar_chrome_width =
      toolbar->width() - baseline_location_bar_bounds.width();
#endif
  const int baseline_location_bar_leading_offset =
      baseline_location_bar_bounds.x() - baseline_layout.header_bounds.x();
  ASSERT_GT(baseline_non_location_bar_width, 0);
  ASSERT_GT(baseline_location_bar_bounds.width(), 0);
  ASSERT_FALSE(baseline_layout.combined_surface_outline_bounds.IsEmpty());
  const int baseline_surface_top =
      baseline_layout.combined_surface_outline_bounds.y();
  const int baseline_surface_bottom =
      baseline_layout.combined_surface_outline_bounds.bottom();
  ASSERT_EQ(baseline_surface_top, baseline_layout.header_bounds.y());

  const auto toggle_views =
      views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
          kVerticalTabStripCollapseButtonElementId,
          views::ElementTrackerViews::GetContextForView(browser_view),
          /*require_visible=*/true);
  ASSERT_EQ(1u, toggle_views.size());
  ToolbarButton* const sidebar_toggle =
      views::AsViewClass<ToolbarButton>(toggle_views.front());
  ASSERT_TRUE(sidebar_toggle);
  const std::array<std::pair<std::string_view, ToolbarButton*>, 4>
      header_buttons = {
          {{"sidebar toggle", sidebar_toggle},
           {"back", browser_view->toolbar_button_provider()->GetBackButton()},
           {"forward", toolbar->forward_button()},
           {"reload", toolbar->reload_button()}}};
  for (const auto& [description, button] : header_buttons) {
    ASSERT_TRUE(button) << description;
    ASSERT_TRUE(button->GetVisible()) << description;
  }

  const auto browser_point_to_device_pixel = [&](gfx::Point point) {
    views::View::ConvertPointToScreen(browser_view, &point);
    gfx::Rect point_dip_bounds(point, gfx::Size(1, 1));
    point_dip_bounds.Offset(-root_in_screen.x(), -root_in_screen.y());
    return gfx::ToEnclosingRect(gfx::ScaleRect(gfx::RectF(point_dip_bounds),
                                               device_scale_factor))
        .CenterPoint();
  };
  const auto read_compositor_pixels = [&]() -> SkBitmap {
    compositor->ScheduleFullRedraw();
    CHECK(applied_.WaitForPresentedFrame(*widget));
    base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
    auto request = std::make_unique<viz::CopyOutputRequest>(
        viz::CopyOutputRequest::ResultFormat::RGBA,
        viz::CopyOutputRequest::ResultDestination::kSystemMemory,
        readback.GetCallback());
    request->set_area(gfx::Rect(compositor->size()));
    compositor->root_layer()->RequestCopyOfOutput(std::move(request));
    compositor->ScheduleFullRedraw();
    std::unique_ptr<viz::CopyOutputResult> result = readback.Take();
    CHECK(result);
    CHECK(!result->IsEmpty());
    auto scoped_bitmap = result->ScopedAccessSkBitmap();
    SkBitmap bitmap = scoped_bitmap.GetOutScopedBitmap();
    CHECK(bitmap.readyToDraw());
    return bitmap;
  };

  const auto color_distance = [](SkColor first, SkColor second) {
    const int red = SkColorGetR(first) - SkColorGetR(second);
    const int green = SkColorGetG(first) - SkColorGetG(second);
    const int blue = SkColorGetB(first) - SkColorGetB(second);
    return red * red + green * green + blue * blue;
  };
  const auto expect_header_chrome_pixels = [&](const char* phase) {
    const SkBitmap current_pixels = read_compositor_pixels();
    const gfx::Rect composited_header_bounds =
        browser_view->top_container()->bounds();
    for (const auto& [description, button] : header_buttons) {
      const gfx::Rect button_bounds = view_bounds_in_browser(button);
      ASSERT_FALSE(button_bounds.IsEmpty()) << phase << ": " << description;
      ASSERT_TRUE(composited_header_bounds.Contains(button_bounds))
          << phase << ": " << description;
      const std::optional<ui::ImageModel>& model =
          button->GetImageModel(button->GetState());
      ASSERT_TRUE(model.has_value() && model->IsVectorIcon())
          << phase << ": " << description;
      const SkColor expected_foreground =
          model->GetVectorIcon().color().ResolveToSkColor(
              button->GetColorProvider());

      int sampled_pixels = 0;
      int foreground_like_pixels = 0;
      SkColor first_actual = SK_ColorTRANSPARENT;
      for (int y = button_bounds.y(); y < button_bounds.bottom(); ++y) {
        for (int x = button_bounds.x(); x < button_bounds.right(); ++x) {
          const gfx::Point pixel =
              browser_point_to_device_pixel(gfx::Point(x, y));
          if (!gfx::Rect(current_pixels.width(), current_pixels.height())
                   .Contains(pixel)) {
            continue;
          }
          const SkColor actual = current_pixels.getColor(pixel.x(), pixel.y());
          if (sampled_pixels == 0) {
            first_actual = actual;
          }
          ++sampled_pixels;
          if (color_distance(actual, expected_foreground) <
              color_distance(actual, first_actual)) {
            ++foreground_like_pixels;
          }
        }
      }
      ASSERT_GT(sampled_pixels, 0) << phase << ": " << description;
      EXPECT_GT(foreground_like_pixels, 0)
          << phase << ": " << description
          << " has no composited foreground pixels";
      EXPECT_LT(foreground_like_pixels, sampled_pixels / 2)
          << phase << ": " << description
          << " is covered by a foreground-colored layer instead of painting "
             "an icon over the Header Surface; first actual="
          << first_actual << ", expected foreground=" << expected_foreground
          << ", sampled background=" << first_actual
          << ", button bounds=" << button_bounds.ToString()
          << ", Header bounds=" << composited_header_bounds.ToString();
    }
  };
  expect_header_chrome_pixels("closed-baseline");

  RunTestSequence(ReplaceAndShowSidePanel(SidePanelType::kToolbar, std::nullopt,
                                          animation_container.get()));
  const auto expect_current_frame = [&](const char* phase) {
    ASSERT_TRUE(layout->last_native_layout_for_testing()) << phase;
    const auto result = *layout->last_native_layout_for_testing();
    gfx::Rect outer_surface_bounds =
        result.horizontal_plan.content_column_params.visual_client_area;
    outer_surface_bounds.Inset(
        gfx::Insets(yee::kSidebarMetrics.content_gutter));
    const gfx::Rect main_surface_bounds =
        result.combined_surface_outline_bounds;
    ASSERT_FALSE(main_surface_bounds.IsEmpty()) << phase;
    EXPECT_EQ(baseline_surface_top, main_surface_bounds.y()) << phase;
    EXPECT_EQ(baseline_surface_bottom, main_surface_bounds.bottom()) << phase;
    EXPECT_EQ(baseline_surface_top, result.header_bounds.y()) << phase;
    EXPECT_EQ(outer_surface_bounds.y(), main_surface_bounds.y()) << phase;
    EXPECT_EQ(outer_surface_bounds.bottom(), main_surface_bounds.bottom())
        << phase;
    EXPECT_EQ(1, result.top_container_calculation_count) << phase;
    EXPECT_EQ(result.header_bounds, result.top_container_input_bounds) << phase;
    EXPECT_EQ(result.header_bounds, browser_view->top_container()->bounds())
        << phase;
    EXPECT_EQ(result.header_bounds.right(), main_surface_bounds.right())
        << phase;
    EXPECT_EQ(main_surface_bounds, result.main_background_bounds) << phase;
    EXPECT_TRUE(result.main_background_bounds.Contains(result.header_bounds))
        << phase;
    EXPECT_EQ(result.main_background_bounds,
              browser_view->main_background_region_for_testing()->bounds())
        << phase;
    EXPECT_EQ(main_background_region->GetLocalBounds(), surface_fill->bounds())
        << phase;
    ASSERT_EQ(main_background_region->layer()->parent(),
              browser_view->top_container()->layer()->parent())
        << phase;
    const auto& sibling_layers =
        main_background_region->layer()->parent()->children();
    const auto current_main_layer_position =
        std::ranges::find(sibling_layers, main_background_region->layer());
    const auto current_header_layer_position = std::ranges::find(
        sibling_layers, browser_view->top_container()->layer());
    ASSERT_NE(sibling_layers.end(), current_main_layer_position) << phase;
    ASSERT_NE(sibling_layers.end(), current_header_layer_position) << phase;
    EXPECT_LT(current_main_layer_position, current_header_layer_position)
        << phase;
    auto* const main_background =
        browser_view->main_background_region_for_testing()
            ->background()
            ->AsA<CustomCornersBackground>();
    ASSERT_TRUE(main_background) << phase;
    auto* const top_container_background = browser_view->top_container()
                                               ->background()
                                               ->AsA<CustomCornersBackground>();
    ASSERT_TRUE(top_container_background) << phase;
    EXPECT_FALSE(top_container_background->is_visible()) << phase;
    const std::optional<gfx::RoundedCornersF> main_background_radii =
        main_background->GetRoundedCornerRadii();
    ASSERT_TRUE(main_background_radii) << phase;
    const float top_radius =
        result.main_background_bounds.y() == main_surface_bounds.y()
            ? yee::kSidebarMetrics.content_corner_radius
            : 0.0f;
    const float bottom_radius =
        result.main_background_bounds.bottom() == main_surface_bounds.bottom()
            ? yee::kSidebarMetrics.content_corner_radius
            : 0.0f;
    EXPECT_EQ(gfx::RoundedCornersF(top_radius, top_radius, bottom_radius,
                                   bottom_radius),
              *main_background_radii)
        << phase;
    EXPECT_EQ(result.side_panel.current_bounds,
              browser_view->side_panel()->bounds())
        << phase;
    EXPECT_EQ(result.side_panel.local_clip_bounds,
              browser_view->side_panel()->clip_to_visible_area_for_testing())
        << phase;
    if (result.side_panel.local_clip_bounds) {
      gfx::Rect visible_panel_bounds = *result.side_panel.local_clip_bounds;
      visible_panel_bounds.Intersect(
          gfx::Rect(result.side_panel.current_bounds.size()));
      if (!visible_panel_bounds.IsEmpty()) {
        visible_panel_bounds.Offset(
            result.side_panel.current_bounds.OffsetFromOrigin());
        EXPECT_TRUE(outer_surface_bounds.Contains(visible_panel_bounds))
            << phase;
        views::View* const side_panel_surface =
            browser_view->GetViewByID(yee::kSidePanelSurfaceViewId);
        ASSERT_TRUE(side_panel_surface) << phase;
        EXPECT_TRUE(side_panel_surface->GetVisible()) << phase;
        EXPECT_EQ(visible_panel_bounds, side_panel_surface->bounds()) << phase;
        if (result.side_panel.leading) {
          EXPECT_EQ(yee::kSidebarMetrics.resize_gutter_width,
                    main_surface_bounds.x() - visible_panel_bounds.right())
              << phase;
        } else {
          EXPECT_EQ(yee::kSidebarMetrics.resize_gutter_width,
                    visible_panel_bounds.x() - main_surface_bounds.right())
              << phase;
        }
      }
    }
    EXPECT_EQ(result.multi_contents_bounds,
              browser_view->multi_contents_view()->bounds())
        << phase;
    const gfx::Rect location_bar_bounds = view_bounds_in_browser(location_bar);
    EXPECT_TRUE(result.header_bounds.Contains(location_bar_bounds)) << phase;
    EXPECT_EQ(baseline_location_bar_leading_offset,
              location_bar_bounds.x() - result.header_bounds.x())
        << phase;
    int expected_location_bar_width =
        result.header_bounds.width() - baseline_non_location_bar_width;
#if BUILDFLAG(IS_WIN)
    // SidePanel이 caption 아래로 이동하면 실제 Toolbar의 예약 폭도 바뀐다.
    // 카드 배경의 폭 대신 현재 caption exclusion이 적용된 Toolbar를 검증한다.
    expected_location_bar_width =
        toolbar->width() - baseline_toolbar_chrome_width;
#endif
    EXPECT_EQ(expected_location_bar_width, location_bar_bounds.width())
        << phase;
    EXPECT_EQ(
        result.native_content_animating,
        browser_view->multi_contents_view()->is_animating_content_for_testing())
        << phase;
    expect_header_chrome_pixels(phase);
  };

  ApplyScheduledLayout();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const double start_reveal =
      layout->last_native_layout_for_testing()->side_panel.reveal;
  expect_current_frame("open-start");

  gfx::AnimationContainerTestApi(animation_container.get())
      .IncrementTime(base::Milliseconds(100));
  ApplyScheduledLayout();
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const double mid_reveal =
      layout->last_native_layout_for_testing()->side_panel.reveal;
  EXPECT_LT(start_reveal, mid_reveal);
  EXPECT_LT(mid_reveal, 1.0);
  expect_current_frame("open-mid-leading");
  EXPECT_EQ(!base::i18n::IsRTL(),
            layout->last_native_layout_for_testing()->side_panel.leading);

  browser_view->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kSidePanelHorizontalAlignment, true);
  ApplyScheduledLayout();
  expect_current_frame("open-mid-alignment-switch");
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(base::i18n::IsRTL(),
            layout->last_native_layout_for_testing()->side_panel.leading);

  gfx::AnimationContainerTestApi(animation_container.get())
      .IncrementTime(base::Seconds(1));
  ApplyScheduledLayout();
  expect_current_frame("open-end");
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_EQ(1.0, layout->last_native_layout_for_testing()->side_panel.reveal);
  EXPECT_FALSE(layout->last_native_layout_for_testing()->side_panel.animating);

  browser_view->side_panel()->Close(/*animated=*/true);
  gfx::AnimationContainerTestApi(animation_container.get())
      .IncrementTime(base::Milliseconds(100));
  ApplyScheduledLayout();
  expect_current_frame("close-mid");
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  const double closing_reveal =
      layout->last_native_layout_for_testing()->side_panel.reveal;
  EXPECT_GT(closing_reveal, 0.0);
  EXPECT_LT(closing_reveal, 1.0);

  browser_view->side_panel()->Open(/*animated=*/true);
  gfx::AnimationContainerTestApi(animation_container.get())
      .IncrementTime(base::Milliseconds(100));
  ApplyScheduledLayout();
  expect_current_frame("reverse-open-mid");
  ASSERT_TRUE(layout->last_native_layout_for_testing());
  EXPECT_GT(layout->last_native_layout_for_testing()->side_panel.reveal,
            closing_reveal);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       VerticalTabsSinglePaneCollapse) {
  if (!features::UseSidePanelFlyoverAnimation()) {
    GTEST_SKIP();
  }
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  RunTestSequence(ClearResizeCounts(),
                  Do([this] { ExpectOneVisibleVerticalTabStripToggle(); }),
                  ToggleVerticalTabStripCollapsed(true), Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  CheckResizeCounts(1U, 0U));
}

class YeeRendererResizeUiTest : public YeeAppliedSurfaceUiTest,
                                public testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    if (GetParam()) {
      features_.InitAndEnableFeature(features::kSidePanelFlyoverAnimation);
    } else {
      features_.InitAndDisableFeature(features::kSidePanelFlyoverAnimation);
    }
    set_open_about_blank_on_browser_launch(true);
    InteractiveBrowserTest::SetUp();
  }

 private:
  base::test::ScopedFeatureList features_;
  gfx::ScopedAnimationDurationScaleMode animation_scale_{
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION};
};

IN_PROC_BROWSER_TEST_P(YeeRendererResizeUiTest,
                       YeeResizeCollapseReloadKeepsRendererLive) {
  auto* const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(state);
  state->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  // 표면색 주입이나 재귀 레이아웃 없이 실제 resize/animation 경로가 renderer와
  // 이를 담은 host를 함께 갱신해야 한다.
  const GURL page(
      "data:text/html,<style>html,body{margin:0;height:100%;"
      "background:rgb(255,0,255)}div{height:8px;width:100%;"
      "background:rgb(0,255,255)}</style><div></div>");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));
  auto* const browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* const widget = browser_view->GetWidget();
  auto* const contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* const web_view = browser_view->multi_contents_view()
                             ->GetActiveContentsContainerView()
                             ->contents_view();
  const auto verify_live_renderer = [&] {
    int observed_renderer_width = -1;
    int expected_renderer_width = -1;
    ASSERT_TRUE(RendererReady(*contents, *web_view));
    ASSERT_TRUE(([&] {
#if BUILDFLAG(IS_WIN)
      // Aura는 DIP 폭을 올림한 물리 픽셀로 renderer에 전달한다. 125%에서
      // 정수 innerWidth는 1 CSS px 클 수 있으므로 실제 viewport의 픽셀 폭을
      // 검증한다. native bounds와 아래 sentinel readback도 그대로 확인한다.
      observed_renderer_width =
          content::EvalJs(contents,
                          "Math.round(window.visualViewport.width * "
                          "window.devicePixelRatio)")
              .ExtractInt();
      expected_renderer_width =
          gfx::ScaleToCeiledSize(web_view->size(),
                                 widget->GetCompositor()->device_scale_factor())
              .width();
#else
      observed_renderer_width =
          content::EvalJs(contents, "window.innerWidth").ExtractInt();
      expected_renderer_width = web_view->width();
#endif
      return observed_renderer_width == expected_renderer_width;
    })())
        << "renderer width=" << observed_renderer_width
        << ", expected=" << expected_renderer_width
        << ", WebView width=" << web_view->width() << ", native bounds="
        << contents->GetRenderWidgetHostView()->GetViewBounds().ToString()
        << ", devicePixelRatio="
        << content::EvalJs(contents, "window.devicePixelRatio");
#if BUILDFLAG(IS_WIN)
    EXPECT_EQ(web_view->size(),
              contents->GetRenderWidgetHostView()->GetViewBounds().size());
#endif
    ui::Compositor* const compositor = widget->GetCompositor();
    ASSERT_TRUE(compositor);
    base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
    auto request = std::make_unique<viz::CopyOutputRequest>(
        viz::CopyOutputRequest::ResultFormat::RGBA,
        viz::CopyOutputRequest::ResultDestination::kSystemMemory,
        readback.GetCallback());
    request->set_area(gfx::Rect(compositor->size()));
    compositor->root_layer()->RequestCopyOfOutput(std::move(request));
    compositor->ScheduleFullRedraw();
    auto result = readback.Take();
    ASSERT_TRUE(result);
    ASSERT_FALSE(result->IsEmpty());
    auto pixels = result->ScopedAccessSkBitmap();
    const SkBitmap bitmap = pixels.GetOutScopedBitmap();
    gfx::Point center = web_view->GetBoundsInScreen().CenterPoint();
    center -= widget->GetRootView()->GetBoundsInScreen().OffsetFromOrigin();
    center =
        gfx::ScaleToFlooredPoint(center, compositor->device_scale_factor());
    ASSERT_TRUE(gfx::Rect(bitmap.width(), bitmap.height()).Contains(center));
    const SkColor color = bitmap.getColor(center.x(), center.y());
    EXPECT_GT(SkColorGetR(color), 220u);
    EXPECT_LT(SkColorGetG(color), 35u);
    EXPECT_GT(SkColorGetB(color), 220u);
  };
  verify_live_renderer();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_size(gfx::Size(1000, 800));
  ASSERT_TRUE(
      applied_.Run(PageConditions(true), [&] { widget->SetBounds(bounds); }));
  verify_live_renderer();
  RunTestSequence(ApplyStepsAndWait(
      applied_,
      [&] {
        auto conditions = PageConditions(true);
        conditions.push_back(VerticalIdle());
        return conditions;
      },
      Steps(PressButton(kVerticalTabStripCollapseButtonElementId))));
  verify_live_renderer();
  const double reload_scale = widget->GetCompositor()->device_scale_factor();
  auto reload_conditions = PageConditions(true);
  reload_conditions.push_back(applied_.source().ExpectRendererViewport(
      *contents, gfx::ScaleToCeiledSize(web_view->size(), reload_scale),
      reload_scale, true));
  ASSERT_TRUE(applied_.Run(
      reload_conditions,
      [&] {
        chrome::Reload(browser(), WindowOpenDisposition::CURRENT_TAB);
        ASSERT_TRUE(content::WaitForLoadStop(contents));
      },
      yee::test::AppliedDomain::kRenderer));
  verify_live_renderer();
  RunTestSequence(ApplyStepsAndWait(
      applied_,
      [&] {
        auto conditions = PageConditions(true);
        conditions.push_back(VerticalIdle());
        return conditions;
      },
      Steps(PressButton(kVerticalTabStripCollapseButtonElementId))));
  verify_live_renderer();
}

INSTANTIATE_TEST_SUITE_P(Flyover, YeeRendererResizeUiTest, testing::Bool());

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       YeeHardClipOwnersMatchAtDeviceScale) {
  const GURL magenta_url(
      "data:text/html,<meta name=theme-color content=%230044aa>"
      "<style>html,body{margin:0;width:100%;height:100%;"
      "background:%23ff00ff;overflow:hidden}</style>");
  const GURL cyan_url(
      "data:text/html,<meta name=theme-color content=%23aa4400>"
      "<style>html,body{margin:0;width:100%;height:100%;"
      "background:%2300ffff;overflow:hidden}</style>");

  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), magenta_url));
  yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
      browser()->tab_strip_model()->GetActiveWebContents())
      ->SetPageSurfaceColorForTesting(SkColorSetRGB(0, 68, 170));
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const widget = browser_view->GetWidget();
  ASSERT_TRUE(widget);
  ui::Compositor* const compositor = widget->GetCompositor();
  ASSERT_TRUE(compositor);
  const float device_scale_factor = compositor->device_scale_factor();
  ASSERT_GT(device_scale_factor, 0.0f);

  const gfx::Rect root_in_screen = widget->GetRootView()->GetBoundsInScreen();
  const auto to_device_pixels = [&](const gfx::Rect& screen_dip_bounds) {
    gfx::Rect root_dip_bounds = screen_dip_bounds;
    root_dip_bounds.Offset(-root_in_screen.x(), -root_in_screen.y());
    return gfx::ToEnclosingRect(
        gfx::ScaleRect(gfx::RectF(root_dip_bounds), device_scale_factor));
  };
  const auto expect_matching_clip_owners =
      [&](ContentsWebView* contents) -> gfx::Rect {
    CHECK(contents);
    CHECK(contents->GetVisible());
    CHECK(contents->layer());
    CHECK(contents->holder()->layer());

    // Views 바탕과 renderer를 소유한 NativeViewHost는 compositor에 같은 hard
    // clip을 전달해야 한다. 같은 DIP 입력은 소수 배율에서도 같은 device-pixel
    // clip을 만든다.
#if BUILDFLAG(IS_WIN)
    // Aura의 renderer layer는 holder의 자식이다. 공통 hard mask가 실제
    // renderer를 감싸는지 확인하고 픽셀 검사를 통해 경계 누출도 검증한다.
    CHECK(contents->holder()->native_view());
    ui::Layer* const renderer_layer =
        contents->holder()->native_view()->layer();
    EXPECT_TRUE(contents->layer()->Contains(renderer_layer));
    gfx::RoundedCornersF expected_native_radii =
        contents->holder()->GetNativeViewCornerRadii();
    if (base::i18n::IsRTL()) {
      expected_native_radii =
          gfx::RoundedCornersF(expected_native_radii.upper_right(),
                               expected_native_radii.upper_left(),
                               expected_native_radii.lower_left(),
                               expected_native_radii.lower_right());
    }
    const gfx::Rect clip = contents->holder()->GetNativeViewClipRect();
    if (!clip.IsEmpty()) {
      // target의 일부가 잘린 변은 원래 모서리가 보이지 않는 사각 경계다.
      const gfx::Size native_size = renderer_layer->bounds().size();
      if (clip.x() > 0 || clip.y() > 0) {
        expected_native_radii.set_upper_left(0);
      }
      if (clip.right() < native_size.width() || clip.y() > 0) {
        expected_native_radii.set_upper_right(0);
      }
      if (clip.right() < native_size.width() ||
          clip.bottom() < native_size.height()) {
        expected_native_radii.set_lower_right(0);
      }
      if (clip.x() > 0 || clip.bottom() < native_size.height()) {
        expected_native_radii.set_lower_left(0);
      }
    }
    EXPECT_EQ(expected_native_radii, renderer_layer->rounded_corner_radii());
    EXPECT_EQ(contents->layer()->clip_rect(), renderer_layer->clip_rect());
#else
    EXPECT_EQ(contents->layer()->rounded_corner_radii(),
              contents->holder()->layer()->rounded_corner_radii());
    EXPECT_EQ(contents->layer()->clip_rect(),
              contents->holder()->layer()->clip_rect());
    EXPECT_TRUE(contents->holder()->layer()->GetMasksToBounds());
    EXPECT_FALSE(contents->holder()->layer()->is_fast_rounded_corner());
#endif
    EXPECT_EQ(contents->layer()->rounded_corner_radii(),
              contents->holder()->GetNativeViewCornerRadii());
    EXPECT_EQ(contents->layer()->clip_rect(),
              contents->holder()->GetNativeViewClipRect());
    EXPECT_TRUE(contents->layer()->GetMasksToBounds());
    EXPECT_FALSE(contents->layer()->is_fast_rounded_corner());

    EXPECT_EQ(contents->GetBoundsInScreen(),
              contents->holder()->GetBoundsInScreen());
    EXPECT_EQ(to_device_pixels(contents->GetBoundsInScreen()),
              to_device_pixels(contents->holder()->GetBoundsInScreen()));

    gfx::Rect visible_clip = contents->layer()->clip_rect();
    if (visible_clip.IsEmpty()) {
      visible_clip = contents->GetLocalBounds();
    }
    views::View::ConvertRectToScreen(contents, &visible_clip);
    const gfx::Rect visible_clip_in_pixels = to_device_pixels(visible_clip);
    EXPECT_TRUE(gfx::Rect(compositor->size()).Contains(visible_clip_in_pixels));
    return visible_clip_in_pixels;
  };
  const auto read_compositor_pixels = [&]() -> SkBitmap {
#if BUILDFLAG(IS_WIN)
    // Views layout 완료만으로 새 분할 renderer의 프레임이 Viz에 도착하지는
    // 않는다. 각 실제 페이지의 copy 가능 프레임을 기다린 뒤 root를 읽는다.
    for (const auto& container : browser_view->GetContentsContainerViews()) {
      auto* const contents = container->contents_view();
      if (contents->IsDrawn() && contents->web_contents()) {
        content::WaitForCopyableViewInFrame(
            contents->web_contents()->GetPrimaryMainFrame());
      }
    }
#endif
    base::test::TestFuture<std::unique_ptr<viz::CopyOutputResult>> readback;
    auto request = std::make_unique<viz::CopyOutputRequest>(
        viz::CopyOutputRequest::ResultFormat::RGBA,
        viz::CopyOutputRequest::ResultDestination::kSystemMemory,
        readback.GetCallback());
    request->set_area(gfx::Rect(compositor->size()));
    compositor->root_layer()->RequestCopyOfOutput(std::move(request));
    compositor->ScheduleFullRedraw();
    std::unique_ptr<viz::CopyOutputResult> result = readback.Take();
    CHECK(result);
    CHECK(!result->IsEmpty());
#if BUILDFLAG(IS_WIN)
    // Aura의 fractional scale readback은 root DIP 경계를 바깥 픽셀로 올린다.
    const gfx::Size expected_readback_size =
        gfx::ToEnclosingRect(
            gfx::ScaleRect(gfx::RectF(compositor->root_layer()->bounds()),
                           device_scale_factor))
            .size();
    EXPECT_EQ(expected_readback_size, result->size());
#else
    EXPECT_EQ(compositor->size(), result->size());
#endif
    auto scoped_bitmap = result->ScopedAccessSkBitmap();
    SkBitmap bitmap = scoped_bitmap.GetOutScopedBitmap();
    CHECK(bitmap.readyToDraw());
    return bitmap;
  };
  const auto expect_sentinel_contained = [](const SkBitmap& bitmap,
                                            const gfx::Rect& clip_in_pixels,
                                            auto is_renderer_sentinel,
                                            std::string_view description) {
    const gfx::Point center = clip_in_pixels.CenterPoint();
    ASSERT_TRUE(is_renderer_sentinel(bitmap.getColor(center.x(), center.y())))
        << description << " renderer center was not captured";
    size_t renderer_pixels_outside_clip_bounds = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        if (!clip_in_pixels.Contains(x, y) &&
            is_renderer_sentinel(bitmap.getColor(x, y))) {
          ++renderer_pixels_outside_clip_bounds;
        }
      }
    }
    EXPECT_EQ(0U, renderer_pixels_outside_clip_bounds)
        << description << " renderer sentinel escaped its shared "
        << "device-pixel clip";
  };

  const auto is_magenta = [](SkColor color) {
    return SkColorGetR(color) >= 220 && SkColorGetG(color) <= 35 &&
           SkColorGetB(color) >= 220;
  };
  const auto is_cyan = [](SkColor color) {
    return SkColorGetR(color) <= 35 && SkColorGetG(color) >= 220 &&
           SkColorGetB(color) >= 220;
  };
  const auto expect_corner_clipped = [](const SkBitmap& bitmap,
                                        const gfx::Rect& clip_in_pixels,
                                        const gfx::RoundedCornersF& radii,
                                        auto is_renderer_sentinel) {
    const auto expect_one_corner_clipped = [&](float radius, int x, int y) {
      if (radius > 0.0f) {
        EXPECT_FALSE(is_renderer_sentinel(bitmap.getColor(x, y)));
      }
    };
    expect_one_corner_clipped(radii.upper_left(), clip_in_pixels.x(),
                              clip_in_pixels.y());
    expect_one_corner_clipped(radii.upper_right(), clip_in_pixels.right() - 1,
                              clip_in_pixels.y());
    expect_one_corner_clipped(radii.lower_right(), clip_in_pixels.right() - 1,
                              clip_in_pixels.bottom() - 1);
    expect_one_corner_clipped(radii.lower_left(), clip_in_pixels.x(),
                              clip_in_pixels.bottom() - 1);
  };

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ContentsContainerView* const single_container =
      multi_contents_view->GetActiveContentsContainerView();
  ASSERT_TRUE(single_container);
  ContentsWebView* const single_contents = single_container->contents_view();
  const gfx::Rect single_clip = expect_matching_clip_owners(single_contents);
  SkBitmap bitmap = read_compositor_pixels();
  expect_sentinel_contained(bitmap, single_clip, is_magenta, "single pane");
  expect_corner_clipped(bitmap, single_clip,
                        single_contents->GetBackgroundRadii(), is_magenta);

  // 브라우저 chrome 애니메이션 중의 비어 있지 않은 rect clip을 확인한다.
  // 13-DIP target outset은 1.25 배율에서 소수 픽셀 경계에 놓인다.
  single_container->SetTargetContentBounds(gfx::Outsets::TLBR(0, 0, 0, 13));
  single_container->DeprecatedLayoutImmediately();
  ASSERT_FALSE(single_contents->layer()->clip_rect().IsEmpty());
  const yee::PageViewportGeometry target_geometry =
      single_container->GetPageViewportGeometry();
  views::View* const page_clip_host =
      single_container->GetViewByID(yee::kPageViewportClipHostViewId);
  ASSERT_TRUE(page_clip_host);
  views::View& target_host = yee::GetPageTargetHost(*page_clip_host);
  EXPECT_EQ(target_host.GetLocalBounds(), single_contents->bounds());
  EXPECT_EQ(single_contents->layer()->clip_rect(),
            target_geometry.current_clip_in_target);
  EXPECT_EQ(single_container->GetContentsViewBounds(),
            target_geometry.content_and_footer_in_container);
  EXPECT_EQ(target_geometry.current_viewport_in_container,
            page_clip_host->bounds());
  gfx::Rect target_host_bounds = target_geometry.target_stack_in_container;
  target_host_bounds.Offset(
      -target_geometry.current_viewport_in_container.OffsetFromOrigin());
  EXPECT_EQ(target_host_bounds, target_host.bounds());
  for (const views::View* target_child : target_host.children()) {
    EXPECT_EQ(target_host.GetLocalBounds(), target_child->bounds());
  }
  EXPECT_EQ(page_clip_host->GetLocalBounds(),
            yee::GetViewportOverlayHost(*page_clip_host).bounds());
  const gfx::Rect target_clip = expect_matching_clip_owners(single_contents);
  gfx::Rect target_clip_in_screen = target_geometry.current_clip_in_target;
  views::View::ConvertRectToScreen(single_contents, &target_clip_in_screen);
  EXPECT_EQ(target_geometry.current_viewport_in_screen, target_clip_in_screen);
  bitmap = read_compositor_pixels();
  expect_sentinel_contained(bitmap, target_clip, is_magenta,
                            "single-pane target bounds");
  single_container->SetTargetContentBounds(std::nullopt);
  single_container->DeprecatedLayoutImmediately();

  // 분할 선택 페이지의 지연 Mojo 응답은 NTP 이동을 예약할 수 있다.
  // clip 검사는 고정된 두 페이지를 묶어 실제 renderer의 경계만 확인한다.
  auto* const model = browser()->tab_strip_model();
  auto* const magenta_contents = model->GetActiveWebContents();
  ASSERT_EQ(magenta_url, magenta_contents->GetLastCommittedURL());
  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), /*index=*/-1,
                   /*foreground=*/true);
  auto* const cyan_contents = model->GetActiveWebContents();
  ASSERT_NE(magenta_contents, cyan_contents);
  ASSERT_TRUE(content::NavigateToURL(cyan_contents, cyan_url));
  model->AddToNewSplit({model->GetIndexOfWebContents(magenta_contents)},
                       split_tabs::SplitTabVisualData(),
                       split_tabs::SplitTabCreatedSource::kToolbarButton);
  model->ActivateTabAt(model->GetIndexOfWebContents(magenta_contents));
  ApplyScheduledLayout();
  ASSERT_TRUE(multi_contents_view->IsInSplitView());
  ContentsWebView* const active_contents =
      multi_contents_view->GetActiveContentsView();
  ContentsWebView* const inactive_contents =
      multi_contents_view->GetInactiveContentsView();
  ASSERT_TRUE(active_contents);
  ASSERT_TRUE(inactive_contents);
  ASSERT_EQ(magenta_contents, active_contents->web_contents());
  ASSERT_EQ(cyan_contents, inactive_contents->web_contents());
  ASSERT_EQ(magenta_url, magenta_contents->GetLastCommittedURL());
  ASSERT_EQ(cyan_url, cyan_contents->GetLastCommittedURL());
  yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
      active_contents->web_contents())
      ->SetPageSurfaceColorForTesting(SkColorSetRGB(0, 68, 170));
  yee::BrowserSurfaceColorController::GetOrCreateForWebContents(
      inactive_contents->web_contents())
      ->SetPageSurfaceColorForTesting(SkColorSetRGB(170, 68, 0));
  ApplyScheduledLayout();

  const gfx::Rect active_clip = expect_matching_clip_owners(active_contents);
  const gfx::Rect inactive_clip =
      expect_matching_clip_owners(inactive_contents);
  EXPECT_FALSE(active_clip.Intersects(inactive_clip));
  bitmap = read_compositor_pixels();
  expect_sentinel_contained(bitmap, active_clip, is_magenta,
                            "active split pane");
  expect_sentinel_contained(bitmap, inactive_clip, is_cyan,
                            "inactive split pane");
  expect_corner_clipped(bitmap, active_clip,
                        active_contents->GetBackgroundRadii(), is_magenta);
  expect_corner_clipped(bitmap, inactive_clip,
                        inactive_contents->GetBackgroundRadii(), is_cyan);

  LOG(INFO) << "Yee hard clip compositor readback: device_scale_factor="
            << device_scale_factor
            << ", compositor_pixels=" << compositor->size().ToString()
            << ", single_clip_pixels=" << single_clip.ToString()
            << ", target_clip_pixels=" << target_clip.ToString()
            << ", active_split_clip_pixels=" << active_clip.ToString()
            << ", inactive_split_clip_pixels=" << inactive_clip.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth) {
  auto disable_find_bar_animation =
      FindBarHost::SetEnableAnimationsForTesting(false);
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const browser_widget = browser_view->GetWidget();
  ASSERT_TRUE(browser_widget);
  gfx::Rect window_bounds = browser_widget->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(800, 600));
  browser_widget->SetBounds(window_bounds);
  ApplyScheduledLayout();

  RunTestSequence(EnterSplitView(), Do([this]() {
                    FindBarController::From(browser())->Show();
                    ApplyScheduledLayout();
                  }));

  ContentsContainerView* const active_container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(active_container);
  const gfx::Rect active_viewport_in_screen =
      active_container->GetPageViewportGeometry().current_viewport_in_screen;
  ASSERT_FALSE(active_viewport_in_screen.IsEmpty());

  FindBarHost* const find_bar_host =
      static_cast<FindBarHost*>(FindBarController::From(browser())->find_bar());
  ASSERT_TRUE(find_bar_host);
  ASSERT_TRUE(find_bar_host->IsFindBarVisible());
  views::Widget* const find_bar_widget = find_bar_host->GetHostWidget();
  ASSERT_TRUE(find_bar_widget);
  const gfx::Rect find_bar_bounds_in_screen =
      find_bar_widget->GetWindowBoundsInScreen();

  EXPECT_TRUE(active_viewport_in_screen.Contains(find_bar_bounds_in_screen))
      << "active viewport=" << active_viewport_in_screen.ToString()
      << ", find bar=" << find_bar_bounds_in_screen.ToString();
  EXPECT_LT(
      active_viewport_in_screen.width(),
      find_bar_host->GetFindBarViewForTesting()->GetPreferredSize().width())
      << "The regression requires a pane narrower than the preferred Find "
         "Bar width.";

  FindBarView* const find_bar_view = find_bar_host->GetFindBarViewForTesting();
  for (int id :
       {VIEW_ID_FIND_IN_PAGE_TEXT_FIELD, VIEW_ID_FIND_IN_PAGE_PREVIOUS_BUTTON,
        VIEW_ID_FIND_IN_PAGE_NEXT_BUTTON, VIEW_ID_FIND_IN_PAGE_CLOSE_BUTTON}) {
    views::View* const control = find_bar_view->GetViewByID(id);
    ASSERT_TRUE(control) << "Missing Find Bar control id " << id;
    EXPECT_TRUE(control->GetVisible()) << "Hidden Find Bar control id " << id;
    EXPECT_TRUE(
        find_bar_bounds_in_screen.Contains(control->GetBoundsInScreen()))
        << "Find Bar control " << id << " escapes its Widget: widget="
        << find_bar_bounds_in_screen.ToString()
        << ", control=" << control->GetBoundsInScreen().ToString();
  }

  std::vector<views::View*> descendants{find_bar_view};
  while (!descendants.empty()) {
    views::View* const parent = descendants.back();
    descendants.pop_back();
    for (views::View* child : parent->children()) {
      descendants.push_back(child);
      if (!child->GetVisible()) {
        continue;
      }
      EXPECT_TRUE(
          find_bar_bounds_in_screen.Contains(child->GetBoundsInScreen()))
          << "Visible Find Bar child " << child->GetClassName()
          << " escapes its Widget: widget="
          << find_bar_bounds_in_screen.ToString()
          << ", child=" << child->GetBoundsInScreen().ToString();
    }
  }

  LOG(INFO) << "Yee Find Bar containment: active viewport="
            << active_viewport_in_screen.ToString()
            << ", find bar=" << find_bar_bounds_in_screen.ToString();
}

#if BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplRtlFractionalDsfUiTest,
                       YeeFindBarUsesPixelStableBoundsAtMinimumWidth) {
  auto disable_find_bar_animation =
      FindBarHost::SetEnableAnimationsForTesting(false);
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const browser_widget = browser_view->GetWidget();
  ASSERT_TRUE(browser_widget);
  gfx::Rect window_bounds = browser_widget->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(800, 600));
  browser_widget->SetBounds(window_bounds);
  ApplyScheduledLayout();

  RunTestSequence(EnterSplitView(), Do([this]() {
                    FindBarController::From(browser())->Show();
                    ApplyScheduledLayout();
                  }));

  ContentsContainerView* const active_container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(active_container);
  const gfx::Rect active_viewport_in_screen =
      active_container->GetPageViewportGeometry().current_viewport_in_screen;
  ASSERT_FALSE(active_viewport_in_screen.IsEmpty());

  FindBarHost* const find_bar_host =
      static_cast<FindBarHost*>(FindBarController::From(browser())->find_bar());
  ASSERT_TRUE(find_bar_host);
  ASSERT_TRUE(find_bar_host->IsFindBarVisible());
  views::Widget* const find_bar_widget = find_bar_host->GetHostWidget();
  ASSERT_TRUE(find_bar_widget);
  const gfx::Rect find_bar_bounds_in_screen =
      find_bar_widget->GetWindowBoundsInScreen();
  EXPECT_TRUE(active_viewport_in_screen.Contains(find_bar_bounds_in_screen));

  const ui::Compositor* const compositor = find_bar_widget->GetCompositor();
  ASSERT_TRUE(compositor);
  const float device_scale_factor = compositor->device_scale_factor();
  EXPECT_FLOAT_EQ(1.25f, device_scale_factor);
  const gfx::Size frame_pixel_size = gfx::ScaleToRoundedSize(
      find_bar_bounds_in_screen.size(), device_scale_factor);
  const gfx::Size frame_dip_size = gfx::ToFlooredSize(
      gfx::ConvertSizeToDips(frame_pixel_size, device_scale_factor));
  EXPECT_EQ(find_bar_bounds_in_screen.size(), frame_dip_size)
      << "A lossy frame size leaves a newly shown macOS Widget transparent: "
      << "active viewport=" << active_viewport_in_screen.ToString()
      << ", find bar=" << find_bar_bounds_in_screen.ToString()
      << ", frame pixels=" << frame_pixel_size.ToString();
}
#endif  // BUILDFLAG(IS_MAC)

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       YeeStatusBubblesStayInsideOwningSplitPane) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const browser_widget = browser_view->GetWidget();
  ASSERT_TRUE(browser_widget);
  gfx::Rect window_bounds = browser_widget->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(800, 600));
  browser_widget->SetBounds(window_bounds);
  ApplyScheduledLayout();

  RunTestSequence(EnterSplitView());

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  ASSERT_TRUE(multi_contents_view->IsInSplitView());

  for (ContentsWebView* contents :
       {multi_contents_view->GetActiveContentsView(),
        multi_contents_view->GetInactiveContentsView()}) {
    ASSERT_TRUE(contents);
    const gfx::Rect contents_bounds = contents->GetBoundsInScreen();
    ASSERT_FALSE(contents_bounds.IsEmpty());

    StatusBubbleViews* const bubble = contents->GetStatusBubble();
    ASSERT_TRUE(bubble);
    bubble->Reposition();
    bubble->SetStatus(u"test");
    views::Widget* const popup = bubble->GetPopupForTesting();
    ASSERT_TRUE(popup);

    auto expect_contained = [&](const char* phase) {
      const gfx::Rect popup_bounds = popup->GetWindowBoundsInScreen();
      EXPECT_TRUE(contents_bounds.Contains(popup_bounds))
          << phase << ": contents=" << contents_bounds.ToString()
          << ", popup=" << popup_bounds.ToString();
    };

    expect_contained("standard");
    const int resting_x = popup->GetWindowBoundsInScreen().x();
    bubble->MouseMovedAtForTesting(
        popup->GetWindowBoundsInScreen().CenterPoint(), false);
    expect_contained("mouse avoidance");
    EXPECT_NE(resting_x, popup->GetWindowBoundsInScreen().x());

    bubble->MouseMovedAtForTesting(gfx::Point(), true);
    expect_contained("mouse exit");

    bubble->SetBubbleWidth(contents_bounds.width());
    expect_contained("expanded");
    bubble->Hide();
  }
}

class YeeBrowserViewTabbedLayoutImplAiOverlayUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest {
 public:
  YeeBrowserViewTabbedLayoutImplAiOverlayUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(
            /*disable_animations=*/true,
            /*enable_ai_overlay_dialog=*/true) {}
};

class YeeBrowserViewTabbedLayoutImplOptionalViewportChildrenUiTest
    : public YeeBrowserViewTabbedLayoutImplUiTest {
 public:
  YeeBrowserViewTabbedLayoutImplOptionalViewportChildrenUiTest()
      : YeeBrowserViewTabbedLayoutImplUiTest(
            /*disable_animations=*/true,
            /*enable_ai_overlay_dialog=*/false,
            /*enable_vertical_tabs=*/true,
            /*enable_indigo_overlay=*/true) {}
};

IN_PROC_BROWSER_TEST_F(
    YeeBrowserViewTabbedLayoutImplOptionalViewportChildrenUiTest,
    YeeOptionalTargetChildrenShareTargetBoundsAndClip) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  ContentsContainerView* const container =
      browser_view->multi_contents_view()->GetActiveContentsContainerView();
  ASSERT_TRUE(container);
  ASSERT_TRUE(container->indigo_overlay_view());
  ASSERT_TRUE(container->read_anything_immersive_overlay_view());
  ASSERT_TRUE(container->actor_overlay_web_view());

  views::View* const clip_host =
      container->GetViewByID(yee::kPageViewportClipHostViewId);
  ASSERT_TRUE(clip_host);
  views::View& target_host = yee::GetPageTargetHost(*clip_host);
  std::vector<views::View*> target_children = {
      container->contents_view(), container->data_protection_overlay_view(),
      container->indigo_overlay_view(),
      container->read_anything_immersive_overlay_view(),
      container->actor_overlay_web_view()};
  ASSERT_EQ(target_children.size(), target_host.children().size());
  for (views::View* child : target_children) {
    EXPECT_EQ(&target_host, child->parent());
    EXPECT_EQ(target_host.GetLocalBounds(), child->bounds());
  }

  container->SetTargetContentBounds(gfx::Outsets::TLBR(0, 0, 0, 13));
  container->DeprecatedLayoutImmediately();
  const yee::PageViewportGeometry geometry =
      container->GetPageViewportGeometry();
  ASSERT_FALSE(geometry.current_clip_in_target.IsEmpty());
  for (views::View* child : target_children) {
    EXPECT_EQ(target_host.GetLocalBounds(), child->bounds());
    if (child->layer()) {
      EXPECT_EQ(geometry.current_clip_in_target, child->layer()->clip_rect());
    }
  }
  ExpectWebViewNativeClip(container->contents_view(),
                          geometry.current_clip_in_target);
  ExpectWebViewNativeClip(container->actor_overlay_web_view(),
                          geometry.current_clip_in_target);

  container->SetTargetContentBounds(std::nullopt);
  container->DeprecatedLayoutImmediately();
}

#if BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeWindowsNativeClipSurvivesRendererAttachment) {
  auto* const browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  views::WebView* const web_view = browser_view->multi_contents_view()
                                       ->GetActiveContentsContainerView()
                                       ->devtools_web_view();
  ASSERT_FALSE(web_view->web_contents());
  ASSERT_FALSE(web_view->holder()->native_view());
  const gfx::Rect requested_clip(2, 3, 20, 30);
  EXPECT_TRUE(web_view->holder()->SetNativeViewClipRect(requested_clip));
  EXPECT_TRUE(web_view->holder()->GetNativeViewClipRect().IsEmpty());

  auto contents = content::WebContents::Create(
      content::WebContents::CreateParams(browser_view->GetProfile()));
  web_view->SetWebContents(contents.get());
  ASSERT_TRUE(web_view->holder()->native_view());
  EXPECT_EQ(requested_clip, web_view->holder()->GetNativeViewClipRect());
  web_view->SetWebContents(nullptr);
  EXPECT_FALSE(web_view->holder()->native_view());
  EXPECT_TRUE(web_view->holder()->GetNativeViewClipRect().IsEmpty());
}
#endif

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplAiOverlayUiTest,
                       YeeAiOverlayStaysInsideOwningSplitPane) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const browser_widget = browser_view->GetWidget();
  ASSERT_TRUE(browser_widget);
  gfx::Rect window_bounds = browser_widget->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(800, 600));
  browser_widget->SetBounds(window_bounds);
  ApplyScheduledLayout();

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  ASSERT_TRUE(multi_contents_view->IsInSplitView());

  ASSERT_EQ(2U, multi_contents_view->contents_container_views().size());
  ContentsContainerView* const first_container =
      multi_contents_view->contents_container_views()[0];
  ContentsContainerView* const second_container =
      multi_contents_view->contents_container_views()[1];
  ASSERT_TRUE(first_container);
  ASSERT_TRUE(second_container);
  const auto matching_ai_overlays =
      views::ElementTrackerViews::GetInstance()->GetAllMatchingViews(
          kAiOverlayDialogWebViewElementId,
          views::ElementTrackerViews::GetContextForView(browser_view));
  EXPECT_THAT(matching_ai_overlays,
              testing::UnorderedElementsAre(
                  first_container->ai_overlay_dialog_view(),
                  second_container->ai_overlay_dialog_view()));
  first_container->ai_overlay_dialog_view()->SetVisible(false);
  second_container->ai_overlay_dialog_view()->SetVisible(false);
  multi_contents_view->SetActiveIndex(1);
  ApplyScheduledLayout();

  ttc::AiOverlayDialogController* const controller =
      ttc::AiOverlayDialogController::From(browser());
  ASSERT_TRUE(controller);
  controller->ShowOverlay();
  EXPECT_FALSE(first_container->ai_overlay_dialog_view()->GetVisible());
  EXPECT_TRUE(second_container->ai_overlay_dialog_view()->GetVisible());
  controller->HideOverlay();
  EXPECT_FALSE(second_container->ai_overlay_dialog_view()->GetVisible());
  multi_contents_view->SetActiveIndex(0);
  controller->ShowOverlay();
  EXPECT_TRUE(first_container->ai_overlay_dialog_view()->GetVisible());
  EXPECT_FALSE(second_container->ai_overlay_dialog_view()->GetVisible());
  controller->HideOverlay();
  EXPECT_FALSE(first_container->ai_overlay_dialog_view()->GetVisible());

  constexpr gfx::Size kOversizedPreferredSize(800, 600);
  for (ContentsContainerView* container :
       multi_contents_view->contents_container_views()) {
    ASSERT_TRUE(container);
    const DevToolsContentsResizingStrategy devtools_strategy(
        devtools::DockSide::kRight,
        gfx::Rect(0, 0, container->width() / 2, container->height()));
    container->SetContentsResizingStrategy(devtools_strategy);
    container->devtools_web_view()->SetVisible(true);
    views::WebView* const overlay = container->ai_overlay_dialog_view();
    ASSERT_TRUE(overlay);
    views::View* const clip_host =
        container->GetViewByID(yee::kPageViewportClipHostViewId);
    ASSERT_TRUE(clip_host);
    views::View& overlay_host = yee::GetViewportOverlayHost(*clip_host);
    EXPECT_EQ(&overlay_host, overlay->parent());
    EXPECT_EQ((yee::PageViewportChildPolicy{
                  yee::PageViewportChildHost::kViewportOverlayHost,
                  yee::PageViewportMigrationStatus::kApproved}),
              yee::GetPageViewportChildPolicy(
                  yee::PageViewportChildFamily::kAiOverlayDialog));
    overlay->SetPreferredSize(kOversizedPreferredSize);
    overlay->SetVisible(true);
  }

  auto expect_contained = [&](split_tabs::SplitTabLayout layout) {
    multi_contents_view->UpdateSplitVisualData(
        split_tabs::SplitTabVisualData(layout));
    ApplyScheduledLayout();

    for (ContentsContainerView* container :
         multi_contents_view->contents_container_views()) {
      views::WebView* const overlay = container->ai_overlay_dialog_view();
      ASSERT_TRUE(overlay);
      const gfx::Rect contents_bounds =
          container->contents_view()->GetBoundsInScreen();
      const gfx::Rect overlay_bounds = overlay->GetBoundsInScreen();
      views::View* const clip_host =
          container->GetViewByID(yee::kPageViewportClipHostViewId);
      ASSERT_TRUE(clip_host);
      views::View& overlay_host = yee::GetViewportOverlayHost(*clip_host);
      const gfx::Rect expected_local_clip =
          yee::ResolveDirectChildClipInLocalSpace(
              overlay->bounds(), overlay_host.GetLocalBounds());
      if (overlay->layer()) {
        EXPECT_EQ(expected_local_clip, overlay->layer()->clip_rect());
      }
      EXPECT_EQ(expected_local_clip,
                overlay->holder()->GetNativeViewClipRect());
      ASSERT_FALSE(contents_bounds.IsEmpty());
      ASSERT_FALSE(overlay_bounds.IsEmpty());
      EXPECT_TRUE(contents_bounds.Contains(overlay_bounds))
          << "layout=" << static_cast<int>(layout)
          << ", contents=" << contents_bounds.ToString()
          << ", overlay=" << overlay_bounds.ToString();
      EXPECT_EQ(gfx::Size(std::min(kOversizedPreferredSize.width(),
                                   contents_bounds.width()),
                          std::min(kOversizedPreferredSize.height(),
                                   contents_bounds.height())),
                overlay_bounds.size());
    }
  };

  expect_contained(split_tabs::SplitTabLayout::kSideBySide);
  expect_contained(split_tabs::SplitTabLayout::kStacked);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       YeeTabModalDialogHostStaysInsideOwningSplitPane) {
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);

  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  views::Widget* const browser_widget = browser_view->GetWidget();
  ASSERT_TRUE(browser_widget);
  gfx::Rect window_bounds = browser_widget->GetWindowBoundsInScreen();
  window_bounds.set_size(gfx::Size(800, 600));
  browser_widget->SetBounds(window_bounds);
  ApplyScheduledLayout();

  MultiContentsView* const multi_contents_view =
      browser_view->multi_contents_view();
  ASSERT_TRUE(multi_contents_view);
  ASSERT_FALSE(multi_contents_view->IsInSplitView());

  ContentsContainerView* const single_container =
      multi_contents_view->GetActiveContentsContainerView();
  ASSERT_TRUE(single_container);
  TabModalDialogHost* const single_host =
      single_container->web_contents_modal_dialog_host();
  ASSERT_TRUE(single_host);
  const gfx::Rect single_container_bounds =
      single_container->ConvertRectToWidget(single_container->GetLocalBounds());
  const gfx::Rect toolbar_bounds = browser_view->toolbar()->ConvertRectToWidget(
      browser_view->toolbar()->GetLocalBounds());
  constexpr gfx::Size kSingleDialogSize(240, 120);
  const int expected_single_y = toolbar_bounds.bottom() - 3;
  const int expected_single_middle_x =
      single_container_bounds.x() + single_container->width() / 2;
  const int expected_single_starting_x =
      std::max(expected_single_middle_x - kSingleDialogSize.width() / 2, 0);
  EXPECT_EQ(
      gfx::Point(std::min(expected_single_starting_x,
                          browser_view->width() - kSingleDialogSize.width()),
                 expected_single_y),
      single_host->GetDialogPosition(kSingleDialogSize));
  const gfx::Rect browser_contents_bounds =
      browser_view->contents_container()->ConvertRectToWidget(
          browser_view->contents_container()->GetLocalBounds());
  EXPECT_EQ(gfx::Size(browser_view->contents_container()->width(),
                      browser_contents_bounds.bottom() - expected_single_y),
            single_host->GetMaximumDialogSize());

  chrome::NewSplitTab(browser(), split_tabs::SplitTabLayout::kSideBySide,
                      split_tabs::SplitTabCreatedSource::kToolbarButton);
  ApplyScheduledLayout();
  ASSERT_TRUE(multi_contents_view->IsInSplitView());

  auto expect_contained = [&](split_tabs::SplitTabLayout layout) {
    multi_contents_view->UpdateSplitVisualData(
        split_tabs::SplitTabVisualData(layout));
    ApplyScheduledLayout();

    for (ContentsContainerView* container :
         multi_contents_view->contents_container_views()) {
      ASSERT_TRUE(container);
      TabModalDialogHost* const host =
          container->web_contents_modal_dialog_host();
      ASSERT_TRUE(host);
      const gfx::Rect card_bounds =
          container->ConvertRectToWidget(container->GetLocalBounds());
      const gfx::Rect page_bounds = container->ConvertRectToWidget(
          container->GetPageViewportGeometry().current_viewport_in_container);
      ASSERT_FALSE(card_bounds.IsEmpty());
      ASSERT_TRUE(card_bounds.Contains(page_bounds));

      const gfx::Size maximum_size = host->GetMaximumDialogSize();
      EXPECT_EQ(card_bounds.size(), maximum_size);
      const gfx::Rect maximum_dialog(host->GetDialogPosition(maximum_size),
                                     maximum_size);
      EXPECT_TRUE(card_bounds.Contains(maximum_dialog))
          << "layout=" << static_cast<int>(layout)
          << ", card=" << card_bounds.ToString()
          << ", maximum dialog=" << maximum_dialog.ToString();

      const gfx::Size small_size(std::min(240, maximum_size.width()),
                                 std::min(120, maximum_size.height()));
      const gfx::Rect small_dialog(host->GetDialogPosition(small_size),
                                   small_size);
      EXPECT_TRUE(card_bounds.Contains(small_dialog))
          << "layout=" << static_cast<int>(layout)
          << ", card=" << card_bounds.ToString()
          << ", small dialog=" << small_dialog.ToString();
      EXPECT_LE(std::abs(card_bounds.CenterPoint().x() -
                         small_dialog.CenterPoint().x()),
                1);
      EXPECT_EQ(std::max(card_bounds.y(), page_bounds.y() - 3),
                small_dialog.y());
    }
  };

  expect_contained(split_tabs::SplitTabLayout::kSideBySide);
  expect_contained(split_tabs::SplitTabLayout::kStacked);
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       VerticalTabsSinglePaneExpand) {
  if (!features::UseSidePanelFlyoverAnimation()) {
    GTEST_SKIP();
  }
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  RunTestSequence(ToggleVerticalTabStripCollapsed(true), Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  ClearResizeCounts(), ToggleVerticalTabStripCollapsed(false),
                  Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  CheckResizeCounts(1U, 0U));
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       VerticalTabsSplitViewCollapse) {
  if (!features::UseSidePanelFlyoverAnimation()) {
    GTEST_SKIP();
  }
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  RunTestSequence(EnterSplitView(), Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  ClearResizeCounts(), ToggleVerticalTabStripCollapsed(true),
                  Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  CheckResizeCounts(1U, 1U));
}

IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplContentLayoutUiTest,
                       VerticalTabsSplitViewExpand) {
  if (!features::UseSidePanelFlyoverAnimation()) {
    GTEST_SKIP();
  }
  tabs::VerticalTabStripStateController::From(browser())
      ->SetVerticalTabsEnabled(true);
  ApplyScheduledLayout();
  RunTestSequence(EnterSplitView(),
                  Do([this] { ExpectOneVisibleVerticalTabStripToggle(); }),
                  ToggleVerticalTabStripCollapsed(true), Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  ClearResizeCounts(), ToggleVerticalTabStripCollapsed(false),
                  Do([this] {
                    ApplyScheduledLayout();
                    ExpectOneVisibleVerticalTabStripToggle();
                  }),
                  CheckResizeCounts(1U, 1U));
}

// 실제 전환 중간에도 그룹 이름과 알약의 가시성·폭을 유지한다.
IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplTabAnimationUiTest,
                       YeeHorizontalGroupTransitionsKeepHeaderClear) {
  auto* model = browser()->tab_strip_model();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(orientation);
  orientation->SetVerticalTabsEnabled(false);
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-tabbar-capture-dir");
  // 캡처 대기가 짧은 모션을 건너뛰지 않도록 촬영 때만 같은 곡선을 느리게 본다.
  const bool slow_capture = !captures.empty();
  gfx::ScopedAnimationDurationScaleMode duration(
      slow_capture ? gfx::ScopedAnimationDurationScaleMode::SLOW_DURATION
                   : gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto reduced_motion =
      gfx::AnimationTestApi::SetPrefersReducedMotionForTesting(false);
  ASSERT_TRUE(gfx::Animation::ShouldRenderRichAnimation());
  ASSERT_FALSE(gfx::Animation::PrefersReducedMotion());
  // 선행 일반 탭이 있는 경우에도 연속 첫 멤버 닫기를 확인한다.
  for (int first_member : {0, 1}) {
    SCOPED_TRACE(testing::Message() << "first member=" << first_member);
    // 독립 시나리오마다 이전 프레임·animation 관측을 해제한다.
    applied_.Stop();
    const auto capture = [&](const char* phase, int frame) {
      if (!captures.empty()) {
        ASSERT_TRUE(base::CreateDirectory(captures));
        CaptureYeeWindowFrame(
            view->GetWidget(),
            captures.AppendASCII(base::StringPrintf(
                "motion-first%d-%s-%02d.png", first_member, phase, frame)));
      }
    };
    while (model->count() < first_member + 4) {
      chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
    }
    ASSERT_EQ(first_member + 4, model->count());
    const auto group = model->AddToNewGroup(
        {first_member, first_member + 1, first_member + 2});
    model->ChangeTabGroupVisuals(
        group, tab_groups::TabGroupVisualData(
                   u"Group motion", tab_groups::TabGroupColorId::kBlue, false));
    model->ActivateTabAt(model->count() - 1);
    auto tick = [&] { ASSERT_TRUE(PresentedFrame(*view->GetWidget())); };
    ApplyScheduledLayout();
    auto header = [&] {
      return views::ElementTrackerViews::GetInstance()->GetFirstMatchingView(
          kTabGroupHeaderElementId,
          views::ElementTrackerViews::GetContextForView(view), true);
    };
    auto marker = [&]() -> views::View* {
      auto* heading = header();
      if (!heading) {
        return nullptr;
      }
      if (auto* legacy =
              views::AsViewClass<TabContainerImpl>(heading->parent())) {
        return legacy->GetGroupViews(group)->underline();
      }
      auto* unified = views::AsViewClass<TabGroupView>(heading->parent());
      return unified ? unified->group_line() : nullptr;
    };
    const auto painted_capsule_bounds = [&]() {
      auto* const capsule = marker();
      if (!capsule) {
        return gfx::Rect();
      }
      gfx::Canvas canvas(capsule->size(), 1.f, false);
      yee::PaintHorizontalTabGroupCapsule(canvas, *capsule, SK_ColorBLUE);
      const SkBitmap bitmap = canvas.GetBitmap();
      gfx::Rect painted;
      for (int y = 0; y < capsule->height(); ++y) {
        if (SkColorGetA(bitmap.getColor(capsule->width() / 2, y)) > 0) {
          painted.Union(gfx::Rect(0, y, capsule->width(), 1));
        }
      }
      return painted;
    };
    const auto layout_settled = [&] {
      ApplyScheduledLayout();
      auto* heading = header();
      if (!heading) {
        return false;
      }
      if (auto* legacy =
              views::AsViewClass<TabContainerImpl>(heading->parent())) {
        return !legacy->IsAnimating();
      }
      // 그룹은 상위 unpinned 폭을 따라 다시 목표를 잡을 수 있다.
      // 두 소유자가 모두 멈춘 뒤 기준 폭과 최종 폭을 비교한다.
      for (auto* parent = heading->parent(); parent;
           parent = parent->parent()) {
        if ((views::IsViewClass<TabGroupView>(parent) ||
             views::IsViewClass<UnpinnedTabContainerView>(parent)) &&
            static_cast<TabCollectionAnimatingLayoutManager*>(
                parent->GetLayoutManager())
                ->is_animating()) {
          return false;
        }
      }
      return true;
    };
    // 새 그룹의 생성·배치 전환이 끝난 상태에서 닫기 자체를 검사한다.
    ASSERT_TRUE(Await(GroupAnimations(header())));
    ASSERT_TRUE((layout_settled)());
    const auto group_name = [&]() -> views::Label* {
      views::Label* label = nullptr;
      const auto find = [&](auto&& self, views::View* current) -> void {
        if (auto* candidate = views::AsViewClass<views::Label>(current);
            candidate && candidate->GetText() == u"Group motion") {
          label = candidate;
        }
        for (views::View* child : current->children()) {
          self(self, child);
        }
      };
      find(find, header());
      return label;
    };
    ASSERT_TRUE(group_name());
    const auto check_icons = [&](views::View* closing_view) {
      int checked_icons = 0;
      const auto check_icon = [&](auto&& self, views::View* current) -> void {
        if (auto* icon = views::AsViewClass<TabIcon>(current);
            icon && icon->IsDrawn()) {
          ++checked_icons;
          // TabIcon의 attention-ring 여백을 제외한 실제 favicon paint 범위다.
          gfx::Rect favicon_bounds = icon->GetContentsBounds();
          views::View::ConvertRectToScreen(icon, &favicon_bounds);
          EXPECT_FALSE(
              favicon_bounds.Intersects(group_name()->GetBoundsInScreen()))
              << "icon=" << favicon_bounds.ToString()
              << ", name=" << group_name()->GetBoundsInScreen().ToString()
              << ", header=" << header()->GetBoundsInScreen().ToString()
              << ", closing=" << closing_view->GetBoundsInScreen().ToString()
              << ", local closing=" << closing_view->bounds().ToString()
              << ", local header=" << header()->bounds().ToString()
              << ", kind=" << closing_view->GetClassName()
              << ", first=" << first_member << ", count=" << model->count();
        }
        for (views::View* child : current->children()) {
          self(self, child);
        }
      };
      check_icon(check_icon, closing_view);
      return checked_icons;
    };
    views::ViewTracker closing(GetTabAnchorForModelIndex(view, first_member));
    views::ViewTracker next_closing(
        GetTabAnchorForModelIndex(view, first_member + 1));
    check_icons(closing.view());
    check_icons(next_closing.view());
    capture("before-close", 0);
    yee::test::AppliedConditions close_conditions = GroupAnimations(header());
    close_conditions.push_back(
        applied_.View(*closing.view(), {.destroyed = true},
                      yee::test::AppliedMatch::kViewDestroyed));
    close_conditions.push_back(
        applied_.View(*next_closing.view(), {.destroyed = true},
                      yee::test::AppliedMatch::kViewDestroyed));
    const auto close_ticket = applied_.Begin(close_conditions);
    auto motion = GroupAnimations(header(), true).front();
    auto* legacy = views::AsViewClass<TabContainerImpl>(header()->parent());
    if (legacy) {
      // 닫히는 탭 자체의 실제 진행값을 읽는다.
      motion = applied_.Condition(
          applied_.source().ObserveTabContainerAnimation(*legacy, 10,
                                                         closing.view()),
          yee::test::AnimationMatches{.group_id = 10, .running = true});
    }
    auto& motion_range = std::get<yee::test::AnimationMatches>(motion.expected);
    motion_range.progress_min = .1;
    motion_range.progress_max = .9;
    motion.freshness = yee::test::AppliedFreshness::kNewObservation;
    motion.mode = yee::test::AppliedEvaluationMode::kObservedSinceDispatch;
    auto current_motion = motion;
    current_motion.freshness =
        yee::test::AppliedFreshness::kCurrentAfterDispatch;
    current_motion.mode = yee::test::AppliedEvaluationMode::kCurrent;
    const yee::test::AppliedConditions close_mid{motion, current_motion};
    const auto mid_ticket =
        applied_.Begin(close_mid, yee::test::AppliedDomain::kAnimation);
    scoped_refptr<gfx::AnimationContainer> close_clock;
    base::TimeDelta close_frame_delta;
    ASSERT_FALSE(applied_.session().Dispatch(
        mid_ticket, base::BindLambdaForTesting([&] {
          ASSERT_FALSE(applied_.session().Dispatch(
              close_ticket, base::BindLambdaForTesting([&] {
                model->CloseWebContentsAt(
                    first_member, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                model->CloseWebContentsAt(
                    first_member, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                if (legacy) {
                  auto& animator = const_cast<views::BoundsAnimator&>(
                      legacy->GetBoundsAnimatorForTesting());
                  ASSERT_TRUE(animator.IsAnimating(closing.view()));
                  ASSERT_TRUE(animator.IsAnimating(next_closing.view()));
                  ASSERT_GT(animator.GetAnimationDuration(), base::TimeDelta());
                  close_clock = animator.container();
                  close_frame_delta = animator.GetAnimationDuration() *
                                      gfx::ScopedAnimationDurationScaleMode::
                                          duration_multiplier() /
                                      16;
                } else {
                  // 닫기 목표 layout을 적용해 그룹의 native 모션을 시작한다.
                  ApplyScheduledLayout();
                  auto* manager =
                      static_cast<TabCollectionAnimatingLayoutManager*>(
                          header()->parent()->GetLayoutManager());
                  ASSERT_TRUE(manager->is_animating());
                  close_clock = manager->animation_coordinator().container();
                  // 기본 coordinator는 200 ms EASE_IN_OUT이다. 시간 1/4의
                  // 실제 published progress는 0.125로 중간 범위 안에 있다.
                  close_frame_delta = base::Milliseconds(200) *
                                      gfx::ScopedAnimationDurationScaleMode::
                                          duration_multiplier() /
                                      16;
                }
                ASSERT_TRUE(close_clock);
                // headless compositor의 첫 tick이 200 ms 뒤에 올 수 있다.
                // 두 native 경로 모두 중간 상태를 만들고 검사 동안 멈춘다.
                // bounds callback이 이 UI turn의 running 표본을 전달한다.
                gfx::AnimationContainerTestApi(close_clock.get())
                    .IncrementTime(close_frame_delta * 4);
                if (!legacy) {
                  // unified tick은 layout을 무효화한다. 실제 sibling bounds도
                  // 같은 진행값으로 적용한 뒤 중간 기하를 검사한다.
                  ApplyScheduledLayout();
                }
              })));
        })));
    const auto mid_result =
        applied_.session().WaitForApplied(mid_ticket, close_mid);
    ASSERT_EQ(yee::test::AppliedResultKind::kApplied, mid_result.kind)
        << mid_result.ToString();
    ASSERT_TRUE(mid_result.diagnostics.conditions[0].observed);
    EXPECT_TRUE(std::get<yee::test::AnimationMatches>(
                    mid_result.diagnostics.conditions[0].observed->value)
                    .running);
    // 결과 전달 task 전에도 tick이 올 수 있다. 실제 검사 직전 현재 진행값을
    // 다시 읽고, sibling bounds 갱신이 끝난 같은 UI turn에서 검사한다.
    const auto current_sample = applied_.source().Read(
        {motion.owner, yee::test::AppliedConditionKind::kAnimationMatches});
    ASSERT_TRUE(current_sample);
    const auto& current_animation =
        std::get<yee::test::AnimationMatches>(current_sample->value);
    ASSERT_TRUE(current_animation.running);
    ASSERT_GT(current_animation.progress_min, 0);
    ASSERT_LT(current_animation.progress_max, 1);
    ASSERT_GE(current_animation.progress_min, .1);
    ASSERT_LE(current_animation.progress_max, .9);
    int close_running_samples = 0;
    for (views::View* closing_view : {closing.view(), next_closing.view()}) {
      ASSERT_TRUE(closing_view);
      ASSERT_TRUE(closing_view->IsDrawn());
      close_running_samples += check_icons(closing_view);
    }
    for (int i = 0; i < 16; ++i) {
      if (close_clock) {
        // 고정 대기가 아닌 fixture의 native animation 시계 자극이다.
        gfx::AnimationContainerTestApi(close_clock.get())
            .IncrementTime(close_frame_delta);
      }
      tick();
      for (views::View* closing_view : {closing.view(), next_closing.view()}) {
        if (!closing_view) {
          continue;
        }
        check_icons(closing_view);
      }
      if (i == 0 || i == 3 || i == 7 || i == 15) {
        capture("closing", i);
      }
    }
    ASSERT_EQ(first_member + 2, model->count());
    ASSERT_TRUE(applied_.Wait(close_ticket, close_conditions));
    EXPECT_GT(close_running_samples, 0);
    ASSERT_TRUE(([&] { return !closing.view() && !next_closing.view(); })());
    ASSERT_TRUE((layout_settled)());
    if (!legacy) {
      ASSERT_FALSE(close_clock->is_running());
    }
    ASSERT_TRUE(marker());
    const int expanded_width = marker()->width();
    for (bool collapsed : {true, false}) {
      SCOPED_TRACE(testing::Message() << "collapsed=" << collapsed);
      std::set<int> sampled_widths;
      std::set<int> sampled_heights;
      std::string frame_samples;
      const int start_width = marker()->width();
      const auto transition_start = base::TimeTicks::Now();
      const auto capsule = applied_.views().ObserveCapsule(*marker(), 50);
      int running_samples = 0;
      const auto decoration = applied_.views().ObserveView(*marker());
      std::vector<yee::test::AppliedOwnerToken> member_icons;
      const auto observe_icons = [&](auto&& self,
                                     views::View* current) -> void {
        if (views::IsViewClass<TabIcon>(current))
          member_icons.push_back(applied_.views().ObserveView(*current));
        for (views::View* child : current->children())
          self(self, child);
      };
      observe_icons(observe_icons,
                    GetTabAnchorForModelIndex(view, first_member));
      std::vector<yee::test::AppliedSignalKey> progress_keys{
          {capsule, yee::test::AppliedConditionKind::kAnimationMatches},
          {decoration, yee::test::AppliedConditionKind::kViewStateMatches}};
      for (auto icon : member_icons)
        progress_keys.push_back(
            {icon, yee::test::AppliedConditionKind::kViewStateMatches});
      auto progress = applied_.views().SubscribeCompositorFrames(
          *view->GetWidget(), progress_keys,
          base::BindLambdaForTesting([&](const yee::test::AppliedSignalSample&
                                             sample) {
            // 지연된 과거 animation만 담긴 표본은 geometry와 합치지 않는다.
            const auto find = [&](yee::test::AppliedOwnerToken owner,
                                  yee::test::AppliedConditionKind kind)
                -> const yee::test::AppliedSnapshot* {
              for (const auto& snapshot : sample.snapshots)
                if (snapshot.owner == owner &&
                    yee::test::GetAppliedConditionKind(snapshot.value) == kind)
                  return &snapshot;
              return nullptr;
            };
            const auto* motion = find(
                capsule, yee::test::AppliedConditionKind::kAnimationMatches);
            const auto* geometry = find(
                decoration, yee::test::AppliedConditionKind::kViewStateMatches);
            if (!motion || !geometry ||
                motion->sample_id != geometry->sample_id)
              return;
            const auto& animation =
                std::get<yee::test::AnimationMatches>(motion->value);
            if (!animation.running || animation.progress_min <= 0 ||
                animation.progress_max >= 1)
              return;
            const auto& decoration_sample =
                std::get<yee::test::ViewStateMatches>(geometry->value);
            ++running_samples;
            EXPECT_TRUE(decoration_sample.visible);
            sampled_widths.insert(decoration_sample.bounds.width());
            sampled_heights.insert(decoration_sample.painted_bounds.height());
            EXPECT_LE(
                std::abs(decoration_sample.bounds.height() / 2 -
                         decoration_sample.painted_bounds.CenterPoint().y()),
                1);
            if (collapsed) {
              for (auto icon : member_icons) {
                const auto* state = find(
                    icon, yee::test::AppliedConditionKind::kViewStateMatches);
                ASSERT_TRUE(state);
                EXPECT_EQ(motion->sample_id, state->sample_id);
                EXPECT_FALSE(
                    std::get<yee::test::ViewStateMatches>(state->value).drawn);
              }
            }
          }));
      yee::test::AppliedConditions capsule_idle = GroupAnimations(header());
      capsule_idle.push_back(applied_.Condition(
          capsule,
          yee::test::AnimationMatches{.group_id = 50, .running = false},
          yee::test::AppliedMatch::kExact,
          yee::test::AppliedFreshness::kNewApply));
      // 닫기와 마찬가지로 최종 layout과 중간 animation 요청의 domain을 나눈다.
      const auto capsule_ticket = applied_.Begin(capsule_idle);
      auto height_motion =
          applied_.Condition(capsule,
                             yee::test::AnimationMatches{.group_id = 50,
                                                         .running = true,
                                                         .progress_min = .1,
                                                         .progress_max = .9},
                             yee::test::AppliedMatch::kExact,
                             yee::test::AppliedFreshness::kNewObservation);
      height_motion.mode =
          yee::test::AppliedEvaluationMode::kObservedSinceDispatch;
      auto current_height_motion = height_motion;
      current_height_motion.freshness =
          yee::test::AppliedFreshness::kCurrentAfterDispatch;
      current_height_motion.mode = yee::test::AppliedEvaluationMode::kCurrent;
      yee::test::AppliedConditions capsule_mid{height_motion,
                                               current_height_motion};
      if (!legacy) {
        auto width_motion = GroupAnimations(header(), true).front();
        auto& range =
            std::get<yee::test::AnimationMatches>(width_motion.expected);
        range.progress_min = .1;
        range.progress_max = .9;
        width_motion.freshness = yee::test::AppliedFreshness::kNewObservation;
        width_motion.mode =
            yee::test::AppliedEvaluationMode::kObservedSinceDispatch;
        auto current_width_motion = width_motion;
        current_width_motion.freshness =
            yee::test::AppliedFreshness::kCurrentAfterDispatch;
        current_width_motion.mode = yee::test::AppliedEvaluationMode::kCurrent;
        capsule_mid.push_back(width_motion);
        capsule_mid.push_back(current_width_motion);
      }
      const auto capsule_mid_ticket =
          applied_.Begin(capsule_mid, yee::test::AppliedDomain::kAnimation);
      scoped_refptr<gfx::AnimationContainer> capsule_clock;
      base::TimeDelta capsule_frame_delta;
      const auto change_capsule = [&] {
        ASSERT_FALSE(applied_.session().Dispatch(
            capsule_ticket, base::BindLambdaForTesting([&] {
              model->ChangeTabGroupVisuals(
                  group, tab_groups::TabGroupVisualData(
                             u"Group motion",
                             tab_groups::TabGroupColorId::kBlue, collapsed));
              ASSERT_TRUE(marker());
              auto* state =
                  yee::HorizontalTabGroupCapsuleState::From(*marker());
              ASSERT_TRUE(state);
              ASSERT_TRUE(state->GetSnapshot().running);
              const auto height_duration =
                  state->GetAnimationDurationForTesting();
              ASSERT_GT(height_duration, base::TimeDelta());
              capsule_clock = state->GetAnimationContainerForTesting();
              ASSERT_TRUE(capsule_clock);
              capsule_frame_delta =
                  height_duration *
                  gfx::ScopedAnimationDurationScaleMode::duration_multiplier() /
                  16;
              // 높이는 폭과 별도 시계를 쓴다. 첫 compositor 프레임 전에
              // native 중간 높이를 만들고 표본 검사 동안 유지한다.
              gfx::AnimationContainerTestApi(capsule_clock.get())
                  .IncrementTime(capsule_frame_delta * 4);
              if (!legacy) {
                ApplyScheduledLayout();
                auto* manager =
                    static_cast<TabCollectionAnimatingLayoutManager*>(
                        header()->parent()->GetLayoutManager());
                ASSERT_TRUE(manager->is_animating());
                ASSERT_EQ(close_clock.get(),
                          manager->animation_coordinator().container());
                // 닫기와 같은 native 시계로 폭의 중간 진행을 고정한다.
                // 첫 presented frame이 200 ms 뒤여도 이 상태를 건너뛰지 않는다.
                gfx::AnimationContainerTestApi(close_clock.get())
                    .IncrementTime(close_frame_delta * 4);
                ApplyScheduledLayout();
              }
            })));
      };
      ASSERT_FALSE(applied_.session().Dispatch(
          capsule_mid_ticket, base::BindLambdaForTesting(change_capsule)));
      if (collapsed &&
          views::IsViewClass<TabContainerImpl>(header()->parent())) {
        // 자식 layout을 전파하지 않는 classic 부모의 첫 tick 전 상태도
        // 확인한다.
        const auto check_immediate_icon = [&](auto&& self,
                                              views::View* current) -> void {
          if (auto* icon = views::AsViewClass<TabIcon>(current)) {
            EXPECT_FALSE(icon->GetVisible());
          }
          for (views::View* child : current->children()) {
            self(self, child);
          }
        };
        check_immediate_icon(check_immediate_icon,
                             GetTabAnchorForModelIndex(view, first_member));
      }
      ASSERT_TRUE(applied_.Wait(capsule_mid_ticket, capsule_mid));
      ASSERT_TRUE(marker());
      sampled_heights.insert(painted_capsule_bounds().height());
      if (!legacy) {
        sampled_widths.insert(marker()->width());
      }
      for (int i = 0; i < 20; ++i) {
        if (i > 0) {
          gfx::AnimationContainerTestApi(capsule_clock.get())
              .IncrementTime(capsule_frame_delta);
        }
        if (!legacy && i > 0) {
          // fixture 시계 자극이다. 실제 native layout을 적용한 뒤 paint를 본다.
          gfx::AnimationContainerTestApi(close_clock.get())
              .IncrementTime(close_frame_delta);
          ApplyScheduledLayout();
        }
        tick();
        ASSERT_TRUE(marker());
        EXPECT_TRUE(marker()->GetVisible());
        sampled_widths.insert(marker()->width());
        const gfx::Rect painted = painted_capsule_bounds();
        sampled_heights.insert(painted.height());
        frame_samples += base::StringPrintf(
            "frame=%d elapsed_ms=%.1f width=%d height=%d rich=%d reduced=%d\n",
            i, (base::TimeTicks::Now() - transition_start).InMillisecondsF(),
            marker()->width(), painted.height(),
            gfx::Animation::ShouldRenderRichAnimation(),
            gfx::Animation::PrefersReducedMotion());
        EXPECT_LE(std::abs(marker()->height() / 2 - painted.CenterPoint().y()),
                  1);
        if (collapsed) {
          auto* member = GetTabAnchorForModelIndex(view, first_member);
          const auto check_icon = [&](auto&& self,
                                      views::View* current) -> void {
            if (auto* icon = views::AsViewClass<TabIcon>(current)) {
              EXPECT_FALSE(icon->IsDrawn());
            }
            for (views::View* child : current->children()) {
              self(self, child);
            }
          };
          check_icon(check_icon, member);
        }
        if (i == 0 || i == 3 || i == 7 || i == 19) {
          capture(collapsed ? "collapsing" : "expanding", i);
        }
      }
      if (!legacy) {
        // 부모 폭 적용이 그룹 목표를 다시 잡으면 마지막 표본에도 모션이 남는다.
        // TestApi는 runner를 복구하지 않으므로 현재 native 주기를 끝까지
        // 자극하고 layout을 적용한 뒤 최종 applied 조건을 기다린다.
        gfx::AnimationContainerTestApi(close_clock.get())
            .IncrementTime(close_frame_delta * 16);
        ApplyScheduledLayout();
      }
      // TestApi는 runner를 복구하지 않는다. 기존 표본의 native tick으로
      // 높이를 끝내 container를 비우면 다음 Start가 실시간으로 돌아간다.
      ASSERT_FALSE(capsule_clock->is_running());
      ASSERT_TRUE(applied_.Wait(capsule_ticket, capsule_idle));
      ASSERT_TRUE((layout_settled)());
      if (!legacy) {
        // 빈 container의 다음 Start가 실제 runner를 다시 시작한다.
        ASSERT_FALSE(close_clock->is_running());
      }
      EXPECT_GT(running_samples, 0);
      progress = {};
      const int final_height =
          collapsed
              ? yee::kSidebarMetrics.horizontal_group_collapsed_capsule_height
              : yee::kSidebarMetrics.horizontal_group_capsule_height;
      EXPECT_EQ(final_height, painted_capsule_bounds().height());
      EXPECT_TRUE(std::ranges::any_of(
          sampled_heights,
          [&](int height) {
            return height > yee::kSidebarMetrics
                                .horizontal_group_collapsed_capsule_height &&
                   height <
                       yee::kSidebarMetrics.horizontal_group_capsule_height;
          }))
          << "알약이 접힘·펼침 사이의 실제 중간 높이를 거치지 않음\n"
          << frame_samples;
      const int end_width = marker()->width();
      EXPECT_TRUE(std::ranges::any_of(
          sampled_widths,
          [&](int width) {
            return width > std::min(start_width, end_width) &&
                   width < std::max(start_width, end_width);
          }))
          << "알약이 양 끝 사이의 실제 중간 폭을 거치지 않음\n"
          << frame_samples;
      if (collapsed) {
        EXPECT_LT(marker()->width(), expanded_width);
        // 접힌 모델은 native 테마를 거쳐도 작은 장식 높이로 복귀한다.
        ui::MockOsSettingsProvider os_settings;
        os_settings.SetPreferredContrast(
            ui::NativeTheme::PreferredContrast::kMore);
        ApplyScheduledLayout();
        EXPECT_FALSE(yee::UsesHorizontalTabGroupCapsule(*marker()));
        os_settings.SetPreferredContrast(
            ui::NativeTheme::PreferredContrast::kNoPreference);
        ApplyScheduledLayout();
        ASSERT_TRUE(yee::UsesHorizontalTabGroupCapsule(*marker()));
        EXPECT_EQ(final_height, painted_capsule_bounds().height());
      } else {
        EXPECT_EQ(marker()->width(), expanded_width);
      }
    }
    // 실제 중간 progress를 관측한 뒤 현재도 running인 같은 UI turn에서
    // 되돌린다.
    const auto capsule = applied_.views().ObserveCapsule(*marker(), 50);
    auto middle =
        applied_.Condition(capsule,
                           yee::test::AnimationMatches{.group_id = 50,
                                                       .running = true,
                                                       .progress_min = .1,
                                                       .progress_max = .9},
                           yee::test::AppliedMatch::kExact,
                           yee::test::AppliedFreshness::kNewObservation);
    middle.mode = yee::test::AppliedEvaluationMode::kObservedSinceDispatch;
    middle.observation_group = 1;
    const auto current_running = applied_.Condition(
        capsule, yee::test::AnimationMatches{.group_id = 50, .running = true});
    ASSERT_TRUE(applied_.Run(
        {middle, current_running},
        [&] {
          model->ChangeTabGroupVisuals(
              group,
              tab_groups::TabGroupVisualData(
                  u"Group motion", tab_groups::TabGroupColorId::kBlue, true));
        },
        yee::test::AppliedDomain::kAnimation));
    auto reversed = GroupAnimations(header());
    reversed.push_back(applied_.Condition(
        capsule, yee::test::AnimationMatches{.group_id = 50, .running = false},
        yee::test::AppliedMatch::kExact,
        yee::test::AppliedFreshness::kNewApply));
    ASSERT_TRUE(applied_.Run(
        reversed,
        [&] {
          const auto current = applied_.source().Read(
              {capsule, yee::test::AppliedConditionKind::kAnimationMatches});
          ASSERT_TRUE(current);
          ASSERT_TRUE(
              std::get<yee::test::AnimationMatches>(current->value).running);
          const int turning_height = painted_capsule_bounds().height();
          model->ChangeTabGroupVisuals(
              group,
              tab_groups::TabGroupVisualData(
                  u"Group motion", tab_groups::TabGroupColorId::kBlue, false));
          EXPECT_EQ(turning_height, painted_capsule_bounds().height());
        },
        yee::test::AppliedDomain::kAnimation));
    EXPECT_EQ(yee::kSidebarMetrics.horizontal_group_capsule_height,
              painted_capsule_bounds().height());
    // 마지막 멤버의 헤더 삭제도 close 전에 등록한다.
    const auto last_header =
        applied_.View(*header(), {.destroyed = true},
                      yee::test::AppliedMatch::kViewDestroyed);
    ASSERT_TRUE(applied_.Run({last_header}, [&] {
      model->CloseWebContentsAt(first_member,
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    }));
    EXPECT_EQ(first_member + 1, model->count());
    EXPECT_FALSE(header());
  }
}

// 세로 맞춤설정도 수평 배치와 같은 Header 아래 행을 사용한다.
IN_PROC_BROWSER_TEST_F(YeeBrowserViewTabbedLayoutImplUiTest,
                       YeeCustomizationPanelStaysBelowWindowHeader) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL(chrome::kChromeUINewTabURL)));
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* impl = static_cast<BrowserViewTabbedLayoutImpl*>(
      view->GetBrowserViewLayoutForTesting());
  auto* orientation = tabs::VerticalTabStripStateController::From(browser());
  auto* panel = SidePanelUI::From(browser());
  const auto captures =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "yee-tabbar-capture-dir");
  ASSERT_TRUE(orientation);
  ASSERT_TRUE(panel);
  for (bool vertical : {false, true}) {
    orientation->SetVerticalTabsEnabled(vertical);
    panel->Show(SidePanelEntryId::kCustomizeChrome, std::nullopt, true);
    ApplyScheduledLayout();
    ASSERT_TRUE(panel->IsSidePanelShowing());
    ASSERT_EQ(SidePanelEntryId::kCustomizeChrome, panel->GetCurrentEntryId());
    auto* const web_view =
        views::ElementTrackerViews::GetInstance()
            ->GetUniqueViewAs<views::WebView>(
                kCustomizeChromeSidePanelWebViewElementId,
                views::ElementTrackerViews::GetContextForView(view));
    ASSERT_TRUE(web_view);
    auto* const contents = web_view->GetWebContents();
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    const auto customization_dom = ObserveDom(
        *contents, yee::test::WebUiAppliedSelectors::Probe::kCustomization);
    ASSERT_TRUE(Await(
        {DomCondition(customization_dom, {.customization_ready = true},
                      yee::test::AppliedMatch::kWebUiCustomizationReady)}));
    ASSERT_TRUE(([&] {
      return content::EvalJs(contents, R"JS(
        (() => {
          const app = document.querySelector('customize-chrome-app')?.shadowRoot;
          const layout = app?.querySelector('yee-tab-layout')?.shadowRoot
            ?.querySelector('#layout:not(:disabled)');
          const shortcuts = app?.querySelector('customize-chrome-shortcuts')
            ?.shadowRoot;
          const toggle = shortcuts?.querySelector('#showToggle');
          const collapse = shortcuts?.querySelector('cr-collapse');
          const radio = shortcuts?.querySelector('#radioSelection');
          return Boolean(layout && toggle && collapse && radio?.selected &&
            shortcuts.querySelector('#radioSelection cr-radio-button') &&
            collapse.classList.contains(toggle.checked ?
              'collapse-opened' : 'collapse-closed'));
        })()
      )JS")
          .ExtractBool();
    })());
    content::SimulateEndOfPaintHoldingOnPrimaryMainFrame(contents);
    content::WaitForCopyableViewInFrame(contents->GetPrimaryMainFrame());
    ApplyScheduledLayout();
    ASSERT_TRUE(impl->last_native_layout_for_testing());
    const auto& result = *impl->last_native_layout_for_testing();
    EXPECT_EQ(browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel,
              result.horizontal_plan.row_mode);
    EXPECT_GE(result.side_panel.current_bounds.y(),
              result.header_bounds.bottom());
    EXPECT_GT(result.header_bounds.width(),
              result.side_panel.current_bounds.width());
    EXPECT_EQ(result.header_bounds.x(),
              result.combined_surface_outline_bounds.x());
    EXPECT_EQ(result.header_bounds.width(),
              result.combined_surface_outline_bounds.width());
    if (!captures.empty()) {
      CaptureYeeWindowFrame(
          view->GetWidget(),
          captures.AppendASCII(vertical ? "customize-sidebar.png"
                                        : "customize-horizontal.png"));
    }
    // 다른 패널로 바꾸면 Sidebar의 기존 카드 장식으로 복귀한다.
    // 같은 폭의 entry 교체에는 새 page geometry apply가 없을 수 있다.
    // 실제 page geometry와 entry/animation 상태를 함께 확인한다.
    auto reading_conditions = PageConditions();
    reading_conditions.push_back(applied_.Condition(
        applied_.source().ObserveUiState(
            *view,
            yee::test::AppliedBrowserObservers::UiProbe::kSidePanelEntry),
        yee::test::PresentationMatches{
            .presentation_id =
                static_cast<uint64_t>(SidePanelEntryId::kReadingList) + 1},
        yee::test::AppliedMatch::kPresentationRevision));
    reading_conditions.push_back(applied_.Condition(
        applied_.source().ObserveBrowserAnimation(
            *view, *BrowserAnimationController::From(browser()),
            SidePanelAnimations::kSidePanel, SidePanelAnimations::kPanelWidth,
            81),
        yee::test::AnimationMatches{.group_id = 81, .running = false}));
    ASSERT_TRUE(applied_.Run(
        reading_conditions,
        [&] {
          panel->Show(SidePanelEntryId::kReadingList, std::nullopt, true);
        },
        yee::test::AppliedDomain::kLayout, TestTimeouts::action_timeout()));
    ASSERT_TRUE(([&] {
      ApplyScheduledLayout();
      return panel->GetCurrentEntryId() == SidePanelEntryId::kReadingList;
    })());
    ASSERT_TRUE(impl->last_native_layout_for_testing());
    EXPECT_EQ(vertical
                  ? browser_view_layout::NativeSidePanelRowMode::kSharedRow
                  : browser_view_layout::NativeSidePanelRowMode::kTopAbovePanel,
              impl->last_native_layout_for_testing()->horizontal_plan.row_mode);
    panel->Close();
    ApplyScheduledLayout();
  }
}

}  // namespace
