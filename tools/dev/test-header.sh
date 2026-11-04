#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "unit" "unit|interactive|all" "$@"

if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi

UNIT_FILTER='BrowserSurfaceColorControllerTest.*:BrowserSurfacePresentationResolverTest.*:YeeRestingTextViewTest.*:YeeSurfaceColorTest.HeaderRolesRemainReadableOnLightAndDarkPages:YeeSurfaceColorTest.FocusStrokeKeepsNonTextContrast:YeeSurfaceColorTest.PopupProvidersAreExactAndDoNotGrowGlobalCache:YeeSurfaceGeometryTest.SplitAndSingleHeadersShareMetricsContract'
INTERACTIVE_FILTER='YeePopupWindowUiTest.PaneHeadersRetainNativeControls:SplitTabLayout/YeeMultiContentsViewUiTest.YeeSingleAndSplitHeadersShareLocationBarGeometry/*:SplitTabLayout/YeeMultiContentsViewUiTest.YeePresentationFollowsSingleAndSplitSources/*:SplitTabLayout/YeeMultiContentsViewUiTest.YeeInactivePaneHeaderActivatesAddressEditing/*:SplitTabLayout/YeeMultiContentsViewUiTest.YeeOmniboxPopupFollowsSingleAndSplitHeader/*'
UNIT_FILTER+=':BrowserSurfaceColorSampleTest.*'
UNIT_FILTER+=':YeeNativePresentationTest.*:YeeTabIconPresentationTest.*'
UNIT_FILTER+=':SplitPaneControlsTest.*:SplitPaneControlsTransitTest.*:SidePanelResizeGutterTest.*:SidePanelResizeGutterViewTest.*'
INTERACTIVE_FILTER+=':SplitTabLayout/YeeMultiContentsViewUiTest.YeeRenderedBoundaryTracksDelayedCssWithoutMetadata/*'
INTERACTIVE_FILTER+=':YeeContentsViewUiTest.YeeMediaButtonTracksHeaderAndVisibility'
INTERACTIVE_FILTER+=':YeeContentsViewUiTest.YeeOmniboxSuggestionsHaveBalancedInsets'
RTL_FILTER='SplitTabLayout/YeeMultiContentsViewUiTest.YeeRenderedBoundaryTracksDelayedCssWithoutMetadata/*:SplitTabLayout/YeeMultiContentsViewUiTest.YeePresentationFollowsSingleAndSplitSources/*'

if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the Header regression targets"
  sync_yee_ui_sources

  targets=()
  [[ "$MODE" == "unit" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/tabs/common:yee_header_unittests)
  [[ "$MODE" == "interactive" || "$MODE" == "all" ]] && \
    targets+=(chrome/browser/ui/views/yee:yee_interactive_ui_tests)
  build_regression_targets "Header" "${targets[@]}"
fi

if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  print "Running Header unit regressions: tier=$YEE_TEST_TIER."
  run_tiered_native_test_gate "$YEE_OUT_DIR/yee_header_unittests" \
    --gtest_filter="$UNIT_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0
fi

if [[ "$MODE" == "interactive" || "$MODE" == "all" ]]; then
  print "Running interactive Header regressions: tier=$YEE_TEST_TIER."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter="$INTERACTIVE_FILTER" \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000

  print "Running rendered color/binding regressions in RTL at 125% scale."
  run_tiered_native_test_gate --display "$YEE_OUT_DIR/yee_interactive_ui_tests" \
    --gtest_filter="$RTL_FILTER" \
    --force-ui-direction=rtl \
    --force-device-scale-factor=1.25 \
    --test-launcher-jobs=1 \
    --test-launcher-retry-limit=0 \
    --ui-test-action-max-timeout=20000 \
    --ui-test-action-timeout=10000
fi
