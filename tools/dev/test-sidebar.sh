#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

"$SCRIPT_DIR/test-run-preflight.sh"

MODE="${1:-unit}"
SKIP_BUILD=false
if [[ "$MODE" == "--no-build" ]]; then
  MODE="unit"
  SKIP_BUILD=true
elif [[ "${2:-}" == "--no-build" ]]; then
  SKIP_BUILD=true
fi

if [[ "$MODE" != "unit" && "$MODE" != "interactive" && "$MODE" != "all" ]]; then
  print -u2 "Usage: ./tools/dev/test-sidebar.sh [unit|interactive|all] [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src

UNIT_FILTER='Favorites*:*GroupMarkSignalsTest.*:SidebarFooter*:*SplitPaneControlsTest.*:YeeSurfaceColorTest.*:YeeSurfaceGeometryTest.*:YeeShellColorTest.*:YeeSidebarItemColorTest.*:All/TabGroupHeaderViewTest.YeeVerticalHoverUpdatesSurfaceAndForegroundTogether/*:All/TabCollectionAnimatingLayoutManagerTest.RemoveChildDuringActiveAnimation/*'
# Header의 주소줄·팝업 검사는 test-header.sh가 소유한다. Sidebar 구조와
# 실제 drag·split 조작만 선택해 같은 창 검사를 두 번 실행하지 않는다.
INTERACTIVE_FILTER='VerticalTabDragTest.*Favorite*:VerticalTabDragTest.LoneTabCanPinAndUnpinInsideItsOwnSidebar:VerticalTabDragTest.YeeSidebar*:VerticalTabDragTest.DragWithinUnpinnedContainer:VerticalTabDragTest.CancelDragWithinUnpinnedContainer:VerticalTabDragTest.DragSplitTabs:VerticalTabDragTest.DragOverSplit:VerticalTabDragTest.DragOverSplitInGroup:VerticalTabDragTest.DragMultipleTabs:VerticalTabDragTest.DragMultipleTabsInGroup:VerticalTabDragTest.DragWithinGroup:VerticalTabDragTest.DragOutOfGroup:VerticalTabDragTest.DragGroupHeader:VerticalTabDragTest.DragCollapsedGroupStaysCollapsed:VerticalTabDragTest.DragCollapsedGroupOverExpandedGroup:VerticalTabDragTest.DragToScroll:All/TabStripCollectionControllerInteractiveUiTest.YeeFavoriteContextMenuHonorsCapacityAndAllowsUnpin/Vertical:SplitTabLayout/MultiContentsViewUiTest.YeeSidebarControlsPersistAcrossSplitAndCollapse/*:SplitTabLayout/MultiContentsViewUiTest.YeeSplitCommandsUpdateChromiumModel/*:SplitTabLayout/MultiContentsViewUiTest.InsetsOnlyInSplit/*:SplitTabLayout/MultiContentsViewUiTest.ResizeMouseDoubleClickEqualizesSplitViews/*:SplitTabLayout/MultiContentsViewUiTest.RoundedCornersForSplitView/*:SplitTabLayout/MultiContentsViewUiTest.BackgroundVisibility/*'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Sidebar regression targets"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "unit" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/tabs/common:yee_layout_unittests)
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(interactive_ui_tests)
  build_regression_targets "Sidebar" "${targets[@]}"
fi

if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  print "Running Sidebar policy, geometry, and native View unit regressions."
  print "Native popup/focus tests may activate test widgets in the GUI session."
  "$YEE_OUT_DIR/yee_layout_unittests" \
    --gtest_filter="$UNIT_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Interactive Sidebar regressions open real Yee windows and may take focus."
  prepare_ui_test_display
  gracefully_quit_yee
  "$YEE_OUT_DIR/interactive_ui_tests" \
    "${YEE_UI_TEST_WINDOW_ARGS[@]}" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi
