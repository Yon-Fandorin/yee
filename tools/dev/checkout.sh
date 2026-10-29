#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"
require_yee_sync_jobs

mkdir -p "$LOCAL_BUILD_ROOT"

if [[ ! -d "$DEPOT_TOOLS_DIR/.git" ]]; then
  require_free_gib 120 "the initial Chromium checkout"
  print "Cloning depot_tools into $DEPOT_TOOLS_DIR"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
fi

require_depot_tools
REVISION="$(python3 "$SCRIPT_DIR/chromium-version.py" show --get revision)"
SOURCE_URL="$(python3 "$SCRIPT_DIR/chromium-version.py" show --get source_url)"

if [[ -f "$CHROMIUM_SRC/BUILD.gn" ]]; then
  require_chromium_src
  print "Chromium checkout already exists at $CHROMIUM_SRC"
  print "Use ./tools/dev/sync.sh to update it."
  exit 0
fi

if [[ -e "$CHROMIUM_ROOT/.gclient" || -e "$CHROMIUM_SRC" ]]; then
  print -u2 "Partial checkout detected at $CHROMIUM_ROOT."
  print -u2 "Resume it with: (cd '$CHROMIUM_ROOT' && '$DEPOT_TOOLS_DIR/gclient' sync --no-history --revision 'src@$REVISION' --jobs '$YEE_SYNC_JOBS')"
  exit 9
fi

require_free_gib 115 "the shallow Chromium source checkout"
mkdir -p "$CHROMIUM_ROOT"
cd "$CHROMIUM_ROOT"

print "Fetching the pinned Chromium revision $REVISION without repository history or a separate git cache."
gclient config --name src --unmanaged "$SOURCE_URL"
if ! caffeinate -dimsu gclient sync --no-history --revision "src@$REVISION" --jobs "$YEE_SYNC_JOBS"; then
  print -u2 "Resume in '$CHROMIUM_ROOT' with: gclient sync --no-history --revision 'src@$REVISION' --jobs '$YEE_SYNC_JOBS'"
  exit 9
fi
require_chromium_src

print "Checkout complete: $CHROMIUM_SRC"
print "Free space: $(available_gib) GiB"
