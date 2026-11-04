#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "all" "core|interactive|browser|all" "$@"

if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the applied wait gate"
  sync_yee_ui_sources
  targets=()
  [[ "$MODE" == core || "$MODE" == all ]] && targets+=(chrome/browser/ui/views/yee:multi_contents_geometry_unittests)
  [[ "$MODE" == interactive || "$MODE" == all ]] && targets+=(chrome/browser/ui/views/yee:yee_interactive_ui_tests)
  [[ "$MODE" == browser || "$MODE" == all ]] && targets+=(chrome/browser/ui/views/yee:site_controls_browsertests)
  build_regression_targets "applied wait" "${targets[@]}"
fi

if [[ "$MODE" == core || "$MODE" == all ]]; then
  run_tiered_native_test_gate "$YEE_OUT_DIR/multi_contents_geometry_unittests" \
    --gtest_filter='AppliedWaitTest.*' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
if [[ "$MODE" == interactive || "$MODE" == all ]]; then
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter='YeeAppliedWaitInteractiveTest.*:YeeAppliedWaitDragInteractiveTest.*' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 --ui-test-action-timeout=10000
fi
if [[ "$MODE" == browser || "$MODE" == all ]]; then
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
    --gtest_filter='YeeAppliedWaitBrowserTest.*' \
    --test-launcher-jobs=1 --test-launcher-retry-limit=0
fi
