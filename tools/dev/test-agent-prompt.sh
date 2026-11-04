#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
configure_test_tiers "" "" "$@"
if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi

if [[ "$SKIP_BUILD" == false ]]; then
  sync_yee_ui_sources
  build_regression_targets "agent prompt" \
    'chrome/browser/ui/views/yee:agent_bridge_prompt_unittests'
fi

TEST_BINARY="$YEE_OUT_DIR/agent_bridge_prompt_unittests"
FILTER='AgentBridgePromptTest.*:AgentBridgePointerGeometryTest.*'
run_tiered_native_test_gate "$TEST_BINARY" --gtest_filter="$FILTER" \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
