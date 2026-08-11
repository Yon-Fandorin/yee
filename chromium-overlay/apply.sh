#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="${0:A:h}"
CHROMIUM_SRC="${1:-}"
PATCH_FILES=(
  "$SCRIPT_DIR/patches/0001-enable-yee-vertical-shell-defaults.patch"
  "$SCRIPT_DIR/patches/0002-brand-yee-application.patch"
  "$SCRIPT_DIR/patches/0003-add-yee-shell-scaffold.patch"
  "$SCRIPT_DIR/patches/0004-place-yee-content-in-layout.patch"
  "$SCRIPT_DIR/patches/0005-replace-runway-with-native-toolbar.patch"
  "$SCRIPT_DIR/patches/0006-add-interactive-tab-sidebar.patch"
  "$SCRIPT_DIR/patches/0007-unify-tab-sidebar-motion.patch"
  "$SCRIPT_DIR/patches/0008-fade-pinned-tab-sidebar-with-motion.patch"
  "$SCRIPT_DIR/patches/0009-float-edge-tab-sidebar.patch"
  "$SCRIPT_DIR/patches/0010-refine-floating-tab-sidebar-surface.patch"
  "$SCRIPT_DIR/patches/0011-align-floating-tab-sidebar-hit-region.patch"
  "$SCRIPT_DIR/patches/0012-sync-floating-tab-sidebar-foreground-opacity.patch"
  "$SCRIPT_DIR/patches/0013-match-pilot-tabs-and-location-bar.patch"
  "$SCRIPT_DIR/patches/0014-fix-windows-protoc-python-aliases.patch"
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

# Later shell patches intentionally refine hunks introduced by earlier ones, so
# an early patch may no longer pass a standalone reverse check once the whole
# series is present. Find the newest applied shell patch first and treat the
# preceding shell series as applied through that point.
shell_series_applied_through=0
for patch_index in 13 12 11 10 9 8 7 6 5 4 3; do
  patch_file="${PATCH_FILES[$patch_index]}"
  if git -C "$CHROMIUM_SRC" apply --reverse --check "$patch_file" \
      >/dev/null 2>&1; then
    shell_series_applied_through=$patch_index
    break
  fi
done

for patch_index in {1..${#PATCH_FILES}}; do
  patch_file="${PATCH_FILES[$patch_index]}"
  if (( patch_index >= 3 &&
        patch_index <= shell_series_applied_through )); then
    print "Already applied: ${patch_file:t}"
    continue
  fi

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

"$SCRIPT_DIR/install-brand-assets.sh" "$CHROMIUM_SRC"

print "Yee Chromium overlay is ready in $CHROMIUM_SRC"
