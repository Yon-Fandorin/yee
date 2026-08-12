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

인자 없이 `run.sh`를 실행하면 기본 페이지로 `https://example.com`을 연다.
다른 URL이나 Chromium 플래그를 넘기면 전달한 인자를 그대로 사용한다.

`run.sh`는 macOS 26에서 Chromium의 native `GlassFrame`과 Yee의 옅은 민트
theme seed를 활성화한다. 이 효과는 프레임과 Toolbar, vertical Tab sidebar처럼
실제 `WebContents`가 차지하지 않는 브라우저 UI 표면에만 적용된다. Light tint는
38%, expand-on-hover 표면은 72%와 18px blur로 시작해 뒤 배경이 읽히도록 한다.
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
