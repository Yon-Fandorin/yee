# Browser Surface real-app validation matrix

This directory contains captures from the rebuilt macOS Yee app. Every launch
used an isolated profile, a graceful shutdown of the prior Yee process, and the
foreground `chromium-dev/run.sh` path. The exact PDF-copy pass used the explicit
`chrome://infobar-internals` validation target and activated its PDF row by
keyboard so the WebContents click-pipe defect could not hide the result.
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
| Single-pane Sidebar collapse/expand transition; pre-fix 1284×880, fixed 1200×800 | Pre-fix: `sidebar-collapse-animation.mov`, `sidebar-expand-animation.mov`, `sidebar-expand-toolbar-overlap-midframe.png`; fixed: `sidebar-expand-animation-fixed.mov`, `sidebar-expand-animation-fixed-native-transition-contact-sheet.png`, `sidebar-expand-animation-fixed-mid-2333ms.png`, `sidebar-expand-animation-fixed-mid-2367ms.png`, `sidebar-expand-animation-fixed-mid-2400ms.png` | **pass after F23 fix** — the pre-fix expansion leaves Toolbar controls in the Sidebar, while all 19 native-timestamp frames from 2.215–2.588 s in the fixed recording keep Sidebar Toggle, Back, Forward, Reload, and Omnibox inside the moving Browser Surface Header edge |
| 1171×768 split Find Bar, expanded/collapsed | `split-expanded-findbar.jpeg`, `split-collapsed-findbar.jpeg` | pass at this width — bar stays inside its active pane |
| Native fullscreen, split, Sidebar collapsed/expanded | `split-collapsed-fullscreen-final.jpeg`, `split-expanded-fullscreen-repro.jpeg`, `split-expanded-fullscreen-roundtrip.jpeg` | pass on final and round-trip frames — no stale Sidebar reservation or outer-edge escape |
| 800×600 split, Sidebar expanded/collapsed | `minwidth-800x600-split-expanded.jpeg`, `minwidth-800x600-split-collapsed.jpeg` | pass for structural chrome — narrow web content clips inside its own pane |
| 800×600 split, Sidebar collapsed, Find Bar | `minwidth-800x600-split-collapsed-findbar.jpeg` | pass — bar is contained by the active pane |
| 800×600 split, Sidebar expanded, LTR light, Find Bar | `minwidth-800x600-ltr-split-expanded-findbar.jpeg` | **pre-fix failure evidence (F10)** — active pane is approximately x=250…517 while Find Bar paints at x=123…507, covering about 127 px of Sidebar; the pane-local fix is built and automated, but a post-fix real-app capture remains open |
| 800×600 split, Sidebar expanded, RTL dark, forced scale 1.25, Find Bar | `minwidth-800x600-dark-rtl-scale125-split-expanded-findbar.jpeg` | **pre-fix failure evidence (F10)** — the mirrored bar crosses the active pane edge into the right Sidebar; the pane-local fix is built and automated, but a post-fix real-app capture remains open |
| 1171×768 split, dark + RTL + forced scale 1.25, Sidebar expanded | `dark-rtl-scale125-split-expanded.jpeg` | pass for final structural bounds at the normal captured width; this OS capture is not exact device-pixel clip proof |
| 800×600 split, dark + RTL + forced scale 1.25, Sidebar expanded/collapsed | `minwidth-800x600-dark-rtl-scale125-split-expanded.jpeg`, `minwidth-800x600-dark-rtl-scale125-split-collapsed.jpeg` | pass for final structural bounds; this capture does not prove exact device-pixel clip equality |
| Native compositor hard-clip readback, single/animation-target/split | `BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeHardClipOwnersMatchAtDeviceScale` | pass at forced DSF 1.0/1.25/1.5/2.0 with launcher retries disabled — Views and NativeViewHost clip/radius inputs are exact matches; compositor sentinel pixels outside the owning body, including Sidebar/gutter/divider/inactive-pane space, are exactly zero |
| 1000×700 native Customize Chrome Side Panel, hidden/open, LTR/RTL | `f13-f14-side-panel-hidden-ltr.png`, `f13-f14-side-panel-open-ltr.png`, `f13-f14-side-panel-hidden-rtl.png`, `f13-f14-side-panel-open-rtl.png` | pass for stable real-app pixels — the panel remains inside the Surface and outer edge, starts exactly below the shared Header row, and mirrors from physical right to physical left without entering the Yee Sidebar |

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
- `BrowserViewTabbedLayoutNativeGeometryTest.*` plus
  `YeeSurfaceGeometryTest.*`: 19/19 passed in the small
  `yee_layout_unittests` target. This covers strict row thresholds, both panel
  types, horizontal/vertical/no-tab inputs, exclusions, width allocation,
  separator facts, one finalized-Header top-child calculation, reveal and
  transition rounding, local clip, split inset, and literal underlap behavior.
- The focused F13/F14 applied-layout filter passed 8/8 in default LTR and 8/8
  in forced RTL + DSF 1.25 with retry limit zero. It compares the planner's
  current result to applied top-container, panel, animation-content,
  background, shadow, MCV, split-inset, and clip geometry. It also injects a
  deliberately distinct BrowserView-owned split inset and proves the target
  animation layout preserves the resulting content bounds. The fresh-plan
  animation-switch case also passed three consecutive runs.
- `SidePanelCoordinatorTest.ShowFromAnimationReparentsContentView`: 1/1 passed
  in the explicit browser-level gate.
- `VerticalTabsSinglePaneCollapse`, `VerticalTabsSinglePaneExpand`,
  `VerticalTabsSplitViewCollapse`, and `VerticalTabsSplitViewExpand`: 4/4
  interactive UI tests passed.
- `YeeHeaderSnapshotInvalidatesToolbarChildLayout` and the selected single-pane
  collapse/expand interactive flows: 3/3 passed after the F23 lifecycle fix.
- `YeeHardClipOwnersMatchAtDeviceScale`: passed on the first and only attempt at
  forced DSF 1.0, 1.25, 1.5, and 2.0. The 1.25 run read back a 1500×1708
  compositor surface and verified single, non-empty animation-target, active
  split, and inactive split clip ownership without a visual tolerance.
- `YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth`: passed with launcher
  retries disabled in default LTR and forced RTL + DSF 1.25. At 800×600 the
  active pane and Find Bar were both exactly 267 DIP wide (`x=549` in LTR,
  `x=28` in RTL), and every visible Find Bar child stayed inside the Widget.
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
- The combined F8/F9/F10 filter passed 4/4 in default LTR and 4/4 in forced RTL
  + DSF 1.25, with launcher retries disabled.
- The rebuilt Yee app/framework, `interactive_ui_tests`, and the narrow
  `yee_header_unittests` target were built with actual Ninja invocations after
  their current sources. A Ninja dry run is not used as build evidence because
  Siso can update virtual build-log state without producing the output.
- `chromium-dev/test-run-preflight.sh`: passed.

## Remaining gaps

- F10 post-fix real-app pixels: the newly built app could not expose a CGWindow
  because the macOS session was at the lock screen; Computer Use returned
  `cgWindowNotFound`, and a desktop capture confirmed only the lock screen.
  No post-fix visual pass was inferred from the automated geometry result.
- Transition pixels beyond the recorded Sidebar path: Sidebar collapse/expand
  now has fixed-build native-timestamp evidence and closes the first-party F23
  pixel gap. Equivalent InfoBar and fullscreen recordings remain open because
  the earlier Computer Use pass repeatedly lost the restarted Yee window with
  `cgWindowNotFound`; no pass was inferred from that tooling failure.
- Native Side Panel transition/split pixels: stable hidden/open LTR and RTL
  states now have real-app captures. Computer Use still cannot target this
  local bundle, and local DevTools can invoke the WebUI entry action but cannot
  freeze native chrome at a controlled animation tick or create the native
  split-tab state. Therefore true native mid-animation and split+panel pixel
  cells remain open; the exact applied-layout tests are recorded separately
  and are not promoted to visual evidence.
- Cross-platform fractional-scale pixels: F22 now has exact macOS compositor
  readback at DSF 1.0/1.25/1.5/2.0. The old OS screenshot remains normalized
  and is not used as proof; Windows 125/150/200% and Linux 200% real-app runs
  are still unavailable in this macOS workspace.
- Status Bubble: pane-local popup geometry is now automated in both panes and
  in LTR/RTL + DSF 1.25, but the available desktop tool has no hover/mouse-move
  primitive. Real hover pixels and pointer-transition behavior therefore
  remain open rather than being inferred from the geometry test.
- AI overlay and tab-modal dialogs: pane-local host geometry is automated in
  both split orientations and direction/scale configurations, but the real AI
  WebUI controls and a rendered native tab-modal dialog have not received a
  post-fix pixel/keyboard pass. Those visual-interaction cells remain open.
- Reserved Yee Sidebar slots: enabling Pins, Bookmarks, Chat, or Agent is a
  product-scope decision under `AGENTS.md`; it was not guessed during
  validation.
- Specialized page overlays and platform matrix: Glic/Lens/DevTools crash and
  Windows/Linux compositor cells require their real feature/platform states.
- Fresh independent functional and commit-readiness reviewers passed the final
  source, test, patch, documentation, and artifact-evidence gate. They did not
  independently reproduce the real-app pixel capture matrix; those captures
  remain first-party evidence and findings stay `built` or `open`, never
  self-promoted to `verified`.
