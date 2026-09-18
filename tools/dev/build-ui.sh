#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
require_free_gib 5 "the isolated $YEE_PRODUCT_NAME UI target"

prepare_yee_build_inputs

configure_yee_build_cache

cd "$CHROMIUM_SRC"
print "Building the isolated $YEE_PRODUCT_NAME UI target with $YEE_BUILD_JOBS parallel jobs."
caffeinate -dimsu nice -n 10 \
  autoninja -C "out/$YEE_OUT_NAME" -j "$YEE_BUILD_JOBS" \
    chrome/browser/ui/views/yee:yee_ui

print "$YEE_PRODUCT_NAME UI target complete. Run ./tools/dev/build.sh only when an integrated app is needed."
