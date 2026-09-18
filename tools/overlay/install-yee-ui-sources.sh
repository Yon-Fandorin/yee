#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
if [[ -z "$CHROMIUM_SRC" || ! -f "$CHROMIUM_SRC/chrome/browser/ui/BUILD.gn" ||
      ( -n "${2:-}" && "${2:-}" != --check ) || $# -gt 2 ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src [--check]"
  exit 2
fi
exec python3 "$SCRIPT_DIR/lib/overlay_tools.py" sources "$@"
