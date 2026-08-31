#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
require_free_gib 5 "the isolated Yee UI target"

if [[ "$YEE_BUILD_JOBS" != <-> || "$YEE_BUILD_JOBS" -lt 1 ]]; then
  print -u2 "YEE_BUILD_JOBS must be a positive integer: $YEE_BUILD_JOBS"
  exit 14
fi

sync_yee_ui_sources

if [[ ! -f "$YEE_OUT_DIR/build.ninja" ]]; then
  "$SCRIPT_DIR/configure.sh"
fi

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
print "Building the isolated Yee UI target with $YEE_BUILD_JOBS parallel jobs."
caffeinate -dimsu nice -n 10 \
  autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" \
    chrome/browser/ui/views/yee:yee_ui

print "Yee UI target complete. Run ./chromium-dev/build.sh only when an integrated app is needed."
