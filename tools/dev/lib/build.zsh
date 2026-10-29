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
  configure_yee_build_cache
  print "Building ${suite_name} regression targets."
  (
    cd "$CHROMIUM_SRC"
    caffeinate -dimsu nice -n 10 \
      autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" "$@"
  )
}

# 실행과 같은 바이너리·인수로 목록을 확인해 비어 있는 회귀 검사의 성공을 막는다.
run_native_test_gate() {
  local test_binary="$1"
  shift
  local argument test_filter='' filter_count=0
  for argument in "$@"; do
    if [[ "$argument" == --gtest_filter=* ]]; then
      test_filter="${argument#--gtest_filter=}"
      (( filter_count += 1 ))
    fi
  done
  if (( filter_count != 1 )); then
    print -u2 'Native 검사는 명시한 --gtest_filter= 인수 하나가 필요하다.'
    return 2
  fi

  local test_listing list_result
  if test_listing="$("$test_binary" "$@" --gtest_list_tests)"; then
    print -r -- "$test_listing" | \
      python3 "$COMMON_DIR/lib/native_test_discovery.py" --filter="$test_filter" || return $?
  else
    list_result=$?
    print -u2 "Native 테스트 목록 조회 실패 (${list_result}): $test_binary"
    return "$list_result"
  fi
  "$test_binary" "$@"
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
  sync_yee_branding
  sync_yee_ui_sources
  if [[ ! -f "$YEE_OUT_DIR/build.ninja" ]]; then
    "$COMMON_DIR/configure.sh"
  fi
}
