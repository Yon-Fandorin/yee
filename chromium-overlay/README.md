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
- 직접 실행해도 기본으로 켜지는 content-free Yee 레이아웃 구획판
- 첫 번째 titlebar product slot의 2줄 Context Switcher UI contract
- 실제 `ToolbarView`를 재배치한 sidebar toggle, navigation, Omnibox, extension dock
- 버튼으로 고정/해제되고 왼쪽 끝에서 flyout되는 native Tab sidebar
- 파일럿 토큰을 1차 이식한 42px 2줄 탭과 36px 글라스 주소창

The brand asset installer crops the presentation whitespace from
`assets/brand/yee-logo-v8c-dino-nubs.png`, generates the macOS iconset/ICNS, and
updates Chromium's bundled product-logo sizes. It uses the macOS-native `sips`
tool and Chromium's own lightweight ICNS packer, so the output is reproducible
without committing generated Chromium binaries.

The scaffold is intentionally structural: 64px titlebar, 232px sidebar, and an
8px gutter. The first product slot shows a two-line tenant/workspace Context
Switcher contract; its data source and interaction are deliberately deferred.
The central runway is Chromium's real `ToolbarView`, compactly laid out as
sidebar toggle, back, forward, reload, divider, native Omnibox, and extension
dock. It does not paint a second imitation toolbar over Chromium controls. The
existing `MultiContentsView` is retained and placed inside the Browser Content
region. The Yee shell is the application default so Finder, Dock, and test-tool
launches share the same composition. `--disable-yee-shell-scaffold` explicitly
restores the stock Chromium composition for comparison.

The sidebar has three deliberate layout states. Expanded reserves its native
232px width plus the 8px content gutter. Collapsed leaves only an invisible 8px
edge target and gives that width back to `MultiContentsView`. Hovering the edge
uses Chromium's native expand-on-hover animation as a glass flyout, so it does
not resize the page beneath it. The renderer viewport is derived from the same
proposed bounds as the visible content container; a parent resize therefore
updates `RenderWidgetHostView` in that layout pass instead of leaving the page
at its previous sidebar-sized viewport. During a pinned expand or collapse, the
native tab panel, page inset, and shell outline all follow the same
`kTabStripWidth` animation value. The page therefore resizes continuously with
the panel instead of snapping at the start or end. The pinned tab rail also
follows that value for opacity, so it no longer remains visually over the page
until disappearing on the final frame. Hover still remains an overlay and does
not resize the page. As hover opens, that same native tab view moves inward by
the collapsed 8px edge width and gains four rounded corners plus a shallow
shadow. This creates a detached glass flyout rather than a sheet attached to
the window edge. The blur helper also derives its fill and edge stroke from the
configured 8px target instead of Chromium's stock 56px collapsed rail. The
flyout uses an 18px radius, deliberately distinct from the content frame's
13px radius. Its shadow uses a broader elevation with lower key opacity so the
surface separates softly instead of reading as a heavy nested card.
The flyout now moves through the Views transform path, keeping its visible tabs
and input coordinates aligned. Its hit region extends back by the same offset,
so the edge trigger and floating surface remain one continuous hover target.
The hidden collapsed rail also applies Chromium's full hover-geometry sequence
to the entire tab foreground, so favicons and controls fade from the first
closing frame instead of exposing the stock 56px internal layout transition.

The first native pass maps the checked-in pilot's core tab and address tokens
onto Chromium rather than claiming pixel parity. Expanded tabs are 42px tall
with a title and hostname line, a 24px favicon tile, a 9px radius, and the pilot
mint active indicator. The Omnibox keeps its native edit model, security state,
suggestions, page actions, and extension integration while using a 36px height,
10px radius, and restrained glass surface. High-contrast mode continues to use
Chromium's system-derived colors and border behavior. A screen-to-screen capture
comparison and the resulting visual corrections remain the next checkpoint.

The trailing Agent Status area is currently a UI-only contract with three
preview states: `Ready`, `Working`, and `Needs input`. It intentionally has no
agent-runtime dependency. The status view owns a small compositor layer and
only repaints that layer when its state changes. `MultiContentsView` receives
its final Yee bounds directly from Chromium's proposed-layout pass, so it is
never placed at the stock bounds and moved a second time afterward. Rounded
corners use `MultiContentsView::SetBackgroundRadii()` rather than masking the
entire parent layer; Chromium therefore propagates the clip to WebContents,
DevTools, footers, overlays, and split views through their native holders. The
layout engine receives a generic `BrowserContentLayoutConfig` containing
insets, radii, and separator policy; it contains no Yee-specific branch.
The titlebar follows the same boundary: `BrowserToolbarLayoutConfig` supplies
the Toolbar bounds without adding a Yee branch to the generic layout engine.
Normal launches stay pinned to `Ready`;
`--yee-agent-status-demo` explicitly cycles the states for visual review, and
`--yee-agent-status=<ready|working|needs-input>` pins one state.

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
