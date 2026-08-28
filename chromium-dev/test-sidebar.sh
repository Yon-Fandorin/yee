#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

MODE="${1:-all}"
SKIP_BUILD=false
if [[ "$MODE" == "--no-build" ]]; then
  MODE="all"
  SKIP_BUILD=true
elif [[ "${2:-}" == "--no-build" ]]; then
  SKIP_BUILD=true
fi

if [[ "$MODE" != "unit" && "$MODE" != "interactive" && "$MODE" != "all" ]]; then
  print -u2 "Usage: ./chromium-dev/test-sidebar.sh [unit|interactive|all] [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src

UNIT_FILTER='Favorites*:*GroupMarkSignalsTest.*:SplitPaneControlsTest.*:YeeSurfaceColorTest.*:YeeSurfaceGeometryTest.*:YeeSidebarItemColorTest.*:All/TabCollectionAnimatingLayoutManagerTest.RemoveChildDuringActiveAnimation/*'
INTERACTIVE_FILTER='VerticalTabDragTest.*Favorite*:VerticalTabDragTest.LoneTabCanPinAndUnpinInsideItsOwnSidebar:VerticalTabDragTest.YeeSidebar*:VerticalTabDragTest.DragWithinUnpinnedContainer:VerticalTabDragTest.CancelDragWithinUnpinnedContainer:VerticalTabDragTest.DragSplitTabs:VerticalTabDragTest.DragOverSplit:VerticalTabDragTest.DragOverSplitInGroup:VerticalTabDragTest.DragMultipleTabs:VerticalTabDragTest.DragMultipleTabsInGroup:VerticalTabDragTest.DragWithinGroup:VerticalTabDragTest.DragOutOfGroup:VerticalTabDragTest.DragGroupHeader:VerticalTabDragTest.DragCollapsedGroupStaysCollapsed:VerticalTabDragTest.DragCollapsedGroupOverExpandedGroup:VerticalTabDragTest.DragToScroll:All/TabStripCollectionControllerInteractiveUiTest.YeeFavoriteContextMenuHonorsCapacityAndAllowsUnpin/Vertical:SplitTabLayout/MultiContentsViewUiTest.Yee*/*:SplitTabLayout/MultiContentsViewUiTest.InsetsOnlyInSplit/*:SplitTabLayout/MultiContentsViewUiTest.ResizeMouseDoubleClickEqualizesSplitViews/*:SplitTabLayout/MultiContentsViewUiTest.RoundedCornersForSplitView/*:SplitTabLayout/MultiContentsViewUiTest.BackgroundVisibility/*'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Sidebar regression targets"
  "$YEE_ROOT/chromium-overlay/install-yee-ui-sources.sh" "$CHROMIUM_SRC"

  export XDG_CACHE_HOME="$LOCAL_BUILD_ROOT/cache"
  export CLANG_MODULE_CACHE_PATH="$LOCAL_BUILD_ROOT/cache/clang/ModuleCache"
  export GOCACHE="$LOCAL_BUILD_ROOT/cache/go-build"
  export GOMODCACHE="$LOCAL_BUILD_ROOT/cache/go-mod"
  export CARGO_HOME="$LOCAL_BUILD_ROOT/cache/cargo"
  export npm_config_cache="$LOCAL_BUILD_ROOT/cache/npm"
  export PIP_CACHE_DIR="$LOCAL_BUILD_ROOT/cache/pip"
  mkdir -p \
    "$CLANG_MODULE_CACHE_PATH" \
    "$GOCACHE" \
    "$GOMODCACHE" \
    "$CARGO_HOME" \
    "$npm_config_cache" \
    "$PIP_CACHE_DIR"

  targets=()
  [[ "$MODE" == "unit" || "$MODE" == "all" ]] && targets+=(unit_tests)
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(interactive_ui_tests)

  print "Building Sidebar regression targets."
  (
    cd "$CHROMIUM_SRC"
    caffeinate -dimsu nice -n 10 \
      autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" "${targets[@]}"
  )
fi

if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  print "Running Sidebar unit regressions without opening a browser window."
  print "Native View tests still require access to the active GUI session."
  "$YEE_OUT_DIR/unit_tests" \
    --gtest_filter="$UNIT_FILTER" \
    --test-launcher-jobs=1
fi

gracefully_quit_yee() {
  if ! pgrep -f -- "$YEE_BROWSER_BIN" >/dev/null 2>&1; then
    return
  fi

  print "Requesting a graceful shutdown of the running Yee app."
  osascript -e 'tell application "Yee" to quit'
  for attempt in {1..100}; do
    if ! pgrep -f -- "$YEE_BROWSER_BIN" >/dev/null 2>&1; then
      return
    fi
    sleep 0.1
  done

  print -u2 "Yee did not exit after the graceful shutdown request."
  exit 15
}

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Interactive Sidebar regressions open real Yee windows and may take focus."
  gracefully_quit_yee
  "$YEE_OUT_DIR/interactive_ui_tests" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi
