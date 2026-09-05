# Theme hardening checkpoint — 2026-09-05

The user approved revising the existing identical-hover/selection and exact
native-security-color requirements. The shell spec records the new contract;
geometry, tab ownership, profile policy, and reserved Sidebar slots are unchanged.

## Changes

- Resolve the Header color from the first two rows of an 8-DIP compositor strip.
  Each row needs at least 75% opaque coverage and a color-distance cluster
  covering at least 55% of its width. This removes the 16-channel bucket-boundary
  discontinuity and prevents the lower page majority from overriding its edge.
- Click/tap, activation keys, and viewport resize restart bounded sampling.
  Loading/visibility/theme/interaction settling schedules one verification two
  seconds later. Navigation, hiding/occlusion, and scroll sampling cancel it;
  its callback cannot rearm itself. Arbitrarily late DOM-only changes remain
  outside this bounded policy; this is not a permanent paint observer.
- Popup hover and selected backgrounds have separate 6% and 14% state tints.
  Tint direction permits readable ink on both. The strongest-state popup ink
  is shared with resting/hover rows and native answer/chip children, protecting
  all three backgrounds without washing the state fills out on white pages.
  Header text roles stay unchanged.
- A dangerous security label preserves its native semantic color when readable
  and adjusts toward the contrasting endpoint only when needed for 4.5:1 text
  contrast. Native/high-contrast presentation bypasses custom correction.
- Header gate includes the new sampler cases and explicitly disables launcher
  retries. Rendered color and presentation-binding cases also run in RTL at
  DSF 1.25. Existing layout checkpoint changes in the worktree are preserved.

## Regression coverage

- Every adjacent grayscale pair, including all former quantization boundaries.
- Top-edge ownership despite a differently colored lower majority; sparse alpha;
  conflicting rows, broad gradients, and empty samples.
- Interaction/resize invalidation, hidden-tab cancellation, delayed capture
  publication, and proof that late verification stops rather than polling.
- Deterministic geometry fixtures retain their injected color through later
  resize/late-verification events; this capture suppression is test-only.
- All grayscale surfaces for hover/selection contrast and distinction, and
  native-warning preservation/correction. Existing native/high-contrast popup
  provider checks remain enabled.
- A real-renderer split-pane test changes only an 8px header element after
  initial settling; no metadata/navigation/scroll/input event provides a hint.
  Both split orientations check the live source and the actual Omnibox binding,
  then use a real synthesized click to change the boundary again.

For a manual real-tab check, open
`chromium-dev/fixtures/theme-boundary-regression.html` in a freshly built Yee app.
It is page content, not a browser mockup. The large body deliberately disagrees
with its thin top edge. Use the delayed button and resize below 800px.

## Verification status

- `./chromium-dev/test-header.sh all`: final-source build succeeded; 38/38
  unit tests, 12/12 interactive tests, and 4/4 RTL/125%-scale tests passed.
- Expanded color suite: 42/42 passed, including shell/sidebar/resting-text
  roles. This overlaps the Header unit suite; counts are not additive.
- `./chromium-dev/test-browser-surface-layout.sh interactive --no-build`:
  31/31 applied-layout tests and 8/8 RTL/125%-scale transition tests passed
  against the freshly rebuilt interactive binary. Launcher retries were zero.
- Added ten unit tests and one renderer test parameterized for both split
  orientations. The new grayscale test caught collapsed hover/selection fills
  on white during implementation; the final algorithm and all suites pass.
- `./chromium-dev/build.sh`: succeeded (integrated app current, no remaining
  build steps). Native whitespace checks, overlay whitespace checks excluding
  the unified patch, reverse patch applicability, and Header shell syntax pass.
- Launched the freshly built app with real `about:blank` and regression-fixture
  tabs after the graceful-shutdown check. Manual visual verification remains
  incomplete: the computer-use service returned `cgWindowNotFound`. The real
  renderer tests above do verify capture-to-Omnibox updates, but do not substitute
  for a human visual review of the complete window.

## Follow-up: resize/collapse/reload investigation

After unlocking macOS, manual inspection confirmed top-edge color ownership,
delayed CSS color updates, and responsive color changes. It also observed a
stale-width edge after shrinking the window and collapsing the Sidebar, followed
by a white page after reload. Expanding the window and switching tabs did not
restore the visible page, although its accessibility content remained present.
This observation is unresolved; it is not yet attributed to a code defect.

Added `Flyover/YeeRendererResizeUiTest.YeeResizeCollapseReloadKeepsRendererLive/*`
to the ordinary and RTL layout gates. It checks actual `window.innerWidth`
against the hosted WebView and reads renderer sentinel pixels from the browser
compositor after resize, collapse, reload, and expand. It does not inject a
surface color or force recursive layout. Both flyover settings are covered:
macOS disables that optimization by default, unlike the older layout fixture.

The new test passes in both modes. The expanded gate passes 33/33 normal and
10/10 RTL/125%-scale cases with zero retries; the test binary build and patch
reverse applicability/whitespace checks pass. No product-code fix has been
applied: the automated sequence does not reproduce the manual failure.
Browser tests disable occluded-window backgrounding, so their display lifecycle
is not equivalent to an ordinary app. A fresh real-app verification is blocked
again by `cgWindowNotFound`; an accessible, unlocked desktop is needed to
separate a window/capture lifecycle issue from the suspected layout failure.

### Unlocked desktop, isolated-profile validation

The next real-app check used a new empty profile after confirming that the
previous Yee process had exited normally. No product code changed between the
manual failure and this check. The following real-window operations passed:

- Shrink window, collapse Sidebar, reload, and expand Sidebar: page remains
  rendered and responsive; the earlier stale-width/white-page failure was not
  reproduced.
- Enter and exit immersive Reading Mode: original page restored.
- Create a side-by-side split with the regression page and a real New Tab Page;
  open the New Tab Page's Customize Chrome Side Panel, resize the window and
  collapse the Sidebar: both panes remain rendered within their boundaries.
- Click the regression page while the other pane's contextual Side Panel is
  open: the page becomes active, the contextual panel closes, and the delayed
  edge color updates only that pane's Header.

The original-profile failure remains unconfirmed, not fixed or disproven.
Reading its complete accessibility snapshot was blocked by safety review because
unrelated private tab titles/URLs could be exposed. Permission was requested;
until granted, validation is limited to the isolated profile. Neither profile
corruption nor screen locking has been established as the cause.

## Header/page color-space continuity

The user's cropped split-header image shows a hue difference between the Header
and the page's thin boundary strip, distinct from the Settings card/background
roles discussed earlier. The sampler used `SkBitmap::getColor`, whose documented
contract ignores `SkImageInfo` color space; the Header paints `SkColor` as sRGB.

Normalize the bounded 96-by-2 sampled area to unpremultiplied sRGB using Skia's
color-managed `readPixels` before clustering. Failed conversions fall back through
the existing no-sample path. Geometry, page styling, and sampling frequency are
unchanged. This corrects differing encodings of sRGB-representable page colors;
it does not introduce an HDR/wide-gamut native Header representation.

New tests cover Display P3 readbacks of the reported blue-gray and chromatic
red/green surfaces, sRGB identity, and F16 linear-sRGB readback. The P3/linear
fixtures also assert that their raw channels differ, ensuring they exercise the
old mismatch rather than testing only an sRGB bitmap.

Validation: `test-header.sh all` built successfully and passed 40/40 unit,
12/12 interactive, and 4/4 RTL tests without retries. The focused six sampler
tests also pass. `build.sh` confirmed the integrated app is current. In a fresh
real Yee process, the active single-pane Header and the page's `#506478` boundary
strip visually match after delayed CSS changes; the Omnibox popup follows that
color too. The split renderer/binding paths passed automated tests; the final
manual delayed-color check in split mode was interrupted by an external window
change (`noWindowsAvailable`), so that specific visual check is not claimed.
