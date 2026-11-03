#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

if [[ "${1:-}" != "" && "${1:-}" != "--no-build" ]]; then
  print -u2 "Usage: ./tools/dev/test-omnibox-accessibility.sh [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src
require_yee_output_selection
if [[ "${1:-}" != "--no-build" ]]; then
  require_free_gib 10 "the focused Omnibox accessibility browser gate"
  sync_yee_ui_sources
  build_regression_targets "Omnibox accessibility" \
    chrome/browser/ui/views/yee:site_controls_browsertests
fi

prepare_ui_test_display
gracefully_quit_yee
run_native_test_gate "$YEE_OUT_DIR/site_controls_browsertests" \
  "${YEE_UI_TEST_WINDOW_ARGS[@]}" \
  --gtest_filter='Presentation/OmniboxAccessibilityBrowserTest.*:YeeShell/OmniboxNavigationBrowserTest.*' \
  --test-launcher-jobs=1 \
  --test-launcher-retry-limit=0
