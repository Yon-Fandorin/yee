#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
OPERATION="${2:-}"
if [[ -z "$CHROMIUM_SRC" || "$CHROMIUM_SRC" != /* ||
      ! -f "$CHROMIUM_SRC/chrome/app/theme/chromium/BRANDING" || $# -gt 2 ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src [--check|--prepare|--install-prepared]"
  exit 2
fi

case "$OPERATION" in
  '') asset_args=(--refresh) ;;
  --check) asset_args=(--check) ;;
  --prepare) asset_args=(--prepare) ;;
  --install-prepared) asset_args=() ;;
  *) print -u2 "Unknown brand asset operation: $OPERATION"; exit 2 ;;
esac
exec python3 "$SCRIPT_DIR/brand_assets.py" "$CHROMIUM_SRC" "${asset_args[@]}"
