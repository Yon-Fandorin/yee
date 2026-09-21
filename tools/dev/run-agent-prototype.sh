#!/bin/zsh
# Launch an explicitly opted-in, isolated-profile developer prototype.
set -euo pipefail
SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
# Optional outer-window size for matched viewport experiments. This does not
# claim the page viewport has that size; verify the returned viewport metadata.
typeset -a launch_window_args
launch_window_args=()
window_size_seen=0
dark_mode_seen=0
for arg in "$@"; do
  if [[ "$arg" == --window-size=* ]] && (( ! window_size_seen )); then
    window_size_seen=1
    window_pair="${arg#--window-size=}"
    if [[ ! "$window_pair" =~ '^[0-9]{3,4},[0-9]{3,4}$' ]]; then
      print -u2 "Window dimensions must be integers from 300 to 4000."
      exit 2
    fi
    window_width="${window_pair%,*}"
    window_height="${window_pair#*,}"
    if (( window_width < 300 || window_width > 4000 || window_height < 300 || window_height > 4000 )); then
      print -u2 "Window dimensions must be integers from 300 to 4000."
      exit 2
    fi
    launch_window_args+=("--window-size=$window_pair")
  elif [[ "$arg" == --force-dark-mode ]] && (( ! dark_mode_seen )); then
    dark_mode_seen=1
    launch_window_args+=(--force-dark-mode)
  else
    print -u2 "Usage: zsh tools/dev/run-agent-prototype.sh [--window-size=WIDTH,HEIGHT] [--force-dark-mode]"
    exit 2
  fi
done
# Fail before quitting Yee or allocating a profile when UI validation is blocked.
/usr/bin/swift -module-cache-path "$LOCAL_BUILD_ROOT/swift-module-cache" \
  "$SCRIPT_DIR/check-ui-session.swift"
require_integrated_yee_app_current
gracefully_quit_yee
BRIDGE_DIR="$(mktemp -d /private/tmp/yee-agent.XXXXXX)"
PROFILE_DIR="$BRIDGE_DIR/profile"
if [[ -n "${YEE_TEST_SIDEBAR_WIDTH:-}" ]]; then
  python3 "$SCRIPT_DIR/prepare-agent-test-profile.py" "$BRIDGE_DIR" "$YEE_TEST_SIDEBAR_WIDTH"
fi
/usr/bin/open -n "$YEE_APP_DIR" --args \
  "${launch_window_args[@]}" \
  "--user-data-dir=$PROFILE_DIR" \
  "--yee-agent-bridge=$BRIDGE_DIR" \
  --enable-logging "--log-file=$BRIDGE_DIR/browser.log" \
  --no-first-run --no-default-browser-check \
  "file://$YEE_ROOT/tests/fixtures/agent-browser-prototype.html"
print "Bridge directory: $BRIDGE_DIR"
print "CLI: python3 tools/dev/yee-browser.py --bridge $BRIDGE_DIR attach"
print "Approve in Yee before the CLI can read the selected tab."
print "The private test profile remains in $PROFILE_DIR until you remove it."
