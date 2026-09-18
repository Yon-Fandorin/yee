#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

print_paths
print "free space:     $(available_gib) GiB"
print "architecture:   $(uname -m)"
print "macOS:          $(sw_vers -productVersion) ($(sw_vers -buildVersion))"
print "developer dir:  $(xcode-select -p)"
print "Xcode:          $(xcodebuild -version | paste -sd ' ' -)"

SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
print "macOS SDK:      $SDK_ROOT"

if METAL_BIN="$(resolve_metal_bin)"; then
  print "Metal compiler: $METAL_BIN"
else
  print "Metal compiler: optional toolchain is not installed"
fi

for command_name in git python3 xcrun; do
  if ! command -v "$command_name" >/dev/null; then
    print -u2 "Missing required command: $command_name"
    exit 7
  fi
done

if [[ "$(uname -s)" != "Darwin" ]]; then
  print -u2 "Chromium macOS build scripts require Darwin."
  exit 8
fi

if [[ -x "$DEPOT_TOOLS_DIR/gclient" ]]; then
  print "depot_tools:    ready"
else
  print "depot_tools:    not installed yet"
fi

if [[ -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
  print "checkout:       ready"
  print "shallow source: $(git -C "$CHROMIUM_SRC" rev-parse --is-shallow-repository)"
else
  print "checkout:       not installed yet"
fi
