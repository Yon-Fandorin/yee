#!/bin/zsh

set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

configure_test_tiers "off" "off|on" "$@"
if [[ "$PRINT_FILTERS" == false ]]; then
  require_depot_tools
  require_chromium_src
  require_yee_output_selection
fi
if [[ "$SKIP_BUILD" == false ]]; then
  require_free_gib 10 "the opt-in YouTube playback gate"
  sync_yee_ui_sources
  build_regression_targets "YouTube live playback" \
    chrome/browser/ui/views/yee:site_controls_browsertests
fi
REPORT_DIR="$LOCAL_BUILD_ROOT/youtube-live"
[[ "$PRINT_FILTERS" == true ]] || mkdir -p "$REPORT_DIR"
EXPERIMENT_ARGS=()
REPORT_NAME="$MODE"
if [[ "${YEE_LIVE_YOUTUBE_SEEK:-0}" == "1" ]]; then
  EXPERIMENT_ARGS+=(--yee-live-youtube-seek)
  REPORT_NAME="$MODE-seek"
fi
if [[ -n "${YEE_LIVE_YOUTUBE_VIDEO:-}" ]]; then
  EXPERIMENT_ARGS+=("--yee-live-youtube-video=$YEE_LIVE_YOUTUBE_VIDEO")
  REPORT_NAME="$REPORT_NAME-$YEE_LIVE_YOUTUBE_VIDEO"
fi
run_tiered_native_test_gate --display "$YEE_OUT_DIR/site_controls_browsertests" \
  --gtest_filter='YouTubeLiveBrowserTest.DISABLED_MidrollObservation' \
  --gtest_also_run_disabled_tests \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0 \
  --test-launcher-timeout=2100000 \
  "--yee-live-youtube-mode=$MODE" \
  "--yee-live-youtube-seconds=${YEE_LIVE_YOUTUBE_SECONDS:-180}" \
  "--yee-live-youtube-min-content-seconds=${YEE_LIVE_YOUTUBE_MIN_CONTENT_SECONDS:-60}" \
  "--yee-live-youtube-report=$REPORT_DIR/$REPORT_NAME.json" \
  "${EXPERIMENT_ARGS[@]}"
if [[ "$PRINT_FILTERS" == false && "$YEE_TEST_TIER" != background ]]; then
  print "Playback gate completed. Live delivery evidence: $REPORT_DIR/$REPORT_NAME.json (inspect control ads before claiming coverage)."
fi
