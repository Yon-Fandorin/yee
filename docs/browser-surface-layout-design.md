# Browser Surface layout and clipping design

Status: red-team-hardened implementation design with a partial built
checkpoint. The audit tracks completed evidence and the remaining implementation
and real-app matrix; full completion is still pending.

This document closes the layout gap exposed by Chromium's native PDF InfoBar.
It is an implementation design under `browser-shell-spec.md`, not a replacement
for the product specification. Product measurements continue to come only from
`yee::kSidebarMetrics`.

## 1. Intent

Yee presents Chromium's toolbar, active page, split panes, native notices, and
page overlays as one coherent set of bounded surfaces. Native Chromium models
and commands remain authoritative, while Yee owns their placement inside its
chrome presentation.

The design must guarantee that:

- the Browser Surface gutter, outline, shadow, and renderer boundary come from
  one proposed-layout result;
- a structural row or panel consumes space exactly once;
- native page surfaces cannot paint into the Sidebar, gutter, divider, or a
  neighboring pane;
- window-global UI remains window-global instead of being incorrectly clipped
  to the Browser Surface;
- single-pane, split, animation, small-window, side-panel, and fullscreen states
  have explicit geometry rather than incidental exceptions.

## 2. Confirmed baseline failure that motivated the checkpoint

Before the partial implementation recorded in the audit,
`InfoBarContainerView` and `MultiContentsView` were laid out as BrowserView
siblings with independent rectangles. The InfoBar used Chromium's intermediate
`params.visual_client_area`, while Yee later rebuilt the MCV rectangle from
`browser_params.visual_client_area` and a fixed
`BrowserContentLayoutConfig` inset.

That creates two independent truths:

```text
native flow result:  toolbar -> InfoBar -> remaining content
Yee override:        fixed titlebar/sidebar/gutter -> MultiContentsView
```

That baseline override lost both the InfoBar's horizontal Surface gutter and
the height that Chromium consumed with `params.SetTop()`. The same reset also
lost native Side Panel width and shadow-box insets. A trailing-only InfoBar
inset would have hidden one symptom while leaving the layout model broken.

The baseline outline was also positioned imperatively after proposed layout by
copying `multi_contents_view_->bounds()`. It was a paint sibling, not the owner
of the geometry it appeared to represent.

## 3. Ownership and surface taxonomy

Every visible element belongs to exactly one of the following scopes.

### 3.1 Structural browser layout

These participate in the BrowserView proposed-layout allocation:

- Yee Sidebar reservation and outer Content Gutter;
- the main Browser Surface;
- the shared Browser Surface Header in single-pane mode;
- `MultiContentsView` / Split Canvas;
- native Side Panel allocation;
- the Browser Surface outline and shadow.

They consume or divide space. They must use one resolved layout result.

### 3.2 Pane/body flow and page-owned overlays

These belong to the active `ContentsContainerView` or to one specific split
pane:

- Pane Header;
- the active WebContents' native InfoBar stack;
- WebContents, DevTools, NTP footer, and renderer host;
- actor, AI, Glic selection, immersive, Indigo, enterprise, and other page
  overlays;
- tab-modal dialogs, find UI, and status UI when their position depends on a
  specific page.

They use the actual pane layout produced by `MultiContentsView`; they never
recompute split ratios or divider geometry in BrowserView.

Scope-dependent `OverlayBaseController` subclasses are classified by their
actual native mode. Current Lens is always shared/MCV-scoped and explicitly
does not support pane-local split presentation; Glic selection is tab-scoped
and respects split. Yee does not silently convert either scope.

### 3.3 Anchored or window-global chrome

These keep their existing Widget/anchor ownership:

- toolbar and page-action bubbles;
- Omnibox suggestions;
- the full-window scrim and browser-window modal dialogs;
- fullscreen-exit UI and other OS/window-level affordances.

They may visually overlap the Browser Surface. They must not be forced through
a page clip merely because they are transient.

## 4. One Yee resolver, consumed by native proposed layout

The fix must not replace Chromium's layout engine. The current toolbar,
vertical-tab, and content config structs may remain as narrow installation
inputs while the work is staged, but they may not independently reconstruct the
same final Surface rectangle.

Yee owns the product-state mapping and final frame result in `yee-ui`. Chromium
glue first exposes the native top-container/Side Panel calculation as a value;
the Yee resolver must not pretend that this value already exists today:

```cpp
namespace yee {

struct NativeHorizontalPlan {
  bool force_top_container_to_top = false;
  bool top_container_shares_panel_row = false;
  int top_container_height = 0;
  int panel_target_width = 0;
  int panel_visible_width = 0;
  // Remaining intrinsic/animation inputs needed by the native child planner.
};

struct NativeTopAndSidePanelLayout {
  bool force_top_container_to_top = false;
  bool top_container_shares_panel_row = false;
  bool panel_visible = false;
  bool panel_leading = false;
  bool panel_animating = false;
  double panel_reveal = 0.0;
  int panel_target_width = 0;
  int panel_visible_width = 0;
  int underlap_deficit = 0;
  gfx::Rect top_container_bounds;
  gfx::Rect surface_frame_allocation;
  // Post-panel/shadow allocation used by the native InfoBar before any
  // minimum-width content-underlap expansion.
  gfx::Rect notice_flow_allocation;
  // MCV allocation after native minimum-width underlap rules.
  gfx::Rect body_allocation;
  gfx::Rect body_occlusion_rect;
  bool has_contiguous_notice_flow = true;
  std::optional<gfx::Rect> panel_bounds;
  std::optional<gfx::Rect> panel_animation_content_bounds;
  gfx::Insets shadow_overlay_insets;
  gfx::Rect main_shadow_overlay_bounds;
  gfx::Rect unclipped_contents_region;
  bool native_main_background_required = false;
};

struct BrowserSurfaceLayoutInput {
  gfx::Rect visual_client_area;
  // Yee-resolved once from the actual Sidebar reservation.
  gfx::Rect content_column_bounds;
  NativeTopAndSidePanelLayout native_layout;
  bool split = false;
  bool toolbar_participates = false;
};

struct ResolvedBrowserSurfaceFrame {
  gfx::Rect content_column_bounds;
  gfx::Rect main_surface_bounds;
  gfx::Rect header_bounds;
  gfx::Rect body_bounds;
  gfx::Rect multi_contents_bounds;
  NativeTopAndSidePanelLayout native_layout;
  SurfaceMode mode;
  SurfaceCornerRoles corners;
};

ResolvedBrowserSurfaceFrame ResolveBrowserSurfaceFrame(
    const BrowserSurfaceLayoutInput& input);

}  // namespace yee
```

`content_column_bounds` is resolved once from the actual Sidebar reservation.
Native/Yee calculation is an acyclic pipeline:

1. pure `PlanNativeHorizontalLayout()` resolves branch, intrinsic row height,
   panel width, animation, and row-sharing facts, but does not calculate top
   child bounds;
2. Yee applies the one `kSidebarMetrics.content_gutter` contract and resolves a
   final Surface seed plus participating `header_bounds` from that plan;
3. pure `ResolveNativeTopAndSidePanelLayout()` receives those final Header/body
   candidate rectangles and calculates top-container child proposed layouts,
   panel current/final/animation-content bounds, notice-flow/body allocations,
   underlap, and shadow values exactly once;
4. `ResolveBrowserSurfaceFrame()` consumes the native value without restarting
   from raw `browser_params`.

The top-container child proposal is native glue returned alongside the plain
Yee snapshot and applied directly by BrowserView; it is never computed first in
raw content-column coordinates and then recomputed inside the gutter. An
eligible Yee vertical-tab window must have
`force_top_container_to_top == true`; the planner passes finalized
`header_bounds.bottom()` into panel/body y calculation. Existing non-Yee
row-sharing behavior remains on its native path. Panel type is an input but not
a proxy for row sharing.

All values use the current animation sample, never `SidePanel::bounds()` from
the previous applied frame and never target bounds in place of current visible
bounds. Logical edges are converted to physical coordinates once while
constructing the content column.

`BrowserViewTabbedLayoutImpl::CalculateProposedLayout()` assembles and consumes
one final frame result for the Toolbar Content Column, MCV, and Surface
decoration. It does not start those consumers again from
`browser_params.visual_client_area`.
The existing `BrowserContentLayoutConfig::insets` field is retired from the Yee
path first; cleanup or consolidation of the remaining config carriers is a
later mechanical change, not a prerequisite for the geometry fix.

The hierarchy has three explicit value stages rather than one flattened window
calculation:

1. the native horizontal plan plus finalized-Header child planner own branch
   order, native top children, animation bounds, underlap, and shadows;
2. `ResolvedBrowserSurfaceFrame` owns Yee BrowserView structural rectangles;
3. pure, typed current/target pane geometry functions own MCV cards, headers,
   divider, body rectangles, and directional pane minimums inside
   `multi_contents_bounds`.

`MultiContentsView::CalculateProposedLayout()` is not used as the preview API:
it is private today, mutates corner-separator orientation, and may calculate a
target-size layout in `BeforeApplyLayout()`. Instead,
`ComputeCurrentPaneGeometry(CurrentPaneInput)` takes current MCV size, split
layout/ratio, contents identities, active identity, insets,
drop-target/separator sizes/state, and per-pane directional minimums.
`ComputeTargetPaneGeometry(TargetPaneInput, CurrentPaneGeometry)` is a distinct
type and entry point used only by MCV's target-animation path. BrowserView and
InfoBar code can depend only on `CurrentPaneGeometry`; target output can size
only PageTargetHost/renderer outsets and cannot set a direct InfoBar bound.
Orientation, visibility, and View/layer mutation occur only while applying
these values. For one current snapshot, preview and subsequently applied current
child bounds must be identical.

`surface_outline` becomes a normal entry in `BrowserViewLayoutViews`. Proposed
layout assigns it `main_surface_bounds` in single mode and hides it in split
mode, where each Pane Card owns its own outline/shadow and Split Canvas remains
transparent. The imperative bounds rewrite in `BrowserView::Layout()` is
removed. Paint-state updates may remain imperative, but geometry updates may
not. Proposed layout does not define child z-order, so construction/structural
update code also keeps the event-ignored outline above the owned content and
below window-global scrims.

`GetMinimumSize()` uses a separate pure stable composition of intrinsic child
minimums and actual chrome participation; it does not read `layout_data_`,
applied bounds, or a final frame that itself requires the candidate window
size. A second pure `ResolveSupportedNoticeLayoutMinimum()` uses current
InfoBar flow metrics and MCV directional policy for pane clamping, diagnostics,
and tests. Phase one does not dynamically change the OS window minimum when a
notice appears; doing so is a separate product decision.

The resolver is installed only for Yee-enabled normal tabbed browser windows.
Popup, app/PWA, DevTools, and feature-disabled layouts keep their existing
layout implementation; no Yee-only controller is dereferenced merely because
the command-line switch exists.

That scope is represented by one per-Browser predicate,
`UsesYeeBrowserSurfaceGeometry(const Browser&)`, and passed to shared MCV and
ContentsContainer instances. Global `yee::IsShellEnabled()` may enable unrelated
Yee presentation, but it must not by itself select Surface bounds, split insets,
Header reservations, clip hosts, or corner policy in popup, app/PWA, standalone
DevTools, Picture-in-Picture, or feature-disabled windows. Docked DevTools in an
eligible normal window remains inside that window's page geometry.

## 5. Geometry algorithm

The algorithm uses actual state, not the existence of a pointer or a fixed
window mode. The order is fixed:

1. determine whether the Yee resolver applies to this browser type;
2. apply the actual Sidebar reservation on its declared physical edge;
3. snapshot InfoBar manager identity, fixed animated stack height, and control
   minimum before any pane preview;
4. produce the native horizontal plan, apply the 6-DIP Surface seed/Header, then
   resolve native top children and Side Panel current/final animation,
   notice-flow/body, underlap, and shadow-box allocations exactly once;
5. resolve the final shared Header versus split canvas frame from those values;
6. run pure `ComputeCurrentPaneGeometry()` with current MCV size and per-pane
   directional minimums; only MCV's target path may separately call
   `ComputeTargetPaneGeometry()`;
7. place the direct InfoBar from the matching manager/contents identity and let
   the matching `ContentsContainerView` reserve its semantic height before
   DevTools/page partitioning;
8. apply native child bounds, decoration, page clips, and z-order from those
   value results without a second geometry calculation.

### 5.1 Inputs

- `visual_client_area` from Chromium;
- actual Sidebar display/reservation and animation progress;
- actual toolbar visibility and whether it overlays content;
- single versus split presentation;
- native Side Panel visibility, alignment, width, type, and animation value;
- browser fullscreen, renderer/content fullscreen, immersive enabled/revealed,
  always-show-toolbar, and transition state;
- browser class and the per-instance `UsesYeeBrowserSurfaceGeometry()` result;
- InfoBar manager `WebContents` identity, animation generation, semantic stack
  height, and rigid-control minimum;
- MCV current size, split ratio/orientation, active identity, split insets,
  drop-target/separator state, and target-animation state;
- the one Yee metric contract.

The Sidebar edge is an explicit input. Yee currently resolves it to physical
left. Native Side Panel bounds already contain Chromium's RTL/alignment choice;
the resolver does not mirror them a second time.

For any animation frame `t`, the rectangle equations are:

```text
C(t) = InsetEdge(visual_client_area, physical_sidebar_edge,
                 actual_sidebar_reservation(t))
H(t) = PlanNativeHorizontalLayout(C(t), native_state_snapshot(t))
S(t) = ResolveYeeSurfaceSeed(Inset(C(t), 6 DIP), H(t), chrome_state(t))
N(t) = ResolveNativeTopAndSidePanelLayout(H(t), S(t).header_bounds,
                                          S(t).body_candidate, native_state(t))
M(t) = ResolveBrowserSurfaceFrame(S(t), N(t)).main_surface_bounds
V(t) = Intersect(N(t).body_allocation, BodyBelowParticipatingHeader(M(t)))
P(t) = ComputeCurrentPaneGeometry(V(t).size, current_mcv_snapshot(t),
                                  pane_mins(t))
```

`C(t)` is `content_column_bounds`, `H(t)` is the no-child native horizontal
plan, `S(t)` is the Yee Surface/Header seed, `N(t)` is the complete native child
allocation value, `M(t)` is `main_surface_bounds`, `V(t)` is
`multi_contents_bounds`, and `P(t)` is current pane geometry. RTL changes the
resolved physical edges in the inputs; it does not change these equations. All
producers and clip consumers share these integer DIP rectangles. The final
device-pixel clip must be exactly equal; a one-DIP tolerance is permitted only
for human visual-alignment review, never for a hard clip invariant.

### 5.2 Integrated single-pane mode

```text
content_column_bounds
  = visual_client_area after actual Sidebar reservation

main_surface_bounds
  = intersection of the already-6-DIP-inset Surface seed and
    native_layout.surface_frame_allocation

header_bounds
  = top 42 DIP of main_surface_bounds

multi_contents_bounds
  = intersection of native_layout.body_allocation and main_surface_bounds below
    the participating header_bounds
```

The Surface begins at y=6. The shared Header is 42 DIP, so the MCV begins at
y=48. The outline and shadow use `main_surface_bounds`, not the MCV rectangle.

### 5.3 Integrated split mode

The shared Header consumes no Yee row. `multi_contents_bounds` is always the
intersection of `native_layout.body_allocation` and `main_surface_bounds`; no
Side Panel enum shortcut decides whether it equals the whole frame.
`MultiContentsView` alone calculates the two Pane Card rectangles, divider,
orientation, ratio clamping, and transition layout through
`ComputeCurrentPaneGeometry()`.

`main_surface_bounds` is a structural boundary in split mode, not a painted
combined card. Split Canvas adds no fill, outline, shadow, or second inset; the
two Pane Cards paint and clip their own four-corner boundaries.

BrowserView must not duplicate that math. Consumers that need an active pane
consume `CurrentPaneGeometry` for the current snapshot. They do not query
applied child bounds or invoke MCV's mutating proposed-layout path as a preview.

### 5.4 Native Side Panel

The native Side Panel remains Chromium chrome; it is not routed into a reserved
Yee Sidebar slot and its entry model is unchanged. The extracted native planner
resolves and returns:

- the actual `force_top_container_to_top`/row-sharing decision, with
  `SidePanelType` as only one input;
- leading/trailing physical alignment;
- target width, `ClampFloor` visible width, current/final panel rectangles, and
  `side_panel_animation_content` bounds;
- the native `ClampRound` shadow padding, shadow overlay, background requirement,
  unclipped content region, and allocated versus minimum-width underlap.

The implementation extracts the existing calculations; it must not recompute
their rounding in Yee code or sample already-applied `side_panel->bounds()`.
The Yee resolver consumes the returned frame/body allocations and never
subtracts the panel width again. Native panel padding and shadow stay
panel-owned and are not copied into `kSidebarMetrics`.

State behavior is explicit:

| Resolved native state | Surface/body rule | Panel presentation |
| --- | --- | --- |
| closed | Surface and body both use the full post-Sidebar allocation | hidden |
| `force_top_container_to_top` (Yee vertical tabs; also always true for `kContent`) | top container keeps its returned full row; current panel allocation affects the returned body row | retain native type-specific vertical extent, animation-content bounds, clip, resize affordance, separator, and shadow |
| `top_container_shares_panel_row` | current visible panel allocation affects the top-container row and body according to the returned rectangles | retain native row-sharing bounds and decoration |
| minimum-width underlap/flyover | keep Chromium's underlap allocation for the affected row; the panel may occlude page pixels | retain native overlay clipping and z-order |

In split mode the Side Panel reduces or occludes the entire body/Split Canvas,
not only the active pane. MCV then recalculates both cards within the remaining
canvas. Panel type can change vertical extent or width policy, but the resolver
uses the returned row allocations rather than deriving that effect from the
enum. A type/alignment switch during an animation is a fresh native snapshot,
not a continuation using stale target or applied bounds.

Underlap is native occlusion, not permission for Yee to duplicate the panel
width. MCV uses `body_allocation`, including native underlap expansion; the
InfoBar uses `notice_flow_allocation`, preserving Chromium's pre-underlap
non-occluded notice width; `body_occlusion_rect` remains native z-order
information and is never recomputed by subtracting panel width in Yee. Current
Chromium always returns a contiguous notice slot. If a future/below-contract
state returns `has_contiguous_notice_flow == false` or an empty slot, the View
is hidden for that frame with a diagnostic while its manager/model remain
unchanged. Yee does not move the notice into a second model or over the panel.

Yee's combined outline/shadow owns `main_surface_bounds` only in single mode;
in split it is hidden and Pane Card decoration owns the visible boundaries.
Native panel background, resize separator, and panel shadow survive. Native
`main_shadow_overlay`, `main_background_region`, MCV outer separators, or
corner helpers that repaint the *main* rectangle are suppressed only where
they duplicate Yee's owner. The flyover path that currently forces the main
background visible must respect this distinction.

### 5.5 Fullscreen and immersive modes

Window state alone does not remove Yee's inset or radii. This follows the
existing product spec, which keeps the final canvas/inset/corner/shadow contract
in fullscreen and defers automatic window-state corner changes.

| State | Header/Sidebar participation | Surface result | InfoBar |
| --- | --- | --- | --- |
| restored or maximized | actual visible Yee chrome participates | integrated single or split | use native visibility predicate |
| browser fullscreen with always-show toolbar | toolbar and actual Sidebar participate | integrated single or split | use native visibility predicate |
| browser fullscreen with toolbar hidden | no fixed Header row; Sidebar follows Chromium's actual tab-strip decision | 6-DIP inset Surface remains; single page owns all four 12-DIP outer corners | show only when `IsInfobarVisible()` permits |
| renderer/content fullscreen | no invented toolbar/Sidebar reservation | preserve the inset Surface contract pending the separately deferred compositor/product decision | show only when `ShouldHideInFullscreen()` permits |
| immersive enabled, hidden | no overlay-toolbar reservation | same bounds as the corresponding hidden-toolbar state | native predicate |
| immersive reveal | revealed top container overlays; it does not add a second 48-DIP row or reflow the Surface | underlying Surface bounds remain stable | retain Chromium's extra paint offset behavior |
| enter/exit transition | keep the last current viewport visible while cancelling oversized target outsets; platform transition epoch decides when destination chrome participates | resolve and apply the destination once its native callback reports authoritative bounds | no stale target clip or duplicate layout |

A row in which the Header does not participate resolves `header_bounds` to an
empty rectangle. Consumers do not infer a hidden Header from a stale 42/48-DIP
height.

A hidden Header changes which child owns the upper corners; it does not imply
full-bleed content. Any future decision to make renderer fullscreen full-bleed
must first update `browser-shell-spec.md` and the split checklist, then receive
native compositor validation.

Fullscreen is not one synchronous path. A `SurfaceGeometryTransitionEpoch`
records source state, destination state, platform path, generation, and phase.
At transition start, MCV cancels target-size animation/outsets and every
ContentsContainer reapplies its native clips from the last *current* viewport;
it does not clear to an unbounded clip. Normal layout may then be suppressed.
At the authoritative destination callback, BrowserView creates one new snapshot
and applies the destination geometry. Late callbacks whose epoch/generation no
longer matches are ignored.

The verification paths are explicit:

| Platform/path | Transition gate |
| --- | --- |
| macOS legacy/synchronous and native asynchronous fullscreen | preserve overlay-widget anchor reparenting, cancel current target outsets before reparent, then apply destination after native fullscreen state is authoritative |
| Windows asynchronous fullscreen feature on/off | cover both paths and defer WCO refresh until the destination frame; WCO must not trigger a second stale Surface proposal |
| Linux asynchronous window-manager fullscreen | tolerate delayed/reversed WM state, maximize/tile interaction, and close during transition; generation rejects late callbacks |
| immersive hidden/reveal/always-toolbar | reveal remains an overlay over stable underlying geometry; a real browser/content-fullscreen state change starts a new epoch |

`in_process_fullscreen_` is only one legacy guard and is not the transition
contract. Tests must cover interrupt/reverse and close during every applicable
path, including a transition begun while Sidebar or Side Panel target bounds
are active.

## 6. InfoBar presentation

### 6.1 Model ownership

Keep Chromium's one `InfoBarContainerView`, `InfoBarManager`, delegates,
commands, animation, focus behavior, and active-WebContents switching. Do not
create a Yee InfoBar model or duplicate one container per pane.

### 6.2 Placement without reparenting

The default architecture keeps `InfoBarContainerView` as the same direct
`BrowserView` child. This preserves its destruction order, active-manager
switching, window-controls-overlay/fullscreen behavior, focus-list membership,
and accessibility-pane registration. It is not moved between pane parents.

The View is visually placed over a semantic flow slot:

```text
BrowserView
├─ InfoBarContainerView       direct child; bounds = active flow slot
└─ MultiContentsView
   └─ active ContentsContainerView
      ├─ Pane Header          split only
      └─ Body
         └─ PageViewport      top reduced by InfoBar semantic height
```

In single mode the active body begins immediately below the shared Header. In
split mode MCV exposes the active card's body rectangle below its Pane Header.
The inactive pane has no reserved InfoBar height. The reservation is keyed by
the InfoBar manager's `WebContents` identity plus a layout generation, not by a
possibly stale MCV active index. `ChangeInfoBarManager()` may synchronously
request layout before MCV switches its active index; during that gap the new
identity either maps to its exact ContentsContainer or resolves to empty/hidden.
It must never use the old active pane as a fallback.

BrowserView must keep the InfoBar above page paint in z-order. It remains below
window-global modal/scrim Widgets and does not become a pane-owned model.

### 6.3 Flow and animation

Chromium's refreshed InfoBars are fixed-height. `CalculatePreferredSize()` does
not consume available width for wrapping; narrow text elides while each bar's
`computed_height()` animates. The native container therefore exposes one
read-only value API rather than a fictitious width-constrained measurement:

```cpp
struct InfoBarFlowMetrics {
  int semantic_stack_height = 0;  // visible bars + native separators; no shadow
  int rigid_control_min_width = 0;
  size_t visible_bar_count = 0;
  uint64_t animation_generation = 0;
};
```

`rigid_control_min_width` is computed by native InfoBar controls and includes
buttons/links, close control, and native margins that may not be elided. It is a
generic native query, not a Yee metric. If a future native InfoBar becomes
width-dependent, that API may accept available width and return a new height;
phase one does not claim wrapping that does not exist.

Resolve flow in this order:

1. evaluate Chromium's existing `IsInfobarVisible()` predicate, including
   `ShouldHideInFullscreen()`;
2. snapshot manager `WebContents`, `InfoBarFlowMetrics`, and generation before
   pane calculation;
3. derive active-only directional pane minimums, then run
   `ComputeCurrentPaneGeometry()` for the current visible MCV size;
4. resolve the exact ContentsContainer for the manager identity and place the
   direct child at that body's top and non-occluded width; hide it if the
   identity is temporarily unmapped;
5. let only that ContentsContainer consume `semantic_stack_height` by insetting
   its full page environment *before* `ApplyDevToolsContentsResizingStrategy()`.
   Docked DevTools, its scrim, WebContents, footer, renderer-sized overlays, and
   current-viewport overlays therefore all begin below the notice.

`InfoBarContainerStateChanged`, manager changes, pane-map transactions, and
animation ticks prepare an idempotent `ExternalInfoBarFlowState` outside
`CalculateProposedLayout()`. No setter propagates invalidation or invokes
BrowserView layout recursively from inside a layout proposal. Each generation
produces at most one BrowserView proposal, and MCV/ContentsContainer consume
the prepared state in the same child-layout pass. Open/close animation updates
the semantic height every native tick; it never uses target Sidebar/Side Panel
bounds as the current InfoBar slot.

Directional minimums do not mutate the saved split ratio:

- side-by-side active pane: `max(native pane minimum, leading body inset +
  rigid_control_min_width + trailing body inset)`, converting the native
  body-space query into pane space;
- stacked active pane: native pane minimum plus semantic stack height, preserving
  at least the same usable page height that the native no-InfoBar minimum policy
  provided after Pane Header/insets;
- inactive pane: unchanged native minimum.

MCV outer split insets and the divider are outside these per-pane minima and
are composed exactly once by the supported-notice floor; they are not folded
into `rigid_control_min_width`.

The separate supported-notice minimum resolver composes these values without
reading transient layout data and feeds the pane clamp, diagnostics, and tests;
it does not alter the Widget's OS minimum in phase one. At or above that support
floor, rigid controls remain hittable and the current page viewport is
nonnegative even with every native priority slot occupied. Bounds below that
floor keep deterministic native occlusion/clipping and emit a diagnostic; they
do not silently change the saved split-ratio model or move actions into a new
surface.

Chromium's immersive `GetExtraInfobarOffset()` remains a paint-position offset.
It does not increase the semantic height consumed by the page, matching native
behavior.

The InfoBar's private `ContentShadow` intentionally extends below semantic
bounds. Add an explicit native presentation API that clips the shadow View/layer
to the active body's horizontal paint interval while preserving the shadow
View's full vertical overflow and event pass-through. Do not mask the whole
container to semantic height and do not include shadow height in page flow.

When no Header participates and the page owns the Surface's upper radii, the
same API clips the InfoBar background/children to the active body's upper-corner
path while allowing the bottom shadow to extend. Normal shared-Header and split
Pane-Header modes pass zero top radii. This prevents a rectangular fullscreen
InfoBar from painting across rounded upper corners.

Separator ownership is selective:

| Separator/shadow | Owner in Yee mode |
| --- | --- |
| Browser Surface Header/body boundary | Yee Surface presentation |
| MCV outer top/leading/trailing/corner separators | hidden where they duplicate the Yee Surface or Pane Card outline |
| separator between stacked native InfoBars | native `InfoBarContainerView`, preserved |
| bottom InfoBar `ContentShadow` and its final separator | native container, preserved and horizontally contained |
| native Side Panel resize separator/shadow | native Side Panel, preserved |

There is no broad "hide all native separators" flag.

### 6.4 Focus and accessibility

Because the parent does not change, the explicit accessibility decision is to
preserve Chromium's browser-chrome traversal: top/bookmark container, the one
InfoBar pane, native Side Panel, then MCV's visible panes in their existing
order. A notice visually aligned with the end/right/bottom pane is still one
browser notice group, not a duplicated AX child of that pane. The InfoBar group
appears exactly once and its manager identity must match the ContentsContainer
that receives the semantic reservation on every applied generation.

If pane activation occurs while focus is inside the InfoBar, keep focus when
the same visible InfoBar survives; if the active manager removes that View,
follow Chromium's normal active-pane focus restoration. Tests must cover
forward/reverse traversal, queued critical-bar promotion, pane swap/close,
detach-to-new-window, and confirm there is no stale focus-list or AX node.

## 7. Stable content and overlay clipping

InfoBar remains an external BrowserView child, so it is not part of the clip
hierarchy. Each `ContentsContainerView` instead gains one persistent page
boundary below its optional Pane Header:

```text
ContentsContainerView
├─ PageViewportClipHost             current visible page size
│  ├─ PageTargetHost                 target-sized during resize animation
│  │  └─ renderer-sized page children
│  └─ ViewportOverlayHost             current-size anchored page UI
├─ Pane Header / mini toolbar
├─ semantic emphasis / card outline
└─ capture border
```

The host migration is role-by-role, not a blanket reparent. Every consumer first
receives one parent-independent value:

```cpp
struct PageViewportGeometry {
  gfx::Rect page_environment_in_container;  // after Header + InfoBar, pre-DevTools
  gfx::Rect current_viewport_in_container;
  gfx::Rect target_stack_in_container;
  gfx::Rect current_clip_in_target;
  gfx::Rect current_viewport_in_screen;
  gfx::RoundedCornersF viewport_radii;
  uint64_t generation = 0;
};
```

Public bounds methods continue returning `ContentsContainerView`-local or
screen coordinates regardless of nesting. `GetContentsViewBounds()`, DevTools
dock inference, capture-border offsets, tab-modal/find/status conversion, and
controller APIs use explicit conversions or this value; they never compare a
nested child's local `bounds()` directly with container-local bounds.

Before any move, a checked migration ledger records current parent, destination,
external getter/element id, controller-cached pointer, MCV focus-map membership,
native host/layer, z-order, coordinate-space API, destruction dependency, and
whether duplicate ids are legal. The implementation gate is:

| Existing family | Phase-one geometry/clip | Reparent gate or permanent role |
| --- | --- | --- |
| `ContentsWebView` | consume `PageViewportGeometry`; retain direct holder native clip/radii | may move only after observer, presentation outlet, tab-modal host, focus map, and native-holder tests are parent-independent |
| docked DevTools WebView/scrim, NTP footer/separator | reserve InfoBar before DevTools partition; convert all public bounds to container space; retain direct native clips | remain direct until dock-placement inference and footer ownership no longer assume direct-child coordinates |
| Read Anything immersive | consume shared geometry and explicit clip while direct | remain direct until destructor no longer calls `RemoveChildViewT()` on the old parent and destroy-with-overlay tests pass |
| data protection, Indigo, contents scrim, actor, Glic selection | share target-stack/current-clip geometry while preserving controller pointers | move under `PageTargetHost` only after per-family focus, lookup, layer recreation, and teardown tests |
| AI overlay dialog | clamp to current viewport and explicit clip | replace global first-matching lookup with active-pane retrieval or pane-unique id before any move; duplicate pane ids may not select an inactive overlay |
| toast anchor | derive from current viewport | non-painting and non-focusable; may move to `ViewportOverlayHost` after anchor lookup test |
| Glic context-sharing border | current-viewport geometry and persistent outer mask | inner controller layer may be recreated, but cannot remove the outer mask; preserve tab scope |
| Pane Header/mini toolbar, Yee emphasis, card outline | card geometry | permanent direct card chrome outside page clip |
| capture border | intersect with current page viewport in container coordinates | permanent direct card chrome; update the direct-child/highest-z invariant explicitly |

The initial ledger is concrete (`CCV` = owning `ContentsContainerView`):

| Family | Current owner / lookup | Focus, native, and lifetime edges | Phase-one disposition |
| --- | --- | --- | --- |
| `ContentsWebView` | CCV direct; raw member; `VIEW_ID_TAB_CONTAINER` | MCV focus map/subscription; `NativeViewHost`; bounds observer; Yee presentation outlet; tab-modal context; Read Anything points to it | direct; shared geometry plus holder clip/radii |
| DevTools WebView | CCV direct; raw member; `VIEW_ID_DEV_TOOLS_DOCKED` | accessible pane; `NativeViewHost`; dock placement inferred against CCV coordinates | direct; parent-independent dock geometry before any move |
| DevTools scrim | CCV direct; raw member/layer | accessible pane; follows docked DevTools bounds | direct; same post-InfoBar DevTools geometry |
| NTP footer/separator | CCV direct; raw members; separator element id | footer WebView is in MCV focus map/subscription and affects `GetContentsViewBounds()`/lower corners | direct; explicit container-space footer geometry/native clip |
| data-protection overlay | CCV direct; raw member | renderer-sized View; no pane-unique element id | direct until target/current clip and teardown tests pass |
| Indigo | CCV direct; raw member | explicitly before Contents in focus list; controller-created layer behavior | direct; preserve focus edge and explicit clip |
| AI dialog WebView | CCV direct; raw member; same element id can exist in both CCVs | `NativeViewHost`; current controller performs browser-global first match | direct; replace with active/owning-CCV lookup before any move |
| Read Anything immersive | CCV direct; raw member | MCV focus map/subscription; holds raw pointer to Contents; CCV destructor removes it from `this` first | direct until destructor is parent-agnostic |
| contents scrim | CCV direct; raw member/layer | renderer-sized, event-blocking semantics | direct; explicit page-environment clip and z-order |
| actor overlay WebView | CCV direct; raw member; `VIEW_ID_ACTOR_OVERLAY` | MCV focus map/subscription; `NativeViewHost` | direct first; PageTargetHost only after focus/native teardown gates |
| Glic selection host | CCV direct; element id; controller finds it through the owning CCV | tab-scoped; FillLayout/layer may be recreated; respects split | direct first; pane clip, focus/background, then optional target-host move |
| Glic context border | CCV direct; raw member/controller bound to Contents | non-event subtree; controller may replace inner layer | direct current-viewport role with persistent external mask |
| toast anchor | CCV direct; raw member | non-painting/non-focusable anchor | direct first; optional ViewportOverlayHost after lookup parity |
| mini toolbar / Pane Header | CCV direct; raw member | observes Yee presentation binding; disconnects during CCV destruction | permanent direct card chrome |
| emphasis / container outline | CCV direct; raw members | layered card paint; event ignored | permanent direct card chrome with fixed z-order |
| capture border | CCV direct; raw member | `GetChildrenInZOrder()` requires highest direct child; contents-relative offset | permanent direct; intersect via container-space geometry |
| Lens host | MCV direct; raw member; global Lens host id / FillLayout | current Lens is shared; controller requires BrowserView/MCV sibling ordering; MCV destructor nulls before child removal | permanent shared MCV host for this work |
| tab-modal / Find / Status | native host/Widget outside the page-host tree | tab-modal uses CCV context; Find owner uses BrowserView; Status is transient Widget on native Contents | never reparent; consume pane-local screen/container geometry only |

Rules:

- `PageViewportClipHost` and `ViewportOverlayHost` never adopt the oversized
  target bounds used to avoid renderer reflow;
- only MCV `BeforeApplyLayout()` calls
  `ComputeTargetPaneGeometry(TargetPaneInput, CurrentPaneGeometry)`. Its target
  page stack retains the current external InfoBar semantic top, while target
  outsets apply only to `PageTargetHost`; all registered renderer-sized
  siblings share that target coordinate system;
- BrowserView/InfoBar code has no target-geometry accessor. A debug assertion
  rejects using target output for direct InfoBar, current card/divider, or
  `PageViewportClipHost` bounds;
- the current-sized clip host owns the visible rectangular and rounded boundary
  every frame;
- `ContentsWebView` and every auxiliary WebView retain direct
  `NativeViewHost` radii and `MasksToBounds`; an ancestor layer is not trusted
  to clip platform native surfaces;
- Page Header, card outline/shadow, semantic highlight, close controls, and
  divider controls stay outside the page clip;
- overlay controllers may create or destroy their own inner layers, but may not
  destroy or replace the persistent page clip host;
- `PageViewportClipHost`, `PageTargetHost`, and `ViewportOverlayHost` use
  `FocusBehavior::NEVER`, are ignored as AX grouping nodes, and are event
  transparent except through visible descendants; migrated children retain
  their existing MCV focus-map entries or receive an explicit replacement;
- a child that cannot safely be reparented remains direct, consumes the same
  `PageViewportGeometry`, and receives an explicit current clip. It cannot be
  marked complete based only on a shared bounding rectangle;
- closing a split/window, renderer crash, and controller destruction while a
  Sidebar/Side Panel/InfoBar animation is active must invalidate callbacks by
  generation or weak pointer before hosts are destroyed.

### 7.1 Corner roles

Corner radii are resolved by role, not copied to every child:

- integrated single page: upper corners 0, lower corners 12;
- split Pane Card: card outline 12 on all corners; Pane Header inner upper
  corners 11; page viewport upper corners 0 and lower corners 11;
- hidden-Header fullscreen single page: all four page corners 12, matching the
  still-inset outer Surface;
- single pane with a `kContent` Side Panel: the page-facing internal panel edge
  is square and separated by native panel chrome; the panel/background owns the
  outer trailing lower corner. A `kToolbar` panel leaves the reduced main
  Surface's page corner roles unchanged;
- a footer or docked DevTools owns the corresponding visible lower corner while
  the covered WebContents corner becomes 0.

### 7.2 Lens and Glic

Current Lens is always a shared overlay. It keeps the existing direct MCV host,
global element id, FillLayout, and controller sibling topology; this design
does not insert a wrapper or claim pane-local split support. The shared host is
bounded by the resolved MCV Surface, so it cannot enter the Yee Sidebar or outer
gutter. Opening Lens before/after split characterizes and preserves Chromium's
current unsupported/shared behavior rather than inventing close/suspend policy.

Glic selection is the tab-scoped `OverlayBaseController` path. It consumes its
owning `PageViewportGeometry`, respects split, and may continue managing its
inner layer/WebView while the persistent pane host supplies a non-destructible
outer mask. Any reparent must generalize the controller's parent/z-order lookup
and pass focus/background/teardown tests first. This avoids relying on a one-time
`SetMasksToBounds(true)` that the controller can later undo by destroying its
host layer.

### 7.3 Small overlay containment

Clipping is not a substitute for usable layout. Fixed/preferred-size overlays
such as the 200/270-DIP AI dialog must clamp their size to the available page
viewport and use `AdjustToFit()`-equivalent placement. No interactive control
may exist only in a clipped negative-coordinate region.

## 8. Dependent native UI

### 8.1 Tab-modal dialogs

The dialog host already belongs to a `ContentsContainerView`, but split y
placement currently uses the hidden global Toolbar. Resolve the anchor from the
owning Pane Header/body boundary. The host/Widget ownership does not change.
For Yee split, both maximum size and anchor are pane-local so a dialog cannot
cross the divider; single-pane and non-Yee windows preserve Chromium's existing
whole-content maximum. Stacked split retains its resize-handle collision rule,
but clamps against that pane's actual card bounds.

### 8.2 Find Bar

The native owner/Widget remains unchanged. Its initial and collision-avoidance
bounds use `current_viewport_in_screen`, not all of BrowserView. Convert both x
and y from renderer selection coordinates to the host Widget. The bar may move
within the active pane but not into the Yee Sidebar, outer gutter, divider, or
inactive pane.

The owner must therefore return the active `ContentsContainerView` rectangle in
Widget coordinates from `GetFindBarClippingBox()` on eligible Yee windows; the
whole `BrowserView::bounds()` is not a valid clipping box. `FindBarHost` first
shrinks the bar width to that rectangle, then clamps x against both
`clipping_box.x()` and `clipping_box.right() - width`. The RTL branch must
include the clipping rectangle's nonzero origin instead of treating
`clipping_box.width()` as a window-relative right edge. Selection-avoidance
fallbacks use those same physical left/right edges. `FindBarView` already makes
the text field flexible, so shrinking may consume text-field width but must not
clip the match count, previous/next, or close controls.

### 8.3 Status Bubble

Continue anchoring the transient Widget to its `ContentsWebView` native view.
Clamp the popup and its one-DIP page overlap from that pane's
`current_viewport_in_screen`. Browser UI may overlap its page pixels but not the
outer gutter or a neighboring pane. Clipping and hit-test tests must also prove
that an underlying page action does not receive a second click through visible
Find/status/dialog UI.

## 9. Paint and hit-test order

The hard partial order, back to front, is:

```text
window/Surface substrate
  < MCV page hosts and page-owned overlays
  < direct InfoBar + ContentShadow
  < native main-shadow / Side Panel / animation-content when overlapping
  < single combined Surface outline (event ignored)
  < anchored Widgets
  < full-window scrim / window-modal UI
```

This preserves BrowserView's native `MCV < InfoBar < main shadow < Side Panel`
relationship, including underlap. An actor/AI/Glic/Read Anything surface cannot
cover InfoBar controls, while a native Side Panel may occlude the notice only in
the same state where Chromium's native order does so. Split mode has no combined
outline; its MCV-owned Pane Card outline does not geometrically overlap the
inset InfoBar slot.

`ProposedLayout` does not set z-order, so child construction or explicit
`ReorderChildView()` establishes this partial order and a structural test locks
it. Outline shadows are painted behind their Surface/Card even when the outline
stroke View is later in child order.

The combined outline remains event-ignored. Clip hosts participate in hit test
only through their visible descendants. A clipped child cannot receive input in
the clipped region.

## 10. State and transition invariants

The following are hard invariants, including intermediate animation frames:

1. `main_surface_bounds` is contained by `content_column_bounds`.
2. A visible InfoBar manager identity is contained by exactly one matching body;
   its semantic height is absent from that body's entire page environment and
   from no other pane.
3. No page-owned layer intersects the Sidebar, outer gutter, divider, or an
   inactive pane outside its clip.
4. Structural width/height is consumed once; no later reset starts again from
   raw `browser_params`.
5. The visible rounded edge belongs to a current-sized host, never only to an
   oversized target child.
6. Single/split/fullscreen transitions clear stale radii, native clip rects, and
   target bounds before the destination frame is shown.
7. Side Panel and Sidebar hover animations do not re-enable a second background,
   outline, separator, or shadow.
8. Geometry is stable under LTR/RTL and 100/125/150/200% scale. Shared DIP Rects
   and radii convert to exactly the same device-pixel clip for every hard owner;
   no page pixel outside that clip is tolerated.
9. Native Side Panel width/padding and Yee Sidebar/gutter are each consumed
   once; an allocated panel reduces the whole split canvas, while native
   underlap may only occlude it from a higher z-order.
10. The InfoBar's parent, container identity, manager, delegate, focus-list
    node, and AX pane identity do not change when switching panes or modes.
11. Native top/Side Panel planning uses the current state snapshot and native
    integer rounding; no consumer reads last-applied panel bounds as current.
12. Pane preview is side-effect-free and byte-equal to the subsequently applied
    current MCV geometry for the same snapshot; target-size geometry is named
    and kept separate.
13. External InfoBar flow state is prepared outside proposed layout; one
    generation cannot re-enter BrowserView layout or leave a one-frame stale
    DevTools/page top.
14. Every public page/content rect declares container-local, target-local, or
    screen coordinates and remains semantically stable after reparenting.
15. Ineligible browser classes never enter Yee Surface geometry through a
    process-global feature predicate.
16. Teardown, renderer crash, and reversed transitions invalidate stale layout
    or controller callbacks before their target View/layer is destroyed.
17. The finalized Header rectangle passed to native top-child calculation is
    byte-equal to `ResolvedBrowserSurfaceFrame::header_bounds`; the panel/body
    origin derives from that same bottom and the 6-DIP seed is never reapplied.

Reduced-motion mode may shorten or eliminate cosmetic interpolation, but must
reach the same final layout and focus state.

## 11. Implementation boundaries

### Yee-owned code

`browser/ui/` owns:

- metric values, plain native snapshot input, `BrowserSurfaceLayoutInput`,
  `ResolvedBrowserSurfaceFrame`, the stable BrowserView minimum composition,
  and the separate supported-notice minimum query;
- Yee-specific state mapping for Sidebar/gutter/Header/Surface roles;
- Surface/Pane paint, outline, shadow, persistent clip-host factories, and
  duplicate-decoration policy helpers.

### Minimal Chromium glue in patch 0001

Chromium View glue owns:

- extracting the current native top-container/Side Panel allocation as a pure
  value while preserving native branch order, animation-content bounds,
  underlap, integer rounding, and decoration inputs;
- collecting that state and calling the Yee resolver once;
- placing existing child Views from the resolved rectangles;
- keeping the existing InfoBar View parent while applying its active-body
  bounds, native flow-metrics/shadow-clip API, identity-keyed semantic page
  reservation, and non-reentrant generation contract;
- extracting type-separated current/target pane geometry and per-pane
  directional minimums so BrowserView can consume current only while MCV target
  animation owns renderer outsets;
- hosting persistent page clip Views and propagating the same geometry to
  native WebViews and controller-managed exceptions;
- preserving parent-independent coordinate, focus-map, element-id, z-order, and
  teardown contracts for every migrated child;
- pane-aware dialog/find/status coordinates.

`TabStripModel`, InfoBar delegates/managers, `WebContents`, Omnibox, page
actions, and native Side Panel models remain unchanged.

The Side Panel allocation extraction and generic InfoBar flow/presentation
queries are nontrivial Chromium View glue. They are required seams, not implied
to exist already and not hidden inside a Yee config struct. They may not import
Yee product policy into `TabStripModel`, Side Panel entry models, or InfoBar
delegates.

## 12. Implementation sequence

1. Add characterization tests for browser-class routing, current InfoBar
   parent/manager/focus/AX and synchronous manager switch, native Side Panel
   branch order/rounding/animation-content, MCV current-versus-target geometry,
   every direct child's lookup/focus/lifetime/z-order, and platform fullscreen
   callbacks.
2. Introduce `UsesYeeBrowserSurfaceGeometry()` and pass it into shared MCV and
   ContentsContainer code; prove all ineligible browser classes retain their
   existing hierarchy and bounds.
3. Extract no-child `PlanNativeHorizontalLayout()` and finalized-Header
   `ResolveNativeTopAndSidePanelLayout()` with equivalence tests before Yee use.
   Sample start/mid/end ticks; assert top children are calculated once in final
   Header coordinates and panel/notice-flow/body/occlusion/animation/shadow
   values equal applied native layout without raw/previous-bound resets.
4. Extract pure, type-separated `ComputeCurrentPaneGeometry()` and
   `ComputeTargetPaneGeometry()`, the stable BrowserView minimum, and diagnostic
   supported-notice floor. Prove no mutation, exact current preview/apply parity,
   target consumption only by renderer hosts, and no notice-driven OS minimum.
5. Add native `InfoBarFlowMetrics`/presentation clipping and the generation-
   keyed external flow transaction. First fix the single-pane PDF bounds; then
   add split, multiple bars, directional minimums, and pre-DevTools reservation.
6. Add the Yee frame resolver, retire the Yee full-window MCV inset reset, and
   instrument every MCV/outline/Header rect to prove descent from one frame.
7. Move outline bounds into proposed layout, explicitly establish child z-order,
   and remove manual positioning. Preserve all native internal separators and
   Side Panel decoration that are not duplicates.
8. Introduce `PageViewportGeometry` and convert public coordinate consumers
   while children remain in their current parents. Add current/target native
   clips to every WebView/overlay family before structural migration.
9. Add persistent viewport/target/overlay hosts. Migrate one child family only
   after its ledger gates pass; keep DevTools/Read Anything and any unsafe family
   direct until coordinate/destructor contracts are parent-independent.
10. Keep shared Lens on its MCV host, make tab-scoped Glic selection and small
    overlays consume pane geometry, and make tab-modal/Find/Status consume
    pane-local coordinates without changing native overlay scope.
11. Add platform/path-specific fullscreen transition epochs and cancellation of
    target outsets without changing the spec's inset/corner policy.
12. Remove only config fields made dead by the working resolver; do not combine
    unrelated Chromium layout abstractions for cleanup.
13. Regenerate `patches/0001-integrate-yee-shell.patch`, run the
    appropriate full build, gracefully stop every Yee process, relaunch, and
    validate the evidence matrix in the real app.

Each step keeps a buildable/testable checkpoint. A failed equivalence or
teardown gate stops the next migration. Do not land a geometry resolver unused
by the outline or renderer boundary, or a host reparent whose old lookup,
coordinate, focus, z-order, and destructor assumptions have not been removed.

## 13. Required automated coverage

### Geometry tests

- `YeeInfoBarUsesActiveSurfaceFlow`: expanded/collapsed single pane, InfoBar
  open/close, 0/1/every permitted priority slot, exact x/right and page-top
  relationship;
- `YeeSplitInfoBarUsesOnlyActivePane`: both orientations, pane switches, and
  manager-before-active-index transition, pane swap/close/detach, minimum
  supported axis with rigid actions reachable and page height nonnegative;
- `YeeExternalInfoBarFlowIsSingleGeneration`: one parent proposal per manager,
  structural, or animation generation; no reentry or one-frame stale
  WebContents/DevTools top;
- `YeeSidePanelAndSurfaceConsumeWidthOnce`: leading/trailing, content/toolbar
  types, row-sharing decision, entry/alignment switch mid-animation,
  open/close, narrow-window underlap, animation-content and native
  `ClampFloor`/`ClampRound` parity;
- `YeeNativePanelPlannerMatchesAppliedLayout`: sampled current native planner
  output equals applied panel, animation-content, body, shadow, and unclipped
  region; finalized Header bounds produce top children exactly once, panel y
  follows final Header bottom, notice-flow/occlusion Rects are explicit, and no
  raw-params second top layout or last-applied-bounds input exists;
- `YeePanePreviewIsPureAndMatchesApply`: no state/View/layer mutation or
  invalidation; current preview child Rects byte-equal applied Rects while a
  different target-size layout exists;
- `YeeTargetPaneGeometryIsRendererOnly`: only MCV target-animation code can
  create target geometry; active InfoBar/current card/divider/clip-host Rects
  stay byte-equal current output while PageTargetHost consumes target outsets;
- `YeeMinimumCompositionIsLayoutIndependent`: stable BrowserView minimum and
  current supported-notice directional minima are correct before first layout
  and after state changes without transient data, OS-minimum mutation, or saved
  ratio mutation; BrowserView/frame minimum is byte-equal across every InfoBar
  animation tick;
- `YeeSurfaceDecorationUsesResolvedBounds`: outline, separator, background, and
  shadow use one rectangle and one owner;
- `YeeGeometryScopeMatchesBrowserClass`: NORMAL eligible; APP/PWA, APP_POPUP,
  POPUP, standalone DevTools, Picture-in-Picture, WCO PWA, and feature-off keep
  their previous geometry; docked DevTools in NORMAL participates;
- `YeeFullscreenPreservesSurfaceWithoutStaleChromeRows`: hidden,
  always-visible, content-fullscreen, transition interrupt/reverse/close, and
  immersive-reveal states on every applicable platform path; keep the 6-DIP
  Surface while reserving only actually participating chrome.

### Clip and animation tests

- `YeePageViewportHostOwnsCurrentVisibleClip`: single/split and every Sidebar
  animation frame;
- `YeePageOverlayHostsAreHardClipped`: main/auxiliary WebViews, Glic selection,
  AI, actor, watermark, immersive, and Indigo; shared MCV Lens retains its
  distinct native scope and Surface bounds;
- `YeeSplitTransitionClearsStaleClip`: enter, reverse, orientation change, pane
  switch, tab switch/detach, exit, renderer crash, and window close;
- `YeeSmallPaneKeepsOverlayInteractive`: minimum pane with AI/fixed overlays;
- `YeeInfoBarShadowStaysInsideActiveBody`: horizontal paint cannot enter gutter
  or divider, bottom overflow remains visible and event-transparent, and
  hidden-Header top radii clip the InfoBar background;
- `YeeNestedCoordinateAPIsStayContainerLocal`: DevTools left/right/bottom,
  footer/capture border, target outsets/negative origins, and each migration
  before/after parent change;
- every native WebView holder is checked separately because an ancestor Views
  layer is not proof of platform-native surface clipping.

### Interaction and accessibility tests

- InfoBar parent/container/manager/delegate identity, forward/reverse focus
  order, queued-bar promotion, close action, pane activation/swap/close/detach,
  and accessibility-tree uniqueness;
- every host/migrated family preserves MCV focus-map traversal, active-pane
  element lookup, duplicate-id behavior, controller pointer validity, and
  destruction order;
- tab-modal, Find Bar, and Status Bubble containment per pane;
- keyboard and screen-reader behavior through single/split/fullscreen changes;
- RTL and reduced-motion variants;
- create/destroy, tab background/foreground, renderer crash, split exit, and
  window close with each overlay family visible during a layout animation.

### Mandatory state matrix

Each row requires geometry assertions. Rows that involve compositor/native
surfaces, shadows, or rounded clipping also require start/mid/end pixel evidence
in the real app; pairwise sampling may reduce redundant cells only after every
named transition and owner has direct coverage.

| Axis | Required cells |
| --- | --- |
| Browser class | eligible NORMAL; APP/PWA; APP_POPUP; POPUP; standalone and docked DevTools; Picture-in-Picture; WCO PWA; feature off |
| InfoBar lifecycle | 0/1/all native priority slots; add/remove/promotion; normal/reduced motion; active switch/swap/close/detach during open/close; manager identity equals reserved container every applied frame |
| Split | side-by-side/stacked; active start/end; LTR/RTL; min/normal; ratio drag/orientation/swap/close/detach; no saved-ratio mutation |
| Side Panel | closed/open/opening/closing; toolbar/content; leading/trailing; entry/alignment switch mid-animation; underlap; panel animation-content preserved |
| Fullscreen/window | macOS legacy/native async and immersive; Windows async feature on/off and WCO; Linux async WM; reverse/interrupt/close; restored/maximized/Zoom/snap/tile |
| Scale | Windows 100/125/150/200%; macOS Retina/non-Retina; Linux 100/200%; exact device-pixel hard clips |
| Overlay family | renderer/aux WebViews; DevTools each dock edge; footer; Read Anything; actor/AI/Glic selection; shared MCV Lens; watermark/Indigo; create/destroy/crash/background/foreground/target animation |

### Real-app pixel matrix

Use real tabs and the real PDF InfoBar. Validate all four corners and every
separator/shadow at:

- expanded and collapsed Sidebar;
- single, side-by-side split, and stacked split;
- Side Panel closed/open/animating;
- minimum supported window and normal window;
- restored, maximized, content fullscreen, and immersive reveal;
- light/dark, active/inactive window;
- macOS Retina/non-Retina and Windows/Linux 100/125/150/200% where available.

Each evidence record includes revision/build id, OS/window manager and scale,
feature flags, window DIP/device-pixel bounds, resolver/native-planner/MCV/page
Rect dump with generation, start/mid/end screenshots, and expected/actual
result. Layer-property/radii tests cannot prove native compositor clipping; a
disabled broad BrowserView layout test is also not proof. Pixel evidence is
required for renderer, auxiliary WebViews, rounded corners, and InfoBar shadow
leakage.

## 14. Acceptance criteria

The design is implemented only when:

- the PDF default-viewer InfoBar is inside the correct single or active split
  body selected by manager identity and pushes the entire page/DevTools
  environment without entering any gutter;
- the InfoBar separator and shadow remain inside the same surface;
- the Browser Surface outline, MCV, and renderer viewport share one resolved
  geometry source;
- native Side Panel planner output matches applied current child/shadow bounds,
  including mid-animation switches and underlap, without previous-frame reads;
- MCV preview is pure, current/target geometry is never conflated, and external
  InfoBar flow cannot re-enter layout or appear in the wrong pane for one frame;
- Side Panel, fullscreen, small-window, RTL, and all animation states preserve
  the invariants above;
- every page-owned compositor and native WebView is hard-clipped to its final
  visible pane;
- every migrated child has passing lookup, focus/AX, coordinate, z-order,
  controller-lifetime, crash, and teardown gates; unsafe children remain direct
  with explicit geometry/clip rather than being blanket-reparented;
- ineligible browser classes retain their prior geometry and shared/tab-scoped
  overlay behavior remains native;
- no duplicate background, outline, separator, or shadow appears;
- all geometry and interactive tests pass, the full build passes, and the real
  Yee app matrix has been captured after a graceful restart.

## 15. Non-goals

- replacing Chromium's tab, InfoBar, Side Panel, page-action, or WebContents
  models;
- routing a native Side Panel into a reserved Yee Sidebar slot;
- converting shared Lens into a pane-scoped overlay, converting tab-scoped Glic
  selection into a shared overlay, or closing either merely because split starts;
- clipping window-global dialogs and toolbar bubbles to page corners;
- introducing a second set of Yee dimensions in Chromium layout constants;
- treating a prototype page as verification of native compositor behavior.
