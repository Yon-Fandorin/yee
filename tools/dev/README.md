# 로컬 Chromium 개발 환경

Chromium 소스 준비, 빌드, 앱 실행과 테스트를 담당한다.
소스와 빌드 결과는 Git에서 제외된 `.local-build/`에 둔다.
기본 빌드 결과 경로는 `chromium/src/out/YeePilot`이다.

명령은 저장소 루트에서 실행한다. macOS는 `.sh`, Windows는 `.ps1` 스크립트를 사용한다.
두 플랫폼은 같은 제품 소스, 브랜딩 설정과 GN 빌드 설정을 사용한다.
`depot_tools`에는 플랫폼별 상태가 있으므로 Windows와 WSL/Linux에서 같은
디렉터리를 공유하지 않는다.

## 파일 안내

- 실행 스크립트: 소스 준비·설정·빌드·실행·테스트 명령
- [`lib/`](lib/README.md): 경로 설정, 실행 전 검사와 공통 코드
- 브라우저 CLI/MCP와 비교·측정 명령: 현재 이 디렉터리에서 관리
- 도구 테스트: [`tests/tooling/`](../../tests/tooling/README.md)
- 테스트용 페이지·시나리오: [`tests/fixtures/`](../../tests/fixtures/README.md)

패치 적용과 소스·브랜딩 생성은 [오버레이 도구](../overlay/README.md)가 담당한다.
전체 파일 배치는 [프로젝트 구조](../../docs/project-structure.md)를 따른다.

## 자주 사용하는 명령

macOS에서는 `.sh`, Windows에서는 `.ps1`을 사용한다.
필요한 작업에 맞는 명령을 선택한다.

| 명령 | 할 일 |
| --- | --- |
| `doctor.*` | 개발 환경 확인 |
| `checkout.*` | Chromium 소스 처음 받기 |
| `sync.*` | 현재 커밋에 필요한 관련 소스 맞추기 |
| `configure.*` | 제품 코드 적용과 빌드 설정 생성 |
| `build-ui.*` | `yee_ui`만 컴파일 |
| `build.*` | 실제 앱 빌드 |
| `run.*` | 빌드한 앱 실행 |
| `smoke-test.*` | 기본 실행과 페이지 열기 확인 |
| `usage.*` | 사용 중인 디스크 용량 확인 |

## macOS에서 처음 준비하기

```sh
./tools/dev/doctor.sh
./tools/dev/checkout.sh
./tools/dev/setup-metal.sh
./tools/dev/configure.sh
./tools/dev/build.sh
./tools/dev/smoke-test.sh
```

`checkout.sh`는 `fetch --no-history chromium`으로 전체 Git 이력 없이 소스를 받는다.
별도의 공유 Git 캐시도 만들지 않는다. 빌드는 디버그 정보를 줄여 용량을 절약하지만,
컴포넌트 빌드보다 수정 후 앱을 다시 만드는 시간이 길어질 수 있다.
설정은 [`build/args.gn`](../../build/args.gn)에 있다.

ANGLE의 그래픽 코드 빌드에 필요한 Metal 도구는 `setup-metal.sh`로 준비한다.
Xcode 26에서 `xcrun metal`이 실제 컴파일러를 찾지 못하면 빌드 도구가 올바른 경로를 찾는다.
Xcode 파일 자체는 수정하지 않는다.

현재 macOS 도구의 여유 공간 기준은 다음과 같다.

| 작업 | 기본 최소 여유 공간 |
| --- | --- |
| `depot_tools` 최초 설치 | 120 GiB |
| Chromium 소스 최초 받기 | 115 GiB |
| 관련 소스 동기화 | 35 GiB |
| 빌드 설정 생성 | 45 GiB |
| `yee_ui` 빌드 | 5 GiB |
| 전체 앱 빌드 | 35 GiB |

`build.sh`의 전체 앱 기준은 `YEE_BUILD_MIN_FREE_GIB`로 지정할 수 있으며
지정할 수 있는 최소값은 15 GiB다. 기본값은 35 GiB로 유지한다.

## macOS에서 작업하기

```sh
./tools/dev/sync.sh
./tools/dev/configure.sh
./tools/dev/build-ui.sh
./tools/dev/build.sh
./tools/dev/smoke-test.sh
./tools/dev/run.sh
./tools/dev/usage.sh
```

`sync.sh`는 현재 Chromium 커밋에 필요한 관련 소스를 맞춘다.
Chromium 자체를 최신 커밋으로 바꾸지는 않는다. 버전을 올릴 때는 원본 변경과
제품 패치 재적용을 별도로 진행해 기존 수정 사항을 보존한다.

두 빌드 명령은 먼저 브랜딩과 제품 소스를 동기화한다. `build-ui.sh`는
`//chrome/browser/ui/views/yee:yee_ui`만 컴파일하며 앱을 다시 링크하지 않는다.
이 대상 밖의 코드 변경이나 실제 앱 확인에는 `build.sh`로 `chrome`을 빌드한다.
macOS에서는 기본으로 빌드 작업 **3개**를 동시에 실행하며 프로세스 우선순위를 낮춘다.

```sh
YEE_BUILD_JOBS=1 ./tools/dev/build.sh
YEE_BUILD_JOBS=6 ./tools/dev/build.sh
```

소스와 빌드 결과를 둘 위치는 `YEE_LOCAL_BUILD_ROOT`로 바꿀 수 있다. macOS 도구는 그 아래의
`depot_tools/`, `chromium/src/`와 고정된 `out/YeePilot`을 사용한다.
컴파일러와 도구 캐시는 같은 위치의 `cache/`에 모은다.

## macOS 앱 실행과 확인

`run.sh`는 설정의 제품 이름으로 `.app` 파일을 찾는다. 인자 없이 실행하면 기존 개발
프로필로 브라우저만 열고, URL이나 Chromium 플래그를 전달하면 그대로 사용한다.
기본 실행은 macOS 앱 실행 기능인 Launch Services로 창을 활성화하고 종료까지 기다린다.
자동화에서 즉시 반환하려면 `--background`를 사용하며 이 모드는 앱을 활성화하지 않는다.

별도로 빌드한 Framework가 앱 안의 Framework보다 최신이면 `run.sh`와
`smoke-test.sh`가 멈추고 `build.sh` 실행을 안내한다. 오래된 앱으로 결과를 확인하는 일을
막기 위한 검사다. `test-run-preflight.sh`가 이 조건과 실행 방식을 테스트한다.

`run.sh`는 프로필과 실행 인자를 전달한다. Glass·색상·투명도·테마 설정은
제품 소스가 담당한다. 현재 화면 배치와 Agent 동작은
[브라우저 코드](../../browser/README.md), [셸 명세](../../docs/browser-shell-spec.md)와
[Agent 구조](../../docs/agent-browser-architecture.md)를 참고한다.

`smoke-test.sh`는 임시 프로필로 창을 표시하지 않는 브라우저를 실행한다.
로컬 data URL이 열렸는지 DevTools로 확인한 뒤 프로필을 정리한다.
외부 네트워크가 필요 없는 기본 실행 테스트이며 실제 화면 배치는 별도로 확인해야 한다.
macOS 앱 서비스에 접근할 수 있는 일반 터미널에서 실행한다.

새 빌드의 실제 앱 확인 전에는 [`AGENTS.md`](../../AGENTS.md)에 따라 모든 개발 브라우저의
정상 종료를 확인하고 새 앱을 실행한다.

## Windows 준비 사항

현재 `doctor.ps1`이 확인하는 준비 사항은 다음과 같다.

- NTFS 볼륨의 Windows 환경
- Visual Studio 2026 이상, **Desktop development with C++** 및 MFC/ATL 구성 요소
- Windows SDK 10.0.28000.2270 이상과 x64 Debugging Tools
- Windows용 Git

SDK와 Visual Studio는 `C:`에 두고, 소스와 산출물은 다른 드라이브에 둘 수 있다.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
$env:YEE_LOCAL_BUILD_ROOT = 'F:\browser-dev'
.\tools\dev\doctor.ps1
.\tools\dev\checkout.ps1
.\tools\dev\configure.ps1
.\tools\dev\build.ps1
.\tools\dev\smoke-test.ps1
```

위 데이터 경로는 `F:\browser-dev\depot_tools`, `F:\browser-dev\chromium\src`와
`F:\browser-dev\chromium\src\out\YeePilot`로 해석된다. 필요한 경우
`YEE_DEPOT_TOOLS_DIR`, `YEE_CHROMIUM_ROOT`, `YEE_OUT_NAME`을 개별 지정할 수 있다.
기존 호환 출력 디렉터리를 재사용하려면 설정·빌드 전에 `YEE_OUT_NAME`을 지정한다.

## Windows에서 작업하기

```powershell
.\tools\dev\sync.ps1
.\tools\dev\configure.ps1
.\tools\dev\build-ui.ps1
.\tools\dev\build.ps1
.\tools\dev\smoke-test.ps1
.\tools\dev\run.ps1
.\tools\dev\usage.ps1
```

`sync.ps1`도 현재 커밋에 필요한 관련 소스를 맞춘다. `src`를 최신 커밋으로 바꾸지는 않는다.
버전을 올린 뒤에는 `configure.ps1`로 제품 변경 내역을 다시 적용할 수 있는지 확인한다.

Windows에서는 기본으로 빌드 작업 **2개**를 동시에 실행한다. `doctor.ps1`은 물리 코어 수와
RAM 약 8 GiB당 한 작업을 기준으로 별도의 권장값을 출력한다. 필요할 때 지정한다.

```powershell
$env:YEE_BUILD_JOBS = '4'
.\tools\dev\build.ps1
.\tools\dev\record-build-memory.ps1
```

메모리 기록 도구는 빌드 중 RAM, 드라이브 여유 공간과 빌드 프로세스 상태를 기록한다.
기록은 Git에서 제외된 `.local-exclude/build-memory/`에 저장한다.
저장 위치는 `YEE_BUILD_MEMORY_LOG`로 바꿀 수 있다.
`usage.ps1`은 큰 디렉터리를 모두 읽어 오래 걸릴 수 있으므로 빌드와 별도로 실행한다.

Windows 개발 실행 파일은 `out\YeePilot\chrome.exe`, 설치 프로그램은
`mini_installer.exe`다. 생성되는 제품명 리소스와 선택된 아이콘에는 브랜딩 설정을
사용한다. 현재 설치 경로·AppID·ProgID 등의 설치 식별자는 Chromium과 공유한다.
제품 전용 설치 식별자와 기존 데이터 이전 방법은 아직 검토가 남아 있어 기존 Chromium 설치와
충돌할 수 있다. 설치 프로그램을 별도로 테스트할 때는 이 한계를 명시하는 옵션을 사용한다.

```powershell
.\tools\dev\build.ps1 -Target mini_installer -AllowSharedChromiumInstallIdentity
```

기존 Chromium 실행 파일을 `run.ps1`로 실행해 기본 동작을 확인할 수도 있다.
제품 이름과 아이콘을 반영하려면 소스에서 빌드해야 한다. 남은 이름·자산·OS 식별자는
[브랜딩 적용 범위](../../docs/branding-coverage.md)를 확인한다.
이 macOS 작업 환경에서는 Windows PowerShell 실행과 빌드를 검증하지 않았다.

## 변경 후 테스트

개발 명령은 [Python 테스트](../../tests/tooling/README.md)로 확인한다.
C++ UI를 바꿨다면 변경 범위에 맞는 테스트를 선택한다.

- `test-browser-surface-layout.sh fast`: 크기·위치 계산 테스트
- `test-browser-surface-layout.sh interactive`: 실제 BrowserView·Side Panel·전환 테스트
- `test-browser-surface-layout.sh browser`: Side Panel의 브라우저 동작 제어 변경 테스트
- `all`: 여러 변경을 마친 뒤 전체 확인

Header와 Sidebar의 세부 검증은 [Header 안내](../../docs/header/README.md)와
[Sidebar 안내](../../docs/sidebar/README.md)를 따른다. C++ UI 테스트 명령은 기본으로
필요한 프로그램을 빌드하며 일부 단계는 실제 창을 열거나 기존 개발 브라우저를 정상 종료한다.
