# Metal helpers; loaded by common.zsh.

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
