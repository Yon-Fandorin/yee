#!/bin/zsh
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "unit" "unit|browser|all" "$@"

if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 15 "Projects regression targets"
  sync_yee_ui_sources
  targets=()
  [[ "$MODE" == unit || "$MODE" == all ]] && targets+=(chrome/browser/yee_projects:project_service_unittests)
  [[ "$MODE" == browser || "$MODE" == all ]] && targets+=(chrome/browser/ui/views/yee:site_controls_browsertests)
  build_regression_targets "Projects" "${targets[@]}"
fi
if [[ "$MODE" == unit || "$MODE" == all ]]; then
  run_tiered_native_test_gate "$YEE_OUT_DIR/project_service_unittests" \
    --gtest_filter='ProjectServiceTest.*' --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
if [[ "$MODE" == browser || "$MODE" == all ]]; then
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
    --gtest_filter='ProjectNavigationBrowserTest.*' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
    --enable-features=TabStripUnification \
    --gtest_filter='ProjectNavigationBrowserTest.UnifiedHorizontalLayoutToleratesMissingNativeAllocation' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
