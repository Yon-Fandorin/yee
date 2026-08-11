#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
source "$SCRIPT_DIR/common.zsh"

print "Free filesystem space: $(available_gib) GiB"

for measured_path in "$DEPOT_TOOLS_DIR" "$CHROMIUM_ROOT" "$YEE_OUT_DIR"; do
  if [[ -e "$measured_path" ]]; then
    du -sh "$measured_path"
  fi
done
