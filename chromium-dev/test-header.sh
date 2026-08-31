#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

MODE="${1:-all}"
SKIP_BUILD=false
if [[ "$MODE" == "--no-build" ]]; then
  MODE="all"
  SKIP_BUILD=true
elif [[ "${2:-}" == "--no-build" ]]; then
  SKIP_BUILD=true
fi

if [[ "$MODE" != "unit" && "$MODE" != "interactive" && "$MODE" != "all" ]]; then
  print -u2 "Usage: ./chromium-dev/test-header.sh [unit|interactive|all] [--no-build]"
  exit 2
fi

require_depot_tools
require_chromium_src

UNIT_FILTER='BrowserSurfaceColorControllerTest.*:BrowserSurfacePresentationResolverTest.*:YeeRestingTextViewTest.*:YeeSurfaceColorTest.HeaderRolesRemainReadableOnLightAndDarkPages:YeeSurfaceColorTest.FocusStrokeKeepsNonTextContrast:YeeSurfaceColorTest.PopupProvidersAreExactAndDoNotGrowGlobalCache:YeeSurfaceGeometryTest.SplitAndSingleHeadersShareMetricsContract'
INTERACTIVE_FILTER='YeePopupWindowUiTest.PaneHeaderOmitsUnsupportedSidebarControl:SplitTabLayout/MultiContentsViewUiTest.YeeSingleAndSplitHeadersShareLocationBarGeometry/*:SplitTabLayout/MultiContentsViewUiTest.YeePresentationFollowsSingleAndSplitSources/*:SplitTabLayout/MultiContentsViewUiTest.YeeLocationBarRehostSynchronizesOncePerGeneration/*:SplitTabLayout/MultiContentsViewUiTest.YeeInactivePaneHeaderActivatesAddressEditing/*:SplitTabLayout/MultiContentsViewUiTest.YeeOmniboxPopupFollowsSingleAndSplitHeader/*'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Header regression targets"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "unit" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/tabs/common:yee_header_unittests)
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(interactive_ui_tests)
  build_regression_targets "Header" "${targets[@]}"
fi

if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  print "Running Header unit regressions without opening a browser window."
  print "Native View tests still require access to the active GUI session."
  "$YEE_OUT_DIR/yee_header_unittests" \
    --gtest_filter="$UNIT_FILTER" \
    --test-launcher-jobs=1
fi

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Interactive Header regressions open real Yee windows and may take focus."
  gracefully_quit_yee
  "$YEE_OUT_DIR/interactive_ui_tests" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi
