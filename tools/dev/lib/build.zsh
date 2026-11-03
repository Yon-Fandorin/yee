# Build helpers; loaded by common.zsh.

sync_yee_branding() {
  "$YEE_ROOT/tools/overlay/install-branding.sh" "$CHROMIUM_SRC"
  "$YEE_ROOT/tools/overlay/install-brand-assets.sh" "$CHROMIUM_SRC"
}

sync_yee_ui_sources() {
  local yee_destination="$CHROMIUM_SRC/chrome/browser/ui/views/yee/BUILD.gn"
  if [[ -f "$yee_destination" ]]; then
    "$YEE_ROOT/tools/overlay/install-yee-ui-sources.sh" "$CHROMIUM_SRC"
  else
    "$YEE_ROOT/tools/overlay/apply.sh" "$CHROMIUM_SRC"
  fi
}

configure_yee_build_cache() {
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
  require_yee_output_selection || return $?
  configure_yee_build_cache || return $?
  print "Building ${suite_name} regression targets."
  (
    cd "$CHROMIUM_SRC" || return $?
    caffeinate -dimsu nice -n 10 \
      autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" "$@"
  )
}

# 선택한 실행 파일의 발견 검사·실행·XML 대조와 소스·바이너리 기록을 함께 남긴다.
run_native_test_gate() {
  local test_binary="$1"
  shift
  require_yee_build_root_selection || return $?
  require_yee_output_selection || return $?
  if [[ ! -f "$test_binary" || ! -x "$test_binary" ]]; then
    print -u2 -r -- "Native 테스트 바이너리가 없거나 실행할 수 없다: $test_binary (선택한 out: $YEE_OUT_DIR)"
    return 20
  fi
  python3 "$COMMON_DIR/lib/native_gate_record.py" \
    --repo-root "$YEE_ROOT" --build-root "$LOCAL_BUILD_ROOT" \
    --out-dir "$YEE_OUT_DIR" --out-name "$YEE_OUT_NAME" \
    --root-explicit "$YEE_LOCAL_BUILD_ROOT_EXPLICIT" --out-explicit "$YEE_OUT_NAME_EXPLICIT" \
    --pid "$$" -- "$test_binary" "$@"
}

# Compatibility for existing focused regression gates.
configure_regression_build_cache() {
  configure_yee_build_cache
}

require_yee_build_jobs() {
  if [[ "$YEE_BUILD_JOBS" != <-> || "$YEE_BUILD_JOBS" -lt 1 ]]; then
    print -u2 "YEE_BUILD_JOBS must be a positive integer: $YEE_BUILD_JOBS"
    return 14
  fi
}

prepare_yee_build_inputs() {
  require_yee_build_jobs
  require_yee_output_selection || return $?
  sync_yee_branding
  sync_yee_ui_sources
  if [[ ! -f "$YEE_OUT_DIR/build.ninja" ]]; then
    "$COMMON_DIR/configure.sh"
  fi
}
