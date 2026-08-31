#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/yee-run-preflight.XXXXXX")"
trap 'rm -rf -- "$TEST_DIR"' EXIT INT TERM

older="$TEST_DIR/older-framework"
newer="$TEST_DIR/newer-framework"
missing="$TEST_DIR/missing-framework"
touch -t 202601010000 "$older"
touch -t 202601010001 "$newer"

if ! integrated_yee_app_is_current "$older" "$newer"; then
  print -u2 "FAIL: a newer bundled Framework must be accepted."
  exit 1
fi
if integrated_yee_app_is_current "$newer" "$older"; then
  print -u2 "FAIL: a stale bundled Framework must be rejected."
  exit 1
fi
if ! integrated_yee_app_is_current "$missing" "$older"; then
  print -u2 "FAIL: a missing isolated Framework must not block launch."
  exit 1
fi
if integrated_yee_app_is_current "$newer" "$missing"; then
  print -u2 "FAIL: a missing bundled Framework must be rejected."
  exit 1
fi

fake_chromium_src="$TEST_DIR/chromium-src"
mkdir -p "$fake_chromium_src/chrome/browser/ui/views/yee"
touch "$fake_chromium_src/chrome/browser/ui/BUILD.gn"
touch "$fake_chromium_src/chrome/browser/ui/views/yee/BUILD.gn"
CHROMIUM_SRC="$fake_chromium_src"
sync_yee_ui_sources >/dev/null
if ! cmp -s \
  "$YEE_ROOT/chromium-overlay/yee-ui/chrome/browser/ui/views/yee/sidebar_footer.cc" \
  "$fake_chromium_src/chrome/browser/ui/views/yee/sidebar_footer.cc"; then
  print -u2 "FAIL: Yee UI sources were not synchronized before a build."
  exit 1
fi

print "PASS: Yee build preflight detects stale bundles and synchronizes UI sources."
