#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

if ! METAL_TOOLCHAIN_ROOT="$(
  /usr/bin/xcodebuild -showComponent metalToolchain -json 2>/dev/null | \
    /usr/bin/plutil -extract toolchainSearchPath raw -o - - 2>/dev/null
)"; then
  print "Downloading Xcode's optional Metal Toolchain."
  /usr/bin/xcodebuild -downloadComponent MetalToolchain
  METAL_TOOLCHAIN_ROOT="$(
    /usr/bin/xcodebuild -showComponent metalToolchain -json | \
      /usr/bin/plutil -extract toolchainSearchPath raw -o - -
  )"
fi

METAL_BIN="$METAL_TOOLCHAIN_ROOT/Metal.xctoolchain/usr/bin/metal"
if [[ ! -x "$METAL_BIN" ]]; then
  print -u2 "Xcode reports a Metal Toolchain, but its compiler is missing: $METAL_BIN"
  exit 13
fi

mkdir -p "$LOCAL_BUILD_ROOT"
print -r -- "$METAL_BIN" > "$METAL_TOOLCHAIN_CACHE"

print "Metal compiler cached for Chromium builds:"
print "  $METAL_BIN"
"$METAL_BIN" --version
