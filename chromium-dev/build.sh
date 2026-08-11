#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
require_free_gib 35 "the Chromium chrome target"

if [[ "$YEE_BUILD_JOBS" != <-> || "$YEE_BUILD_JOBS" -lt 1 ]]; then
  print -u2 "YEE_BUILD_JOBS must be a positive integer: $YEE_BUILD_JOBS"
  exit 14
fi

if [[ ! -f "$YEE_OUT_DIR/build.ninja" ]]; then
  "$SCRIPT_DIR/configure.sh"
fi

# Xcode 26 can leave `xcrun metal` pointed at its stub after the optional Metal
# Toolchain is installed. Use the locally cached mounted-component path without
# copying or modifying Xcode files.
if ! YEE_METAL_BIN="$(resolve_metal_bin)"; then
  print -u2 "Metal Toolchain is missing. Install it with:"
  print -u2 "  ./chromium-dev/setup-metal.sh"
  exit 12
fi
export YEE_METAL_BIN
export PATH="$SCRIPT_DIR/shims:$PATH"

# Keep compiler/tool caches inside the ignored local build root. This avoids
# hidden growth in the user cache directory and works in restricted shells.
export XDG_CACHE_HOME="$LOCAL_BUILD_ROOT/cache"
export CLANG_MODULE_CACHE_PATH="$LOCAL_BUILD_ROOT/cache/clang/ModuleCache"
export GOCACHE="$LOCAL_BUILD_ROOT/cache/go-build"
export GOMODCACHE="$LOCAL_BUILD_ROOT/cache/go-mod"
export CARGO_HOME="$LOCAL_BUILD_ROOT/cache/cargo"
export npm_config_cache="$LOCAL_BUILD_ROOT/cache/npm"
export PIP_CACHE_DIR="$LOCAL_BUILD_ROOT/cache/pip"
mkdir -p \
  "$CLANG_MODULE_CACHE_PATH" \
  "$GOCACHE" \
  "$GOMODCACHE" \
  "$CARGO_HOME" \
  "$npm_config_cache" \
  "$PIP_CACHE_DIR"

cd "$CHROMIUM_SRC"
print "Building with $YEE_BUILD_JOBS parallel jobs at reduced process priority."
caffeinate -dimsu nice -n 10 \
  autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" chrome

print "Build complete. Free space: $(available_gib) GiB"
"$SCRIPT_DIR/usage.sh"
