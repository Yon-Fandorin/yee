#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
# Default remains conservative. An explicitly approved incremental build may
# lower the threshold, but never below the supported 15 GiB safety floor.
YEE_BUILD_MIN_FREE_GIB="${YEE_BUILD_MIN_FREE_GIB:-35}"
if [[ "$YEE_BUILD_MIN_FREE_GIB" != <-> ]] || (( YEE_BUILD_MIN_FREE_GIB < 15 )); then
  print -u2 "YEE_BUILD_MIN_FREE_GIB must be an integer of at least 15."
  exit 14
fi
require_free_gib "$YEE_BUILD_MIN_FREE_GIB" "the Chromium chrome target"

prepare_yee_build_inputs

# Xcode 26 can leave `xcrun metal` pointed at its stub after the optional Metal
# Toolchain is installed. Use the locally cached mounted-component path without
# copying or modifying Xcode files.
if ! YEE_METAL_BIN="$(resolve_metal_bin)"; then
  print -u2 "Metal Toolchain is missing. Install it with:"
  print -u2 "  ./tools/dev/setup-metal.sh"
  exit 12
fi
export YEE_METAL_BIN
export PATH="$SCRIPT_DIR/shims:$PATH"

# Keep compiler/tool caches inside the ignored local build root. This avoids
# hidden growth in the user cache directory and works in restricted shells.
configure_yee_build_cache

cd "$CHROMIUM_SRC"
print "Building with $YEE_BUILD_JOBS parallel jobs at reduced process priority."
caffeinate -dimsu nice -n 10 \
  autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" chrome

print "Build complete. Free space: $(available_gib) GiB"
"$SCRIPT_DIR/usage.sh"
