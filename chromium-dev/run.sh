#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

PROFILE_DIR="$LOCAL_BUILD_ROOT/runtime-profile"
LAUNCH_IN_BACKGROUND=false

if [[ "${1:-}" == "--background" ]]; then
  LAUNCH_IN_BACKGROUND=true
  shift
fi

if [[ ! -x "$YEE_BROWSER_BIN" ]]; then
  print -u2 "Built Yee browser is missing: $YEE_BROWSER_BIN"
  print -u2 "Run ./chromium-dev/build.sh first."
  exit 10
fi

require_integrated_yee_app_current

mkdir -p "$PROFILE_DIR"

browser_args=(
  "--user-data-dir=$PROFILE_DIR"
  --no-first-run
  --no-default-browser-check
  --hide-crash-restore-bubble
  "${@}"
)

if [[ "$LAUNCH_IN_BACKGROUND" == true ]]; then
  /usr/bin/open -g -n "$YEE_APP_DIR" --args "${browser_args[@]}"
  exit 0
fi

exec "$YEE_BROWSER_BIN" "${browser_args[@]}"
