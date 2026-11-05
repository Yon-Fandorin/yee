#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "unit" "unit|interactive|all" "$@"

if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi

if [[ "$PRINT_FILTERS" == false ]]; then
  "$SCRIPT_DIR/test-run-preflight.sh"
fi

UNIT_FILTER='Favorites*:*GroupMarkSignalsTest.*:SidebarFooter*:*SplitPaneControlsTest.*:YeeSurfaceColorTest.*:YeeSurfaceGeometryTest.*:YeeShellColorTest.*:YeeSidebarItemColorTest.*:YeeTabGroupHeaderViewTest.Yee*:All/YeeTabCollectionAnimatingLayoutManagerTest.RemoveChildDuringActiveAnimation/*:All/YeeTabCollectionAnimatingLayoutManagerTest.YeeDragOverlayPreservesOnlyVerticalSourceOpacity/*:All/YeeTabCollectionAnimatingLayoutManagerTest.YeeFavoriteClockRetargetDoesNotChangeSharedTabClock/*'
UNIT_FILTER+=':YeeTabGroupHeaderViewTest.EditorBubbleButtonVisibilityOnHover:YeeTabGroupHeaderViewTest.FocusModeMarkAndExpandedButtons'
# Header의 주소줄·팝업 검사는 test-header.sh가 소유한다. Sidebar 구조와
# 실제 drag·split 조작만 선택해 같은 창 검사를 두 번 실행하지 않는다.
INTERACTIVE_FILTER='YeeVerticalTabDragTest.*Favorite*:YeeVerticalTabDragTest.LoneTabCanPinAndUnpinInsideItsOwnSidebar:YeeVerticalTabDragTest.YeeSidebar*:YeeVerticalTabDragTest.DragWithinUnpinnedContainer:YeeVerticalTabDragTest.CancelDragWithinUnpinnedContainer:YeeVerticalTabDragTest.DragSplitTabs:YeeVerticalTabDragTest.DragOverSplit:YeeVerticalTabDragTest.DragOverSplitInGroup:YeeVerticalTabDragTest.DragMultipleTabs:YeeVerticalTabDragTest.DragMultipleTabsInGroup:YeeVerticalTabDragTest.DragWithinGroup:YeeVerticalTabDragTest.DragOutOfGroup:YeeVerticalTabDragTest.DragGroupHeader:YeeVerticalTabDragTest.DragCollapsedGroupStaysCollapsed:YeeVerticalTabDragTest.DragCollapsedGroupOverExpandedGroup:YeeVerticalTabDragTest.DragToScroll:All/YeeTabStripCollectionControllerInteractiveUiTest.YeeFavoriteContextMenuHonorsCapacityAndAllowsUnpin/Vertical:SplitTabLayout/YeeMultiContentsViewUiTest.YeeSidebarControlsPersistAcrossSplitAndCollapse/*:SplitTabLayout/YeeMultiContentsViewUiTest.YeeSplitCommandsUpdateChromiumModel/*:SplitTabLayout/YeeMultiContentsViewUiTest.InsetsOnlyInSplit/*:SplitTabLayout/YeeMultiContentsViewUiTest.ResizeMouseDoubleClickEqualizesSplitViews/*:SplitTabLayout/YeeMultiContentsViewUiTest.RoundedCornersForSplitView/*:SplitTabLayout/YeeMultiContentsViewUiTest.BackgroundVisibility/*'
INTERACTIVE_FILTER+=':SplitTabLayout/YeeMultiContentsViewUiTest.ResizeGestureDoubleTapEqualizesSplitViews/*'
# Windows의 실제 popup 포인터 동작 검사는 해당 플랫폼에서만 컴파일된다.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    INTERACTIVE_FILTER+=':YeeVerticalTabStripRegionViewInteractiveUiTest.YeeFooterPointerExitKeepsPopupAndEscapeRestoresFocus'
    ;;
esac
INTERACTIVE_FILTER+=':All/YeeTabStripCollectionControllerInteractiveUiTest.YeeDragPreviewFollowsStripOrientation/*:All/YeeTabStripCollectionControllerInteractiveUiTest.YeePinnedSplitBorderFollowsStripOrientation/*'
INTERACTIVE_FILTER+=':All/YeeTabStripCollectionControllerInteractiveUiTest.YeeClosingTabCanReattachWithoutModel/*'
INTERACTIVE_FILTER+=':SplitTabLayout/YeeMultiContentsViewUiTest.YeeDividerMouseDragResizesBothPanesAndKeepsTabs/*'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Sidebar regression targets"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "unit" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/tabs/common:yee_layout_unittests)
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/yee:yee_interactive_ui_tests)
  build_regression_targets "Sidebar" "${targets[@]}"
fi

if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  print "Running Sidebar policy, geometry, and native View unit regressions."
  run_tiered_native_test_gate "$YEE_OUT_DIR/yee_layout_unittests" \
    --gtest_filter="$UNIT_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Running interactive Sidebar regressions: tier=$YEE_TEST_TIER."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi
