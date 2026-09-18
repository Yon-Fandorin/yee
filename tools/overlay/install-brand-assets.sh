#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
YEE_ROOT="${SCRIPT_DIR:h:h}"
CHROMIUM_SRC="${1:-}"
CHECK_ONLY="${2:-}"
brand_values=("${(@f)$(python3 "$SCRIPT_DIR/brand_config.py" show --format lines)}")
PRODUCT_NAME="$brand_values[1]"
LOGO_SOURCE="$brand_values[3]"
LOGO_CROP_SIZE="$brand_values[4]"

if [[ -z "$CHROMIUM_SRC" || "$CHROMIUM_SRC" != /* ||
      ( -n "$CHECK_ONLY" && "$CHECK_ONLY" != --check ) || $# -gt 2 ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src [--check]"
  exit 2
fi

if [[ ! -f "$CHROMIUM_SRC/chrome/app/theme/chromium/BRANDING" ]]; then
  print -u2 "Not a Chromium src checkout: $CHROMIUM_SRC"
  exit 2
fi

if [[ ! -f "$LOGO_SOURCE" ]]; then
  print -u2 "$PRODUCT_NAME logo source is missing: $LOGO_SOURCE"
  exit 3
fi

if [[ "$(uname -s)" != "Darwin" ]] || ! command -v sips >/dev/null || \
    ! command -v c++ >/dev/null; then
  print -u2 "$PRODUCT_NAME macOS brand assets require sips and a C++ compiler on macOS."
  exit 4
fi

if [[ "$CHECK_ONLY" == --check ]]; then
  print "$PRODUCT_NAME macOS brand asset inputs are available; rendering was skipped."
  exit 0
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/yee-brand-assets.XXXXXX")"
ICONSET_DIR="$WORK_DIR/Product.iconset"
MASTER_PNG="$WORK_DIR/yee-app-icon-master.png"
MAKEICNS_BIN="$WORK_DIR/makeicns"

cleanup() {
  if [[ -n "$WORK_DIR" && -d "$WORK_DIR" && "$WORK_DIR" == */yee-brand-assets.* ]]; then
    rm -rf "$WORK_DIR"
  fi
}
trap cleanup EXIT

mkdir -p "$ICONSET_DIR"

# The concept sheet has generous presentation whitespace. Crop it optically so
# the mark remains legible in the Dock and at favicon sizes.
sips --cropToHeightWidth "$LOGO_CROP_SIZE" "$LOGO_CROP_SIZE" "$LOGO_SOURCE" --out "$MASTER_PNG" >/dev/null
sips --resampleHeightWidth 1024 1024 "$MASTER_PNG" >/dev/null

render_png() {
  local size="$1"
  local destination="$2"

  mkdir -p "${destination:h}"
  sips --resampleHeightWidth "$size" "$size" "$MASTER_PNG" \
    --out "$destination" >/dev/null
}

render_png 16 "$ICONSET_DIR/icon_16x16.png"
render_png 32 "$ICONSET_DIR/icon_16x16@2x.png"
render_png 32 "$ICONSET_DIR/icon_32x32.png"
render_png 64 "$ICONSET_DIR/icon_32x32@2x.png"
render_png 128 "$ICONSET_DIR/icon_128x128.png"
render_png 256 "$ICONSET_DIR/icon_128x128@2x.png"
render_png 256 "$ICONSET_DIR/icon_256x256.png"
render_png 512 "$ICONSET_DIR/icon_256x256@2x.png"
render_png 512 "$ICONSET_DIR/icon_512x512.png"
render_png 1024 "$ICONSET_DIR/icon_512x512@2x.png"

c++ -Os \
  "$CHROMIUM_SRC/tools/mac/icons/additional_tools/makeicns.cc" \
  -o "$MAKEICNS_BIN"
"$MAKEICNS_BIN" "$ICONSET_DIR" \
  "$CHROMIUM_SRC/chrome/app/theme/chromium/mac/app.icns"

for size in 16 24 48 64 128 256; do
  render_png "$size" \
    "$CHROMIUM_SRC/chrome/app/theme/chromium/product_logo_${size}.png"
done

for size in 24 48 64 128 256; do
  render_png "$size" \
    "$CHROMIUM_SRC/chrome/app/theme/chromium/linux/product_logo_${size}.png"
done

render_png 32 \
  "$CHROMIUM_SRC/chrome/app/theme/chromium/chromeos/chrome_app_icon_32.png"
render_png 192 \
  "$CHROMIUM_SRC/chrome/app/theme/chromium/chromeos/chrome_app_icon_192.png"

print "Installed $PRODUCT_NAME app icon and product logos in $CHROMIUM_SRC"
