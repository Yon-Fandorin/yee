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

FAST_TARGET='chrome/browser/ui/views/tabs/common:yee_layout_unittests'
FAST_FILTER='BrowserViewTabbedLayoutNativeGeometryTest.*:YeeSurfaceGeometryTest.*'
INTERACTIVE_FILTER='BrowserViewTabbedLayoutImplFeatureOffUiTest.NativeSidePanelRowModesMatchAppliedLayout:BrowserViewTabbedLayoutImplNativeAnimationUiTest.MidAnimationTypeAndWidthSwitchUseFreshPlans:BrowserViewTabbedLayoutImplUiTest.YeeNativePlannerResultMatchesAppliedLayout:BrowserViewTabbedLayoutImplUiTest.YeeSplitTargetLayoutUsesHandedInInsets:BrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal:*TopContainerBackground*'
BROWSER_FILTER='SidePanelCoordinatorTest.ShowFromAnimationReparentsContentView'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Browser Surface layout ${MODE} gate"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "fast" || "$MODE" == "all" ]] && \
    targets+=("$FAST_TARGET")
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(interactive_ui_tests)
  [[ "$MODE" == "browser" || "$MODE" == "all" ]] && \
    targets+=(browser_tests)
  build_regression_targets "Browser Surface layout ${MODE}" "${targets[@]}"
fi

if [[ "$MODE" == "fast" || "$MODE" == "all" ]]; then
  print "Running the small pure native/Yee Browser Surface layout gate."
  "$YEE_OUT_DIR/yee_layout_unittests" \
    --gtest_filter="$FAST_FILTER" \
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
fi

if [[ "$MODE" == "browser" || "$MODE" == "all" ]]; then
  print "Running the explicit browser-level Side Panel coordinator gate."
  gracefully_quit_yee
  "$YEE_OUT_DIR/browser_tests" \
    --gtest_filter="$BROWSER_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi
