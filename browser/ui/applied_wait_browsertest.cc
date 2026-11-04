// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"

#include <algorithm>
#include <set>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/animation/browser_animation_provider.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"
#include "chrome/browser/ui/views/yee/browser_surface_transition.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/view.h"

namespace yee::test {
namespace {

class YeeAppliedWaitBrowserTest : public InProcessBrowserTest {
 protected:
  AppliedIntent Intent(const AppliedConditions& conditions,
                       AppliedDomain domain = AppliedDomain::kLayout) {
    AppliedIntent intent{.domain = domain};
    std::set<AppliedOwnerToken> owners;
    for (const auto& condition : conditions) {
      if (owners.insert(condition.owner).second) {
        intent.owner_tokens.push_back(condition.owner);
      }
    }
    return intent;
  }
  WaitOptions Options(base::TimeDelta budget = TestTimeouts::action_timeout()) {
    return {
        .deadline = base::TimeTicks::Now() + budget,
        .test_name =
            testing::UnitTest::GetInstance()->current_test_info()->name(),
        .tier = WaitOptions::Tier::kB,
        .backend = base::CommandLine::ForCurrentProcess()->HasSwitch("headless")
                       ? WaitOptions::Backend::kHeadless
                       : WaitOptions::Backend::kNative};
  }
  BrowserView* view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }
};

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       TabModelUsesNativeHandlesAndPinnedState) {
  AppliedBrowserObservers source;
  auto* model = browser()->tab_strip_model();
  const auto owner = source.ObserveTabModel(*model);
  auto expected = std::get<TabModelMatches>(
      source.Read({owner, AppliedConditionKind::kTabModelMatches})->value);
  ASSERT_EQ(1u, expected.tabs.size());
  EXPECT_EQ(
      static_cast<uint64_t>(model->GetActiveTab()->GetHandle().raw_value()),
      expected.active_tab);
  expected.tabs[0].pinned = true;
  const AppliedConditions conditions{{owner, expected}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  model->SetTabPinned(0, true);
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       BrowserCollectionReportsCreateAndCloseIdentities) {
  AppliedBrowserObservers source;
  const auto owner = source.ObserveBrowserSet();
  base::test::TestFuture<AppliedSignalSample> created;
  auto subscription = source.Subscribe(
      {{owner, AppliedConditionKind::kBrowserSetMatches}},
      base::BindLambdaForTesting([&](const AppliedSignalSample& sample) {
        const auto& value =
            std::get<BrowserSetMatches>(sample.snapshots[0].value);
        if (!created.IsReady() && !value.created.empty()) {
          created.SetValue(sample);
        }
      }));
  auto* other = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(created.Wait());
  auto value = std::get<BrowserSetMatches>(
      source.Read({owner, AppliedConditionKind::kBrowserSetMatches})->value);
  const auto closing = source.BrowserToken(*other);
  EXPECT_EQ(closing, value.created.back());
  std::erase_if(value.browsers,
                [&](const auto& item) { return item.browser == closing; });
  value.closed.push_back(closing);
  const AppliedConditions conditions{
      {owner, value, AppliedFreshness::kNewObservation}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  other->GetWindow()->Close();
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       FutureBrowserBindsByMovedTabHandle) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  auto* model = browser()->tab_strip_model();
  const auto handle =
      static_cast<uint64_t>(model->GetTabAtIndex(1)->GetHandle().raw_value());
  AppliedBrowserObservers source;
  const auto owner = source.ObserveBrowserSet();
  auto expected = std::get<BrowserSetMatches>(
      source.Read({owner, AppliedConditionKind::kBrowserSetMatches})->value);
  const auto new_browser = source.ExpectBrowserForTab(handle);
  for (auto& item : expected.browsers)
    std::erase(item.tab_handles, handle);
  expected.browsers.push_back({new_browser, {handle}});
  expected.created.push_back(new_browser);
  const AppliedConditions conditions{
      {owner, expected, AppliedFreshness::kNewObservation}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  chrome::MoveTabsToNewWindow(browser(), {1});
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       NewPaneBindsOnlyTheExpectedTab) {
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  auto* model = browser()->tab_strip_model();
  auto* panes = view()->multi_contents_view();
  auto* secondary = panes->contents_container_views()[1].get();
  auto* clip = secondary->GetViewByID(kPageViewportClipHostViewId);
  ASSERT_TRUE(clip);
  base::test::TestFuture<void> initial_apply;
  auto subscription = PageViewportAppliedState::From(*clip)->Subscribe(
      base::BindLambdaForTesting([&](const PageViewportAppliedSnapshot&) {
        if (!initial_apply.IsReady())
          initial_apply.SetValue();
      }));
  const auto initial_split =
      model->AddToNewSplit({0, 1}, split_tabs::SplitTabVisualData(),
                           split_tabs::SplitTabCreatedSource::kToolbarButton);
  ASSERT_TRUE(initial_apply.Wait());
  AppliedBrowserObservers source;
  const auto initial_owner = source.views().ObservePageHosts(*clip, 8, 1);
  auto expected = std::get<PageHostsApplied>(
      source.Read({initial_owner, AppliedConditionKind::kPageHostsApplied})
          ->value);
  const auto handle =
      static_cast<uint64_t>(model->GetTabAtIndex(2)->GetHandle().raw_value());
  expected.tab_handle = handle;
  model->RemoveSplit(initial_split);
  const auto owner = source.ExpectPageHosts(*browser(), *panes, 8, handle);
  EXPECT_FALSE(source.Read({owner, AppliedConditionKind::kPageHostsApplied}));
  const AppliedConditions conditions{
      {.owner = owner,
       .expected = expected,
       .coordinates = GetAppliedViewCoordinates(
           *clip, AppliedCoordinates::Space::kParent)}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        model->AddToNewSplit({0, 2}, split_tabs::SplitTabVisualData(),
                             split_tabs::SplitTabCreatedSource::kToolbarButton);
      })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_EQ(handle, std::get<PageHostsApplied>(
                        result.diagnostics.conditions[0].last->value)
                        .tab_handle);
  const auto layout_owner = source.ObservePaneLayout(*browser(), *panes);
  const auto layout =
      source.Read({layout_owner, AppliedConditionKind::kPaneLayoutMatches});
  ASSERT_TRUE(layout);
  EXPECT_EQ(panes->size(),
            std::get<PaneLayoutMatches>(layout->value).parent_extent);
  EXPECT_EQ(2u, std::get<PaneLayoutMatches>(layout->value).tab_handles.size());
  const auto model_owner = source.ObserveTabModel(*model);
  const auto model_value =
      source.Read({model_owner, AppliedConditionKind::kTabModelMatches});
  ASSERT_TRUE(model_value);
  const AppliedConditions combined{
      {.owner = owner,
       .expected = expected,
       .freshness = AppliedFreshness::kCurrentAfterDispatch,
       .coordinates = conditions[0].coordinates},
      {.owner = layout_owner,
       .expected = layout->value,
       .freshness = AppliedFreshness::kCurrentAfterDispatch,
       .coordinates = layout->coordinates},
      {model_owner, model_value->value,
       AppliedFreshness::kCurrentAfterDispatch}};
  auto intent = Intent(combined);
  intent.operation = AppliedIntent::Operation::kNoOp;
  auto options = Options();
  options.already_applied = WaitOptions::AlreadyApplied::kAllowAlreadyApplied;
  const auto unchanged = session.BeginRequest(intent, combined, options);
  ASSERT_FALSE(session.Dispatch(unchanged, base::DoNothing()));
  const auto no_op = session.WaitForApplied(unchanged, combined);
  EXPECT_EQ(AppliedResultKind::kApplied, no_op.kind) << no_op.ToString();
  const auto detached =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(
      detached, base::BindLambdaForTesting([&] {
        const auto old_split = *model->GetActiveTab()->GetSplit();
        model->RemoveSplit(old_split);
        model->RestoreSplit(old_split, {0, 1},
                            split_tabs::SplitTabVisualData());
      })));
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed,
            session.WaitForApplied(detached, conditions).kind);
  EXPECT_FALSE(source.IsOwnerAlive(owner));
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       SurfaceEpochRejectsStaleAndReverseDestination) {
  views::View host;
  BrowserSurfaceChromeState windowed;
  BrowserSurfaceChromeState fullscreen{
      .stable_state = SurfaceGeometryStableState::kFullscreenHiddenChrome};
  auto& epoch = InitializeSurfaceGeometryTransition(host, windowed);
  const auto stale = BeginSurfaceGeometryTransition(
      epoch, fullscreen, SurfaceGeometryPlatformPath::kMacNativeAsync);
  AppliedBrowserObservers source;
  const auto owner = source.ObserveSurfaceEpoch(host);
  const AppliedConditions conditions{
      {owner,
       SurfaceEpochCommitted{stale, SurfaceEpochCommitted::Phase::kStable,
                             stale,
                             SurfaceEpochCommitted::Destination::kFullscreen},
       AppliedFreshness::kCurrentAfterDispatch}};
  auto intent = Intent(conditions, AppliedDomain::kSurface);
  intent.request_epoch = stale;
  AppliedWaitSession session(source);
  const auto ticket = session.BeginRequest(intent, conditions,
                                           Options(base::Milliseconds(100)));
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        BeginSurfaceGeometryTransition(
            epoch, windowed, SurfaceGeometryPlatformPath::kMacNativeAsync);
        EXPECT_FALSE(CommitSurfaceGeometryTransition(epoch, fullscreen));
        EXPECT_TRUE(CommitSurfaceGeometryTransition(epoch, windowed));
        host.SetBounds(1, 1, 20, 20);
      })));
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session.WaitForApplied(ticket, conditions).kind);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kSurfaceEpochCommitted});
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(epoch.generation, snapshot->request_epoch);
  EXPECT_EQ(SurfaceEpochCommitted::Destination::kWindowed,
            std::get<SurfaceEpochCommitted>(snapshot->value).destination);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       WebUiShadowDomAckAndMutationShareDocument) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    document.body.innerHTML = '<div id="host"></div>';
    document.querySelector('#host').attachShadow({mode:'open'}).innerHTML =
      '<div id="popup" style="width:200px;height:80px"><div class="row" selected></div></div>';
  )JS"));
  AppliedBrowserObservers source;
  base::test::TestFuture<bool> ready;
  const auto owner = source.InstallWebUiObserver(
      *contents->GetPrimaryMainFrame(),
      {{"#host"}, "#popup", ".row", "[selected]"}, ready.GetCallback());
  ASSERT_TRUE(ready.Get());
  auto expected = std::get<WebUiStateMatches>(
      source.Read({owner, AppliedConditionKind::kWebUiStateMatches})->value);
  expected.popup_rows.push_back(2);
  expected.selected_index = 1;
  const AppliedConditions conditions{
      {.owner = owner,
       .expected = expected,
       .coordinates =
           source.Read({owner, AppliedConditionKind::kWebUiStateMatches})
               ->coordinates}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  content::ExecuteScriptAsync(contents, R"JS(
      const popup = document.querySelector('#host').shadowRoot.querySelector('#popup');
      popup.children[0].removeAttribute('selected');
      popup.insertAdjacentHTML('beforeend', '<div class="row" selected></div>');
    )JS");
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  const auto pending =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(
      pending, base::BindLambdaForTesting([&] {
        EXPECT_TRUE(content::NavigateToURL(contents, GURL("about:blank#next")));
        EXPECT_TRUE(content::NavigateToURL(
            contents, GURL("data:text/html,new-document")));
      })));
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed,
            session.WaitForApplied(pending, conditions).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       MissingIntermediateShadowHostRejectsInstallation) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    document.body.innerHTML = '<div id="host"></div>';
    document.querySelector('#host').attachShadow({mode:'open'}).innerHTML =
      '<div id="popup"></div>';
  )JS"));
  AppliedBrowserObservers source;
  for (const auto& hosts :
       {std::vector<std::string>{"#missing", "#nested"},
        std::vector<std::string>{"#host", "#missing", "#nested"},
        std::vector<std::string>{"#host", "#popup", "#nested"}}) {
    base::test::TestFuture<bool> ready;
    const auto owner = source.InstallWebUiObserver(
        *contents->GetPrimaryMainFrame(),
        {hosts, "#popup", ".row", "[selected]"}, ready.GetCallback());
    EXPECT_FALSE(ready.Get());
    EXPECT_FALSE(source.IsOwnerAlive(owner));
    EXPECT_FALSE(
        source.Read({owner, AppliedConditionKind::kWebUiStateMatches}));
  }
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       RendererMetadataAndDocumentLifetime) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      contents,
      GURL("data:text/html,<body style='background:red'>metadata</body>")));

  content::RenderFrameSubmissionObserver submission(contents);
  base::test::TestFuture<void> frame;
  submission.NotifyOnNextMetadataChange(frame.GetCallback());
  view()->GetWidget()->SetBounds(
      view()->GetWidget()->GetWindowBoundsInScreen() + gfx::Vector2d(1, 1));
  // viewport metadata가 없는 경우 크기 변경을 자극으로 사용한다.
  if (!submission.LastRenderFrameMetadata().local_surface_id) {
    auto bounds = view()->GetWidget()->GetWindowBoundsInScreen();
    bounds.set_width(bounds.width() + 1);
    view()->GetWidget()->SetBounds(bounds);
    ASSERT_TRUE(frame.Wait());
  }
  AppliedBrowserObservers source;
  const auto owner = source.ObserveRendererViewport(*contents);
  const auto snapshot =
      source.Read({owner, AppliedConditionKind::kRendererViewportMatches});
  ASSERT_TRUE(snapshot);
  EXPECT_FALSE(std::get<RendererViewportMatches>(snapshot->value)
                   .viewport_pixels.IsEmpty());
  const AppliedConditions conditions{
      {.owner = owner,
       .expected = snapshot->value,
       .freshness = AppliedFreshness::kCurrentAfterDispatch,
       .coordinates = snapshot->coordinates}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::DoNothing()));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session.WaitForApplied(ticket, conditions).kind);
  const auto next =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(
      session.Dispatch(next, base::BindLambdaForTesting([&] {
                         EXPECT_TRUE(content::NavigateToURL(
                             contents, GURL("data:text/html,replaced")));
                       })));
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed,
            session.WaitForApplied(next, conditions).kind);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       FutureRendererSurfaceBindsResizeAndReload) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(
      content::NavigateToURL(contents, GURL("data:text/html,viewport-before")));
  content::RenderFrameSubmissionObserver metadata(contents);
  auto initial = metadata.LastRenderFrameMetadata();
  if (!initial.local_surface_id) {
    base::test::TestFuture<void> ready;
    metadata.NotifyOnNextMetadataChange(ready.GetCallback());
    auto bounds = view()->GetWidget()->GetWindowBoundsInScreen();
    bounds.set_width(bounds.width() + 16);
    view()->GetWidget()->SetBounds(bounds);
    ASSERT_TRUE(ready.Wait());
    initial = metadata.LastRenderFrameMetadata();
  }
  AppliedBrowserObservers source;
  auto pixels = initial.viewport_size_in_pixels;
  pixels.set_width(pixels.width() +
                   static_cast<int>(32 * initial.device_scale_factor));
  const auto resized = source.ExpectRendererViewport(
      *contents, pixels, initial.device_scale_factor);
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({resized}), {resized}, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        auto bounds = view()->GetWidget()->GetWindowBoundsInScreen();
        bounds.set_width(bounds.width() + 32);
        view()->GetWidget()->SetBounds(bounds);
      })));
  const auto result = session.WaitForApplied(ticket, {resized});
  ASSERT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_EQ(AppliedCoordinates::Units::kPixel,
            result.diagnostics.conditions[0].last->coordinates.units);
  EXPECT_EQ(
      initial.device_scale_factor,
      result.diagnostics.conditions[0].last->coordinates.device_scale_factor);
  const auto reload = source.ExpectRendererViewport(
      *contents, pixels, initial.device_scale_factor, true);
  const auto next = session.BeginRequest(Intent({reload}), {reload}, Options());
  ASSERT_FALSE(
      session.Dispatch(next, base::BindLambdaForTesting([&] {
                         EXPECT_TRUE(content::NavigateToURL(
                             contents, GURL("data:text/html,viewport-after")));
                       })));
  const auto replaced = session.WaitForApplied(next, {reload});
  EXPECT_EQ(AppliedResultKind::kApplied, replaced.kind) << replaced.ToString();
  EXPECT_NE(std::get<RendererViewportMatches>(resized.expected).document,
            std::get<RendererViewportMatches>(reload.expected).document);
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       MembershipChangeWithoutTabMoveIsObserved) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, false);
  auto* model = browser()->tab_strip_model();
  model->AddToNewGroup({0, 1});
  AppliedBrowserObservers source;
  const auto owner = source.ObserveTabModel(*model);
  auto expected = std::get<TabModelMatches>(
      source.Read({owner, AppliedConditionKind::kTabModelMatches})->value);
  ASSERT_FALSE(expected.tabs[0].group.is_empty());
  expected.tabs[0].group = {};
  const AppliedConditions conditions{{owner, expected}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  model->RemoveFromGroup({0});
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationGroup, kAppliedGroup);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationMotion, kAppliedMotion);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationSequence,
                                     kAppliedSequence);
class AppliedAnimationProvider : public CachingBrowserAnimationProvider {
 public:
  DECLARE_SAFE_CAST_TARGET()
  AppliedAnimationProvider() {
    SetSequenceParams(kAppliedGroup, Default(kAppliedSequence, 0.0, false));
  }
  GroupInfos GenerateAnimations() const override {
    return {
        Group(kAppliedGroup,
              Motion(kAppliedMotion, TotalDurationMs(200), gfx::Tween::LINEAR,
                     Animate(kAppliedSequence, FromValue(0.0), ToValue(1.0))))};
  }
};
DEFINE_SAFE_CAST_TARGET(AppliedAnimationProvider)

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       BrowserAnimationStartAndResetUseOneObserver) {
  auto rich = gfx::AnimationTestApi::SetRichAnimationRenderMode(
      gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);
  gfx::ScopedAnimationDurationScaleMode duration(
      gfx::ScopedAnimationDurationScaleMode::NORMAL_DURATION);
  auto* controller = BrowserAnimationController::From(browser());
  ASSERT_TRUE(controller);
  auto* provider = controller->AddAnimationProvider(
      std::make_unique<AppliedAnimationProvider>());
  base::ScopedClosureRunner remove(base::BindLambdaForTesting(
      [&] { (void)controller->RemoveProviderForTesting(provider); }));
  AppliedBrowserObservers source;
  const auto owner = source.ObserveBrowserAnimation(
      *view(), *controller, kAppliedGroup, kAppliedSequence, 703);
  AppliedCondition condition{owner, AnimationMatches{703, true, 0, 1}};
  condition.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  condition.coordinates = GetAppliedViewCoordinates(*view());
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent({condition}), {condition}, Options());
  ASSERT_FALSE(session.Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        controller->Start(kAppliedGroup, kAppliedMotion, "YeeApplied", "Start");
        controller->Reset(kAppliedGroup, kAppliedMotion);
      })));
  const auto result = session.WaitForApplied(ticket, {condition});
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_TRUE(std::get<AnimationMatches>(
                  result.diagnostics.conditions[0].observed->value)
                  .running);
  EXPECT_FALSE(controller->IsAnimating(kAppliedGroup));
}

IN_PROC_BROWSER_TEST_F(YeeAppliedWaitBrowserTest,
                       PresentationSourcePublishesRealRevision) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* controller = BrowserSurfaceColorController::FromWebContents(contents);
  ASSERT_TRUE(controller);
  controller->SetPageSurfaceColorForTesting(SK_ColorRED);
  auto* binding = view()
                      ->multi_contents_view()
                      ->GetActiveContentsContainerView()
                      ->GetYeeBrowserSurfacePresentationBinding();
  ASSERT_TRUE(binding);
  AppliedBrowserObservers source;
  const auto owner = source.ObservePresentation(*view(), *binding);
  const auto previous =
      source.Read({owner, AppliedConditionKind::kPresentationMatches});
  ASSERT_TRUE(previous);
  auto expected = std::get<PresentationMatches>(previous->value);
  const auto revision = binding->GetPresentation()->revision;
  const auto palette = ResolveBrowserSurfacePresentation(
      SK_ColorBLUE, binding->source_id(), revision + 1,
      binding->GetPresentation()->popup_revision + 1);
  expected.presentation_id = palette.revision;
  expected.background_color = palette.surface;
  expected.foreground_color = palette.primary;
  const AppliedConditions conditions{{.owner = owner,
                                      .expected = expected,
                                      .coordinates = previous->coordinates}};
  AppliedWaitSession session(source);
  const auto ticket =
      session.BeginRequest(Intent(conditions), conditions, Options());
  ASSERT_FALSE(session.Dispatch(ticket, base::BindLambdaForTesting([&] {
                                  controller->SetPageSurfaceColorForTesting(
                                      SK_ColorBLUE);
                                })));
  const auto result = session.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(view()->GetYeeHeaderPresentation());
  EXPECT_EQ(palette.surface, view()->GetYeeHeaderPresentation()->surface);
}

}  // namespace
}  // namespace yee::test
