#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "fast" "fast|horizontal|interactive|browser|all" "$@"

if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi

FAST_TARGETS=(
  'chrome/browser/ui/views/tabs/common:yee_layout_unittests'
  'chrome/browser/ui/views/yee:multi_contents_geometry_unittests'
)
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    FAST_TARGETS+=(chrome/browser/ui/views/yee:yee_aura_unittests)
    ;;
esac
NATIVE_FAST_FILTER='BrowserAnimationControllerTest.*:YeeBrowserViewTabbedLayoutNativeGeometryTest.*:YeeSurfaceGeometryTest.*:YeeSurfaceColorTest.HorizontalTabsFollowThemeAndKeepReadableStates:YeeSurfaceColorTest.HorizontalGroupsAndMembersShareNativeThemeFallback'
NATIVE_FAST_FILTER+=':PageViewportAppliedStateTest.*:FavoritesPreviewAppliedStateTest.*:HorizontalTabGroupCapsuleStateTest.*:YeeBrowserAnimationControllerTest.*-BrowserAnimationControllerTest.Reset_MidMotion_SameMotion'
YEE_FAST_FILTER='AppliedWaitTest.*:BrowserSurfaceTransitionTest.*:MultiContentsGeometryTest.*:PageViewportGeometryTest.*:PageViewportMigrationTest.*'
# 원본 TopContainerBackground는 Yee 셸에서 실패하므로 필수 목록에서 빼고,
# 같은 시나리오를 Yee 셸 기준으로 고친 Yee 복사본만 선택한다.
INTERACTIVE_FILTER='YeeBrowserViewTabbedLayoutImplFeatureOffUiTest.NativeSidePanelRowModesMatchAppliedLayout:YeeBrowserViewTabbedLayoutImplNativeAnimationUiTest.MidAnimationTypeAndWidthSwitchUseFreshPlans:YeeBrowserViewTabbedLayoutImplUiTest.YeeNativePlannerResultMatchesAppliedLayout:YeeBrowserViewTabbedLayoutImplUiTest.YeeFullscreenPreservesSurfaceWithoutStaleChromeRows:YeeBrowserViewTabbedLayoutImplUiTest.YeeSplitTargetLayoutUsesHandedInInsets:YeeBrowserViewTabbedLayoutImplUiTest.YeePageViewportGeometryMatchesAppliedDevToolsEdges:YeeBrowserViewTabbedLayoutImplUiTest.YeePageRemainsPhysicallyClickableWithSidePanelOpen:YeeBrowserViewTabbedLayoutImplUiTest.YeePageHostsPreserveSplitFocusAndContainerReuse:YeeBrowserViewTabbedLayoutImplUiTest.YeeInfoBarFollowsExactSplitPaneIdentity:YeeBrowserViewTabbedLayoutImplUiTest.YeeSurfaceDecorationAndZOrderUseResolvedFrame:YeeBrowserViewTabbedLayoutImplUiTest.YeeInfoBarPresentationRoundsOnlyTheTopVisibleBar:YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal:YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth:YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.YeeStatusBubblesStayInsideOwningSplitPane:YeeBrowserViewTabbedLayoutImplAiOverlayUiTest.YeeAiOverlayStaysInsideOwningSplitPane:YeeBrowserViewTabbedLayoutImplOptionalViewportChildrenUiTest.YeeOptionalTargetChildrenShareTargetBoundsAndClip:YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.YeeTabModalDialogHostStaysInsideOwningSplitPane:YeeOmniboxPopupViewWebUITest.HiddenWidgetClearsClassicPopupState:All/YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest.TopContainerBackground/*'
INTERACTIVE_FILTER+=':YeeBrowserViewTabbedLayoutImplFeatureOffUiTest.YeeShellWithoutVerticalTabsUsesNativeToolbar:YeeBrowserViewTabbedLayoutImplUiTest.TestMinimumWindowSizeWhenSidePanelIsOpen'
FLYOUT_FILTER='YeeBrowserViewTabbedLayoutImplUiTest.YeeSidebarFlyoutKeepsExpandedControlsAndPageBounds:YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.YeeSidebarFlyoutPinAndReverseKeepContinuousPresentation'
HORIZONTAL_FILTER='YeeBrowserViewTabbedLayoutImplUiTest.YeeHorizontalTabsPreserveRowsColorsAndTabIdentity:YeeBrowserViewTabbedLayoutImplTabAnimationUiTest.YeeHorizontalGroupTransitionsKeepHeaderClear:YeeBrowserViewTabbedLayoutImplUiTest.YeeCustomizationPanelStaysBelowWindowHeader'
HORIZONTAL_GROUP_UX_FILTER='TabDragging/YeeDetachToBrowserTabDragControllerTest.DragSingleTabLeftIntoGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragSingleTabRightIntoGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragTabWithinGroupDoesNotModifyGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.YeeGroupDragKeepsSingleCapsuleAtEveryPosition/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragOnlyTabInGroupRetainsGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragSingleTabRightOfRightmostGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.RevertDragSingleTabIntoGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.RevertTabDragOutOfGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragGroupHeaderDragsGroup/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragTabLeftPastCollapsedGroupHeader/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragTabRightPastCollapsedGroupHeader/0:TabDragging/YeeDetachToBrowserTabDragControllerTest.DragCollapsedGroupHeaderRemainsCollapsed/0'
VERTICAL_GROUP_UX_FILTER='YeeVerticalTabDragTest.YeeProjectHiddenGroupRowsKeepFavoriteDropAnchor:YeeVerticalTabDragTest.YeeProjectHiddenSplitGroupRowsKeepFavoriteDropAnchor:YeeVerticalTabDragTest.YeeSidebarTopLevelTabCanEnterExpandedGroupMiddle:YeeVerticalTabDragTest.DragWithinGroup:YeeVerticalTabDragTest.DragOutOfGroup:YeeVerticalTabDragTest.YeeSidebarGroupHeaderDragHasNoFavoriteAffordance:YeeVerticalTabDragTest.DragGroupHeader:YeeVerticalTabDragTest.DragCollapsedGroupStaysCollapsed:YeeVerticalTabDragTest.DragFavoriteToExpandedGroupFirstSlot:YeeVerticalTabDragTest.DragFavoriteToExpandedGroupLastSlot:YeeVerticalTabDragTest.CancelFavoriteToGroupDragRestoresSource'
VERTICAL_GROUP_UX_FILTER+=':YeeVerticalTabDragTest.YeeSidebarSingleFavoriteDragReordersWithinDock'
POPUP_UX_FILTER='YeeOmniboxPopupViewWebUITest.YeeAimPopupOpensAndDismissesAfterClosedThemeChanges'
POPUP_UX_FILTER+=':Shadow/YeeOmniboxPopupAimShadowGeometryTest.YeeAimShadowFitsWidgetAndKeepsHostTransparent/*'
# OS 전용 사례는 실제로 컴파일되는 플랫폼에서만 필수 필터에 포함한다.
case "$(uname -s)" in
  Darwin)
    HORIZONTAL_FILTER+=':YeeBrowserViewTabbedLayoutImplTabAnimationUiTest.YeeMacHorizontalFullscreenExitRestoresToolbarWithoutFocus'
    INTERACTIVE_FILTER+=':YeeBrowserViewTabbedLayoutImplTabAnimationUiTest.YeeMacNativeResizeKeepsWindowedChromeGeometry'
    INTERACTIVE_FILTER+=':YeeBrowserViewTabbedLayoutImplRtlFractionalDsfUiTest.YeeFindBarUsesPixelStableBoundsAtMinimumWidth'
    INTERACTIVE_FILTER+=':YeeOmniboxPopupViewWebUITest.MultiWindowActivationRestartsAutocompleteWithoutStaleState'
    INTERACTIVE_FILTER+=':YeeImmersiveModeControllerMacInteractiveTest.ToggleFullscreenWithVerticalTabstrip:YeeImmersiveModeControllerMacInteractiveTest.ContentFullscreenChildren:YeeImmersiveModeControllerMacInteractiveTest.HandlesOverlayWidgetDestruction'
    INTERACTIVE_FILTER+=':YeeBrowserNativeWidgetMacGlassTest.GlassViewGeometryMatchesShellPolicy:YeeBrowserNativeWidgetMacGlassTest.FullscreenWindowIsOpaque:YeeBrowserNativeWidgetMacGlassTest.ActiveInactiveTintOpacity:YeeBrowserNativeWidgetMacGlassTest.PaintAsActiveLockMaintainsActiveTint:YeeBrowserNativeWidgetMacGlassTest.TabFullscreenWindowIsOpaque:YeeBrowserNativeWidgetMacGlassTest.MultipleProfilesTintOpacity'
    ;;
  MINGW*|MSYS*|CYGWIN*)
    POPUP_UX_FILTER+=':YeeCustomizeChromeInteractiveTest.YeeTabLayoutSelectPopupFollowsItsHost'
    HORIZONTAL_GROUP_UX_FILTER+=':TabDragging/YeeDetachToBrowserTabDragControllerTest.YeeGroupMembershipDragSurvivesLayoutSwitch/0'
    INTERACTIVE_FILTER+=':YeeBrowserViewTabbedLayoutImplUiTest.YeeWindowsNativeClipSurvivesRendererAttachment'
    HORIZONTAL_FILTER+=':YeeBrowserViewTabbedLayoutImplUiTest.YeeWindowsHorizontalTabsConnectPageSurfaceToHeader:YeeBrowserViewTabbedLayoutImplUiTest.YeeWindowsSplitThemeChangesKeepCaptionReservations:CaptionPresentation/YeeWindowsCaptionInputUiTest.HoverUsesVisibleControlBounds/*'
    HORIZONTAL_FILTER+=':YeeBrowserViewTabbedLayoutImplUiTest.YeeWindowsCaptionButtonsChangeWindowState:YeeBrowserViewTabbedLayoutImplUiTest.YeeWindowsSidebarGroupEditorAndFirstTabClose:YeeWindowsStartupUiTest.StartupAndHorizontalTabRoundTrip'
    ;;
esac
INTERACTIVE_FILTER+=":$HORIZONTAL_FILTER"
INTERACTIVE_FILTER+=":$HORIZONTAL_GROUP_UX_FILTER:$VERTICAL_GROUP_UX_FILTER:$POPUP_UX_FILTER"
INTERACTIVE_FILTER+=":$FLYOUT_FILTER"
# Keep transition and compositor regressions in the ordinary checkpoint gate,
# including the combined states that isolated open/close tests cannot cover.
TRANSITION_TESTS=(
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
  TRANSITION_FILTERS+=("YeeBrowserViewTabbedLayoutImplContentLayoutUiTest.$regression")
done
TRANSITION_FILTER="${(j.:.)TRANSITION_FILTERS}"
INTERACTIVE_FILTER+=":$TRANSITION_FILTER"
RENDERER_LIFECYCLE_FILTER='Flyover/YeeRendererResizeUiTest.YeeResizeCollapseReloadKeepsRendererLive/*'
INTERACTIVE_FILTER+=":$RENDERER_LIFECYCLE_FILTER"
PHYSICAL_RESIZE_FILTER='YeeBrowserViewTabbedLayoutImplUiTest.YeeSidePanelGutterPhysicallyResizesBesideWebContents'
INTERACTIVE_FILTER+=":$PHYSICAL_RESIZE_FILTER"
OWNED_BROWSER_FILTER='YeeSidePanelCoordinatorTest.ShowFromAnimationReparentsContentView:YeeSelectionOverlayBrowserTest.SelectionUsedFromController:YeeSelectionOverlayBrowserTest.SelectionStaysScopedThroughSplitLifecycle'
BROWSER_FILTER='LensOverlayControllerBrowserTest.OverlayClosesIfRendererExits:LensOverlayControllerSideBySideBrowserTest.BackgroundBlurLiveInitiallyInSplitTab:SadTabSplitViewBrowserTest.SadTabMovedToSecondarySplitView:ReadAnythingControllerBrowserTest.CloseTabWithIrmInSplitView_ClosesIrm:ReadAnythingControllerBrowserTest.FocusInactiveIrmInSplitView_ActivatesTab:ReadAnythingControllerBrowserTest.ShowImmersive_AfterUnresponsiveRenderer_DoesNotCrash:BrowserViewTest.CloseWidgetWithTabsNoCrash'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Browser Surface layout ${MODE} gate"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "fast" || "$MODE" == "all" ]] && \
    targets+=("${FAST_TARGETS[@]}")
  [[ "$MODE" == "horizontal" || "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/yee:yee_interactive_ui_tests)
  [[ "$MODE" == "browser" || "$MODE" == "all" ]] && \
    targets+=(browser_tests chrome/browser/ui/views/yee:site_controls_browsertests)
  build_regression_targets "Browser Surface layout ${MODE}" "${targets[@]}"
fi

if [[ "$MODE" == "fast" || "$MODE" == "all" ]]; then
  print "Running the small pure native/Yee Browser Surface layout gate."
  run_tiered_native_test_gate "$YEE_OUT_DIR/yee_layout_unittests" \
    --gtest_filter="$NATIVE_FAST_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
  run_tiered_native_test_gate "$YEE_OUT_DIR/multi_contents_geometry_unittests" \
    --gtest_filter="$YEE_FAST_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
      run_tiered_native_test_gate "$YEE_OUT_DIR/yee_aura_unittests" \
        --gtest_filter='YeeAuraWindowTest.*' \
        --test-launcher-jobs=1 --test-launcher-retry-limit=0
      ;;
  esac
fi

if [[ "$MODE" == "horizontal" || "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  default_filter="$INTERACTIVE_FILTER"
  rtl_filter="$TRANSITION_FILTER:$RENDERER_LIFECYCLE_FILTER:$PHYSICAL_RESIZE_FILTER:$FLYOUT_FILTER:$HORIZONTAL_FILTER:$HORIZONTAL_GROUP_UX_FILTER:$VERTICAL_GROUP_UX_FILTER:$POPUP_UX_FILTER"
  if [[ "$MODE" == "horizontal" ]]; then
    default_filter="$HORIZONTAL_FILTER:$HORIZONTAL_GROUP_UX_FILTER"
    rtl_filter="$HORIZONTAL_FILTER:$HORIZONTAL_GROUP_UX_FILTER"
  fi
  print "Running applied ${MODE} UI tests: tier=$YEE_TEST_TIER."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter="$default_filter" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000

  print "Running ${MODE} UI regressions in RTL at 125% scale."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter="$rtl_filter" \
    --force-ui-direction=rtl \
    --force-device-scale-factor=1.25 \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000

  print "Checking the same horizontal contract with Chromium's unified tab strip."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --enable-features=TabStripUnification \
    --gtest_filter="$HORIZONTAL_FILTER:YeeBrowserViewTabbedLayoutImplUiTest.OverflowArrowsUseShellHoverCorners" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi

if [[ "$MODE" == "browser" || "$MODE" == "all" ]]; then
  print "Running the explicit browser-level Side Panel and overlay lifecycle gate."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
    --gtest_filter="$OWNED_BROWSER_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/browser_tests" \
    --gtest_filter="$BROWSER_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi
