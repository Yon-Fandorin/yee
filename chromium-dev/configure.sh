#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
require_free_gib 45 "GN generation and the compact Chromium build"

"$YEE_ROOT/chromium-overlay/apply.sh" "$CHROMIUM_SRC"

GN_ARGS="$(awk 'NF && $1 !~ /^#/' "$YEE_ARGS_FILE" | paste -sd ' ' -)"
cd "$CHROMIUM_SRC"
gn gen "out/$YEE_OUT_NAME" --args="$GN_ARGS"

print "Configured compact output at $YEE_OUT_DIR"
gn args "out/$YEE_OUT_NAME" --list --short | \
  grep -E '^(is_debug|is_component_build|symbol_level|is_official_build|use_lld) ='
