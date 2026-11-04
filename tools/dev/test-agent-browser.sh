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
  require_free_gib 10 "agent browser contract tests"
  sync_yee_ui_sources
  build_regression_targets "agent browser" \
    'chrome/browser/ui/views/yee:multi_contents_geometry_unittests'
fi

TEST_BINARY="$YEE_OUT_DIR/multi_contents_geometry_unittests"
FILTER='AgentBrowserContractTest.*:AgentInputRequestTest.*:AgentTabActivityTest.*:AgentRequestLedgerTest.*:AgentTaskPermissionsTest.*'
# An old cached binary must not report success with zero matching tests.
run_tiered_native_test_gate "$TEST_BINARY" --gtest_filter="$FILTER" \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
