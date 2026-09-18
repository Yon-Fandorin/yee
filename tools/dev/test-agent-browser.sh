#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
require_depot_tools
require_chromium_src

if [[ "${1:-}" != "--no-build" ]]; then
  require_free_gib 10 "agent browser contract tests"
  sync_yee_ui_sources
  build_regression_targets "agent browser" \
    'chrome/browser/ui/views/yee:multi_contents_geometry_unittests'
fi

TEST_BINARY="$YEE_OUT_DIR/multi_contents_geometry_unittests"
FILTER='AgentBrowserContractTest.*:AgentInputRequestTest.*:AgentTabActivityTest.*:AgentRequestLedgerTest.*:AgentTaskPermissionsTest.*'
# An old cached binary must not report success with zero matching tests.
TEST_LIST="$("$TEST_BINARY" --gtest_list_tests --gtest_filter="$FILTER")"
for suite in AgentBrowserContractTest AgentInputRequestTest AgentTabActivityTest AgentRequestLedgerTest AgentTaskPermissionsTest; do
  if ! print -r -- "$TEST_LIST" | rg -q "^${suite}\\."; then
    print -u2 "Missing test suite ${suite}; rebuild before verifying."
    exit 1
  fi
done
"$TEST_BINARY" --gtest_filter="$FILTER" \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
