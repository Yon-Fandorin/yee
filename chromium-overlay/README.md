# Chromium source overlay

> UI 패치를 추가하거나 갱신하기 전에
> [`../docs/browser-shell-spec.md`](../docs/browser-shell-spec.md)의 불변 조건과
> 구현 완료 체크리스트를 확인한다. 프로토타입의 시각적 결과만 복사하지 않는다.

This directory contains the first in-tree `yee` checkpoint for an upstream
Chromium checkout. It does not introduce another tab model. It opts new profiles
into Chromium's existing native vertical-tab implementation, which already
observes `TabStripModel` and renders real `WebContents` tabs.

## Apply to a Chromium checkout

macOS:

```sh
./chromium-overlay/apply.sh /absolute/path/to/chromium/src
```

Windows PowerShell:

```powershell
.\chromium-overlay\apply.ps1 -ChromiumSrc F:\chromium\src
```

The script checks each patch before applying it, skips patches that are already
present, and refuses mismatched target files. The overlay currently changes:

- `prefs::kVerticalTabsEnabled`: `false` → `true` for new profiles
- `kVerticalTabStripDefaultUncollapsedWidth`: `240` → `244`
- Chromium product and installer names to `Yee`, producing `Yee.app` on macOS
- macOS app icon and Chromium product logos to the v8c Yee dinosaur mark
- a compact native `ToolbarView` with sidebar toggle, new tab, agent activity,
  navigation, Omnibox, and extension dock
- a rounded `MultiContentsView` surface on the Windows 11 shell background
- a pinned or edge-hovering native vertical tab sidebar whose real pinned
  tabs, bookmarks entry, groups, and tabs match the pilot section rhythm
- no duplicate stock vertical-tab launchers or full-shell toolbar separators;
  the pilot titlebar owns those actions and the content starts at 48 DIP
- Windows proto wrappers so Store app aliases cannot shadow depot_tools Python

The brand asset installers crop the presentation whitespace from
`assets/brand/yee-logo-v8c-dino-nubs.png`. macOS generates an ICNS with `sips`
and Chromium's lightweight ICNS packer. Windows uses `System.Drawing` to
generate the multi-resolution `chromium.ico` and bundled product-logo PNGs.
Generated Chromium assets stay in the local checkout rather than this repo.

The Windows shell follows the latest pilot's 48px titlebar, 6px content gutter,
and 8px content radius. Tenant/workspace identity is no longer duplicated in
the titlebar; it belongs to the future sidebar pass. The central runway is
Chromium's real `ToolbarView`, compactly laid out as
sidebar toggle, new tab, compact agent activity, back, forward, reload,
native Omnibox, and extension dock. It does not paint a second
imitation toolbar over Chromium controls. The
existing `MultiContentsView` is retained and placed inside the Browser Content
region. The Yee shell is the application default so Finder, Dock, and test-tool
launches share the same composition. `--disable-yee-shell-scaffold` explicitly
restores the stock Chromium composition for comparison.

The sidebar has three deliberate layout states. Expanded reserves its native
244px width plus the 6px content gutter. Collapsed keeps its existing invisible
8px edge target and gives the remaining width back to `MultiContentsView`.
Hovering the edge
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
onto Chromium rather than claiming pixel parity. Expanded tabs are 40px tall
with a title and hostname line, a 28px favicon tile, a 7px radius, and the pilot
mint active indicator. Real pinned tabs form Favorites, Bookmarks opens the
real bookmark manager, and groups and tabs continue to use `TabStripModel`.
The current checkpoint intentionally omits the sidebar Agent section and leaves
extension, page-status, and site-information icons unchanged. The Omnibox keeps
its native edit model, security state,
suggestions, page actions, and extension integration while using the Windows
pilot's 36px height and 8px radius. High-contrast mode continues to use
Chromium's system-derived colors and border behavior. Pixel parity remains an
iterative capture checkpoint across macOS and Windows.

The Yee composition hides Chromium's stock vertical-tab top launcher row and
bottom New Tab row because Sidebar and New Tab already live in the 48 DIP
titlebar. It also suppresses the native Toolbar dividers that would otherwise
draw unmatched vertical rules. The content and sidebar begin directly below
that titlebar at 48 DIP; their own 6 DIP horizontal gutter remains intact. The
content shadow stays behind the renderer, while a transparent, non-interactive
composited sibling paints the pilot's `#dfdfdf` one-DIP rounded outline above
opaque WebContents. This keeps the boundary visible without tinting page pixels.

The refined Windows Omnibox uses the pilot's white paper surface and
`#dfdfdf` line, interpolating to `#cfcfcf` on hover. Its native focus ring now
follows the same 8 DIP rounded rectangle instead of Chromium's pill path. When
the pinned sidebar is expanded, the existing sidebar-width notification also
updates the Omnibox leading margin so its left edge tracks the real Browser
Content boundary after user resizing; collapsed mode returns to the compact
default margin.

Windows cannot use Chromium's native `GlassFrame`, which is restricted to
macOS 26. The Yee overlay therefore paints the latest Windows pilot's neutral
`#f3f3f3` Fluent surface below Chromium's real controls. The existing 18px
Views compositor blur remains scoped to the floating tab-sidebar state; the
pinned shell and content gutter use one continuous Windows background.

The latest pilot uses one compact Toolbar instead of a second trailing status
surface. Agent activity is a native 30 DIP toolbar button beside New Tab, so
the real Omnibox receives the full remaining width and Windows DPI or
window-state changes cannot place product UI beneath minimize, maximize, or
close. The Toolbar begins at the titlebar's own 10px inset rather than
inheriting the vertical tab strip width.

Windows' existing caption-button container also consumes the same 48 DIP
visual titlebar height. Its hover/focus paint reaches the top edge instead of
leaving a one-DIP seam. Restored-window hit testing still returns `HTTOP`
before caption-button handling, preserving top-edge resize. The native 46 DIP
button width, icons, actions, and Windows 11 Snap Layouts behavior remain
unchanged.

Yee normal windows also request `DWMWA_COLOR_NONE` for the outer DWM border,
so activating or focusing the window does not reintroduce a dark one-pixel
frame. Restored windows additionally keep a one-pixel transparent DWM top-frame
extension. It is not a visible inset: it preserves Windows' rounded compositor
shadow around the borderless client surface, giving the light shell separation
from similarly colored backgrounds. Maximized and fullscreen windows need no
outer elevation, while system high-contrast mode retains its native border for
accessibility.

Restored Yee windows also extend Chromium's client surface through the native
left, right, and bottom resize frame. This prevents those three non-client
bands from appearing as a dark outline while retaining the same resize
affordance through `BrowserFrameViewWin` hit testing inside the painted edge.
Maximized, fullscreen, and system high-contrast windows keep Chromium's stock
Windows frame calculations.

The rounded content surface now follows `MultiContentsView`'s actual laid-out
bounds instead of recomputing an edge from the nominal sidebar width. This keeps
restored tab widths from exposing a second-colored strip around the clipped
page corners. A white paper backing, `#dfdfdf` outline, and quiet 1px/3px shadow
match the Windows pilot while keeping the surrounding gutter on the single
Fluent shell color.

The layout now has one geometry owner. Chromium's final content bounds drive
the animated sidebar surface and the rounded paper backing. The titlebar does
not replay vertical-tab geometry, so user-resized or restored tab widths cannot
shift the Toolbar. The latest pilot moves tenant/workspace identity into the
sidebar footer; its obsolete titlebar Context Switcher has been removed from
the native shell and will be implemented with the sidebar work later.

The pinned tab sidebar is intentionally transparent over the same Fluent
base as the content gutter. Chromium's additional 72%-opaque toolbar-theme
paint and the scaffold's second sidebar tint were removed, so the exposed areas
around the content's upper-left and lower-left curves no longer form lighter
end caps. The 18px blur and translucent surface now activate only while the
sidebar is detached as an expand-on-hover flyout.

Explicit shell content also disables Chromium's theme-colored
`MainBackgroundRegionView` and the two vertical-tab `CustomFloatingCorner`
helpers. Those stock cracking/corner layers otherwise remain visible precisely
inside the rounded content surface's upper-left and lower-left cutouts,
producing two differently colored end caps at fractional Windows scale. Stock
Chromium layouts keep both helpers enabled by default.

The compact Agent activity button is currently a UI-only contract with three
preview states: `Ready`, `Working`, and `Needs input`. It intentionally has no
agent-runtime dependency. Its native ToolbarButton owns hover, focus,
accessibility, and status-badge paint; the obsolete titlebar accessory has been
removed.
`MultiContentsView` receives its final Yee bounds directly from Chromium's
proposed-layout pass, so it is never placed at stock bounds and moved later.
Rounded corners use `MultiContentsView::SetBackgroundRadii()` rather than
masking the entire parent layer; Chromium therefore propagates the clip to
WebContents, DevTools, footers, overlays, and split views through native
holders. `BrowserView` paints the Windows shell and real content backing via
its standard Background path, eliminating the ignored full-window scaffold
child and its manual `Layout()` setters.
Normal launches stay pinned to `Ready`;
`--yee-agent-status-demo` explicitly cycles the states for visual review, and
`--yee-agent-status=<ready|working|needs-input>` pins one state.

Use a non-mutating compatibility check before updating Chromium:

```powershell
.\chromium-overlay\apply.ps1 -ChromiumSrc F:\chromium\src -CheckOnly
```

The identity and Agent activity elements are UI contracts, not fake integrations.
The Omnibox remains Chromium's real location bar and the tab list remains backed
by `TabStripModel`/`WebContents`. Dynamic tenant switching and a live agent
runtime still need product-service integration.

## Build configuration

After applying the patch from the Chromium `src` directory on macOS:

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

On Windows, Chromium intentionally keeps the development executable filename
`chrome.exe`; its embedded product metadata, icon, and installer display name
are branded as Yee. Build and run it through the PowerShell commands documented
in [`../chromium-dev/README.md`](../chromium-dev/README.md).
