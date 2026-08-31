# Chromium source overlay

> UI 패치를 추가하거나 갱신하기 전에
> [`../AGENTS.md`](../AGENTS.md), [`AGENTS.md`](./AGENTS.md),
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
present, and refuses mismatched target files.

The patch series is intentionally kept at three responsibility boundaries:

- `0001-integrate-yee-shell.patch`: native browser shell and Chromium UI wiring
- `0002-brand-yee-application.patch`: product naming metadata
- `0003-fix-windows-protoc-python-aliases.patch`: Windows build compatibility

Yee-owned UI source lives under `yee-ui/` and is copied into the Chromium
checkout by the platform-specific installer, rather than being duplicated in
incremental refinement patches.

The resulting Chromium checkout changes:

- `prefs::kVerticalTabsEnabled`: `false` → `true` for new profiles
- `kVerticalTabStripDefaultUncollapsedWidth`: `240` → `244`
- Chromium product and installer names to `Yee`, producing `Yee.app` on macOS
- macOS app icon and Chromium product logos to the v8c Yee dinosaur mark
- a compact native `ToolbarView` whose sidebar header keeps New Tab and agent
  activity while the combined content header starts with the sidebar toggle,
  navigation, Omnibox, and extension dock
- an isolated `//chrome/browser/ui/views/yee:yee_ui` source target for Yee's
  visual substrate, combined surface outline, and agent activity control
- a frame-derived window-controls safe area that keeps macOS traffic lights,
  Windows caption buttons, and Linux client-side decorations outside the Yee
  toolbar without platform-specific shell padding
- a guttered combined browser surface joining Toolbar and `MultiContentsView`
  under one fixed outer-corner token
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

The Windows shell follows the latest pilot's 48px titlebar and 6px browser
surface gutter. Tenant/workspace identity is no longer duplicated in the
titlebar; one Yee-owned Context Switcher Footer presents it below the tab list.
Chromium's real `ToolbarView`
keeps New Tab and compact agent activity in the Sidebar Header, then starts the
Browser Surface Header with sidebar toggle, back, forward, reload, native
Omnibox, and extension dock. It does not paint a second imitation toolbar over
Chromium controls. The
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
flyout uses an 18px radius, deliberately distinct from the content frame. Its
shadow uses a broader elevation with lower key opacity so the
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
The current checkpoint intentionally omits the sidebar Agent section. Extension,
page-status, and site-information controls keep their native behavior; Yee only
lets the neutral site-information button rest on the shared Header color while
preserving its hover, focus, warning, and popover states. The Omnibox keeps its
native edit model, security state,
suggestions, page actions, and extension integration while using the Windows
pilot's compact height and the shared 12px Browser Surface radius. High-contrast mode continues to use
Chromium's system-derived colors and border behavior. Pixel parity remains an
iterative capture checkpoint across macOS and Windows.

The Yee composition hides Chromium's stock vertical-tab top launcher row and
bottom New Tab row because New Tab already lives in the Sidebar Header. The
Sidebar Toggle moves across the column spacer to become the first control in
the Browser Surface Header. The combined surface begins at a 6 DIP top and
leading inset, spans Toolbar and WebContents, and retains 6 DIP trailing and
bottom gutters. A transparent, non-interactive composited sibling paints one
theme-derived outer outline plus a one-DIP Toolbar/Content separator above the
opaque WebContents. The outer surface owns all four 12 DIP corners; the
WebContents clip shares only the two lower corners.

The Browser Surface Header and compact Omnibox use one Yee-owned color
controller. After a navigation, load, tab switch, or page color change it takes
small captures of the rendered page's top strip. Navigation never clears the
last committed Header color: loading samples remain candidates, and only after
loading stops and the same dominant flat color survives three samples across
at least 150ms is it committed once. A window without a committed page color
keeps the current Toolbar color during that gate. Each `WebContents` retains
its last committed surface for its own lifetime. Returning to an already
sampled tab restores that tab's surface immediately, then verifies it with a
new sample, instead of showing the previously active tab's color during the
gate. A user scroll starts a bounded 140ms sampling burst instead of capturing
every frame; two stable
consecutive samples are still required. Once a new color is accepted, the
visible Header follows it with a retargetable 200ms transition; a newer sample
continues from the currently presented color instead of committing another
discrete midpoint. Non-flat or unstable page
samples fall back after the bounded attempts to the active page's CSS
background, then its published theme color, and finally the current Toolbar
color. The selected page color is used without lightness compensation so the Header
remains visually continuous with the rendered page; resting text derives its
foreground from that exact surface. Resting Omnibox text is a non-interactive
origin/page-title layer over the real native editor: the origin drops a leading
`www.`, stays the primary label, and is separated from the one-level-quieter
page title by a dedicated one-pixel rule rather than a text glyph. The editor
returns immediately on click or keyboard focus and unelides the full URL on
the first pointer activation. Address text, the quieter title, site/page
actions, and compact toolbar controls derive primary, secondary, and disabled
foreground roles from the same resolved surface with minimum contrast floors;
semantic security and product colors continue to win. Hover adds only a 2%
contrast tint and there is no persistent outline. The 34 DIP Omnibox is centered
inside the 42 DIP Browser Surface Header with four DIP above and below, rather
than touching the surface's upper edge. Focus uses a page-aware one-DIP stroke
inset 2 DIP from the 34 DIP control edge. Its 10 DIP inner curve reads as an
edit-state indicator while the full-height 12 DIP Omnibox surface and native
hit target remain unchanged. The stroke remains visible while suggestions are
open without escaping into the gutter, prefers a darker derivative of the
current Header, and switches light only when a dark surface cannot provide
three-to-one non-text contrast. The native suggestions popup reuses the
exact resolved page surface for its neutral background, text, and controls,
adds only a 6% contrast tint to hover and selected rows, and preserves semantic
warning and security colors. Because Chromium enables the WebUI Omnibox popup
by default, the popup presenter's Widget supplies this page-aware color provider
to the popup WebContents and `ThemeColorsSourceManager` uses that provider when
generating `chrome://omnibox-popup`'s `colors.css`. Equal resolved surfaces use
a process-stable supplier identity, matching Chromium's process-wide
`ColorProvider` cache lifetime so reopening a popup cannot reuse a stale palette
through a recycled Widget address. A preloaded popup document is
reloaded once after that source is attached so it cannot retain the last active
browser's generic palette. Chromium also prewarms the popup Widget before Yee's
compact-shell mode and active-page surface are available. Immediately before
showing it, the presenter compares that Widget's captured mode and surface with
the current contract and rebuilds only the hidden presentation shell when they
differ; the WebUI contents and logical popup state remain intact. If the page
surface resolves or changes while suggestions are already visible, the same
Widget swaps to the current page-aware color provider in place. Chromium's
color-change listener then refreshes the WebUI `colors.css` while the native
frame, results background, text, and icons converge without closing or flashing
the popup. Styling only
the legacy Views popup does not satisfy this contract. Its transparent Omnibox
cutout receives the actual
compact radius instead of recomputing Chromium's pill radius, so focus no longer
appears to punch a white capsule through the panel. The popup extends 2 DIP
to the Omnibox's sides and bottom, keeps its top flush with the Browser Surface
instead of breaking through it, and starts results 2 DIP below the control. Its
cutout leaves the Omnibox's page-aware one-DIP focus outline visible, and its
native shadow drops to MD elevation 4. The compact-shell opacity transition uses
140ms easing rather than Chromium's
abrupt 82ms default. When the pinned sidebar is expanded, the existing sidebar-width
notification also updates the Omnibox leading margin so its left edge tracks
the real Browser Content boundary after user resizing; collapsed mode returns
to the compact default margin.

Windows cannot use Chromium's native `GlassFrame`, which is restricted to
macOS 26. Glass is a compiled Yee product default rather than a launcher flag,
and the launch scripts do not force a theme seed. On supported macOS systems,
the native frame tint and Yee's theme-derived Views tint restore the pilot's
original material strength: approximately 43% effective opacity in light mode
and 74% in dark mode. Reduced-transparency mode disables
the native material and uses the same frame color opaquely. Windows and Linux
also paint their current theme frame color as an opaque shell. The existing
18px Views compositor blur remains scoped to the floating tab-sidebar state;
the pinned shell and content gutter use one continuous platform background.

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

The combined Browser Surface follows `MultiContentsView`'s actual laid-out
horizontal and bottom bounds, extending upward to the shared 6 DIP shell inset.
Its theme-resolved Toolbar backing, one-DIP outer outline, one-DIP internal
separator, and quiet 1px/3px shadow use `yee::kSidebarMetrics`. The renderer
clip uses the same fixed 12 DIP radius only on the lower corners, so Toolbar and
page read as one outer shape rather than two stacked cards. Automatically
following native window corners is deferred until macOS Zoom and
Windows/Linux maximize, fullscreen, and tiling states can be verified against
the real WebContents compositor without platform-specific presentation hacks.

The layout now has one geometry owner. Chromium's final content bounds drive
the animated sidebar and the combined Browser Surface's horizontal and lower
edges; the shared shell inset owns its upper edge. The titlebar does not replay
vertical-tab geometry, so user-resized or restored tab widths cannot shift the
Toolbar. The latest pilot moves tenant/workspace identity into the sidebar
footer; its obsolete titlebar Context Switcher has been removed from the native
shell and will be implemented with the sidebar work later.

The pinned tab sidebar is intentionally transparent over the same frame surface
as the content gutter.
Chromium's additional 72%-opaque toolbar-theme
paint and the scaffold's second sidebar tint were removed, so the exposed areas
around the content curves no longer form lighter end caps. The 18px blur and
translucent surface now activate only while the
sidebar is detached as an expand-on-hover flyout.

Explicit shell content also disables Chromium's theme-colored
`MainBackgroundRegionView` and the two vertical-tab `CustomFloatingCorner`
helpers. Those stock cracking/corner layers otherwise remain visible precisely
inside the content surface's rounded cutouts, producing differently colored
strips at fractional Windows scale. Stock
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
