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

### 콘텐츠 차단 검증

콘텐츠 차단과 Site Controls는 다음 명령으로 확인한다.

- `test-site-controls.sh`: 사이트별 토글·저장·창과 탭 수명·worker/cache 통합 검사
- `python3 tools/dev/test-procedural-content-blocking.py`: 새 실제 Yee의 로컬 HTTP 탭에서
  native procedural/action 주입·동적 조건 적용과 해제·SPA·iframe·분할 작업을 검사한다.
  켬·끔·프로필 사이트 예외를 각각 격리 프로필로 확인하며 라이브 광고 관측은 하지 않는다.
- `test-youtube-live.sh off|on`: 실제 YouTube의 본편 재생 검사와 광고 전달 관측
- `python3 tools/dev/test-content-blocking-browser-fixture.py --yee`:
  새 실제 Yee의 파일 탭에서 body reader·clone·다른 realm과 오류 처리, CSS와
  대체 MP4의 프레임 디코딩·재생 종료를 검사한다. `--response-only`는 응답 검사만 실행한다.
  빌드 최신 여부를 확인하고 실행 중인 Yee를 정상 종료한 뒤 격리 프로필을 사용한다.
  저장소 소스를 fixture 탭에 직접 설치하므로 native document-start 자동 주입 증거와
  구분한다. 기본 실행은 Chrome의 Web API/CSS/media 전체 fixture다.

앞의 두 shell 명령은 기본으로 필요한 native 테스트 프로그램을 빌드하며,
`--no-build`로 이미 빌드한 프로그램을 사용할 수 있다. 실행 전에 개발 Yee를
정상 종료한다. YouTube 검사는 외부 네트워크를 사용하고, 플레이어 오류·멈춤과
최소 본편 재생 시간을 검사한다. 관측 시간·영상 선택·탐색 옵션과 광고 검증의
범위는 [콘텐츠 차단 checkpoint](../../docs/content-blocking-checkpoint.md#40초대-재생-오류)에 있다.

`test-youtube-live.sh`의 기본 영상은 `5EzB_2Qcakw`, `uq14seOjILU`, `mfmdXPT7nAM`이다.
`YEE_LIVE_YOUTUBE_VIDEO`로 이 중 하나를 선택하고 `YEE_LIVE_YOUTUBE_SECONDS`로
영상별 상한 90~600초를 지정한다. `YEE_LIVE_YOUTUBE_MIN_CONTENT_SECONDS`는
광고·탐색 이동을 제외한 최소 본편 시간이며 기본 60초다. `YEE_LIVE_YOUTUBE_SEEK=1`은
40초·80초 뒤 중간 탐색을 추가한다. opt-in 결과는 `.local-build/youtube-live/`에 생성한다.

초기 로딩·스크롤·영상 전환을 비교하려면 저장소 루트에서
`node tools/dev/observe-youtube-live.mjs perf`를 실행한다.
새 광고 노출 문제나 관련 변경의 회귀 대조가 필요하면 `ads`를 명시한다.
`all`은 광고·출력 수집과 성능 비교를 함께 실행한다.
이 도구는 macOS의 실제 앱과 격리 프로필을 사용한다. `ads`,
`perf`, `smoke`로 단계만 선택할 수 있다. `smoke-on`은 차단 켠 앱의 재생·출력
오디오를 짧게 확인한다. 마지막 인자는 재생·광고 관측 초수다
(`ads`/`all` 기본 600초, 영상별 상한 1,200초). 실행 전에 `common.zsh`의
`gracefully_quit_yee`로 개발 Yee를 정상 종료해야 한다. 앱 출력 오디오 helper는
Xcode 도구로 자동 빌드하며, 정확한 브라우저 PID의 자식 오디오 프로세스만
Core Audio tap에 포함한다. 마이크는 사용하지 않는다. 광고 구간의 PCM은 최대
90초만 보존하고 전체 출력은 초별 RMS로 집계한다. 측정 결과는
`.local-build/youtube-review/`에 둔다. OS의 오디오 캡처 권한이 없거나 출력이
잡히지 않은 경우에는 음성 검증 통과로 취급하지 않는다.

차단 끔 대조군은 한 세션·프로필에서 영상을 순서대로 이동한다.
`YEE_LIVE_YOUTUBE_VIDEOS`에 쉼표로 구분한 공개 영상 ID를 지정할 수 있다.
`YEE_LIVE_YOUTUBE_WARMUP_SECONDS`는 마지막 영상 전의 관측 시간을 줄인다
(10초 이상, 마지막 인자 이하). 로그인된 사용자의 프로필은 사용하지 않는다.
이미 광고를 관측한 프로필을 대조에 유지하려면 `YEE_LIVE_YOUTUBE_PROFILE`에
`.local-build/youtube-review/` 아래 생성된 `profile-*` 경로를 지정한다.

이 도구는 명시적 로컬 디버깅 포트를 사용하며 `navigator.webdriver`와 DOM
controller가 모두 false인지 확인한다. 광고 대조군에서 중간 광고를 관측한 경우
같은 영상·프로필의 차단 켬을 관측해 해당 본편 위치를 넘었는지와 실제 출력을
비교한다. 끔에서 광고와 출력이 있고 켬에서 광고 없이 해당 위치를 넘으면 관측한
범위의 대조를 완료한다. 대조군에 광고가 없으면 미확보로 남긴다.
성능 단계는 콘텐츠 viewport를 실제 페이지에서 1,000×720으로
맞추고 숨겨진 창의 측정을 거부한다. 실행 순서를 번갈아
Chrome·Yee 차단 끔·Yee 차단 켬을 각각 세 번 측정한다. FCP/LCP는 새 문서의
값이며, 입력에 의한 검색 결과 스크롤과 영상 이동은 별도의 rAF 간격으로 기록한다.
영상 이동의 DOM·video-ready 시각과 선택한 영상 ID도 보존한다.
첫 측정에서 고른 영상 ID를 후속 전환에도 사용한다.
`YEE_LIVE_PERFORMANCE_VIDEO`로 검색 결과에 있는 동일 영상 ID를 지정할 수도 있다.
실제 화면에 표시된 compositor 프레임이나 INP로 해석하지 않는다. 비교한
Chromium 버전 차이와 네트워크·광고 전달 변동도 함께 확인해야 한다.
환경 진단에는 `YEE_LIVE_PERFORMANCE_GROUPS=chrome-off`와
`YEE_LIVE_PERFORMANCE_ROUNDS=1`처럼 조건·횟수를 줄일 수 있다.
설치된 Brave를 비교하려면 그룹에 `brave-on`을 지정한다. 격리 프로필에서
읽기 전용 `brave://adblock-internals` 정보로 실제 필터 목록 로딩과 debug mode
비활성화를 확인한 뒤 측정한다. 목록 로딩 대기는 최대 90초이며, 필터 출처·개수도
결과에 남긴다. Brave 기본 Shields와 Yee의 필터·정책이 동일하다는 뜻은 아니다.
`YEE_LIVE_PERFORMANCE_CPU_PROFILE=1`은 초기 로딩과 영상 전환의 V8 CPU
프로파일 및 함수별 표본 시간 요약을 추가한다. 프로파일의 script URL에서 query와
fragment를 제거하고 `*.cpuprofile.gz`로 압축해 보존한다. CPU 수집을 켠 회차는
기본 로딩 비교 표본과 따로 해석한다.
첫 FCP/LCP 이전과 영상 제목 표시 이전 구간도 따로 집계한다. 익명 주입 script는
Debugger의 소스와 저장소 입력을 대조하며 source text는 저장하지 않는다.
Brave scriptlet은 설치된 1.95.104의 고정 runtime prefix와 대조한다. 다른 익명
Brave 코드는 확인 없이 차단 코드로 분류하지 않으며 `unresolved`로 남을 수 있다.
`*.cpu-owners.json`에는 script 소유 분류와 구간 경계만 보존한다. sample timestamp를
복원·정렬한 뒤 구간 경계를 자르며, self 분류와 하위 호출 포함 시간을 구분한다.
정렬 방식은 [DevTools CPUProfileDataModel](https://github.com/ChromeDevTools/devtools-frontend/blob/main/front_end/models/cpu_profile/CPUProfileDataModel.ts)을 참고했다.
`node tools/dev/test-performance-cpu.mjs`로 소유 분류·경계·중복 집계·시간 순서와
특수 함수 이름을 검사한다. 주입 래퍼의 시간에는 원래 페이지·native 작업이 포함될
수 있어 전부 추가 차단 비용으로 해석하지 않는다. cold profile은 새 context 생성
이후 시작할 수 있다. 제목은 100ms polling으로 감지하므로 물리 paint 시각이 아니다.
요청 시간은 URL의 query·fragment와 headers/body를 제외하고 수집한다. 페이지가
resource timing buffer를 비워도 CDP 요청 기록은 유지한다. response event의 전달
시각을 실제 네트워크 header 도착 시각으로 취급하지 않는다.
로딩만 분석할 때는 `YEE_LIVE_PERFORMANCE_SKIP_SCROLL=1`로 스크롤을 생략한다.
로딩·전환 native trace는 각 초기 5초로 제한해 추적 기록의 과다 생성을 막는다.
그 이후 표시를 분석할 때는 해당 trace의 범위를 확인해야 한다. V8 수집과 표시
관측은 계속되며 `nativeTraceWindowMs`에 이 trace 제한을 기록한다.
이 프로파일은 브라우저 프로세스의 네트워크 차단 비용이나 compositor 작업을
측정하지 않는다.
`YEE_LIVE_PERFORMANCE_TRACE=1`은 초기 로딩·영상 전환의 첫 5초와 스크롤 구간의 Chromium
trace를 추가한다. 필터 데이터 읽기·엔진 생성·문서 규칙 적용·worker 결과 대기·요청 매칭의 네이티브
구간과 페이지 렌더링을 함께 기록한다. 결과는 `*.trace.json.gz`이며, 이벤트 인자는
스레드·프로세스 이름과 지정한 frame/input enum·숫자·boolean만 남기고 제거한다.
`node tools/dev/test-performance-trace.mjs`로 URL·소스와 임의 인자의 제거를 검사한다.
스크롤 trace의 `yee-review-scroll-start/end` mark는 해당 renderer와 구간을 식별한다.
시작·초기 로딩·종료 시 process CPU와 RSS도 수집한다. RSS에는 공유 페이지가
중복 포함될 수 있으므로 물리 메모리 사용량으로 해석하지 않는다.
trace를 켠 회차도 일반 로딩 표본과
별도로 해석한다. renderer의 사전 준비가 trace 시작 전에 끝나면 해당 엔진 생성은
기록되지 않으므로, 생성 이벤트가 없다는 사실을 전체 CPU 비용 제거로 해석하지 않는다.
다른 창의 가림 때문에 정상 측정이 불가능하면
`YEE_LIVE_PERFORMANCE_IGNORE_OCCLUSION=1`을 명시할 수 있다. 이 경우 성능
조건 모두에 Chromium의 `--disable-backgrounding-occluded-windows` 테스트 옵션을
적용하고 결과를 `test-occlusion-override`로 표시한다. 광고 관측에는 적용하지 않는다.
이 결과는 일반 창 상태의 실제 화면 애니메이션 검증을 대체하지 않는다.

완료된 관측의 결론·조건·표본 수·한계는 checkpoint에 모은다. 현재 체크포인트는
`.local-build/youtube-review/completed-checkpoint.json`과 최신 Web API·native·procedural fixture
결과만 보존한다. 중간 요약·trace·CPU profile·오디오 수집 helper·이전 로그는 정리했다.
새 분석의 원시 자료는 필요한 검토가 끝날 때 정리하고, 제거한 원본 ID를 요약에
남기면 원본 보존 여부도 명시한다. 사용자 프로필과 Chromium 빌드 캐시는 유지한다.
현재 필수 잔여 작업과 선택적인 성능 후보는
[checkpoint](../../docs/content-blocking-checkpoint.md#남은-성능-작업)에 둔다.

Header와 Sidebar의 세부 검증은 [Header 안내](../../docs/header/README.md)와
[Sidebar 안내](../../docs/sidebar/README.md)를 따른다. C++ UI 테스트 명령은 기본으로
필요한 프로그램을 빌드하며 일부 단계는 실제 창을 열거나 기존 개발 브라우저를 정상 종료한다.
