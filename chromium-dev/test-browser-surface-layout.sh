#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

MODE="${1:-fast}"
SKIP_BUILD=false
if [[ "$MODE" == "--no-build" ]]; then
  MODE="fast"
  SKIP_BUILD=true
elif [[ "${2:-}" == "--no-build" ]]; then
  SKIP_BUILD=true
fi

if [[ "$MODE" != "fast" && "$MODE" != "interactive" && \
      "$MODE" != "browser" && "$MODE" != "all" ]]; then
  print -u2 \
    "Usage: ./chromium-dev/test-browser-surface-layout.sh [fast|interactive|browser|all] [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src

FAST_TARGETS=(
  'chrome/browser/ui/views/tabs/common:yee_layout_unittests'
  'chrome/browser/ui/views/yee:multi_contents_geometry_unittests'
)
NATIVE_FAST_FILTER='BrowserViewTabbedLayoutNativeGeometryTest.*:YeeSurfaceGeometryTest.*'
YEE_FAST_FILTER='BrowserSurfaceTransitionTest.*:MultiContentsGeometryTest.*:PageViewportGeometryTest.*:PageViewportMigrationTest.*'
INTERACTIVE_FILTER='BrowserViewTabbedLayoutImplFeatureOffUiTest.NativeSidePanelRowModesMatchAppliedLayout:BrowserViewTabbedLayoutImplNativeAnimationUiTest.MidAnimationTypeAndWidthSwitchUseFreshPlans:BrowserViewTabbedLayoutImplUiTest.YeeNativePlannerResultMatchesAppliedLayout:BrowserViewTabbedLayoutImplUiTest.YeeFullscreenPreservesSurfaceWithoutStaleChromeRows:BrowserViewTabbedLayoutImplUiTest.YeeSplitTargetLayoutUsesHandedInInsets:BrowserViewTabbedLayoutImplUiTest.YeePageViewportGeometryMatchesAppliedDevToolsEdges:BrowserViewTabbedLayoutImplUiTest.YeePageRemainsPhysicallyClickableWithSidePanelOpen:BrowserViewTabbedLayoutImplUiTest.YeePageHostsPreserveSplitFocusAndContainerReuse:BrowserViewTabbedLayoutImplUiTest.YeeInfoBarFollowsExactSplitPaneIdentity:BrowserViewTabbedLayoutImplUiTest.YeeSurfaceDecorationAndZOrderUseResolvedFrame:BrowserViewTabbedLayoutImplUiTest.YeeInfoBarPresentationRoundsOnlyTheTopVisibleBar:BrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal:BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth:BrowserViewTabbedLayoutImplRtlFractionalDsfUiTest.YeeFindBarUsesPixelStableBoundsAtMinimumWidth:BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeStatusBubblesStayInsideOwningSplitPane:BrowserViewTabbedLayoutImplAiOverlayUiTest.YeeAiOverlayStaysInsideOwningSplitPane:BrowserViewTabbedLayoutImplOptionalViewportChildrenUiTest.YeeOptionalTargetChildrenShareTargetBoundsAndClip:BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeTabModalDialogHostStaysInsideOwningSplitPane:OmniboxPopupViewWebUITest.HiddenWidgetClearsClassicPopupState:OmniboxPopupViewWebUITest.MultiWindowActivationRestartsAutocompleteWithoutStaleState:*TopContainerBackground*'
# Keep transition and compositor regressions in the ordinary checkpoint gate,
# including the combined states that isolated open/close tests cannot cover.
TRANSITION_TESTS=(
  YeeHeaderSnapshotInvalidatesToolbarChildLayout
  YeeHardClipOwnersMatchAtDeviceScale
  VerticalTabsSinglePaneCollapse
  VerticalTabsSinglePaneExpand
  VerticalTabsSplitViewCollapse
  VerticalTabsSplitViewExpand
  YeeCombinedPanelSidebarTransitionsKeepViewportContained
  YeeSplitPanelDevToolsNoticeKeepOwningPane
)
TRANSITION_FILTERS=()
for regression in "${TRANSITION_TESTS[@]}"; do
  TRANSITION_FILTERS+=("BrowserViewTabbedLayoutImplContentLayoutUiTest.$regression")
done
TRANSITION_FILTER="${(j.:.)TRANSITION_FILTERS}"
INTERACTIVE_FILTER+=":$TRANSITION_FILTER"
RENDERER_LIFECYCLE_FILTER='Flyover/YeeRendererResizeUiTest.YeeResizeCollapseReloadKeepsRendererLive/*'
INTERACTIVE_FILTER+=":$RENDERER_LIFECYCLE_FILTER"
BROWSER_FILTER='SidePanelCoordinatorTest.ShowFromAnimationReparentsContentView:LensOverlayControllerBrowserTest.OverlayClosesIfRendererExits:LensOverlayControllerSideBySideBrowserTest.BackgroundBlurLiveInitiallyInSplitTab:SelectionOverlayBrowserTest.SelectionUsedFromController:SelectionOverlayBrowserTest.SelectionStaysScopedThroughSplitLifecycle:SadTabSplitViewBrowserTest.SadTabMovedToSecondarySplitView:ReadAnythingControllerBrowserTest.CloseTabWithIrmInSplitView_ClosesIrm:ReadAnythingControllerBrowserTest.FocusInactiveIrmInSplitView_ActivatesTab:ReadAnythingControllerBrowserTest.ShowImmersive_AfterUnresponsiveRenderer_DoesNotCrash:BrowserViewTest.CloseWidgetWithTabsNoCrash'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Browser Surface layout ${MODE} gate"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "fast" || "$MODE" == "all" ]] && \
    targets+=("${FAST_TARGETS[@]}")
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(interactive_ui_tests)
  [[ "$MODE" == "browser" || "$MODE" == "all" ]] && \
    targets+=(browser_tests)
  build_regression_targets "Browser Surface layout ${MODE}" "${targets[@]}"
fi

if [[ "$MODE" == "fast" || "$MODE" == "all" ]]; then
  print "Running the small pure native/Yee Browser Surface layout gate."
  "$YEE_OUT_DIR/yee_layout_unittests" \
    --gtest_filter="$NATIVE_FAST_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
  "$YEE_OUT_DIR/multi_contents_geometry_unittests" \
    --gtest_filter="$YEE_FAST_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Running focused applied BrowserView and Side Panel layout tests."
  gracefully_quit_yee
  "$YEE_OUT_DIR/interactive_ui_tests" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000

  print "Running transition and hard-clip regressions in RTL at 125% scale."
  gracefully_quit_yee
  "$YEE_OUT_DIR/interactive_ui_tests" \
    --gtest_filter="$TRANSITION_FILTER:$RENDERER_LIFECYCLE_FILTER" \
    --force-ui-direction=rtl \
    --force-device-scale-factor=1.25 \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi

if [[ "$MODE" == "browser" || "$MODE" == "all" ]]; then
  print "Running the explicit browser-level Side Panel and overlay lifecycle gate."
  gracefully_quit_yee
  "$YEE_OUT_DIR/browser_tests" \
    --gtest_filter="$BROWSER_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi
