#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
require_depot_tools
require_chromium_src

if [[ "${1:-}" != "--no-build" ]]; then
  sync_yee_ui_sources
  build_regression_targets "agent prompt" \
    'chrome/browser/ui/views/yee:agent_bridge_prompt_unittests'
fi

TEST_BINARY="$YEE_OUT_DIR/agent_bridge_prompt_unittests"
FILTER='AgentBridgePromptTest.*'
TEST_LIST="$("$TEST_BINARY" --gtest_list_tests --gtest_filter="$FILTER")"
if ! print -r -- "$TEST_LIST" | rg -q '^AgentBridgePromptTest\.'; then
  print -u2 'Missing prompt tests; rebuild before verifying.'
  exit 1
fi
"$TEST_BINARY" --gtest_filter="$FILTER" \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0
