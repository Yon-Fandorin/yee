# Split Canvas controls and Side Panel resize gutter

This is a historical implementation and validation record. The current product
contract lives in the shell and Sidebar documents; the shared values live in
`yee::kSidebarMetrics`.

## Presentation and ownership

- The controls' transfer corridor tapers from a locally widened departure edge
  into the controls. It does not include a triangle spanning the entire divider
  and page. Four directions and both diagonal approaches are covered.
- Leaving the monitored area allows 120 ms of pointer-transfer grace. Focus,
  explicit dismissal, disabled controls, and the existing reveal state machine
  keep their existing ownership.
- `kSidebarMetrics` owns the shared 10 DIP resize gutter and 4 by 24 DIP marker.
  Both Split Canvas and Side Panel presentation consume those metrics.
- A Yee resize gutter is a BrowserView sibling of the native Side Panel card,
  outside its content clip. The old in-card resize area is hidden only for Yee.
  The Side Panel card's backing remains non-interactive and unchanged.
- Chromium's `ResizeArea` retains mouse/touch tracking and RTL drag conversion.
  Native SidePanel callbacks retain width limits, persistence, and metrics.
  Yee supplies the shared resting/hover marker, pointer-following presentation,
  accessible slider name, focus ring, and native-equivalent arrow keys.
- The main surface excludes the same gutter in the shared Header row and in
  the page body. Native Side Panel allocation/animation planning is unchanged.
- Marker position is clamped again after layout so a shorter window cannot
  leave the transformed marker outside the gutter.

## Validation

- App build (including the final marker-clamping refinement) and patch
  reverse-application/whitespace checks passed.
- Fast layout gate: 22 native/Yee geometry tests plus 18 viewport/transition
  tests passed.
- Final Header unit gate: 48 tests passed, including the new diagonal corridor,
  external gutter geometry, keyboard/marker tests, and marker clamping after
  window height changes. The sandboxed attempt
  could not create test profiles or access AppKit; the isolated unit suite
  passed with the required GUI permissions.
- The eight resize/control tests also passed in RTL at 125% device scale.
- On September 6, after permission and screen unlock, the running Yee was
  gracefully quit through its menu and its exit was verified. The interactive
  gate rebuilt the last test correction and passed 33/33 normal plus 10/10 RTL
  tests without retries. This includes external gutter placement, Views
  hit-testing, native keyboard width updates, and the 10 DIP animation gap.
- A new integrated app process was then launched with an isolated profile.
  In its real New Tab and Customize Chrome Side
  Panel, screenshots visibly show the resting marker between the main card's
  right boundary and the Side Panel card's left boundary. No old in-card marker
  was visible in that state. This does not establish what the user's original
  profile/process was displaying.
- Manual dragging did not establish a width change. The screen locked again
  during investigation (`screenLocked=1`, following `noWindowsAvailable`), so
  physical mouse resizing remains unverified. Do not equate the passing Views
  hit-test or direct keyboard callback test with a verified native mouse drag.
  Before resuming manual verification, gracefully quit the remaining isolated
  Yee process and launch fresh, as required by the root work rules.

## Physical mouse coverage follow-up

After the next unlock, the isolated app was gracefully quit and relaunched.
Coordinate-based automation continued to return `noWindowsAvailable` even
while the screen was unlocked and accessibility-based actions/screenshot reads
worked. This tool failure is not evidence that the native resize handler fails.

Added `YeeSidePanelGutterPhysicallyResizesBesideWebContents` to the ordinary
interactive gate and its RTL/125% pass. It hosts a real WebView in the Side
Panel, raises the browser, sends native mouse down/move/up at the gutter's
screen coordinates, and checks a 64 DIP growth and restoration on both sides.
This closes the coverage gap between Views hit-testing/direct callback tests
and actual native pointer delivery.

The first run passed all 34 normal-direction tests but failed the new physical
drag test in RTL/125% on the right-aligned panel. Two focused reruns reproduced
it. Waiting for WebView load and compositor presentation did not remove it:
the resize area received the drag and native requested width changed from 378
to 442, but applied width remained 378.

Inspection found that the compact Toolbar's column-positioning spacer was
counted in its intrinsic minimum width. For a leading panel, this feeds its
already allocated width back into the main-content minimum. The correction
subtracts only that presentation spacer from `ToolbarView::GetMinimumSize` in
compact mode; preferred positioning and non-Yee behavior remain unchanged.
The physical regression additionally requires the Toolbar's intrinsic minimum
to stay unchanged across the drag.

The corrected app compiled, linked, and passed framework/bundle verification;
the integrated-app freshness check passes. `build.sh` stopped at its 35 GiB
free-space guard (25 GiB available), so an approved warm-cache incremental
`chrome`/`interactive_ui_tests` build was used after checking the normal
10 GiB regression reserve. No files were deleted to make space.

With no Yee process remaining, the corrected integrated app was launched in
the existing runtime profile for visual comparison. The screen locked again
before it could be captured (`cgWindowNotFound`, confirmed `screenLocked=1`).
Final corrected-build physical mouse verification and existing-profile visual
confirmation remain pending. The earlier 34-test normal pass and RTL failure
must not be reported as passing verification of this correction.

## Corrected-build verification

After temporarily increasing the battery display-off timeout for this work,
all remaining Yee browser processes were gracefully quit and their absence
confirmed before the final gates. The corrected build passed the applied
Browser Surface interactive gate: 34 normal-direction and 11 RTL/125% tests,
without retries. This includes actual
native mouse growth and restoration of both left- and right-aligned Side
Panels beside a real WebView, and invariant Toolbar intrinsic minimum width.
The previously reproducible RTL physical-drag failure is now covered and
passes. Patch reverse-application and native/root whitespace checks pass.

The corrected Header interactive gate also passed 12 normal and 4 RTL/125%
tests without retries. After confirming
no browser process remained, a fresh app was launched in the runtime profile.
Final manual capture was again blocked by `cgWindowNotFound`; the read-only
CG session check confirmed `screenLocked=1`. Do not report final
existing-profile visual validation complete.
