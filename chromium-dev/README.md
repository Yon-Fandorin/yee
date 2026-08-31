# Space-conscious Chromium environment

This directory manages a real local Chromium checkout without committing its
source or build artifacts to `yee`. Generated data lives under
`yee/.local-build/`, which is Git-ignored.

Both macOS (`.sh`) and Windows (`.ps1`) entry points use the same overlay and
compact `out/YeePilot` configuration. Do not share one `depot_tools` directory
between Windows and WSL/Linux because depot_tools keeps platform-specific state.

## Windows prerequisites

The current Chromium checkout requires:

- 64-bit Windows 10 or newer on an NTFS volume
- Visual Studio 2026 with **Desktop development with C++** and MFC/ATL
- Windows 11 SDK 10.0.28000.2270 or newer (including Debugging Tools)
- Git for Windows

`doctor.ps1` validates these without changing the machine. SDK and Visual Studio
may live on `C:` while source, depot_tools, and build output live on a roomier
drive such as `F:`.

## Windows one-time setup

From the repository root in PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\chromium-dev\doctor.ps1
.\chromium-dev\checkout.ps1
.\chromium-dev\configure.ps1
.\chromium-dev\build.ps1
.\chromium-dev\smoke-test.ps1
```

Generated data defaults to `.local-build`. To reuse or place it elsewhere,
define `YEE_LOCAL_BUILD_ROOT` before running any command:

```powershell
$env:YEE_LOCAL_BUILD_ROOT = 'F:\yee'
.\chromium-dev\doctor.ps1
```

That layout resolves to `F:\yee\depot_tools`, `F:\yee\chromium\src`, and
`F:\yee\chromium\src\out\YeePilot`. Advanced users can override only
`YEE_DEPOT_TOOLS_DIR` or `YEE_CHROMIUM_ROOT` instead.

If a compatible output tree already exists, reuse it instead of retaining a
second Chromium build:

```powershell
$env:YEE_LOCAL_BUILD_ROOT = 'F:\yee'
$env:YEE_OUT_NAME = 'Default'
.\chromium-dev\configure.ps1
.\chromium-dev\build.ps1
```

## Windows daily use

```powershell
.\chromium-dev\sync.ps1
.\chromium-dev\configure.ps1
.\chromium-dev\build-ui.ps1
.\chromium-dev\build.ps1
.\chromium-dev\smoke-test.ps1
.\chromium-dev\run.ps1
.\chromium-dev\usage.ps1
```

`sync.ps1` runs `gclient sync` for the commit currently checked out in
Chromium; it does not advance `src` to a newer `origin/main`. Updating Chromium
source while the Yee overlay is present needs an explicit clean/rebase/reapply
workflow so local patched files are never discarded implicitly. Keep source
updates separate, then rerun `configure.ps1` to check and apply the overlay.

`usage.ps1` recursively scans the large checkout and can take over a minute. It
is intentionally separate from `build.ps1` so a successful build reports
completion immediately.

Set the number of local build jobs for the current shell when needed:

```powershell
$env:YEE_BUILD_JOBS = '4'
.\chromium-dev\build.ps1
```

`doctor.ps1` recommends a machine-local job count capped at the number of
physical CPU cores and one job per 8 GiB of installed RAM. This conservative
limit was selected after six jobs left less than 2 GiB available on the pilot's
32 GiB Windows workstation; four jobs kept useful headroom. Record RAM, output
drive space, Siso working set, and completed work every 30 seconds while a build
is active with:

```powershell
.\chromium-dev\record-build-memory.ps1
```

Records are stored under the Git-ignored `.local-exclude/build-memory/` directory in
this repository. `machine.json` keeps the stable machine recommendation and
`build-memory.jsonl` keeps append-only session samples. Override the log path
with `YEE_BUILD_MEMORY_LOG` when needed.

The pilot does not yet have a distinct Windows install identity. Its installer
would share Chromium's install paths, AppID, and ProgID, so it can conflict with
an existing Chromium installation. For isolated installer development only,
acknowledge that limitation explicitly:

```powershell
.\chromium-dev\build.ps1 -Target mini_installer -AllowSharedChromiumInstallIdentity
```

The development browser remains `out\YeePilot\chrome.exe`, as expected by
Chromium's runtime layout. Its embedded product name and icon are Yee. The
installer target remains `mini_installer.exe` but installs the Yee display name.
Do not distribute it until the install-mode identity is separated.

`run.ps1` also supports a previously built unbranded Chromium binary as a fast
native checkpoint. It seeds an isolated profile with the pilot's 232px vertical
tabs preference and enables Chromium's native vertical-tabs features at
runtime. This reuses the real browser and tab model immediately, but it does not
retrofit Yee's embedded product name or executable icon; those still require a
successful source build.

## macOS one-time setup

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

## macOS daily use

```sh
./chromium-dev/sync.sh
./chromium-dev/configure.sh
./chromium-dev/build-ui.sh
./chromium-dev/build.sh
./chromium-dev/smoke-test.sh
./chromium-dev/run.sh
./chromium-dev/usage.sh
```

인자 없이 `run.sh`를 실행하면 URL을 추가하지 않고 기존 개발 profile 상태로
브라우저만 연다. URL이나 Chromium 플래그를 넘기면 전달한 인자를 그대로 사용한다.

`build-ui.sh`는 `//chrome/browser/ui/views/yee:yee_ui`만 빌드한다. Yee의 배경,
콘텐츠 외곽선, Agent activity 버튼처럼 분리된 시각 코드를 수정할 때 사용하는
빠른 컴파일 경로이며 기존 `out/YeePilot` 산출물을 그대로 재사용한다. 이 명령은
앱을 다시 링크하지 않는다. 실제 브라우저에서 결과를 확인할 시점에만
`build.sh`로 `chrome`을 증분 링크한다. PowerShell에서는 동일하게
`build-ui.ps1`을 사용한다. macOS의 두 빌드 명령은 먼저 overlay의 Yee UI 소스를
Chromium 작업 트리에 동기화하므로, `build.sh`만 실행해도 이전 복사본을 링크하지
않는다.

macOS의 `run.sh`와 `smoke-test.sh`는 분리 링크된 Yee Framework가 앱 번들 내부
Framework보다 새로우면 실행을 중단하고 `build.sh`를 안내한다. 이 guard는 빠른
UI 빌드만 통과한 오래된 앱을 새 결과로 오인하지 않도록 하며,
`test-run-preflight.sh`가 시간 순서와 누락 산출물 계약을 검증한다.

`run.sh`는 프로필과 개발용 lifecycle 인자만 전달하며 Glass, 색상, 투명도나
테마를 지정하지 않는다. macOS 26의 native `GlassFrame`은 Yee 코드의 제품
기본값이고 vertical Tab sidebar에 한정하지 않고 창 전체를 받친다. Yee 배경은
현재 Chromium frame theme 색을 사용하며 native tint와 Views tint를 합성해 초기
pilot과 같은 Light 약 43%, Dark 약 74%의 셸 불투명도를 만든다. macOS의
‘투명도 줄이기’가 켜졌거나
Glass가 지원되지 않는 Windows·Linux에서는 같은 테마 색을 불투명하게 그린다.
따라서 `run.sh`, Finder/Dock 직접 실행과 테스트 실행의 시각 결과가 같다.
이 효과는 실제 `WebContents`가 차지하지 않는 브라우저 UI 표면에서만 드러난다.
Yee Shell은 직접 실행할 때도 기본으로 켜진다. vertical tab 내용은 후속
sidebar UI를 위한 자리로 유지하고, Titlebar·Context·Status·Sidebar 구획을 표시한다. 중앙
Runway에는 별도 모형을 그리지 않고 Chromium의 실제 Toolbar를 compact하게
재배치한다. 순서는 sidebar toggle, 뒤로, 앞으로, 새로고침, 구분자, native
Omnibox, extension dock이다. 실제 `WebContents`는 8px gutter를 둔 Browser
Content 구획 안에 배치된다. 탭 버튼으로 sidebar를 접으면 WebContents와
renderer viewport가 native tab panel과 같은 진행값을 따라 왼쪽 8px까지
연속적으로 확장된다. 패널·콘텐츠·배경 윤곽의 시작과 끝이 한 프레임 흐름으로
맞춰지고 탭 rail의 투명도도 같은 값으로 줄어 마지막 프레임에 따로 사라지지
않는다. 왼쪽 끝의 투명한 8px 영역에 포인터를 두면 동일한 native sidebar가
8px 안쪽으로 이동하고 네 모서리와 얕은 그림자를 가진 glass flyout으로
표시된다. 콘텐츠의 13px과 구분되는 18px 곡률, 더 넓고 옅은 그림자를 사용해
겹친 카드처럼 투박해 보이지 않게 한다. 이때 페이지 폭은 바뀌지 않는다.
비교가 필요할 때
`--disable-yee-shell-scaffold`를 넘기면 기존 Chromium UI로 실행된다.

현재 Agent Status UI는 내부 런타임과 연결하지 않은 preview interface다.
일반 실행은 `Ready`로 고정되어 WebContents 합성을 주기적으로 갱신하지 않는다.
변화 테스트가 필요할 때만 `--yee-agent-status-demo`를 추가하면 `Ready`,
`Working`, `Needs input`을 2.5초 간격으로 순환한다. 특정 상태는
`--yee-agent-status=working` 또는 `--yee-agent-status=needs-input`으로 고정할
수 있다. `ready`도 명시할 수 있으며, 상태 고정 플래그가 있으면 demo 순환은
자동으로 멈춘다.

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
