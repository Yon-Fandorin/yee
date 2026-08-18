#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
SOURCE_ROOT="$SCRIPT_DIR/yee-ui/chrome/browser/ui/views/yee"
DESTINATION_ROOT="$CHROMIUM_SRC/chrome/browser/ui/views/yee"

if [[ -z "$CHROMIUM_SRC" || ! -f "$CHROMIUM_SRC/chrome/browser/ui/BUILD.gn" ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src"
  exit 2
fi

mkdir -p "$DESTINATION_ROOT"

for source_file in "$SOURCE_ROOT"/*; do
  destination_file="$DESTINATION_ROOT/${source_file:t}"
  if [[ -f "$destination_file" ]] && cmp -s "$source_file" "$destination_file"; then
    continue
  fi
  install -m 0644 "$source_file" "$destination_file"
  print "Synced Yee UI source: ${source_file:t}"
done
