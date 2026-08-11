# Native Chromium pilot

This is the smallest runnable checkpoint for `yee`: a real Chromium browser
window with native vertical tabs backed by Chromium's `TabStripModel` and live
`WebContents`.

It deliberately uses the locally installed current Google Chrome binary as a
fast checkpoint independent of the source build. It does not use Electron, CEF,
`<webview>`, or a simulated page surface.

## Run

macOS:

```sh
./native-pilot/launch.sh
```

Windows PowerShell:

```powershell
.\native-pilot\launch.ps1
```

The Windows launcher uses `%ProgramFiles%\Google\Chrome\Application\chrome.exe`
by default. Set `YEE_CHROME_BINARY` when Chrome is installed elsewhere.

The launcher creates an isolated browser profile in
`native-pilot/runtime/profile`, enables Chromium's native vertical-tabs feature,
enables the macOS 26 native `GlassFrame`, and asks Chromium's `ThemeService` to
generate the shell palette from the mint seed. The glass remains browser chrome;
web content stays opaque. It does not read or mutate the user's normal Chrome
profile.

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

The installed-Chrome harness still does not inject the custom shell controls.
The source-built Yee overlay carries the first native Views scaffold: leading
Tenant/Workspace identity, real omnibox, real vertical tabs, rounded content
surface, and a trailing Agent findings status. Use `chromium-dev/run.ps1` to
inspect it after building Chromium.
