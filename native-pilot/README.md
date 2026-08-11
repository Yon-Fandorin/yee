# Native Chromium pilot

This is the smallest runnable checkpoint for `yee`: a real Chromium browser
window with native vertical tabs backed by Chromium's `TabStripModel` and live
`WebContents`.

It deliberately uses the locally installed current Google Chrome binary as a
fast checkpoint independent of the source build. It does not use Electron, CEF,
`<webview>`, or a simulated page surface.

## Run

```sh
./native-pilot/launch.sh
```

The launcher creates an isolated browser profile in
`native-pilot/runtime/profile`, enables Chromium's native vertical-tabs feature,
and asks Chromium's `ThemeService` to generate the shell palette from the mint
porcelain seed. It does not read or mutate the user's normal Chrome profile.

The default window starts with three live pages so the vertical tab list is
immediately visible. To open a different set of initial pages:

```sh
./native-pilot/launch.sh https://chromium.org/ https://lit.dev/
```

## What this checkpoint proves

- The left sidebar owns real open tabs, not DOM mock rows.
- New, selected, closed, loading, and navigated tab state comes from Chromium.
- The content area is a real sandboxed Chromium renderer.
- The visual shell can start from the Arc/Aside-influenced mint direction
  without committing the product to Electron.

The custom Tenant/Workspace launcher shelf is not injected into Chrome by this
harness. That requires an in-tree Chromium Views/WebUI change and is the next
source-integration layer.
