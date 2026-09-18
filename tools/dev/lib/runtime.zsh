# Runtime helpers; loaded by common.zsh.

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
  if ! integrated_yee_app_is_current; then
    print -u2 "Built $YEE_PRODUCT_NAME.app is older than the latest linked $YEE_PRODUCT_NAME Framework."
    print -u2 "Run ./tools/dev/build.sh before launching the real app."
    return 11
  fi

  if [[ -x "$YEE_BROWSER_BIN" ]]; then
    return
  fi

  print -u2 "Built $YEE_PRODUCT_NAME.app is incomplete: its executable is missing."
  print -u2 "Run ./tools/dev/build.sh before launching the real app."
  return 11
}

gracefully_quit_yee() {
  local browser_binary process_pattern
  local -a browser_binaries running_binaries
  browser_binaries=("${(@f)$(python3 "$COMMON_DIR/browser_bundle_executables.py" "$YEE_OUT_DIR")}")
  running_binaries=()
  for browser_binary in "${browser_binaries[@]}"; do
    [[ -z "$browser_binary" ]] && continue
    process_pattern="$(python3 -c 'import re, sys; print(re.escape(sys.argv[1]))' "$browser_binary")"
    if pgrep -f -- "$process_pattern" >/dev/null 2>&1; then
      running_binaries+=("$browser_binary")
    fi
  done
  if (( ${#running_binaries[@]} == 0 )); then
    return
  fi

  print "Requesting graceful shutdown of browser bundles in $YEE_OUT_DIR."
  local shutdown_dir="$LOCAL_BUILD_ROOT/yee-shutdown-helper"
  local shutdown_source="$COMMON_DIR/gracefully-quit-yee.swift"
  local shutdown_binary="$shutdown_dir/gracefully-quit-yee"
  mkdir -p "$shutdown_dir/module-cache"
  if [[ ! -x "$shutdown_binary" || "$shutdown_source" -nt "$shutdown_binary" ]]; then
    xcrun swiftc -module-cache-path "$shutdown_dir/module-cache" \
      "$shutdown_source" -o "$shutdown_binary"
  fi
  for browser_binary in "${running_binaries[@]}"; do
    # The native helper verifies the exact executable, bundle URL, and stable ID.
    "$shutdown_binary" "$browser_binary"
    process_pattern="$(python3 -c 'import re, sys; print(re.escape(sys.argv[1]))' "$browser_binary")"
    local exited=0
    for attempt in {1..100}; do
      if ! pgrep -f -- "$process_pattern" >/dev/null 2>&1; then
        exited=1
        break
      fi
      sleep 0.1
    done
    if (( ! exited )); then
      print -u2 "Browser did not exit after the graceful shutdown request: $browser_binary"
      exit 15
    fi
  done
}
