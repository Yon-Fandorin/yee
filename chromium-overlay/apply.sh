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
  "$SCRIPT_DIR/patches/0014-port-pilot-tabs-to-latest-chromium.patch"
  "$SCRIPT_DIR/patches/0015-fix-windows-protoc-python-aliases.patch"
  "$SCRIPT_DIR/patches/0016-match-pilot-glass-material.patch"
  "$SCRIPT_DIR/patches/0017-reserve-native-caption-controls.patch"
  "$SCRIPT_DIR/patches/0018-unify-rounded-content-surface.patch"
  "$SCRIPT_DIR/patches/0019-use-native-shell-layout-geometry.patch"
  "$SCRIPT_DIR/patches/0020-unify-pinned-sidebar-and-content-gutter.patch"
  "$SCRIPT_DIR/patches/0021-integrate-non-sidebar-shell-layout.patch"
  "$SCRIPT_DIR/patches/0022-let-explicit-content-own-corner-material.patch"
  "$SCRIPT_DIR/patches/0023-fit-native-caption-buttons-to-titlebar.patch"
  "$SCRIPT_DIR/patches/0024-match-latest-pilot-toolbar.patch"
  "$SCRIPT_DIR/patches/0025-suppress-focused-window-border.patch"
  "$SCRIPT_DIR/patches/0026-remove-restored-window-frame-bands.patch"
  "$SCRIPT_DIR/patches/0027-refine-pilot-omnibox.patch"
  "$SCRIPT_DIR/patches/0028-restore-borderless-window-shadow.patch"
  "$SCRIPT_DIR/patches/0029-match-pilot-sidebar-sections.patch"
  "$SCRIPT_DIR/patches/0030-align-pilot-shell-separators.patch"
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
shell_patch_indexes=(1 3 4 5 6 7 8 9 10 11 12 13 14 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30)
for patch_index in 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16 14 13 12 11 10 9 8 7 6 5 4 3 1; do
  patch_file="${PATCH_FILES[$patch_index]}"
  patch_args=()
  if [[ "${patch_file:t}" == "0013-match-pilot-tabs-and-location-bar.patch" ]]; then
    patch_args+=(
      --exclude=chrome/browser/ui/views/tabs/common/tab_view.cc
      --exclude=chrome/browser/ui/views/tabs/common/tab_view.h
    )
  fi
  if git -C "$CHROMIUM_SRC" apply "${patch_args[@]}" --reverse --check "$patch_file" \
      >/dev/null 2>&1; then
    shell_series_applied_through=$patch_index
    break
  fi
done

for patch_index in {1..${#PATCH_FILES}}; do
  patch_file="${PATCH_FILES[$patch_index]}"
  patch_args=()
  if [[ "${patch_file:t}" == "0013-match-pilot-tabs-and-location-bar.patch" ]]; then
    patch_args+=(
      --exclude=chrome/browser/ui/views/tabs/common/tab_view.cc
      --exclude=chrome/browser/ui/views/tabs/common/tab_view.h
    )
  fi
  is_shell_patch=0
  for shell_patch_index in "${shell_patch_indexes[@]}"; do
    if (( patch_index == shell_patch_index )); then
      is_shell_patch=1
      break
    fi
  done
  if (( is_shell_patch && patch_index <= shell_series_applied_through )); then
    print "Already applied: ${patch_file:t}"
    continue
  fi

  if git -C "$CHROMIUM_SRC" apply "${patch_args[@]}" --reverse --check "$patch_file" >/dev/null 2>&1; then
    print "Already applied: ${patch_file:t}"
    continue
  fi

  if ! git -C "$CHROMIUM_SRC" apply "${patch_args[@]}" --check "$patch_file"; then
    print -u2 "Cannot apply ${patch_file:t}; its target files have unexpected changes."
    exit 3
  fi

  git -C "$CHROMIUM_SRC" apply "${patch_args[@]}" "$patch_file"
  print "Applied: ${patch_file:t}"
done

"$SCRIPT_DIR/install-brand-assets.sh" "$CHROMIUM_SRC"

print "Yee Chromium overlay is ready in $CHROMIUM_SRC"
