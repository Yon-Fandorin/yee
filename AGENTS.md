# Yee work rules

Follow this file as process, not as a second product spec.
Product layout and names:

- [`docs/browser-shell-spec.md`](docs/browser-shell-spec.md)
- [`docs/browser-shell-layout-glossary.md`](docs/browser-shell-layout-glossary.md)
- Tab Sidebar UX decisions: [`docs/sidebar/`](docs/sidebar/)

## Ownership

- Chromium keeps `TabStripModel`, `TabView`, `WebContents`, Omnibox, and
  page actions. Do not replace them or rewrite the sidebar around a new
  tab model.
- Yee owns chrome presentation. New product UI belongs in
  `chromium-overlay/yee-ui/`. Chromium originals get the smallest glue
  that can host it.
- Do not put Yee product policy (Favorites cap, dock geometry, group
  mark painting) in `TabStripModel` or other tab-model targets. Views and
  command glue may call Yee helpers.

## Spec vs native code

- The spec is the product document. The prototype CSS is not.
- The native checkpoint is incomplete and already differs from the spec
  in places (tab row height, Favorites dock, group header). Match the
  current native UI plus the user's latest request.
- If a change would fight the spec, say so and ask. Do not silently
  rewrite the spec, and do not silently revert native UI back to an
  older spec paragraph.

## Unclear product questions

Ask before guessing, especially for:

- Arc Favorites vs Arc Pinned Tabs
- whether a feature is Yee chrome or Chromium tab-model
- Agent state vs browser tab state, and where it is shown
- enabling a reserved sidebar slot (Pins, Bookmarks, Chat, Agent)

## Overlay edits

- Yee sources: `chromium-overlay/yee-ui/`, synced with
  `install-yee-ui-sources.sh`.
- Chromium wiring: `patches/0001-integrate-yee-shell.patch`. Branding
  is `0002`, Windows proto aliases `0003`. Do not add a new patch for UI
  polish.
- `.local-build/` is generated. After Chromium glue changes, regenerate
  `0001` as a `git diff` of every touched Chromium file, and drop files
  that match upstream again.
- One visual contract: `yee::kSidebarMetrics`. Do not fork the same
  numbers into Chromium layout constants.

## Verify

- Build before calling UI work done (`build-ui.sh` if only `yee_ui`
  changed, otherwise `build.sh`).
- Confirm in the real Yee app with real tabs. A design mockup page is
  not proof. `file://` fixtures as actual tabs are fine.
- Before each real-app validation, request a graceful shutdown of every
  running Yee browser process, wait until it has exited, and then launch the
  newly built app. Do not validate a new build through an existing process.
- Do not invent launch URLs. Use `./chromium-dev/run.sh` only with URLs
  the user named.

## Git

Do not commit, push, or open a PR unless asked.
