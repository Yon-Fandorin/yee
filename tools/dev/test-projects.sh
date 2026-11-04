#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
MODE=unit
SKIP_BUILD=false
if (( $# > 0 )) && [[ "$1" != --no-build ]]; then
  MODE="$1"
  shift
fi
if [[ "${1:-}" == --no-build ]]; then
  SKIP_BUILD=true
  shift
fi
if (( $# > 0 )) || [[ "$MODE" != unit && "$MODE" != browser && "$MODE" != all ]]; then
  print -u2 "Usage: $0 [unit|browser|all] [--no-build]"
  exit 2
fi
require_depot_tools
require_chromium_src
require_yee_output_selection
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 15 "Projects regression targets"
  sync_yee_ui_sources
  targets=()
  [[ "$MODE" == unit || "$MODE" == all ]] && targets+=(chrome/browser/yee_projects:project_service_unittests)
  [[ "$MODE" == browser || "$MODE" == all ]] && targets+=(chrome/browser/ui/views/yee:site_controls_browsertests)
  build_regression_targets "Projects" "${targets[@]}"
fi
if [[ "$MODE" == unit || "$MODE" == all ]]; then
  run_native_test_gate "$YEE_OUT_DIR/project_service_unittests" \
    --gtest_filter='ProjectServiceTest.*' --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
if [[ "$MODE" == browser || "$MODE" == all ]]; then
  prepare_ui_test_display
  gracefully_quit_yee
  run_native_test_gate "$YEE_OUT_DIR/site_controls_browsertests" \
    "${YEE_UI_TEST_WINDOW_ARGS[@]}" \
    --gtest_filter='ProjectNavigationBrowserTest.*' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
  prepare_ui_test_display
  gracefully_quit_yee
  run_native_test_gate "$YEE_OUT_DIR/site_controls_browsertests" \
    "${YEE_UI_TEST_WINDOW_ARGS[@]}" --enable-features=TabStripUnification \
    --gtest_filter='ProjectNavigationBrowserTest.UnifiedHorizontalLayoutToleratesMissingNativeAllocation' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
