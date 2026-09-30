#!/bin/zsh

set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

MODE="${1:-}"
if [[ ( "$MODE" != "off" && "$MODE" != "on" ) || "$#" -gt 2 ||
      ( "${2:-}" != "" && "${2:-}" != "--no-build" ) ]]; then
  print -u2 "Usage: ./tools/dev/test-youtube-live.sh off|on [--no-build]"
  exit 2
fi
require_depot_tools
require_chromium_src
if [[ "${2:-}" != "--no-build" ]]; then
  require_free_gib 10 "the opt-in YouTube observation"
  sync_yee_ui_sources
  build_regression_targets "YouTube live observation" \
    chrome/browser/ui/views/yee:site_controls_browsertests
fi
gracefully_quit_yee
REPORT_DIR="$LOCAL_BUILD_ROOT/youtube-live"
mkdir -p "$REPORT_DIR"
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
"$YEE_OUT_DIR/site_controls_browsertests" \
  --gtest_filter='YouTubeLiveBrowserTest.DISABLED_MidrollObservation' \
  --gtest_also_run_disabled_tests \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0 \
  --test-launcher-timeout=2100000 \
  "--yee-live-youtube-mode=$MODE" \
  "--yee-live-youtube-seconds=${YEE_LIVE_YOUTUBE_SECONDS:-180}" \
  "--yee-live-youtube-report=$REPORT_DIR/$REPORT_NAME.json" \
  "${EXPERIMENT_ARGS[@]}"
print "Live delivery evidence: $REPORT_DIR/$REPORT_NAME.json (inspect control ads before claiming coverage)."
