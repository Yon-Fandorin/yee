#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
CHECK_ONLY="${2:-}"
if [[ -z "$CHROMIUM_SRC" || "$CHROMIUM_SRC" != /* ||
      ! -f "$CHROMIUM_SRC/chrome/app/theme/chromium/BRANDING" ||
      ( -n "$CHECK_ONLY" && "$CHECK_ONLY" != --check ) || $# -gt 2 ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src [--check]"
  exit 2
fi

# Validate configuration and version inputs before any mutation of the checkout.
python3 "$SCRIPT_DIR/brand_config.py" show >/dev/null
python3 "$SCRIPT_DIR/brand_config.py" install "$CHROMIUM_SRC" --check >/dev/null
check_args=()
[[ "$CHECK_ONLY" == --check ]] && check_args+=(--check)
python3 "$SCRIPT_DIR/lib/overlay_tools.py" patches "$CHROMIUM_SRC" --role branding "${check_args[@]}"
python3 "$SCRIPT_DIR/brand_config.py" install "$CHROMIUM_SRC" "${check_args[@]}"
