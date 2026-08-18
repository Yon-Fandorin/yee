#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
PATCH_FILES=(
  "$SCRIPT_DIR/patches/0001-integrate-yee-shell.patch"
  "$SCRIPT_DIR/patches/0002-brand-yee-application.patch"
  "$SCRIPT_DIR/patches/0003-fix-windows-protoc-python-aliases.patch"
)

if [[ -z "$CHROMIUM_SRC" ]]; then
  print -u2 "usage: $0 /absolute/path/to/chromium/src"
  exit 2
fi

if [[ "$CHROMIUM_SRC" != /* ]]; then
  print -u2 "Chromium src path must be absolute: $CHROMIUM_SRC"
  exit 2
fi

if [[ ! -f "$CHROMIUM_SRC/chrome/browser/ui/tabs/tab_strip_prefs.cc" || \
      ! -f "$CHROMIUM_SRC/chrome/app/theme/chromium/BRANDING" ]]; then
  print -u2 "Not a Chromium src checkout: $CHROMIUM_SRC"
  exit 2
fi

for patch_file in "${PATCH_FILES[@]}"; do
  if git -C "$CHROMIUM_SRC" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
    print "Already applied: ${patch_file:t}"
    continue
  fi

  if ! git -C "$CHROMIUM_SRC" apply --check "$patch_file"; then
    print -u2 "Cannot apply ${patch_file:t}; its target files have unexpected changes."
    exit 3
  fi

  git -C "$CHROMIUM_SRC" apply "$patch_file"
  print "Applied: ${patch_file:t}"
done

"$SCRIPT_DIR/install-yee-ui-sources.sh" "$CHROMIUM_SRC"
"$SCRIPT_DIR/install-brand-assets.sh" "$CHROMIUM_SRC"

print "Yee Chromium overlay is ready in $CHROMIUM_SRC"
