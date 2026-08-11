# Space-conscious Chromium environment

This directory manages a real local Chromium checkout without committing its
source or build artifacts to `yee`. Generated data lives under
`yee/.local-build/`, which is Git-ignored.

## One-time setup

```sh
./chromium-dev/doctor.sh
./chromium-dev/checkout.sh
./chromium-dev/setup-metal.sh
./chromium-dev/configure.sh
./chromium-dev/build.sh
./chromium-dev/smoke-test.sh
```

The checkout uses `fetch --no-history chromium` and deliberately does not use
`--git-cache`, whose shared mirror is roughly 30 GiB. The pilot keeps one static
Release output with debug symbols disabled. This minimizes retained data at the
cost of slower incremental linking compared with a component build.

Chromium's ANGLE build requires Xcode's optional Metal Toolchain. Install only
that component when needed:

`setup-metal.sh` downloads that component only when missing and caches its
mounted compiler path under `.local-build/`.

Some Xcode 26 builds keep `xcrun metal` pointed at a stub even after the
component is installed. `build.sh` handles this locally by resolving the real
compiler from Xcode's installed-component status; it does not copy or modify
Xcode.

`checkout.sh` requires 115 GiB free before the large fetch; `configure.sh`
requires 45 GiB and `build.sh` requires 35 GiB. These guards leave room for
macOS and prevent a nearly-full volume from failing late in the workflow.

## Daily use

```sh
./chromium-dev/sync.sh
./chromium-dev/configure.sh
./chromium-dev/build.sh
./chromium-dev/smoke-test.sh
./chromium-dev/run.sh
./chromium-dev/usage.sh
```

The build defaults to two parallel jobs and a lower process priority so the
machine stays usable during a long compile. Override the job count for one run:

```sh
YEE_BUILD_JOBS=1 ./chromium-dev/build.sh  # quietest, slowest
YEE_BUILD_JOBS=6 ./chromium-dev/build.sh  # faster, heavier
```

`sync.sh` preserves the shallow checkout. `configure.sh` applies the small Yee
overlay once and regenerates the single `out/YeePilot` directory. The smoke test
uses an isolated temporary profile, waits for Chromium's DevTools endpoint, and
checks that a local data URL appears as a rendered page target. It needs no
network access and leaves no profile behind. On macOS, run it from a normal
Terminal session rather than a restricted process sandbox because headless mode
registers with the operating system's application services during startup.

Compiler and tool caches are redirected to `.local-build/cache/` instead of
growing invisibly under the user cache directory. They remain reusable across
incremental builds and are included in the local disk-usage boundary.

Do not create additional `out/*` directories while disk space is constrained.
If iteration speed eventually matters more than retained size, change
`is_component_build` to `true`, understanding that Chromium documents increased
binary size as the tradeoff.
