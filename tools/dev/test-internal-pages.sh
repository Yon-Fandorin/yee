#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

build_tests=true
test_ui="${YEE_TEST_UI:-off}"
test_filter='HistoryBrowserTest.*:BookmarksBrowserTest.*:DownloadsBrowserTest.*:InternalURLsBrowserTest.ProductAndNativeSettingsRoutes'
for argument in "$@"; do
  case "$argument" in
    --no-build) build_tests=false ;;
    --ui=on) test_ui=on ;;
    --ui=off) test_ui=off ;;
    --filter=*) test_filter="${argument#--filter=}" ;;
    *)
      print -u2 "Usage: $0 [--no-build] [--ui=off|on] [--filter=PATTERN]"
      exit 2
      ;;
  esac
done
if [[ "$test_ui" != on && "$test_ui" != off ]]; then
  print -u2 "YEE_TEST_UI must be off or on."
  exit 2
fi

require_depot_tools
require_chromium_src
if [[ "$build_tests" == true ]]; then
  require_free_gib 10 "the focused Internal Pages browser gate"
  sync_yee_ui_sources
  sync_yee_branding
  build_regression_targets "Internal Pages" \
    chrome/browser/ui/views/yee:site_controls_browsertests \
    components/yee_branding:internal_urls_unittests
fi

typeset -a browser_test_args=()
if [[ "$test_ui" == off ]]; then
  print "Running Internal Pages without visible windows."
  browser_test_args=(--headless --allow-chrome-scheme-url --disable-gpu)
else
  print "Running Internal Pages with visible windows; tests may take focus."
  gracefully_quit_yee
fi

"$YEE_OUT_DIR/internal_urls_unittests" --test-launcher-jobs=1
"$YEE_OUT_DIR/site_controls_browsertests" \
  "${browser_test_args[@]}" \
  "--gtest_filter=$test_filter" \
  --test-launcher-jobs=1 \
  --test-launcher-retry-limit=0
