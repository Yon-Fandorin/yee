#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
CHECK_ONLY=0
SKIP_BRAND_ASSETS=0
if (( $# > 0 )); then shift; fi
for option in "$@"; do
  case "$option" in
    --check) CHECK_ONLY=1 ;;
    --skip-brand-assets) SKIP_BRAND_ASSETS=1 ;;
    *) print -u2 "Unknown overlay option: $option"; exit 2 ;;
  esac
done
check_args=()
(( CHECK_ONLY )) && check_args+=(--check)

if [[ -z "$CHROMIUM_SRC" ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src [--check] [--skip-brand-assets]"
  exit 2
fi

if [[ "$CHROMIUM_SRC" != /* ]]; then
  print -u2 "Chromium src path must be absolute: $CHROMIUM_SRC"
  exit 2
fi

if [[ ! -f "$CHROMIUM_SRC/chrome/browser/ui/tabs/tab_strip_prefs.cc" || \
      ! -f "$CHROMIUM_SRC/chrome/app/theme/chromium/BRANDING" ]]; then
  print -u2 "Not a Chromium src checkout: $CHROMIUM_SRC"
  exit 2
fi

# Preflight every input and every patch before the first checkout write.
python3 "$SCRIPT_DIR/brand_config.py" show >/dev/null
python3 "$SCRIPT_DIR/brand_config.py" install "$CHROMIUM_SRC" --check >/dev/null
"$SCRIPT_DIR/install-yee-ui-sources.sh" "$CHROMIUM_SRC" --check
python3 "$SCRIPT_DIR/lib/overlay_tools.py" patches "$CHROMIUM_SRC" "${check_args[@]}"
python3 "$SCRIPT_DIR/brand_config.py" install "$CHROMIUM_SRC" "${check_args[@]}"
if (( ! CHECK_ONLY )); then
  "$SCRIPT_DIR/install-yee-ui-sources.sh" "$CHROMIUM_SRC"
fi
if (( ! SKIP_BRAND_ASSETS )); then
  "$SCRIPT_DIR/install-brand-assets.sh" "$CHROMIUM_SRC" "${check_args[@]}"
fi

PRODUCT_NAME="$(python3 "$SCRIPT_DIR/brand_config.py" show --get name)"
if (( CHECK_ONLY )); then
  print "$PRODUCT_NAME Chromium overlay is compatible with $CHROMIUM_SRC"
else
  print "$PRODUCT_NAME Chromium overlay is ready in $CHROMIUM_SRC"
fi
