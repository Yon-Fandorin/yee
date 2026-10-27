#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

if [[ "${1:-}" == --skip-build && $# == 1 ]]; then
  shift
elif (( $# == 0 )); then
  "$SCRIPT_DIR/build.sh"
else
  print -u2 "usage: $0 [--skip-build]"
  exit 2
fi

require_free_gib 15 "the macOS test disk image"
require_integrated_yee_app_current
configure_yee_build_cache

PACKAGING_ENV="$LOCAL_BUILD_ROOT/macos-packaging-venv"
if [[ ! -x "$PACKAGING_ENV/bin/python" ]]; then
  python3 -m venv "$PACKAGING_ENV"
fi
"$PACKAGING_ENV/bin/python" -m pip install \
  --disable-pip-version-check -r "$SCRIPT_DIR/requirements-macos-packaging.txt"

exec "$PACKAGING_ENV/bin/python" "$SCRIPT_DIR/lib/macos_release.py" \
  --app "$YEE_APP_DIR" --chromium-src "$CHROMIUM_SRC" \
  --release-root "$LOCAL_BUILD_ROOT/releases"
