# yee

Chromium을 기반으로 사용자와 에이전트가 함께 사용하는 브라우저를 탐색하는
pilot 프로젝트다. 현재 목표는 실제 Chromium 탭 모델을 유지하면서, 사용자에게는
Arc/Aside 계열의 조용한 sidebar-first 셸을 제공하고 에이전트에는 브라우저 내부
상태로 이어지는 명확한 통합 경계를 만드는 것이다.

실제 Chromium 구현에서 지켜야 할 레이아웃 치수, OS별 경계, Sidebar 정보 구조와
회귀 체크리스트는 [`docs/browser-shell-spec.md`](docs/browser-shell-spec.md)를
기준으로 한다.

## Native Chromium checkpoint

[`native-pilot/`](native-pilot/)은 설치된 현재 Chrome의 Chromium 런타임과 native
vertical tabs를 격리 프로필로 실행하는 가장 작은 실제 브라우저 체크포인트다.
Electron, CEF, `<webview>`를 사용하지 않으며 탭과 페이지는 실제
`TabStripModel`/`WebContents` 상태다.

```sh
./native-pilot/launch.sh
```

설치된 바이너리를 사용하는 이 체크포인트와 별도로,
[`chromium-dev/`](chromium-dev/)가 얕은 Chromium 체크아웃과 단일 compact Release
빌드 환경을 관리한다. 커스텀 Tenant/Workspace Launcher는 Chromium Views/WebUI
소스 통합에서 추가한다.

[`chromium-overlay/`](chromium-overlay/)에는 동일한 native vertical-tab 기본값을
upstream Chromium checkout에 적용하는 최소 source patch와 GN 설정을 둔다.
macOS 26에서는 Chromium의 native `GlassFrame` 합성을 실행 시 활성화해 웹
콘텐츠를 제외한 프레임, 툴바, Tab sidebar가 뒤 배경을 비추도록 한다.

## Local Chromium build

Chromium 소스, `depot_tools`, 빌드 산출물은 모두 Git에서 제외된
`.local-build/`에 생성한다.

```sh
./chromium-dev/doctor.sh
./chromium-dev/checkout.sh
./chromium-dev/configure.sh
./chromium-dev/build-ui.sh
./chromium-dev/build.sh
./chromium-dev/smoke-test.sh
```

Windows uses equivalent PowerShell entry points:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\chromium-dev\doctor.ps1
.\chromium-dev\checkout.ps1
.\chromium-dev\configure.ps1
.\chromium-dev\build-ui.ps1
.\chromium-dev\build.ps1
.\chromium-dev\smoke-test.ps1
```

SDK and Visual Studio can remain on `C:` while checkout/build data is redirected
to another drive with `YEE_LOCAL_BUILD_ROOT`. See
[`chromium-dev/README.md`](chromium-dev/README.md) for the supported Windows
layout and installer target.

전체 Git 이력과 별도 Git cache를 받지 않으며, 디버그 심볼을 만들지 않고
`out/YeePilot` 하나만 유지한다. 상세한 용량 정책과 일상 명령은
[`chromium-dev/README.md`](chromium-dev/README.md)에 있다.

Yee 고유 시각 컴포넌트는 Chromium 핵심 `BrowserView`와 `ToolbarView`에서
`//chrome/browser/ui/views/yee:yee_ui` 타깃으로 분리되어 있다. 평소 UI 컴파일
확인은 `build-ui.sh`로 끝내고, 실제 앱 링크와 통합 확인이 필요할 때만
`build.sh`를 실행한다.

정적 셸 프로토타입과 함께 실제 Chromium `WebContents`를 사용하는 Yee 빌드를
검증하고 있다. 현재 구현 범위는 Title bar, native Toolbar, Tab sidebar와
Browser Content 경계다. 탭 버튼으로 sidebar를 고정하거나 닫을 수 있고, 닫힌
상태에서는 왼쪽 끝 hover로 콘텐츠 위에 flyout된다.

## Browser shell prototype

별도 빌드 없이 저장소 루트에서 임시 서버를 실행해
[`prototype/index.html`](prototype/index.html)을 확인할 수 있다.

```sh
python3 -m http.server 4173 --bind 127.0.0.1
```

기본 확인 주소는
`http://127.0.0.1:4173/prototype/?titlebar=regular&tenant=offset&sidebar=open`이다.

화면 변형은 쿼리로 비교한다.

- `titlebar=regular|thin`: OS별 기본 Title bar와 압축형
- `tenant=squircle|offset|inset`: Tenant 이미지 실루엣 비교
- `sidebar=open|closed`: 고정 사이드바와 닫힌 사이드바
- `os=windows|mac|linux`: 플랫폼 frame과 caption controls 비교

Tenant/Workspace 맥락은 Title bar가 아니라 Sidebar footer에 유지한다. 현재 기본
실루엣은 한쪽 곡률을 강조한 `offset`이다.

현재 방향은 Arc와 Aside의 sidebar-first 탐색을 참고한 정적 WebUI형 셸이다.

- Title bar의 Leading rail은 Sidebar, New item, Agent activity와 탐색 action을
  소유한다. Omnibox 시작점은 Browser Content 시작점과 맞춘다.
- Command Runway는 주소/검색/명령과 확장 프로그램을 하나의 표면에 묶는다.
- 상세 Agent Status는 Sidebar에 두고 Toolbar에는 compact status만 둔다.
- Tab sidebar의 Group은 사용자가 탭을 정리하는 UI 도구일 뿐 Agent Task를
  소유하지 않는다.
- 사이드바를 닫으면 웹 표면이 전체 폭을 사용한다. 왼쪽 끝에 가리키면 글라스
  패널로 미리 보이고, 클릭하거나 단축키를 사용하면 고정되어 웹 표면을 민다.

프로토타입의 동작 코드는 화면 개념별 ES module로 분리한다.

- `js/launcher.js`: Title bar의 전역 Launcher
- `js/workspace.js`: Tab/Group과 Sidebar의 추가, 선택, 닫기, 고정 상태
- `js/dom.js`: DOM contract helper

`+` 버튼이나 `⌘T`로 새 Tab을 추가하고, `⌘K` 또는 `⌘L`로 Launcher를 연다.
`⌘W`는 현재 Tab을 닫고 `⌘B`는 Sidebar 고정 상태를 전환한다. Launcher에서는
`↑`/`↓`로 열린 Tab을 이동하고 `Enter`로 선택한다.

동작 코드는 시각 클래스 대신 `data-action`, `data-field`, `data-region`을 계약으로
사용한다.
