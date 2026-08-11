#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

mkdir -p "$LOCAL_BUILD_ROOT"

if [[ ! -d "$DEPOT_TOOLS_DIR/.git" ]]; then
  require_free_gib 120 "the initial Chromium checkout"
  print "Cloning depot_tools into $DEPOT_TOOLS_DIR"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
fi

require_depot_tools

if [[ -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
  print "Chromium checkout already exists at $CHROMIUM_SRC"
  print "Use ./chromium-dev/sync.sh to update it."
  exit 0
fi

if [[ -e "$CHROMIUM_ROOT/.gclient" || -e "$CHROMIUM_SRC" ]]; then
  print -u2 "Partial checkout detected at $CHROMIUM_ROOT."
  print -u2 "Resume it with: (cd '$CHROMIUM_ROOT' && '$DEPOT_TOOLS_DIR/gclient' sync --no-history)"
  exit 9
fi

require_free_gib 115 "the shallow Chromium source checkout"
mkdir -p "$CHROMIUM_ROOT"
cd "$CHROMIUM_ROOT"

print "Fetching Chromium without repository history or a 30 GiB git cache."
caffeinate -dimsu fetch --no-history chromium

print "Checkout complete: $CHROMIUM_SRC"
print "Free space: $(available_gib) GiB"
