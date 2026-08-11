#!/bin/zsh

set -euo pipefail

COMMON_DIR="${${(%):-%N}:A:h}"
YEE_ROOT="${COMMON_DIR:h}"
LOCAL_BUILD_ROOT="${YEE_LOCAL_BUILD_ROOT:-$YEE_ROOT/.local-build}"
DEPOT_TOOLS_DIR="$LOCAL_BUILD_ROOT/depot_tools"
CHROMIUM_ROOT="$LOCAL_BUILD_ROOT/chromium"
CHROMIUM_SRC="$CHROMIUM_ROOT/src"
YEE_OUT_NAME="YeePilot"
YEE_OUT_DIR="$CHROMIUM_SRC/out/$YEE_OUT_NAME"
YEE_PRODUCT_NAME="Yee"
YEE_APP_DIR="$YEE_OUT_DIR/$YEE_PRODUCT_NAME.app"
YEE_BROWSER_BIN="$YEE_APP_DIR/Contents/MacOS/$YEE_PRODUCT_NAME"
YEE_ARGS_FILE="$YEE_ROOT/chromium-overlay/args.gn"
METAL_TOOLCHAIN_CACHE="$LOCAL_BUILD_ROOT/metal-toolchain-path"
YEE_BUILD_JOBS="${YEE_BUILD_JOBS:-2}"

available_gib() {
  local available_kib
  available_kib="$(df -Pk "$YEE_ROOT" | awk 'NR == 2 { print $4 }')"
  print $((available_kib / 1024 / 1024))
}

require_free_gib() {
  local required_gib="$1"
  local purpose="$2"
  local current_gib
  current_gib="$(available_gib)"

  if (( current_gib < required_gib )); then
    print -u2 "Need at least ${required_gib} GiB free for ${purpose}; ${current_gib} GiB is available."
    exit 4
  fi

  print "Disk guard: ${current_gib} GiB free (${required_gib} GiB required for ${purpose})."
}

require_depot_tools() {
  if [[ ! -x "$DEPOT_TOOLS_DIR/gclient" ]]; then
    print -u2 "depot_tools is missing. Run ./chromium-dev/checkout.sh first."
    exit 5
  fi

  export PATH="$PATH:$DEPOT_TOOLS_DIR"
}

require_chromium_src() {
  if [[ ! -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
    print -u2 "Chromium source is missing. Run ./chromium-dev/checkout.sh first."
    exit 6
  fi
}

print_paths() {
  print "yee root:       $YEE_ROOT"
  print "depot_tools:    $DEPOT_TOOLS_DIR"
  print "Chromium src:   $CHROMIUM_SRC"
  print "build output:   $YEE_OUT_DIR"
}

resolve_metal_bin() {
  local cached_metal=""
  local metal_toolchain_root=""
  local discovered_metal=""

  if [[ -n "${YEE_METAL_BIN:-}" && -x "$YEE_METAL_BIN" ]]; then
    print -r -- "$YEE_METAL_BIN"
    return 0
  fi

  if [[ -f "$METAL_TOOLCHAIN_CACHE" ]]; then
    IFS= read -r cached_metal < "$METAL_TOOLCHAIN_CACHE"
    if [[ -x "$cached_metal" ]]; then
      print -r -- "$cached_metal"
      return 0
    fi
  fi

  if metal_toolchain_root="$(
    /usr/bin/xcodebuild -showComponent metalToolchain -json 2>/dev/null | \
      /usr/bin/plutil -extract toolchainSearchPath raw -o - - 2>/dev/null
  )"; then
    discovered_metal="$metal_toolchain_root/Metal.xctoolchain/usr/bin/metal"
    if [[ -x "$discovered_metal" ]]; then
      mkdir -p "$LOCAL_BUILD_ROOT"
      print -r -- "$discovered_metal" > "$METAL_TOOLCHAIN_CACHE"
      print -r -- "$discovered_metal"
      return 0
    fi
  fi

  return 1
}
