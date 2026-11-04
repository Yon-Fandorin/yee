#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

test_ui="${YEE_TEST_UI:-off}"
test_filter='ExtensionsBrowserTest.*:HistoryBrowserTest.*:BookmarksBrowserTest.*:DownloadsBrowserTest.*:InternalURLsBrowserTest.*'
typeset -a tier_arguments=()
for argument in "$@"; do
  case "$argument" in
    --ui=on) test_ui=on ;;
    --ui=off) test_ui=off ;;
    --filter=*) test_filter="${argument#--filter=}" ;;
    *) tier_arguments+=("$argument") ;;
  esac
done
if [[ "$test_ui" != on && "$test_ui" != off ]]; then
  print -u2 'YEE_TEST_UI must be off or on.'
  exit 2
fi
# --ui=on은 기존 표시형 실행을 유지한다. 명시한 tier가 있으면 그 선택을 따른다.
[[ "$test_ui" == on ]] && tier_arguments=(--tier=all "${tier_arguments[@]}")
configure_test_tiers "" "" "${tier_arguments[@]}"
if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the focused Internal Pages browser gate"
  sync_yee_ui_sources
  sync_yee_branding
  mac_targets=()
  [[ "$(uname -s)" == Darwin ]] && mac_targets+=(components/yee_branding:mac_identity_unittests)
  build_regression_targets "Internal Pages" \
    chrome/browser/ui/views/yee:site_controls_browsertests \
    components/yee_branding:internal_urls_unittests "${mac_targets[@]}"
fi

run_tiered_native_test_gate "$YEE_OUT_DIR/internal_urls_unittests" --gtest_filter='*' --test-launcher-jobs=1
if [[ "$(uname -s)" == Darwin ]]; then
  run_tiered_native_test_gate "$YEE_OUT_DIR/mac_identity_unittests" --gtest_filter='*' --test-launcher-jobs=1
fi
typeset -a browser_arguments=()
[[ "$YEE_TEST_TIER" == background ]] && browser_arguments+=(--allow-chrome-scheme-url)
run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
  "${browser_arguments[@]}" \
  "--gtest_filter=$test_filter" \
  --test-launcher-jobs=1 \
  --test-launcher-retry-limit=0
