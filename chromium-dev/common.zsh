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
YEE_UNBUNDLED_FRAMEWORK_BIN="$YEE_OUT_DIR/$YEE_PRODUCT_NAME Framework.framework/$YEE_PRODUCT_NAME Framework"
YEE_BUNDLED_FRAMEWORK_BIN="$YEE_APP_DIR/Contents/Frameworks/$YEE_PRODUCT_NAME Framework.framework/$YEE_PRODUCT_NAME Framework"
YEE_ARGS_FILE="$YEE_ROOT/chromium-overlay/args.gn"
METAL_TOOLCHAIN_CACHE="$LOCAL_BUILD_ROOT/metal-toolchain-path"
YEE_BUILD_JOBS="${YEE_BUILD_JOBS:-2}"

integrated_yee_app_is_current() {
  local unbundled_framework="${1:-$YEE_UNBUNDLED_FRAMEWORK_BIN}"
  local bundled_framework="${2:-$YEE_BUNDLED_FRAMEWORK_BIN}"

  if [[ ! -e "$unbundled_framework" ]]; then
    return 0
  fi
  if [[ ! -e "$bundled_framework" ||
        "$unbundled_framework" -nt "$bundled_framework" ]]; then
    return 1
  fi
  return 0
}

require_integrated_yee_app_current() {
  if integrated_yee_app_is_current; then
    return
  fi

  print -u2 "Built Yee.app is older than the latest linked Yee Framework."
  print -u2 "Run ./chromium-dev/build.sh before launching the real app."
  return 11
}

sync_yee_ui_sources() {
  local yee_destination="$CHROMIUM_SRC/chrome/browser/ui/views/yee/BUILD.gn"
  if [[ -f "$yee_destination" ]]; then
    "$YEE_ROOT/chromium-overlay/install-yee-ui-sources.sh" "$CHROMIUM_SRC"
  else
    "$YEE_ROOT/chromium-overlay/apply.sh" "$CHROMIUM_SRC"
  fi
}

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

configure_regression_build_cache() {
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
}

build_regression_targets() {
  local suite_name="$1"
  shift
  configure_regression_build_cache
  print "Building ${suite_name} regression targets."
  (
    cd "$CHROMIUM_SRC"
    caffeinate -dimsu nice -n 10 \
      autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" "$@"
  )
}

gracefully_quit_yee() {
  if ! pgrep -f -- "$YEE_BROWSER_BIN" >/dev/null 2>&1; then
    return
  fi

  print "Requesting a graceful shutdown of the running Yee app."
  osascript -e 'tell application "Yee" to quit'
  for attempt in {1..100}; do
    if ! pgrep -f -- "$YEE_BROWSER_BIN" >/dev/null 2>&1; then
      return
    fi
    sleep 0.1
  done

  print -u2 "Yee did not exit after the graceful shutdown request."
  exit 15
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
