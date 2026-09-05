# Yee theming structural audit — 2026-09-05

## Conclusion and ownership

Yee already has the right ownership boundary: Chromium retains ThemeService,
ColorProvider, WebContents and the native Omnibox. Yee resolves presentation in
`chromium-overlay/yee-ui/chrome/browser/ui/views/yee/`. No replacement theme or
tab model is necessary for the findings below.

There are intentionally two background sources, not two competing themes:

| Surface | Source | Consumers |
| --- | --- | --- |
| Browser chrome | Chromium frame seed → `ResolveShellBackgroundColor` | Native tint, Sidebar, Content Gutter, split backing |
| Page-aware header | Per-WebContents `BrowserSurfaceColorController` → immutable presentation → container binding | Single Toolbar, active/inactive Pane Headers, Omnibox, suggestions |
| Native semantic UI | Chromium ColorProvider | Security/status colors and native accessibility overrides |

Shell colors must not follow a sampled website color. Header colors must follow
their own WebContents, including inactive visible split panes. The existing
binding's source identity, subscription teardown and popup revision mechanism
are useful safeguards and remain intact.

## Findings addressed

| Finding | Previous behavior | Correction | Regression evidence |
| --- | --- | --- | --- |
| Motion preference bypass | The controller's repeating timer interpolated colors regardless of reduced motion, unlike other Yee controls. | Use Chromium animation policy for page/tab transitions; finish an in-flight transition on the next tick when reduced motion is enabled. Do not publish the previous tab's color during reduced-motion activation. | `ReducedMotionPublishesOnlyTheFinalTabAndPageColors`, `EnablingReducedMotionFinishesAnInFlightTransition` |
| Hidden-tab work and stale captures | Visibility only handled becoming visible; an existing scroll burst/capture could continue after hiding. | Cancel sampling and invalidate capture epochs on hide/occlusion; settle the current transition; reject hidden capture/scroll starts. Resume bounded sampling on visibility. | `HidingTabCancelsScrollAndRejectsPendingCapture`, existing visible-split test |
| Missing terminal fallback | After exhausted capture attempts, missing page metadata left the previous document's committed color in place. | Once loading has finished, use the current Toolbar fallback and clear the stale page commitment. Never store the theme fallback as a page color. Ignore a fully transparent theme-color. | `MissingPageColorFallsBackWithoutCachingTheOldDocumentOrTheme`, `MissingColorDuringNavigationPreservesPreviousPresentation` |
| Theme refresh missed rendered CSS | Updating the fallback did not revalidate a cached page surface. CSS media-query changes need not emit a page theme-color/background metadata change. | A changed Toolbar fallback triggers bounded settling samples while retaining the last presented page color until verification. Hidden tabs defer sampling. | `ThemeChangesRevalidatePagesWithoutDiscardingTheirColor`, `ThemeChangeOnHiddenTabWaitsForVisibilityBeforeSampling` |

The theme-refresh fix is triggered by a changed Toolbar fallback. It does not
claim to observe arbitrary website DOM/CSS changes or every system notification
whose resolved Toolbar color stays identical. Continuous compositor capture is
not introduced.

## Comparison with public product evidence

This is a comparison of documented behavior, not a claim to know competitors'
internal theme architecture or to have tested their installed applications.

| Reference | Documented behavior | Yee implication |
| --- | --- | --- |
| [Dia 1.10.1](https://www.diabrowser.com/changelog/1-10-1), 2025-12-17 | Dark backgrounds retained when switching Notion/Slack tabs; group colors derived from favicon or URL-bar theme. | Yee already caches per-tab surfaces and derives group marks from page signals. Protect cache continuity while completing the missing fallback/lifecycle paths. |
| [Dia 1.15.0](https://www.diabrowser.com/changelog/1-15-0), 2026-01-22 | New Tab Page uses the profile color. | Profile-level identity should remain consistent with Chromium's theme source. A Yee-specific New Tab design is a separate product surface, not a reason to recolor websites or add a second preference store. |
| [Aside component changelog](https://docs.aside.com/changelog/components), 2026-06-15 / 07-07 / 07-13 | More consistent browser color-scheme changes; dark-mode menu hover fix; Appearance groups theme controls and shortcuts. | Audit refresh and interaction states across consumers, not only a dark resting screenshot. Keep general appearance controls in Settings under the existing Yee footer contract. |

## Product follow-through boundaries

- General appearance settings belong in the existing Settings route. The footer
  contract explicitly excludes duplicated Appearance/Search/Privacy/Profile
  controls (`sidebar/footer.md`).
- Chromium Profile color and Yee Workspace/Tenant identity are different scopes.
  A Workspace color picker, presets, or sync policy needs an explicit product
  decision before adding persistent state.
- A switch that disables page-aware headers would change the current shell
  specification's default contract. It should be designed as an explicit option
  before implementation, not inferred from another browser's appearance.
- High-contrast and transparency behavior still need native Windows/Linux/macOS
  validation. Passing color math tests does not prove every native widget,
  desktop material, or security-state presentation is correct.

## Verification

- `./chromium-dev/test-header.sh unit`: **28/28 passed**, including six new
  controller regressions and the updated theme-refresh regression. The first
  sandboxed execution could not create native test profiles/GUI resources;
  the successful run used the required local macOS access.
- Overlay source sync, repository whitespace check excluding the unified patch,
  Chromium source `diff --check`, and `0001` reverse-apply check: **passed**.
- `./chromium-dev/build.sh`: **passed**; the updated framework was copied into
  the integrated Yee.app bundle.
- `./chromium-dev/test-header.sh interactive`: **10/10 passed** in real native
  browser windows/tabs, covering single/split geometry, presentation sources,
  LocationBar rehosting, inactive-pane address activation and Omnibox popups.
  The script gracefully quit Yee before starting its browser processes.
- Manual pixel inspection: **not verified**. Launched the newly built Yee.app
  with light/dark local fixture tabs after checking no Yee browser was running.
  Computer Use returned `cgWindowNotFound` for both the app path and registered
  bundle ID. A graceful shutdown followed by a foreground relaunch produced the
  same error. App launch is not recorded as visual proof; native interactive
  assertions provide the available integration evidence.

No layout/model/glue changes were added by this checkpoint. Existing worktree
changes in the Browser Surface layout pipeline were preserved. The Header
interactive suite was an additional check because manual window inspection was
unavailable; broader Browser Surface layout milestone suites were not run for
this color-only controller change.
