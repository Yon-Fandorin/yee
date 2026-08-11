# Chromium source overlay

This directory contains the first in-tree `yee` checkpoint for an upstream
Chromium checkout. It does not introduce another tab model. It opts new profiles
into Chromium's existing native vertical-tab implementation, which already
observes `TabStripModel` and renders real `WebContents` tabs.

## Apply to a Chromium checkout

```sh
./chromium-overlay/apply.sh /absolute/path/to/chromium/src
```

The script checks each patch before applying it, skips patches that are already
present, and refuses mismatched target files. The overlay currently changes:

- `prefs::kVerticalTabsEnabled`: `false` → `true` for new profiles
- `kVerticalTabStripDefaultUncollapsedWidth`: `240` → `232`
- Chromium product and installer names to `Yee`, producing `Yee.app` on macOS
- macOS app icon and Chromium product logos to the v8c Yee dinosaur mark

The brand asset installer crops the presentation whitespace from
`assets/brand/yee-logo-v8c-dino-nubs.png`, generates the macOS iconset/ICNS, and
updates Chromium's bundled product-logo sizes. It uses the macOS-native `sips`
tool and Chromium's own lightweight ICNS packer, so the output is reproducible
without committing generated Chromium binaries.

This keeps the first implementation boundary small and upstream-aligned. It
does not yet implement the custom Tenant/Workspace launcher shelf. That belongs
in a separate patch touching the top toolbar/Views composition after this native
tab checkpoint is built and verified.

## Build configuration

After applying the patch from the Chromium `src` directory:

```sh
gn gen out/YeePilot --args="$(tr '\n' ' ' < /path/to/yee/chromium-overlay/args.gn)"
autoninja -C out/YeePilot chrome
out/YeePilot/Yee.app/Contents/MacOS/Yee \
  --user-data-dir=/tmp/yee-chromium-profile \
  --no-first-run
```

The generated checkout and build are placed in Git-ignored `.local-build/`, not
in repository history. [`../chromium-dev/`](../chromium-dev/) wraps Chromium's
official `depot_tools`, shallow fetch, GN, and Ninja flow with disk guards and a
headless renderer smoke test.
