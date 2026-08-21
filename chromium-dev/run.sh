#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

PROFILE_DIR="$LOCAL_BUILD_ROOT/runtime-profile"

if [[ ! -x "$YEE_BROWSER_BIN" ]]; then
  print -u2 "Built Yee browser is missing: $YEE_BROWSER_BIN"
  print -u2 "Run ./chromium-dev/build.sh first."
  exit 10
fi

mkdir -p "$PROFILE_DIR"

if (( $# == 0 )); then
  set -- "https://example.com"
fi

exec "$YEE_BROWSER_BIN" \
  --user-data-dir="$PROFILE_DIR" \
  --no-first-run \
  --no-default-browser-check \
  --hide-crash-restore-bubble \
  "${@}"
