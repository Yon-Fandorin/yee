#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

require_depot_tools
require_chromium_src
require_free_gib 35 "a Chromium dependency sync"

cd "$CHROMIUM_ROOT"
caffeinate -dimsu gclient sync --no-history

print "Sync complete. Free space: $(available_gib) GiB"
