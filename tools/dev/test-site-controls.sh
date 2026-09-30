#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

if [[ "${1:-}" != "" && "${1:-}" != "--no-build" ]]; then
  print -u2 "Usage: ./tools/dev/test-site-controls.sh [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src
if [[ "${1:-}" != "--no-build" ]]; then
  require_free_gib 10 "the focused Site Controls browser gate"
  sync_yee_ui_sources
  build_regression_targets "Site Controls" \
    chrome/browser/ui/views/yee:site_controls_browsertests
fi

gracefully_quit_yee
"$YEE_OUT_DIR/site_controls_browsertests" \
  --gtest_filter='SiteControlsBrowserTest.*' \
  --test-launcher-jobs=1 \
  --test-launcher-retry-limit=0
