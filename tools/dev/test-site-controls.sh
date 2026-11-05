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
  require_free_gib 10 "the focused Site Controls browser gate"
  sync_yee_ui_sources
  build_regression_targets "Site Controls" \
    chrome/browser/ui/views/yee:site_controls_browsertests
fi

FILTER='CustomizationMojoBrowserTest.*:SiteControlsBrowserTest.*:SidebarFooterBrowserTest.*:DisclosureArrowTest.*:YeeSessionStartupPrefTest.*:YeeTabDataObserverBrowserTest.FaviconAndIconFlags:All/YeeTabStripCollectionControllerBrowserTest.YeeGroupRenameRefreshesInactiveTabAccessibleNames/*:All/YeeTabSearchPageHandlerTest.RemoveSplit_NTP/*'
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) FILTER+=':YeeUiaApplicationExitBrowserTest.*' ;;
esac

run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
  --gtest_filter="$FILTER" \
  --test-launcher-jobs=1 \
  --test-launcher-retry-limit=0
