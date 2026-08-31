#!/bin/zsh

set -euo pipefail
unsetopt BG_NICE

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

SMOKE_TIMEOUT_SECONDS="${YEE_SMOKE_TIMEOUT_SECONDS:-30}"
SMOKE_URL='data:text/html,%3Ctitle%3EYEE_SMOKE_OK%3C%2Ftitle%3E%3Cmain%20id%3D%22yee-smoke%22%3EYEE_SMOKE_OK%3C%2Fmain%3E'

if [[ ! -x "$YEE_BROWSER_BIN" ]]; then
  print -u2 "Built Yee browser is missing: $YEE_BROWSER_BIN"
  print -u2 "Run ./chromium-dev/build.sh first."
  exit 10
fi

require_integrated_yee_app_current

if ! command -v curl >/dev/null 2>&1; then
  print -u2 "curl is required for the DevTools smoke test."
  exit 12
fi

SMOKE_PROFILE="$(mktemp -d /private/tmp/yee-chromium-smoke.XXXXXX)"
SMOKE_STDOUT="$SMOKE_PROFILE/chromium.stdout"
SMOKE_STDERR="$SMOKE_PROFILE/chromium.stderr"
DEVTOOLS_PORT_FILE="$SMOKE_PROFILE/DevToolsActivePort"
CHROMIUM_PID=""

cleanup() {
  if [[ -n "$CHROMIUM_PID" ]] && kill -0 "$CHROMIUM_PID" 2>/dev/null; then
    kill "$CHROMIUM_PID" 2>/dev/null || true
    wait "$CHROMIUM_PID" 2>/dev/null || true
  fi
  rm -rf "$SMOKE_PROFILE"
}
trap cleanup EXIT INT TERM

print "Binary: $($YEE_BROWSER_BIN --version)"

"$YEE_BROWSER_BIN" \
  --headless \
  --disable-background-networking \
  --disable-component-update \
  --disable-default-apps \
  --disable-sync \
  --metrics-recording-only \
  --no-first-run \
  --no-default-browser-check \
  --user-data-dir="$SMOKE_PROFILE" \
  --remote-debugging-port=0 \
  "$SMOKE_URL" \
  >"$SMOKE_STDOUT" \
  2>"$SMOKE_STDERR" &
CHROMIUM_PID=$!

print "Waiting for the DevTools endpoint (timeout: ${SMOKE_TIMEOUT_SECONDS}s)."

SECONDS=0
while (( SECONDS < SMOKE_TIMEOUT_SECONDS )); do
  if [[ -s "$DEVTOOLS_PORT_FILE" ]]; then
    IFS= read -r DEVTOOLS_PORT < "$DEVTOOLS_PORT_FILE"
    if [[ "$DEVTOOLS_PORT" == <-> ]]; then
      TARGETS_JSON="$(
        curl \
          --fail \
          --silent \
          --show-error \
          --max-time 2 \
          "http://127.0.0.1:${DEVTOOLS_PORT}/json/list" \
          2>/dev/null || true
      )"

      if [[ "$TARGETS_JSON" == *'"type": "page"'* && \
            "$TARGETS_JSON" == *'"title": "YEE_SMOKE_OK"'* ]]; then
        print "PASS: browser process, DevTools endpoint, and renderer smoke page are ready."
        exit 0
      fi
    fi
  fi

  if ! kill -0 "$CHROMIUM_PID" 2>/dev/null; then
    if wait "$CHROMIUM_PID"; then
      CHROMIUM_STATUS=0
    else
      CHROMIUM_STATUS=$?
    fi
    CHROMIUM_PID=""

    print -u2 "Chromium exited before the DevTools smoke target was ready (status: $CHROMIUM_STATUS)."
    if (( CHROMIUM_STATUS == 134 )); then
      print -u2 "On macOS, status 134 can mean a restricted parent sandbox blocked application registration."
      print -u2 "Run this script from a normal Terminal session or grant GUI execution permission."
    fi
    if [[ -s "$SMOKE_STDERR" ]]; then
      print -u2 "Chromium stderr:"
      tail -n 80 "$SMOKE_STDERR" >&2
    fi
    exit 13
  fi

  sleep 0.2
done

print -u2 "Timed out waiting for the DevTools smoke target after ${SMOKE_TIMEOUT_SECONDS}s."
if [[ -s "$SMOKE_STDERR" ]]; then
  print -u2 "Chromium stderr:"
  tail -n 80 "$SMOKE_STDERR" >&2
fi
exit 14
