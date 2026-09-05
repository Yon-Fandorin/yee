# Browser Surface real-app validation matrix

This directory contains captures from the rebuilt macOS Yee app. Every launch
used an isolated profile, a graceful shutdown of the prior Yee process, and the
foreground `chromium-dev/run.sh` path. The earlier exact PDF-copy pass used the
explicit `chrome://infobar-internals` validation target and activated its PDF
row by keyboard so the WebContents click-pipe defect could not hide the result.
The 2026-09-03 refresh used Computer Use against the visible real app, enabled
internal debugging pages through `chrome://chrome-urls`, and activated the PDF
row through its native accessibility action in a fresh temporary profile.
Sidebar transition evidence was captured as a window-only macOS recording and
decoded at its original variable-rate frame timestamps rather than through the
post-action Computer Use screenshot delay.
The F13/F14 Side Panel pass used a fresh temporary profile and a real New Tab
Page. Computer Use could not match the local Chromium bundle even though
WindowServer reported an on-screen CGWindow, so the New Tab Page's real
customize action was invoked through its local DevTools endpoint and the native
window was captured directly by WindowServer ID.

## Results

| State | Evidence | Result |
| --- | --- | --- |
| 1171×768, single, Sidebar expanded/collapsed | `expanded-new-tab.jpeg`, `collapsed-new-tab.jpeg` | pass — Browser Surface stays inside the content column and outer edge |
| 1171×768, split with two real tabs, Sidebar expanded/collapsed | `split-expanded-two-tabs.jpeg`, `split-collapsed-two-tabs.jpeg` | pass — both pane cards and divider stay inside the Browser Surface |
| Browser-level default-browser and session-restore InfoBars, expanded/collapsed | `default-browser-infobar-expanded.jpeg`, `default-browser-infobar-collapsed.jpeg`, `session-restore-infobar-expanded.jpeg`, `session-restore-infobar-collapsed.jpeg` | pass — the one native InfoBar stays inside the active pane and below its Pane Header |
| 768×874, exact PDF-copy InfoBar, Sidebar expanded | `pdf-infobar-exact-expanded-sidebar.jpeg` | pass — `Chromium을 기본 PDF 뷰어로 설정`, its action, and close control remain visible; the InfoBar starts after the Sidebar and its trailing edge does not escape the window at a width narrower than the supported 800 px floor |
| Exact PDF-copy InfoBar refresh: 768×875 Sidebar expanded, 1413×768 Sidebar collapsed/round-trip, 1365×768 native fullscreen with hidden Header | `macos-real-app-pdf-infobar-expanded-20260903.png`, `macos-real-app-pdf-infobar-collapsed-20260903.png`, `macos-real-app-pdf-infobar-fullscreen-20260903.png`, `macos-real-app-pdf-infobar-fullscreen-roundtrip-20260903.png` | pass — the exact Korean message, action, close control, top-corner treatment, and horizontal shadow remain inside the current Browser Surface in every stable state; hidden-Header fullscreen removes the stale Header reservation without letting the notice escape the outer edge |
| Single-pane Sidebar collapse/expand transition; pre-fix 1284×880, fixed 1200×800 | Pre-fix: `sidebar-collapse-animation.mov`, `sidebar-expand-animation.mov`, `sidebar-expand-toolbar-overlap-midframe.png`; fixed: `sidebar-expand-animation-fixed.mov`, `sidebar-expand-animation-fixed-native-transition-contact-sheet.png`, `sidebar-expand-animation-fixed-mid-2333ms.png`, `sidebar-expand-animation-fixed-mid-2367ms.png`, `sidebar-expand-animation-fixed-mid-2400ms.png` | **pass after F23 fix** — the pre-fix expansion leaves Toolbar controls in the Sidebar, while all 19 native-timestamp frames from 2.215–2.588 s in the fixed recording keep Sidebar Toggle, Back, Forward, Reload, and Omnibox inside the moving Browser Surface Header edge |
| 1171×768 split Find Bar, expanded/collapsed | `split-expanded-findbar.jpeg`, `split-collapsed-findbar.jpeg` | pass at this width — bar stays inside its active pane |
| 1165×768 fresh real-app split with Find Bar | `macos-real-app-split-fixture-version-20260903.png`, `macos-real-app-split-findbar-20260903.png` | pass — two real tabs, both Pane Headers, the divider, and the native Find Bar stay inside their owning surfaces after the page-host migration |
| Native fullscreen, split, Sidebar collapsed/expanded | `split-collapsed-fullscreen-final.jpeg`, `split-expanded-fullscreen-repro.jpeg`, `split-expanded-fullscreen-roundtrip.jpeg` | pass on final and round-trip frames — no stale Sidebar reservation or outer-edge escape |
| 800×600 split, Sidebar expanded/collapsed | `minwidth-800x600-split-expanded.jpeg`, `minwidth-800x600-split-collapsed.jpeg` | pass for structural chrome — narrow web content clips inside its own pane |
| 800×600 split, Sidebar collapsed, Find Bar | `minwidth-800x600-split-collapsed-findbar.jpeg` | pass — bar is contained by the active pane |
| 800×600 split, Sidebar expanded, LTR light, Find Bar | `minwidth-800x600-ltr-split-expanded-findbar.jpeg` | **pre-fix failure evidence (F10)** — active pane is approximately x=250…517 while Find Bar paints at x=123…507, covering about 127 px of Sidebar |
| 800×600 split, Sidebar expanded, RTL dark, forced scale 1.25, Find Bar | `minwidth-800x600-dark-rtl-scale125-split-expanded-findbar.jpeg` | **pre-fix failure evidence (F10)** — the mirrored bar crosses the active pane edge into the right Sidebar |
| 800×600 post-fix split Find Bar, LTR light and RTL dark at scale 1.0/1.25 | `macos-real-app-minwidth-800x600-ltr-split-expanded-findbar-postfix-20260903.jpeg`, `macos-real-app-minwidth-800x600-dark-rtl-split-expanded-findbar-postfix-20260903.jpeg`, `macos-real-app-minwidth-800x600-dark-rtl-scale125-split-expanded-findbar-postfix-20260903.png` | pass — each native bar stays inside its active pane; at RTL/DSF 1.25 WindowServer reports the child Widget as 264×84 DIP with `alpha=1`, replacing the pre-fix 265×84 `alpha=0` state caused by lossy fractional-scale round-tripping |
| 1171×768 split, dark + RTL + forced scale 1.25, Sidebar expanded | `dark-rtl-scale125-split-expanded.jpeg` | pass for final structural bounds at the normal captured width; this OS capture is not exact device-pixel clip proof |
| 800×600 split, dark + RTL + forced scale 1.25, Sidebar expanded/collapsed | `minwidth-800x600-dark-rtl-scale125-split-expanded.jpeg`, `minwidth-800x600-dark-rtl-scale125-split-collapsed.jpeg` | pass for final structural bounds; this capture does not prove exact device-pixel clip equality |
| Native compositor hard-clip readback, single/animation-target/split | `BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeHardClipOwnersMatchAtDeviceScale` | pass at forced DSF 1.0/1.25/1.5/2.0 with launcher retries disabled — Views and NativeViewHost clip/radius inputs are exact matches; compositor sentinel pixels outside the owning body, including Sidebar/gutter/divider/inactive-pane space, are exactly zero |
| 1000×700 native Customize Chrome Side Panel, hidden/open, LTR/RTL | `f13-f14-side-panel-hidden-ltr.png`, `f13-f14-side-panel-open-ltr.png`, `f13-f14-side-panel-hidden-rtl.png`, `f13-f14-side-panel-open-rtl.png` | pass for stable real-app pixels — the panel remains inside the Surface and outer edge, starts exactly below the shared Header row, and mirrors from physical right to physical left without entering the Yee Sidebar |
| 1131×768 real split plus native Customize Chrome Side Panel | `macos-real-app-split-native-side-panel-settled-20260903.png` | pass for the settled state — the final native panel allocation remains to the physical right of both split panes and does not cover the Sidebar, divider, or either page |
| Native Side Panel transition gutter readback: closed, open start/mid/end, alignment switch, close, and reversal | `BrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal` | pass with launcher retries disabled — 21 actual compositor samples across the leading, trailing, and bottom 6-DIP gutters remain Yee shell material rather than the Chromium toolbar-white anti-crack background; applied Side Panel paint and background bounds also remain inside the one resolved Surface |
| Rendered native before-unload tab-modal dialog in a real split | `macos-real-app-split-beforeunload-navigation-modal-20260903.png` | partial pass — the real warning renders and its Cancel path works from the keyboard/accessibility surface, but Computer Use crops the modal window without the underlying browser, so pane-relative placement is not visually proven by this file |
| 1152×768 host-migrated Contents, two real tabs in split | `f18-host-migration-split-real-app.jpeg` | pass — both nested Contents surfaces remain below their Pane Headers and inside the Sidebar, divider, and outer rounded boundaries |
| 1152×768 host-migrated Contents plus right-docked DevTools in the right split pane | `f18-host-migration-split-devtools-real-app.jpeg` | pass for stable open/close pixels — the nested Contents and direct DevTools branch partition only the owning pane; closing DevTools restores Contents to the full pane without a crash or boundary escape |
| 768×875 WebUI Omnibox popup across two Yee windows and repeated activation changes | `macos-real-app-omnibox-multi-window-visibility-20260903.jpeg` | pass after F26 fix — the popup opens, closes when its window loses activation, and reopens on subsequent input after returning; four window cycles remain responsive, live log scans contain no fatal or `ValidatePopupState`, and no new macOS crash report appears |

The initial fullscreen transition captures are retained as timing evidence, not
as pass artifacts. A later final frame and an exit/re-enter round trip were
required before judging the stable fullscreen result.

## Retained diagnostic artifacts

- `sidebar-collapse-animation-contact-sheet.png` and
  `sidebar-expand-animation-contact-sheet.png` are pre-fix timing/navigation
  aids; the overlap mid-frame is the actual failure evidence.
- `sidebar-expand-animation-fixed-contact-sheet.png` and
  `sidebar-expand-animation-fixed-transition-contact-sheet.png` are intermediate
  decodes retained for comparison. The native-timestamp contact sheet and its
  three cited frames are the acceptance evidence.
- `split-expanded-fullscreen.jpeg` and
  `split-expanded-fullscreen-final.jpeg` are initial/alternate timing frames.
  They are not used to promote the fullscreen result; the cited repro and
  round-trip captures are.
- `split-expanded-picker-infobar.jpeg` records an InfoBar-picker diagnostic
  state and is retained for reproduction, not as pass evidence.
## Automated regression

- `YeeSurfaceGeometryTest.*`: 10/10 passed.
- The repaired fast gate runs `BrowserViewTabbedLayoutNativeGeometryTest.*` in
  `yee_layout_unittests` and all Browser Surface transition, multi-contents,
  viewport-geometry, and migration tests in
  `multi_contents_geometry_unittests`: 28/28 passed. This covers strict row
  thresholds, both panel types, horizontal/vertical/no-tab inputs, exclusions,
  width allocation, separator facts, one finalized-Header top-child
  calculation, reveal and transition rounding, four-edge local panel clipping,
  split inset, literal underlap behavior, and the Yee-owned geometry contracts.
- The focused F13/F14 applied-layout filter passed 8/8 in default LTR and 8/8
  in forced RTL + DSF 1.25 with retry limit zero. It compares the planner's
  current result to applied top-container, panel, animation-content,
  background, shadow, MCV, split-inset, and clip geometry. It also injects a
  deliberately distinct BrowserView-owned split inset and proves the target
  animation layout preserves the resulting content bounds. The fresh-plan
  animation-switch case also passed three consecutive runs.
- `SidePanelCoordinatorTest.ShowFromAnimationReparentsContentView`: 1/1 passed
  in the explicit browser-level gate.
- `SelectionOverlayBrowserTest.SelectionUsedFromController` and
  `SelectionStaysScopedThroughSplitLifecycle`: 2/2 passed at retry limit zero
  after replacing the overlay-parent ancestry assumption with a
  lowest-common-ancestor z-order comparison. The second test opens the real
  controller without a Toolbar button, verifies pane-local bounds through
  split focus changes, expands after the sibling closes, and restores Contents
  input when the overlay closes. The complete Side Panel/Lens/Glic browser
  gate passes 5/5.
- `VerticalTabsSinglePaneCollapse`, `VerticalTabsSinglePaneExpand`,
  `VerticalTabsSplitViewCollapse`, and `VerticalTabsSplitViewExpand`: 4/4
  interactive UI tests passed.
- `YeeHeaderSnapshotInvalidatesToolbarChildLayout` and the selected single-pane
  collapse/expand interactive flows: 3/3 passed after the F23 lifecycle fix.
- `YeeHardClipOwnersMatchAtDeviceScale`: passed on the first and only attempt at
  forced DSF 1.0, 1.25, 1.5, and 2.0. The 1.25 run read back a 1500×1708
  compositor surface and verified single, non-empty animation-target, active
  split, and inactive split clip ownership without a visual tolerance.
- `NativePlannerMatchesAppliedAnimationFramesAndReversal`: passes inside the
  complete 22/22 interactive gate with retries disabled. In addition to exact
  planner/applied bounds, it reads the actual compositor at three physical
  Surface gutters in seven controlled Side Panel states. All 21 samples are
  closer to Yee's shell material than Chromium's toolbar background, directly
  guarding the white-strip regression that a bounds-only assertion missed.
- `YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth`: passed with launcher
  retries disabled at 800×600; the current LTR run reports a 265-DIP active
  pane and 265-DIP Find Bar, and every visible child stays inside the Widget.
  `YeeFindBarUsesPixelStableBoundsAtMinimumWidth` additionally runs in forced
  RTL + DSF 1.25 and requires the actual Widget size to survive the same
  rounded-pixel/floored-DIP conversion used by remote Cocoa. It passed 5/5
  repeated runs, and the complete interactive gate passes 22/22 without
  launcher retries.
- `YeeStatusBubblesStayInsideOwningSplitPane`: passed with launcher retries
  disabled in default LTR and forced RTL + DSF 1.25. At 800×600 both the active
  and inactive pane kept their standard, opposite-side mouse-avoidance,
  mouse-exit restoration, and forced full-pane-width popup bounds inside the
  owning `ContentsWebView` screen rectangle.
- `YeeAiOverlayStaysInsideOwningSplitPane`: passed with launcher retries
  disabled in default LTR and forced RTL + DSF 1.25. The test forces an
  800×600 preferred overlay into each pane with right-docked DevTools and
  verifies exact physical containment and size clamping in both side-by-side
  and stacked splits, including the mirrored RTL origin.
- `YeeTabModalDialogHostStaysInsideOwningSplitPane`: passed with launcher
  retries disabled in default LTR and forced RTL + DSF 1.25. It first verifies
  Chromium's exact original position and maximum-size formulas in single-pane
  mode; both pane hosts in both split orientations then center a small dialog
  at the page-body boundary and keep the maximum advertised dialog rectangle
  inside the owning pane card.
- `YeeShellWithoutVerticalTabsUsesNativeToolbar`: passed in default LTR and
  forced RTL + DSF 1.25 with launcher retries disabled. With both vertical-tab
  features disabled, the native Toolbar retains the Location Bar, native split
  insets and divider opacity remain intact, and Yee mini-toolbar controls are
  absent.
- `YeePopupWindowUiTest.PaneHeadersRetainNativeControls`: passed with launcher
  retries disabled. The POPUP has no vertical-tab controller and neither pane
  mini-toolbar exposes Yee navigation or Sidebar controls.
- The four F24 collapse/expand flows directly count exactly one visible
  `kVerticalTabStripCollapseButtonElementId` in the browser context before and
  after every single/split transition.
- F3/F5/F20 structural-decoration coverage passes the new resolver unit test
  1/1, the complete pure Browser Surface geometry gate 14/14, the applied
  interactive gate 17/17, and the Side Panel/Lens/Glic browser gate 5/5. All
  launcher retries were disabled. The applied tests verify the resolved
  outline bounds/visibility, one separator owner, first-visible-InfoBar
  top-only radii, horizontal native shadow containment, and dynamic Side Panel
  z-order restoration.
- F6 ownership coverage now also asserts the reverse hierarchy invariant: the
  actual direct-container, PageTargetHost, and ViewportOverlayHost child sets
  exactly equal the migration-ledger inventory, including the NTP footer's
  direct separator. The strengthened applied test passes 1/1 at retry limit
  zero; the retained split Contents and docked-DevTools captures plus the 5/5
  Lens/Glic lifecycle gate close F6's previously recorded blockers. F18's
  remaining macOS lifecycle cells are covered by the expanded gates below;
  unavailable platform and real-feature cells remain external gaps.
- F11 fullscreen/immersive coverage passes all 18 pure transition/geometry
  tests and the complete 18/18 applied interactive gate with launcher retries
  disabled. `YeeFullscreenPreservesSurfaceWithoutStaleChromeRows` verifies
  both transition directions, committed-source geometry during a pending
  epoch, immediate target-outset cancellation, rejection of a deliberately
  reintroduced late target, hidden-Header 6-DIP/all-corner geometry, actual
  `NativeViewHost` and layer clips, and stable always-show-toolbar recovery.
- F18's completed macOS automation passes the complete applied interactive
  gate 19/19 and the expanded browser lifecycle gate 10/10, both with launcher
  retries disabled. `YeeAiOverlayStaysInsideOwningSplitPane` finds exactly two
  duplicate-id AI views and proves controller selection follows the active
  pane. `YeePageHostsPreserveSplitFocusAndContainerReuse` verifies forward and
  reverse focus order across both Contents views, both Pane Headers, and the
  divider; none of the three structural hosts becomes a focus target; closing
  and recreating a split reuses the cached containers without changing that
  order. The browser gate additionally covers Sad Tab movement after renderer
  death, Read Anything recreation after an unresponsive renderer, inactive
  pane activation, split-tab close, and synchronous window teardown, alongside
  the existing Side Panel, Lens, and Glic lifecycle cells. After replacing the
  NTP Omnibox transition fixture with deterministic `about:blank` tabs, the new
  focus/reuse cell also passes 5/5 consecutive runs.
- F26 Omnibox state synchronization passes
  `HiddenWidgetClearsClassicPopupState` 1/1,
  `MultiWindowActivationRestartsAutocompleteWithoutStaleState` 5/5 repeated,
  and `YeeOmniboxPopupFollowsSingleAndSplitHeader` 2/2 with launcher retries
  disabled. The tests cover a direct native Widget hide, activation transfer
  between two real Browser windows, autocomplete restart after returning, and
  normal popup movement between the single and split Pane Headers. The complete
  interactive Browser Surface gate passes 22/22.
- The combined F8/F9/F10 filter passed 4/4 in default LTR and 4/4 in forced RTL
  + DSF 1.25, with launcher retries disabled.
- The rebuilt Yee app/framework, `interactive_ui_tests`, and the narrow
  `yee_header_unittests` target were built with actual Ninja invocations after
  their current sources. A Ninja dry run is not used as build evidence because
  Siso can update virtual build-log state without producing the output.
- `chromium-dev/test-run-preflight.sh`: passed.

## Remaining gaps

- F5/F20 final pixels: the 2026-09-03 exact PDF pass covers the hidden-Header
  top-only InfoBar corner and horizontal ContentShadow in the real app. The
  Side Panel mid-animation gutter is now covered by controlled compositor
  readback; it is not inferred from applied View bounds or a loading skeleton.
- Transition pixels beyond the recorded Sidebar path: Sidebar collapse/expand
  has fixed-build native-timestamp evidence and closes the first-party F23
  pixel gap. Computer Use now also confirms stable native macOS fullscreen,
  exit/re-entry geometry, and an exact PDF InfoBar with the Header hidden.
  Controlled fullscreen ticks remain open; a settled post-action screenshot is
  not promoted to transition-time proof. Side Panel ticks are covered by the
  controlled in-process compositor readback above.
- Native Side Panel transition/split pixels: stable hidden/open LTR and RTL
  states plus a real split+panel state have OS-level captures. Controlled
  closed/open/mid/alignment/close/reverse frames now have actual compositor
  pixel evidence at all three exposed Surface gutters. A fresh post-fix
  OS-level capture remains unavailable because the display session captured
  black while locked; this is a capture-tool gap, not a missing controlled
  Side Panel pixel assertion.
- Cross-platform fractional-scale pixels: F22 now has exact macOS compositor
  readback at DSF 1.0/1.25/1.5/2.0. The old OS screenshot remains normalized
  and is not used as proof; Windows 125/150/200% and Linux 200% real-app runs
  are still unavailable in this macOS workspace.
- Status Bubble: pane-local popup geometry is now automated in both panes and
  in LTR/RTL + DSF 1.25, but the available desktop tool has no hover/mouse-move
  primitive. Real hover pixels and pointer-transition behavior therefore
  remain open rather than being inferred from the geometry test.
- AI overlay and tab-modal dialogs: pane-local host geometry is automated in
  both split orientations and direction/scale configurations. A real native
  before-unload dialog now has a rendered/Cancel-path pass, but its screenshot
  is modal-only and cannot prove placement relative to the underlying pane.
  Real AI WebUI controls and pane-relative tab-modal pixels remain open.
- Reserved Yee Sidebar slots: enabling Pins, Bookmarks, Chat, or Agent is a
  product-scope decision under `AGENTS.md`; it was not guessed during
  validation.
- Specialized page overlays and platform matrix: Glic, Lens, DevTools, Read
  Anything, Sad Tab, split close, and window teardown now pass their available
  macOS controller/lifecycle cells. Actual Data Protection, Indigo, and Actor
  feature states plus Windows/Linux native-host and compositor cells still
  require their real feature/platform environments.
- Fresh independent functional and commit-readiness reviewers passed the final
  source, test, patch, documentation, and artifact-evidence gate. They did not
  independently reproduce the real-app pixel capture matrix; those captures
  remain first-party evidence and findings stay `built` or `open`, never
  self-promoted to `verified`.
