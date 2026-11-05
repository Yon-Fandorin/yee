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
| `check-compile.*` | 패치·owned 컴파일 단위 사전 점검 (명시적으로 실행) |
| `build.*` | 실제 앱 빌드 |
| `run.*` | 빌드한 앱 실행 |
| `smoke-test.*` | 기본 실행과 페이지 열기 확인 |
| `usage.*` | 사용 중인 디스크 용량 확인 |

## Stable 기준과 업그레이드

[`build/chromium-version.json`](../../build/chromium-version.json)의 **공통 원본 하나**를
Windows·macOS가 함께 사용한다. 각 OS의 Chrome Stable 패치 번호가 달라도 Yee의
소스는 여기 기록한 버전·커밋으로 고정하며, 빌드·실제 앱 검수·출시 상태는 OS별로 나눈다.
조회한 최신 버전을 자동 적용하거나 설치된 브라우저를 자동 교체하지 않는다.

```sh
python3 tools/dev/chromium-version.py show
python3 tools/dev/chromium-version.py check
```

`check`는 ChromiumDash의 Windows·Mac Stable을 각각 읽고 현재 기준과의 차이를
출력한다. 파일은 바꾸지 않는다. 두 OS의 최신 마일스톤이 다르면 후보를 구분해
보고하며, 이를 두 개의 Yee 원본으로 자동 분기하지 않는다. Windows에서는
`depot_tools`의 실제 `python3`가 PATH에 있는 개발 터미널에서 실행한다.

업그레이드는 다음 순서로 진행한다.

1. 현재 브라우저와 캐시를 보존하고 업그레이드 브랜치에서 버전·커밋을 고정한다.
   `branding/VERSION`은 같은 major/minor/build이며 원본 patch 이상이어야 한다.
2. 새 원본을 별도 작업 폴더에 받는다. 기존 수정이 있는 체크아웃에서 자동 stash,
   강제 checkout, 초기화로 변경을 버리지 않는다. 직접 이식할 경우에도 먼저 원본
   변경과 미추적 파일을 보관하고 복원 가능 여부를 확인한다.
3. 기존 `0001`·`0002`·`0003` 패치의 충돌과 제품 소스의 API 차이를 해결한다.
   적용된 패치를 다시 생성하고 브랜딩 문구·번역 변경을 별도로 검토한다.
4. `configure.*`로 후보 출력의 graph를 준비한다. 긴 앱 빌드 전에
   `check-compile.sh --list --include-unbuilt`로 대상을 확인하고
   `check-compile.sh --include-unbuilt`로 미빌드 object까지 사전 컴파일한다.
   Windows 대응 명령은 `check-compile.ps1 -List -IncludeUnbuilt`와
   `check-compile.ps1 -IncludeUnbuilt`다.
   아래 [사전 컴파일 점검](#chromium-이식-사전-컴파일-점검)을 참고한다.
5. 후보 출력과 격리 프로필로 빌드한다. 컴파일, 영향 범위의 native 회귀 검사,
   탭·그룹·분할·설정·미디어·CLI 연결을 실제 앱에서 확인한다.
6. 독립 검토 후 Windows와 macOS의 검수 결과를 각각 기록한다. 한 OS의 통과로
   다른 OS를 통과 처리하지 않는다. 해당 OS의 검증이 끝난 후보만 배포 대상으로 삼는다.
7. 이전 실행 파일·프로필을 보존한 상태로 새 빌드를 사용한다. 새 프로필에서 생긴
   문제가 없다는 이유만으로 실사용 프로필의 이전·복구까지 검증했다고 기록하지 않는다.

메이저 이식과 작은 보안 패치 갱신은 검증 범위를 구분한다. 큰 이식에는 native
레이아웃·상호작용을 포함한 통합 검수가 필요하다. 작은 갱신도 패치 적용·앱 빌드·미디어와
기본 탐색을 확인하며, 실제 영향을 근거로 회귀 검사를 선택한다.
Brave의 [공개 이식 절차](https://github.com/brave/brave-core/blob/master/docs/chromium_version_upgrade.md)처럼
버전 선택, 충돌 해결, 패치 재생성과 빌드 오류 수정을 나눠 검토한다.

`checkout.*`의 처음 받기와 실패 후 재개는 같은 고정 커밋을 사용한다. `sync.*`는
기준이 이미 일치하는 체크아웃의 의존 소스만 맞춘다. 기존 원본을 자동 승격하지 않는다.
`sync.*`의 `src@unmanaged`는 선행 핀 검사 뒤 원본 자체를 건너뛰는 지정이며,
Yee 패치가 적용된 tracked 변경까지 그대로 보존한다. 처음 받기의 정확한 SHA 지정과 구분한다.
설정·빌드·동기화와 native 회귀 명령은 원본 HEAD가 기준과 다르면 제품 파일을
수정하기 전에 중단한다. 원본 버전은 브랜딩 적용 후 작업 파일이 아니라
`HEAD:chrome/VERSION`에서 확인한다. 낮은 수준의 `tools/overlay/apply.*`는
충돌을 해결하는 이식 도구이므로 이 개발 명령의 고정 커밋 검사를 대신하지 않는다.

다운로드는 기본 두 작업으로 제한한다. 필요하면 두 OS 모두 `YEE_SYNC_JOBS`를
양의 정수로 지정한다. 원본 서버의 429 응답은 동시 요청을 줄이고 잠시 뒤 재개하며,
받아 둔 소스와 고정한 버전을 초기화하지 않는다.

후보 출력은 두 OS 모두 `YEE_OUT_NAME`으로 나눈다. 예를 들어 같은 소스에서
`YeePilot`을 보존하고 `YeeStable157`에 새 앱을 만들 수 있다.

```powershell
$env:YEE_CHROMIUM_ROOT = 'F:\yee\chromium-stable157'
$env:YEE_OUT_NAME = 'YeeStable157'
.\tools\dev\configure.ps1
.\tools\dev\build.ps1
```

```sh
YEE_OUT_NAME=YeeStable157 ./tools/dev/configure.sh
# 선택: 새 후보 출력의 이식 점검
# YEE_OUT_NAME=YeeStable157 ./tools/dev/check-compile.sh --include-unbuilt
YEE_OUT_NAME=YeeStable157 ./tools/dev/build.sh
YEE_OUT_NAME=YeeStable157 ./tools/dev/run.sh
```

## macOS에서 처음 준비하기

```sh
./tools/dev/doctor.sh
./tools/dev/checkout.sh
./tools/dev/setup-metal.sh
./tools/dev/configure.sh
# 선택: 첫 출력에서 이식 컴파일을 점검할 때만 실행
# ./tools/dev/check-compile.sh --include-unbuilt
./tools/dev/build.sh
./tools/dev/smoke-test.sh
```

`checkout.*`는 고정 커밋을 `gclient sync --no-history --revision src@COMMIT`으로 받는다.
별도의 공유 Git 캐시도 만들지 않는다. 빌드는 디버그 정보를 줄여 용량을 절약하지만,
컴포넌트 빌드보다 수정 후 앱을 다시 만드는 시간이 길어질 수 있다.
설정은 [`build/args.gn`](../../build/args.gn)에 있다.

공통 설정은 `proprietary_codecs = true`와 `ffmpeg_branding = "Chrome"`으로
AAC/H.264를 포함한다. 코덱 설정을 바꾼 기존 출력 폴더는 GN 설정을 다시 생성하고
앱 전체를 빌드해야 한다. 지원 여부는 `canPlayType`·MSE 조회와 실제 미디어 디코딩을
함께 확인한다.

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
| 이식 사전 컴파일 | 15 GiB |
| 전체 앱 빌드 | 35 GiB |

`build.sh`의 전체 앱 기준은 `YEE_BUILD_MIN_FREE_GIB`로 지정할 수 있으며
지정할 수 있는 최소값은 15 GiB다. 기본값은 35 GiB로 유지한다.

## macOS에서 작업하기

```sh
./tools/dev/sync.sh
./tools/dev/configure.sh
./tools/dev/build-ui.sh
# 선택: 이미 빌드한 출력에서 컴파일 단위 사전 점검
# ./tools/dev/check-compile.sh --list
# ./tools/dev/check-compile.sh
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
`depot_tools/`, `chromium/src/`와 기본 `out/YeePilot`을 사용한다. 후보 출력은
`YEE_OUT_NAME`으로 구분한다.
컴파일러와 도구 캐시는 같은 위치의 `cache/`에 모은다.

macOS에서 루트를 지정하지 않았고 기본 루트의 도구·소스가 없는데
`.local-build/*/`에 기존 루트가 있으면, checkout을 안내하거나 시작하기 전에 후보를
출력하고 종료 코드 **17**로 멈춘다. 출력 이름을 지정하지 않았고 기본 출력의
`build.ninja`가 없는데 같은 원본에 다른 graph가 있으면, 설정·빌드·실행 전에 후보 출력을
보여 주고 종료 코드 **18**로 멈춘다. 후보를 자동 선택하지 않으며, 명시한 값과
기존 기본 루트·graph가 있는 경우의 동작은 유지한다. 기존 환경에 맞춰 두 값을 지정한다.
기본 루트를 쓰려면 `YEE_LOCAL_BUILD_ROOT="$YEE_ROOT/.local-build"`를 명시한다
(`YEE_ROOT`는 저장소 루트이며, 저장소 루트에서 직접 지정할 때는 `$PWD`를 써도 된다).
가드 메시지에는 호출자 셸에 `YEE_ROOT`가 없어도 쓸 수 있는 실제 절대 경로의 `export` 예시도 출력한다.

```sh
export YEE_LOCAL_BUILD_ROOT="$PWD/.local-build/stable157"
export YEE_OUT_NAME=YeeStable157
```

앱 실행·프로토타입 실행·패키징(`--skip-build` 포함)과 native 테스트의 `--no-build`도
부작용 전에 같은 선택 가드를 적용한다. `doctor.sh`는 선택 힌트를 출력하고 진단을 계속한다.
`observe-youtube-live.mjs`는 앱·실행 파일·종료 helper 모두 공통 경로 선택을 사용하며 선택한 앱이
없으면 관찰을 시작하지 않는다. 종료 helper는 `runtime.zsh`의 공통 함수로 실행 전에 준비한다.
Python 콘텐츠 차단 fixture 세 곳과 Grok cohort는 `yee_build_paths.py`로,
Swift 폼 승인 검사는 같은 zsh 호출로 두 가드와 제품 이름을 재사용한다.

## Chromium 이식 사전 컴파일 점검

이식한 코드의 컴파일 오류를 긴 앱 빌드 전에 확인하는 선택 단계다.
기존 `YEE_LOCAL_BUILD_ROOT`·`YEE_OUT_NAME`의 `build.ninja`가 필요하며, 출력 폴더나
graph가 없으면 생성하지 않고 중단한다. 다른 빌드가 실행 중이면 끝난 뒤 실행한다.

```sh
# 선택: 기존 출력에서 긴 앱 빌드 전에 컴파일 단위 점검
./tools/dev/check-compile.sh --list
./tools/dev/check-compile.sh
./tools/dev/build.sh
```

```powershell
.\tools\dev\check-compile.ps1 -List -IncludeUnbuilt
.\tools\dev\check-compile.ps1 -IncludeUnbuilt
.\tools\dev\build.ps1
```

Windows는 같은 Python 분류·보고 코드를 사용하고 `ninja.exe`를 조회한다.
PowerShell의 `-List`, `-IncludeUnbuilt`는 아래 `--list`, `--include-unbuilt`와 같은 범위다.
소스·도구·원본·출력은 기존 Windows 환경 변수를 따르며, `configure.ps1`로 graph를 준비한다.
Windows 대상은 `^` 해석을 피하도록 `.obj` 경로로 전달하고, 복수 대상은 실제 Python에서
`autoninja.py`를 직접 실행해 배치 파일의 8191자 한도를 피한다.

`--list`는 기존 graph를 읽어 분류만 출력한다. 소스 동기화·컴파일·GN 재생성을
수행하지 않으며 컴파일용 디스크 여유 공간 검사도 요구하지 않는다.
다만 `ninja -t query`는 Ninja로 빌드한 출력(`.ninja_log`·`.ninja_deps`가 있는 경우)에서 내부 로그를 다시 쓸 수 있다.
컴파일 모드는 기존 소스·도구·버전·작업 수·15 GiB 여유 공간
검사를 통과한 뒤 owned 소스를 동기화한다. Chromium 패치를 다시 적용하거나
브랜딩을 생성하지 않으며, graph 갱신이 필요한 변경은 먼저 `configure.sh`로 준비한다.

분류 전에 `build.ninja.d`의 의존 파일 mtime을 `build.ninja.stamp`와 비교한다
(stamp가 없으면 `build.ninja` 기준). 컴파일 모드는 owned 소스 동기화 직후 검사하므로
동기화가 바꾼 `BUILD.gn`도 감지한다. 입력이 더 새롭거나 누락되었거나 depfile이 없으면
종료 코드 **19**로 중단한다. 같은 루트·출력 환경에서 `./tools/dev/configure.sh`로 빌드
설정을 먼저 갱신한다. 자동 재생성은 수행하지 않는다. `--list`는 경고를 출력하고 기존
graph로 분류하며, 이때 플랫폼 미컴파일 분류는 확정된 결과로 간주하지 않는다.

대상은 manifest의 shell 패치(`0001`)가 수정하는 `.cc`·`.mm` 중 삭제하지 않은 파일과,
overlay 소스 복사 규칙으로 계산한 owned `.cc`·`.mm`의 Chromium 목적지다.
Ninja의 조회 subtool로 소스별 object를 찾는다. macOS는 단일 출력에 `../../path/file.cc^`,
다중 출력에는 선택한 각 object 경로를 전달한다. Windows는 항상 선택한 `.obj` 경로를 쓴다.
같은 캐시·`YEE_BUILD_JOBS`로
autoninja를 한 번 실행하며 `-k 0`으로 가능한 컴파일 오류를 함께 확인한다.
macOS는 기존 빌드처럼 우선순위를 낮춘다.
선택한 object가 요구하는 생성 헤더·모듈 등의 의존 단계도 실행하므로 cold 출력에서의
작업량은 변경 파일 수보다 많을 수 있다. cold 시간 단축 여부는 아직 확인하지 않았다.
앱·테스트 바이너리를 빌드 대상으로 지정하지 않는다.

기본 모드는 현재 출력에 물리 파일이 있는 object만 선택한다. 없는 object는
**현재 출력에서 미빌드** 목록과 `plan.json`의 `unbuilt_objects`에 기록하고 생략한다.
일부를 생략하면 `N개 미빌드로 생략 — 완전한 통과 아님`을 출력한다. 컴파일 가능한
object가 모두 미빌드이면 목록 모드와 컴파일 모드 모두 종료 코드 **16**으로 중단하고
`--include-unbuilt` 사용을 안내한다. 전부 플랫폼 미컴파일인 경우를 포함해 실제 컴파일
대상이 0개면 옵션이나 이유와 관계없이 **16**으로 끝난다. 알 수 없는 옵션은 **2**다.

버전 이식·빈 출력·새 타깃의 컴파일까지 점검하려면 `--include-unbuilt`를 명시한다.
이 옵션은 미빌드 object도 포함하므로 해당 타깃의 의존 단계를 처음 빌드할 수 있다.
미빌드 목록은 포함 모드에서도 남기며 `--list`와 함께 사용할 수 있다.

```sh
./tools/dev/check-compile.sh --list --include-unbuilt
./tools/dev/check-compile.sh --include-unbuilt
```

현재 graph에 object가 없는 파일은 **이 플랫폼에서 미컴파일** 목록으로 출력한다.
플랫폼 조건뿐 아니라 비활성 기능·테스트 구성도 원인일 수 있으므로, 다른 플랫폼·구성에서
별도 점검해야 한다. 이 목록과 전체 앱의 링크·실행·native 회귀 검증은 사전 컴파일의
통과로 대체하지 않는다.
컴파일 성공 뒤에도 보존한 `plan.json`에서 미빌드 파일·object 수와 플랫폼 미컴파일
파일 수를 다시 출력한다. 미빌드를 생략했거나 플랫폼 미컴파일이 있으면 완료 요약에도
**완전한 통과 아님**을 표시한다. `--include-unbuilt`로 포함한 object는 생략으로 세지 않는다.

컴파일 모드의 기록 폴더는 공통 경로 코드가 계산한 `LocalBuildRoot/logs/compile-preflight.*`다.
`YEE_LOCAL_BUILD_ROOT`를 지정했다면 그 아래이며, 미지정이면 저장소의 `.local-build/logs/`다.
분류가 끝나면 `plan.json`·`targets.txt`를, 실제 빌드를 호출했을 때만 `build.log`를 남긴다.
오래된 graph는 기록 폴더를 만들기 전에 **19**로 중단한다. `--list`도 기록 폴더를 만들지 않는다.
실패하면 0이 아닌 원래 종료 코드를 반환하고 `failed-sources.txt`·`failed-objects.txt`에
실패 파일을 기록한다. 도구나 생성 단계가 실패해 소스를 식별하지 못하면 그 사실을
표시하므로 `build.log`도 함께 확인한다. 계약 테스트는 실제 빌드 없이 실행한다.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/tooling/test_compile_preflight.py
```

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

새 빌드의 수동 앱 확인 전에는 [`AGENTS.md`](../../AGENTS.md)에 따라 검수 대상 실행 파일이나
격리 프로필을 쓰는 프로세스만 정상 종료한다. 자동 검사별 종료 요구 범위는 아래 설치 검사와
콘텐츠 차단 검증의 설명을 따른다.

## macOS 설치 파일

```sh
./tools/dev/package-macos.sh
./tools/dev/package-macos.sh --skip-build
node tools/dev/test-macos-installer.mjs PACKAGE_DIRECTORY
```

선택한 `$LOCAL_BUILD_ROOT/releases/`(기본 `.local-build/releases/`)에 드래그 설치용
DMG·메타데이터·설치 안내를 만든다.
기본 명령은 전체 앱을 먼저 빌드하며 `--skip-build`도 버전과 번들 구성을 검사한다.
현재는 ad-hoc 서명한 테스트판이며 Developer ID 서명과 Apple 공증은 포함하지 않는다.
독립 프로필, 설치·교체와 검수 범위는 [macOS 테스트판](../../docs/macos-test-release.md)을 참고한다.

설치 검사는 임시 폴더에 복사한 앱 경로나 검사 전용 `--user-data-dir`를 쓰는 제품
프로세스만 종료 확인·종료 대기 대상으로 삼는다. 해당 앱 안의 Helper도 포함한다.
`/Applications/Yee.app`과 개발 out의 앱은 실행 파일·프로필이 분리되어 있으면
종료를 요구하지 않으며, 개수·실행 경로만 검사 폴더의 `processes.json`, 최종 `result.json`과
표준 출력에 남긴다. `open -n`, `--use-mock-keychain`, 격리 프로필로 같은 bundle ID의
설치판과 검사 앱을 함께 실행한다.

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

`YEE_LOCAL_BUILD_ROOT`·`YEE_CHROMIUM_ROOT`를 모두 생략했고 기본 도구·소스가 없는데
저장소 `.local-build/*/`에 기존 소스 또는 도구 후보가 있으면 종료 코드 **17**로 멈춘다.
저장소 밖의 `F:\yee\...` 같은 루트는 검색하지 않으며 `YEE_CHROMIUM_ROOT`로 지정한다.
출력 이름을 생략했고 `YeePilot/build.ninja`가 없는데 같은 원본의 다른 출력에 graph가 있으면
설정·빌드·실행·패키징 전에 후보와 지정 예시를 출력하고 **18**로 멈춘다.
명시값과 유효한 기본값을 우선하며, 후보 없는 새 장비는 기존 준비 동작을 유지한다.
두 가드는 후보를 자동 선택하거나 기존 출력·프로필을 수정하지 않는다.

## Windows에서 작업하기

```powershell
.\tools\dev\sync.ps1
.\tools\dev\configure.ps1
.\tools\dev\build-ui.ps1
# 선택: 미빌드 object까지 이식 컴파일을 점검할 때 실행
# .\tools\dev\check-compile.ps1 -IncludeUnbuilt
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
`mini_installer.exe`다. 제품 표시 이름·아이콘은 브랜딩 설정을 사용하고,
설치·프로필·정책·등록 식별자는 별도 `branding/identity.json`에서 생성한다.
테스트판은 사용자별 설치만 지원하며 기존 Chromium의 데이터를 자동으로 이전하지 않는다.
다른 브라우저의 공유 실행 파일 등록과 과거 제품 데이터도 정리 대상으로 삼지 않는다.

AAC/H.264 빌드를 바꾼 뒤에는 다음 실제 앱 검사를 실행한다. Chromium의 기존
시험 파일을 로컬 HTTP로 제공해 일반 MP4의 직접 재생과 조각 MP4의 MSE에서
영상·음성 디코딩과 재생 종료를 확인한다. 별도 Temp 프로필을 쓰고 정상 종료 후 제거하며,
이 검사는 외부 YouTube 라이브의 재생 검증과 구분한다.
Windows CDP 연결은 새 본체의 실행 파일·전용 프로필·리스너 PID와 loopback
WebSocket 주소를 확인한다. 종료 직전에도 소유권을 확인하며, 달라진 연결에는
`Browser.close`를 보내지 않는다. 확인된 검수용 PID의 정상 종료 뒤 소유권 오류를
검사 실패로 남긴다. 일반 보관 영상의 코덱 전환과 AAC 전용 방송의 실제 AAC
디코딩 증거를 구분하고, 물리 청취나 직접 창 조작을 대신 통과 처리하지 않는다.

```powershell
node tools/dev/test-media-codecs.mjs F:\yee\chromium\src\out\YeePilot\chrome.exe F:\yee\chromium\src
```

```powershell
.\tools\dev\package-windows.ps1
```

이 명령은 `.local-build/releases/`에 unsigned 테스트 설치 파일·메타데이터·안내를 만든다.
`node tools/dev/test-windows-installer.mjs PACKAGE_DIRECTORY`로 실제 설치·기본 프로필·수리·
재설치·제거를 검증한다. 기존 Yee 데이터가 있으면 시험이 시작되지 않는다.
기존 설치를 보존하는 업데이트 검증은
`node tools/dev/test-windows-installer.mjs NEW_PACKAGE --upgrade-existing OLD_PACKAGE`로 실행한다.
등록된 구버전·실행 파일 버전을 확인한 뒤 업데이트·설정 보존·구버전 역설치 거부·동일 버전
수리를 검사하며 기존 설치와 프로필은 삭제하지 않는다. 배포마다 `branding/VERSION`을 올린다.
범위와 제한은 [Windows 테스트판](../../docs/windows-test-release.md)을 참고한다.

기존 Chromium 실행 파일을 `run.ps1`로 실행해 기본 동작을 확인할 수도 있다.
제품 이름과 아이콘을 반영하려면 소스에서 빌드해야 한다. 남은 이름·자산·OS 식별자는
[브랜딩 적용 범위](../../docs/branding-coverage.md)를 확인한다.
Windows 검증은 실제 Windows 환경에서 릴리즈 빌드와 브라우저·렌더러 smoke 및 native UI gate를 실행한다.

## 변경 후 테스트

개발 명령은 [Python 테스트](../../tests/tooling/README.md)로 확인한다.
C++ UI를 바꿨다면 변경 범위에 맞는 테스트를 선택한다.

- `test-header.sh`, `test-sidebar.sh`: 기본값과 단독 `--no-build`는 단위 검사
- `test-browser-surface-layout.sh fast`: 크기·위치 계산 테스트
- `test-browser-surface-layout.sh horizontal`: 수평 탭 전환·그룹 요약·색상·분할·전체화면 확인
- `test-browser-surface-layout.sh interactive`: 실제 BrowserView·Side Panel·전환 테스트
- `test-browser-surface-layout.sh browser`: Side Panel의 브라우저 동작 제어 변경 테스트
- `test-projects.sh [unit|browser|all]`: 기본은 `project_service_unittests`의 7개 검사.
  `browser`는 선택한 tier의 프로젝트 사례와 TabStripUnification 전용 사례를 순서대로 실행
- `test-macos-install.sh`: macOS의 `install_sh_test`에서 임시 DMG 4개·가짜 updater 1개만 선택
- `test-content-blocking-native.sh`: 기존 두 단위 바이너리와 소형
  `yee_service_worker_unittests`의 `WrapSubresourceLoaderFactory_*` 3개 실행

새 gate도 `--no-build`를 지원한다. `test-internal-pages.sh` 기본 필터는
`InternalURLsBrowserTest.*` 전체를 선택하고 macOS에서는 `mac_identity_unittests`도 실행한다.
`test-site-controls.sh`는 Footer·DisclosureArrow·SessionStartupPref 전체와 favicon·그룹 이름
접근성·Tab Search split guard를 같은 소형 브라우저 실행 파일에서 선택한다.
Sidebar unit은 Group Header의 hover·focus guard를, interactive는 gesture double-tap을
추가한다. layout interactive는 native Toolbar·최소 Side Panel 창 크기와 Mac glass의 Yee guard 6개를
포함한다. Windows의 layout fast는 `yee_aura_unittests`도 선택한다.

[gate coverage 검사](../../tests/tooling/README.md#yee-테스트-gate-누락-검사)는 소스·타깃·필터의
누락을 tooling unittest에서 차단한다. 목록 발견은 실행 통과와 구분한다.

세 `.sh` gate의 `interactive`와 `horizontal` 모드는
`//chrome/browser/ui/views/yee:yee_interactive_ui_tests`만 빌드하고
`out/<선택한 이름>/yee_interactive_ui_tests`를 실행한다. Yee 테스트는
`browser/ui/browser_surface_interactive_uitest.cc`와
`browser/ui/split_pane_interactive_uitest.cc`에 있으며 fixture 이름은 `Yee`로 시작한다.
같은 실행 파일에 원본 guard와 아직 이전하지 않은 drag·Omnibox·플랫폼 테스트를
선택해 넣는다. 모든 필수 필터의 실제 사례 발견 검사는 그대로 적용한다.
Windows `.ps1` gate의 실행 파일·필터 전환은 후속 작업이다.

### Background와 foreground tier

native `.sh` gate는 `--tier=background|foreground|all`, `--print-filters`,
`--observe-background`를 지원한다. 기존 `unit|fast|horizontal|interactive|browser|all`은
검사 범위다. YouTube는 `off|on`을 명시하는 opt-in이며 Internal Pages의 `--ui=on`은
기존 표시형 실행인 `--tier=all`로 연결한다. 명시한 tier가 있으면 그 선택을 따른다.

- 기본 `background`: A와 해당 OS에 `needs_display`가 없는 B를 선택한다.
- `foreground`: C와 해당 OS에 `needs_display`가 있는 B를 선택한다.
- `--tier=all`: 해당 범위의 A/B/C를 기존 표시형 실행기로 실행한다.

macOS background의 브라우저·interactive 실행에는 Chromium의 `--headless`와
`--yee-test-tier=background`를 전달한다. headless 화면은 `--screen-info={1920x1080}`,
초기 창은 `--window-size=1280,900`으로 고정해 Side Panel과 헤더에 필요한 폭을 확보한다.
`site_controls_browsertests`와 `yee_interactive_ui_tests`의 Yee 소유 main은 NSApp 생성 전에
background 전용 활성화 차단을 설치한다. native 창 생성과 논리 focus를 유지하면서
`NSApplication`의 `activateIgnoringOtherApps:`와 `activate` 요청을 막는다.
foreground는 upstream main의 초기화·활성화 동작을 유지한다.
interactive main은 Kombucha의 UIControls 초기화를 유지하고 background에서
전면화 등록·modifier key-up 주입·always-on-top 창 종료를 제외한다.
background는 개발 Yee 종료와 물리 display 배치 helper를 호출하지 않는다.
목록 조회도 같은 옵션으로 관찰하며, 실행별 임시 프로필과 프로세스 세션을 만든다.
macOS의 `TMPDIR`와 `MAC_CHROMIUM_TMPDIR`는 모두 그 실행의 임시 경로로 지정한다.
정리는 전용 process group·세션과 기록한 PID·시작 시각·실행 파일로 소유권을 확인한 자식에만 적용한다.
프로필을 명시한 자식은 경계도 검사하며, 프로필 인수가 없는 renderer·GPU helper도 정리한다.
Chromium 자식이 별도의 process group을 만들어도 같은 세션이나 기록한 자손으로 추적한다.
열거와 argv 조회 사이에 끝난 자식의 `EINVAL`·`ESRCH`와 좀비는 종료한 것으로 처리한다.
같은 신원으로 살아 있는 자식은 `EINVAL`·`ESRCH`가 나도 정리한다. 그 밖의 살아 있는
자식의 argv 조회 오류·프로필 경계 침범과 정리 후 남은 자식은 실패한다.
무관한 앱·설치판·기존 개발 창에는 종료 신호를 보내지 않는다. 실패를 foreground에서
자동 재실행하지 않는다.

`needs_display`는 OS별 사유를 담은 manifest 필드다. macOS에서는 세 Views unit 바이너리의
B를 기존 runner에 남긴다. fixture마다 활성화 정책을 Regular로 바꾸며 기존 fake-focus와
headless의 swizzle 수명·paint 동등성을 확인하지 못했기 때문이다. 강제 전면화를 호출하는
upstream glass geometry·Omnibox shadow 두 사례도 같은 예외다. YouTube opt-in은 표시형
재생·광고 관찰을 유지한다. 이 사례들은 `--tier=foreground`에서 빠짐없이 실행한다.
`InternalURLsBrowserTest.ExternalLaunchUsesCanonicalPolicy`도 foreground에서 실행한다.
background의 `--headless --allow-chrome-scheme-url`은 외부 `version` URL을 허용하므로
표시형 launch policy를 검증하는 기존 assertion과 실행 조건이 다르다.
macOS의 upstream `browser_tests`에 남아 있는 B 10개도 foreground에서 실행한다.
그 main에는 Yee의 활성화 차단이 없고 headless의 Widget 표시도 NSApp 활성화를 요청한다.
`test-browser-surface-layout.sh all --tier=background`에서도 이 바이너리는 `cases=none`이다.
Omnibox 접근성은 전부 C이므로 기본값은 `cases=none`이다.

```sh
export YEE_LOCAL_BUILD_ROOT="$PWD/.local-build/stable157"
export YEE_OUT_NAME=YeeStable157
export YEE_BUILD_JOBS=3
export PATH="$YEE_LOCAL_BUILD_ROOT/depot_tools:$PATH"

# 화면을 점유하지 않는 범위와 관찰 기록
./tools/dev/test-header.sh all --tier=background --observe-background --no-build
./tools/dev/test-sidebar.sh all --tier=background --observe-background --no-build
./tools/dev/test-browser-surface-layout.sh all --tier=background --observe-background --no-build
./tools/dev/test-projects.sh all --tier=background --observe-background --no-build
./tools/dev/test-site-controls.sh --tier=background --observe-background --no-build
./tools/dev/test-internal-pages.sh --tier=background --observe-background --no-build
./tools/dev/test-agent-browser.sh --tier=background --observe-background --no-build
./tools/dev/test-agent-prompt.sh --tier=background --observe-background --no-build
./tools/dev/test-content-blocking-native.sh --tier=background --observe-background --no-build
./tools/dev/test-macos-install.sh --tier=background --observe-background --no-build

# 실제 입력·표시와 needs_display 예외: 전용 화면에서 직렬 실행
./tools/dev/test-header.sh all --tier=foreground --no-build
./tools/dev/test-sidebar.sh all --tier=foreground --no-build
./tools/dev/test-browser-surface-layout.sh all --tier=foreground --no-build
./tools/dev/test-projects.sh all --tier=foreground --no-build
./tools/dev/test-site-controls.sh --tier=foreground --no-build
./tools/dev/test-agent-prompt.sh --tier=foreground --no-build
./tools/dev/test-omnibox-accessibility.sh --tier=foreground --no-build
# 선택적인 실재생 관찰
./tools/dev/test-youtube-live.sh off --tier=foreground --no-build
./tools/dev/test-youtube-live.sh on --tier=foreground --no-build
```

`--no-build`는 이미 빌드한 바이너리를 사용한다. `all` 범위도 tier를 생략하면 background다.
`--print-filters`는 환경 검사·동기화·빌드·목록 조회·창 조작 없이 실제 필터와 옵션을 출력한다.

```sh
./tools/dev/test-browser-surface-layout.sh interactive --tier=background --observe-background --print-filters
./tools/dev/test-browser-surface-layout.sh interactive --tier=foreground --print-filters
```

macOS의 `--observe-background`는 Swift helper를 캐시에 빌드하고 약 100ms마다
`lsappinfo front`와 `lsappinfo info -only pid <ASN>`으로 LaunchServices의 전면 앱 PID를
직접 읽는다. 표시 창은 CGWindowList에서 읽는다. `NSWorkspace`의 전면 앱 캐시는 사용하지 않는다.
조회마다 500ms 제한을 두며 timeout·실패·잘못된 출력은 읽기 공백으로 기록한다.
`frontmost_source`는 `launchservices`다. 읽기 공백 사이에도 마지막 유효 PID를 유지해
실제 PID 전환을 센다.
실행기는 `setsid`를 마친 자식의 root PID·pgid를 `observer-sessions.json`에 명시한다.
관찰기가 `sysctl(KERN_PROC_PGRP)`로 그 root를 확인하고 준비 파일을 쓴 뒤에만 테스트를
exec하므로 짧은 목록 조회에도 같은 순서를 적용한다. 매 표본에서 전용 그룹을 열거하고,
전체 프로세스 목록의 root 자손과 이미 확인한 자식을 함께 추적한다.
창 소유 PID가 그 집합에 있을 때만 검사 창으로 센다. 전면 앱 표본과 입력 이벤트는
PID·시작 시각을 저장하고 같은 소유권 판정으로 검사 프로세스에 귀속한다.
무관한 PID의 그룹·세션은 개별 조회하지 않는다.
검사 창의 가시성은 모든 활성 display의 온스크린 목록으로 확인하고, 무관한 창의 생존은
최소화·다른 Space 창을 포함한 전체 목록으로 비교하며 나중에 확인한 자체 helper도 제외한다.
AX focused window 소유 PID는 참고 정보이며 변화나 읽기 실패가 합격을 막지 않는다.
AX·event tap을 읽지 못하면 `notes`에 남기며 권한을 자동 요청하지 않는다.
필수 관측은 fail-closed다. 유효 표본이 2개 미만이거나 창 목록 조회에 실패하거나,
전면 앱을 2회 이상 읽지 못하거나 읽기 공백이 있거나, 실행별 root를 확인하지 못하면
`observation_complete: false`다. 전체 프로세스 목록 또는 실행별 process group 조회가
한 번이라도 실패해도 미완료이며, 성공한 빈 목록은 조회 공백으로 세지 않는다.
`ownership_read_gaps`는 소유권 열거가 실패한 표본 수, `process_list_read_gaps`와
`process_group_read_gaps`는 각각 실패한 조회 수다. 후속 조회가 성공해도 이 공백과
미완료 사유는 유지한다. `frontmost_read_gaps`와 `max_sample_gap_ms`도 기록한다.
최종 기록의 `observed_test_root_pids`·`observed_test_process_groups`도 전달한 모든 실행과 대조한다.
실제 UI에서의 합격 여부는 검수자의 실행으로 확인한다.

각 native 기록 폴더의 `observation.json`과 `record.json`의 `observation`에 요약을 남긴다.
관찰 합격은 `observation_complete: true`, `sample_count >= 2`,
`frontmost_sample_count >= 2`, `frontmost_read_gaps: 0`이며 다음 값이 모두 0이다.
`record.json`의 `observation.passed: true`와 `gate_exit_code: 0`도 확인한다.

- `max_visible_test_windows`: 동시에 관찰한 검사 소유 표시 창의 최댓값
- `test_frontmost_samples`: 검사 소유 프로세스가 전면 앱인 유효 표본 수. 전면 앱이
  바뀌지 않아도 검사 프로세스가 한 번이라도 전면이면 실패한다.
- `input_injections`, `modifier_injections`: 검사 프로세스가 출처인 입력·modifier 이벤트 수
- `unrelated_apps_lost`: 시작 시 활성화 정책이 `NSApplication.ActivationPolicy.regular`인
  무관한 앱 중 관측 동안 한 번도 전면이 아니었고 종료 시 없는 identity 수.
  Dock에 표시되는 일반 앱만 해당한다. bundle ID,
  없으면 실행 파일 경로·이름 순으로 시작 시 캡처한다. 같은 프로세스의 후속 identity를
  읽지 못해도 저장한 값을 사용하며 같은 identity의 재실행은 손실이 아니다.
- `unrelated_windows_lost`: 시작 시 regular였던 무관한 앱 identity별 일반 창(layer 0)
  수의 감소분 중 관측 동안 한 번도 전면이 아니었던 앱의 손실. 메뉴바·상태 창 등 다른
  layer는 제외한다. 이후 활성화 정책이 바뀌어도
  시작 시 캡처한 비교 대상을 유지한다.

`test_frontmost_samples`는 필수 비음수 정수이며 누락·형식 오류는 관측 실패다.
`unrelated_apps_lost_details`·`unrelated_windows_lost_details`에는 손실 identity와
전후 개수(`before`, `after`), 감소분(`lost`)을 기록한다.
앱 identity를 요구하기 전에 검사 소유권과 종료 상태를 확인한다. 시작 후 나타난 무관한 앱은
identity를 읽지 못하거나 곧 종료해도 앱 손실·관측 미완료로 세지 않는다. PID와 읽을 수 있는
identity를 `notes`에 남기며, 읽지 못한 PID는 `application_identity_unreadable_pids`에 기록한다.
시작 시 regular 앱의 identity를 캡처하지 못하면 관측 미완료다.
accessory·prohibited 앱의 identity 읽기 실패는 note로 남긴다.

다음 활동 지표는 기록만 하며 합격을 막지 않는다. 0보다 크면
`background observation note: <지표>=<값>`을 출력한다.

- `cursor_changes`, `modifier_changes`: 연속한 유효 커서·modifier 표본 사이의 변화 수
- `frontmost_changes`: 읽기 공백을 제외한 전면 앱 PID 전환 수. 무관한 앱 사이의 전환은
  허용하며 검사 소유 앱의 전면 여부는 `test_frontmost_samples`로 판정한다.
- `key_window_changes`: AX focused window 소유 PID 전환 수
- `non_regular_apps_lost`: 시작 시 accessory·prohibited였던 앱 identity의 손실 수
- `non_regular_windows_lost`: 시작 시 regular 앱에 속하지 않았던 일반 창 수의 감소분.
  IME·XPC UI 서비스·background agent의 창 생성·소멸은 실패시키지 않는다.
- `user_active_apps_lost`, `user_active_windows_lost`: 시작 시 regular였고 관측 중 한 번이라도
  전면이었던 앱의 종료·일반 창 감소분. 이후 다른 앱이 전면이 되어도 note로 유지한다.
  PID와 프로세스 시작 시각으로 전면 표본을 앱 identity에 연결하며, 같은 identity의 재실행도
  같은 앱으로 취급한다. 검사 소유 앱의 전면화는 계속 `test_frontmost_samples`로 실패한다.

`non_regular_*`·`user_active_*` 카운터도 필수 비음수 정수이며 누락·형식 오류는 관측 실패다.
스키마 버전은 1이다. 각 `non_regular_*_lost_details`·`user_active_*_lost_details`에는 기존 손실 상세와 같은
identity·전후 개수·감소분을 기록한다.

사용자는 background 실행 중 다른 앱에서 계속 작업할 수 있다. 커서·modifier 변화와
무관한 앱 사이의 전환과 사용자가 전면에서 쓰던 앱의 종료·창 감소로 검사 간섭을 판정하지 않는다.
100ms 사이의 짧은 창·focus 변화나 출처 PID가 없는 입력은 이 관찰만으로 배제할 수 없다.
headless의 논리 가시성·bounds·paint·popup 검사는 실제 화면의 픽셀·occlusion·Space·native
활성화 검증을 대체하지 않는다. 실제 입력을 확인하는 C는 기존 foreground 동작을 유지한다.
관찰이 불완전하거나 간섭이 있으면 native 검사가 통과해도 gate는 **23**으로 실패한다.
native 검사 자체의 실패 코드는 유지한다.
보조 정보가 없으면 `background observation note: ...`를 출력한다.
손쉬운 사용 권한이 없어도 필수 관측과 간섭 지표가 통과하면 gate는 성공한다.

`--tier=foreground`와 `--tier=all`은 손쉬운 사용 권한을 갖춘 전용 화면에서 직렬 실행한다.
실행 중 마우스·키보드 입력과 원격 데스크톱·Computer Use 같은 화면 제어를 멈춘다.
전용 모니터만으로 앱 활성화나 입력이 격리되지는 않는다.

분류와 매개변수는 [`test_tiers.json`](../../tests/tooling/test_tiers.json)에서 관리한다.
실행 필터는 해당 OS·바이너리의 manifest와 기존 범위 필터의 교집합이다.

각 native gate는 바이너리 실행 직전에 같은 필터·특성·창 인수에
`--gtest_list_tests`를 추가해 목록을 조회한다. 조회 실패, 빈 목록, 또는 음수 필터와
`DISABLED_` 스위트·사례를 제외한 뒤 활성 사례가 없는 양수 패턴이 하나라도 있으면
중단한다. 다른 패턴이 일치해도 누락을 허용하지 않으며 기존 검사 선택 범위는 유지한다.
JSON의 `pattern_counts`와 `missing_patterns`에서 패턴별 발견 수와 누락을 확인한다.

zsh의 `run_native_test_gate`는 두 선택 가드와 바이너리의 존재·실행 권한을 먼저 확인한다.
바이너리가 없거나 실행 불가능하면 선택한 out 경로를 출력하고 **20**으로 끝난다.
검사마다 `$LOCAL_BUILD_ROOT/logs/native-gates/<UTC시각>-<바이너리이름>-<pid>/`에
`record.json`과 `listing.txt`를 남긴다. UTC 시각에는 마이크로초를 포함해 같은 셸의
연속 실행도 나누며, 기록에는 다음 항목이 들어간다.

- 시작·종료 UTC, Yee HEAD, `git diff HEAD --binary`의 SHA-256과 NUL로 구분한
  정렬된 미추적 파일 목록의 SHA-256·경로 목록. 미추적 내용 지문은 각 경로·NUL·내용 SHA-256의
  원시 바이트를 순서대로 합쳐 SHA-256으로 계산한다. 심볼릭 링크의 내용은 링크 대상 문자열이다.
- 빌드 루트·out 이름·경로와 두 선택의 명시 여부, 실행 디렉터리·인수·GTest 필터.
- 바이너리의 절대 경로·크기·ns mtime·SHA-256과 해시 캐시 재사용 여부.
  같은 경로·크기·mtime이면 `native-gates/.binary-sha256/`의 캐시를 재사용한다.
- 목록 조회 종료 코드와 발견 수, 실행 종료 코드 `exit_code`, 최종 판정 `gate_exit_code`,
  인터럽트 여부 `interrupted`, XML 경로·최신 작성 여부·`tests`·`failures`·`errors` 요약.

`--gtest_output`을 생략하면 기록 폴더의 `results.xml`을 지정한다. 호출자가 지정한 XML
파일·디렉터리 경로는 유지한다. 빈 경로인 `--gtest_output=xml:`은 GTest·Chromium 실행기의
기본 파일명이 다를 수 있어 실행 전에 **2**로 거부한다. 기록 폴더를 쓰려면 옵션을 생략한다.
목록 조회도 XML을 쓸 수 있으므로 실제 실행 직전의 파일
상태와 비교하며, 이전 XML만 남은 경우도 누락으로 처리한다. 종료 코드가 0이어도 새 XML이
없거나 손상되었거나 `tests == 0` 또는 `failures + errors > 0`이면 불일치를 보고하고
**21**로 끝난다. JSON 출력만 지정해 XML이 없는 경우도 동일하다. 목록 조회·실제 실행이
실패하면 원래 종료 코드를 유지하며, 준비·기록 실패는 **22**다. 이 기록은 현재 소스와
실행 바이너리를 식별하며, 해당 소스로 바이너리를 빌드했다는 증명은 아니다.
Ctrl-C(SIGINT)는 `interrupted: true`로 기록하고, background 전용 세션의
자식 정리가 끝날 때까지 기다린다. 실제 자식 종료 코드를 보존하며, 코드가 없으면
**130**을 기록한다. 자식이 0으로 끝나도 최종 판정은 **130**으로 실패한다.

native browser test는 Chromium 실행기가 만든 임시 프로필을 쓰며 고정 CDP 포트를 추가하지 않는다. Mac 설치 검사·smoke는 이미 `--remote-debugging-port=0`과 전용 프로필의 `DevToolsActivePort`를 사용하고 검사 PID·앱·프로필 범위를 추적한다. OS 리스너 PID까지 대조하는 CDP 소유권 검사는 현재 Windows 전용이다.

레이아웃 검사의 OS 전용 필터는 공통 필터에 해당 플랫폼의 사례만 더한다. Mac에는
Windows caption·native clip 검사를 요구하지 않으며, Windows에는 Mac glass·immersive
검사를 요구하지 않는다. 해당 플랫폼에 있어야 할 사례의 누락은 그대로 실패다.

목록 해석은 셸이나 GUI 없이 Windows에서도 사용할 수 있다. 목록 조회의 종료 코드는
호출자가 먼저 확인하고, 같은 필터로 저장한 UTF-8 목록을 검사한다. 파일 인수를 생략하거나
`-`로 지정하면 표준 입력을 읽으며, CLI 종료 코드 0은 발견 성공, 1은 누락, 2는 입력 오류다.

```powershell
python3 tools/dev/lib/native_test_discovery.py listing.txt '--filter=Suite.*:All/ParamTest.Case/*-Suite.Excluded'
```

발견 수는 실행 결과가 아니다. `GTEST_SKIP()` 같은 런타임 생략은 목록만으로 판단할 수
없으므로 실제 실행의 사례 수·통과·실패·생략 결과와 해당 OS의 UI 검증도 별도로 확인한다.

Windows의 native UI 픽셀 검사는 `--enable-pixel-output-in-tests`를 사용한다.
Windows는 실제 앱 종료 때 UI 스레드가 살아 있는 동안 외부 UIA 연결을 정리한다.
다른 창이 남아 있는 일반 창 닫기는 접근성을 유지한다. 접근성 기능과 COM 누수
검사를 유지한 채 정리하며, Windows의 `test-site-controls.sh`는
외부 접근성 클라이언트를 연결한 `UiaApplicationExitBrowserTest.*`도 실행해
일반 창 닫기·앱 종료·마지막 창이 먼저 닫힌 종료를 확인한다.

계산·색상·직접 callback 검사는 background에서 실행한다. native popup·focus 단위 검사의
needs_display 예외와 C 브라우저 검사는 기존 실행기의 창 활성화를 유지한다. Sidebar는 Header의 주소줄
검사를 중복 실행하지 않으므로 두 영역을 바꿨을 때 각각의 gate를 선택한다. 실제
hover·클릭·drag·renderer·native popup 검사를 단위 검사 통과로 대체하지 않는다.
수평 탭 수정 중에는 `fast`와 `horizontal`을 사용하고, 전체 레이아웃 checkpoint에서
`interactive`를 실행한다. 실제 입력은 해당 범위의 `--tier=foreground` 또는 `--tier=all`에서 확인한다.
같은 수평 시나리오를 기본·RTL 125%·통합 탭바 경로에서 실행하며 그룹의 접기/펴기와
탭 추가/제거 후 favicon 요약 갱신, 전환 전후의 pin·group·활성 탭 보존을 확인한다.
회전 모니터 지정은 창 위치를 정하며 macOS 앱 포커스를 격리하지는 않는다.
foreground 실행 중에는 다른 앱의 오버레이 창이 테스트 모니터를 덮지 않게 하고, 마우스·키보드 입력과
원격 데스크톱·Computer Use 같은 화면 제어 도구를 멈춘다. 창이 가려지면 전체화면 Space 전환
대기가 시간 초과될 수 있고, 실행 중 입력이 들어오면 hover·drag 사례가 실패할 수 있다.
탭 그룹 색상·mock clock 레이아웃 애니메이션·Footer 표식 검사는 `ShowInactive()`를
사용한다. Footer는 팝업을 열거나 키보드 focus 상태를 검사할 때만 owner를 활성화한다.
Agent pointer의 native 부모 창과 승인 대화상자, 실제 입력·전체화면 검사의 활성화는
유지한다. native 창 표시 전제의 실패를 검사를 생략하거나 assertion을 줄여 해결하지 않는다.

macOS에서 회전된 모니터가 연결되어 있으면 표시형 UI 검사·Site Controls·Internal
Pages·콘텐츠 차단 fixture·YouTube 관찰·Yee Agent 시험 실행은 해당 모니터의 작업 영역
안에 창을 연다. `run-agent-prototype.sh`의 격리된 시험 창도 같은 배치를 쓴다.
전체화면도 그 창이 있는 모니터에서 시작한다. 각 브라우저 실행 직전에 현재 회전값과
작업 영역을 읽으며 회전된 모니터가 없으면 기본 배치를 사용한다.
`--window-position`으로 시작 위치만 지정하며 검사별 창 크기와 배율은 그대로 적용한다.
Python·JavaScript 검사는 `ui-test-window-args.sh`를 통해 같은 모니터 선택을 사용한다.

macOS `interactive` gate의 전체화면 메뉴막대 검사는 실제 시스템 포인터를 화면
상단으로 옮겨 AppKit의 노출·숨김을 확인한다. 이 검사를 실행하는 터미널이나 IDE에
시스템 설정의 손쉬운 사용 권한이 미리 있어야 한다. 검사는 권한을 자동 요청하거나
시스템 설정을 바꾸지 않으며, 권한이 없으면 해당 전제를 명시하고 실패한다.
포인터는 case 전체가 끝나거나 실패한 뒤 원래 위치로 돌아온다.

### 그룹 종료·재실행 검증

Windows에서 `node tools/dev/test-group-session-restore.mjs <Yee 실행 파일>`로
그룹의 이름·색상·접힘 상태와 탭 구성·순서를 정상 종료 뒤 다시 확인한다.
시작 URL이나 복원 강제 플래그 없이 새 격리 프로필을 사용하며, 명시적인 새 탭
시작 설정이 기본 복원 동작보다 우선하는지도 검사한다. 검사 전 실행 중인 Yee를
정상 종료한다. 검사 기록과 전용 프로필은 `.local-build/group-session-*`에 남는다.
각 연결·종료의 소유권 관찰은 `endpoint-owners.json`에 함께 저장한다.

### 콘텐츠 차단 검증

콘텐츠 차단과 Site Controls는 다음 명령으로 확인한다.

- `test-site-controls.sh`: 사이트별 토글·저장·창과 탭 수명·worker/cache 통합 검사
- `python3 tools/dev/test-procedural-content-blocking.py`: 새 실제 Yee의 로컬 HTTP 탭에서
  native procedural/action 주입·동적 조건 적용과 해제·SPA·iframe·분할 작업을 검사한다.
  켬·끔·프로필 사이트 예외를 각각 격리 프로필로 확인하며 라이브 광고 관측은 하지 않는다.
- `python3 tools/dev/test-baseline-filter-updates.py`: 새 실제 Yee에서 갱신 generation의
  browser·renderer 일치, 원문/상태 손상 복구, 내장 기본본 fallback과 늦게 생긴
  cross-site renderer의 실행 generation 고정을 로컬 HTTP로 확인한다.
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
영상별 상한 90~600초를 지정한다(기본 120초). 기본값은 광고가 60초 가까이 나와도
최소 본편 시간을 채울 여유를 두면서 세 영상 관측을 약 6분에 끝낸다. `YEE_LIVE_YOUTUBE_MIN_CONTENT_SECONDS`는
광고·탐색 이동을 제외한 최소 본편 시간이며 기본 60초다. `YEE_LIVE_YOUTUBE_SEEK=1`은
40초·80초 뒤 중간 탐색을 추가한다. opt-in 결과는 선택한 `$LOCAL_BUILD_ROOT/youtube-live/`에 생성한다.

초기 로딩·스크롤·영상 전환을 비교하려면 저장소 루트에서
`node tools/dev/observe-youtube-live.mjs perf`를 실행한다.
새 광고 노출 문제나 관련 변경의 회귀 대조가 필요하면 `ads`를 명시한다.
`all`은 광고·출력 수집과 성능 비교를 함께 실행한다.
이 도구는 macOS의 실제 앱과 격리 프로필을 사용한다. `ads`,
`perf`, `smoke`로 단계만 선택할 수 있다. `smoke-on`은 차단 켠 앱의 재생·출력
오디오를 짧게 확인한다. 마지막 인자는 재생·광고 관측 초수다
(`ads`/`all` 기본 600초, 영상별 상한 1,200초).
`observe-youtube-live.mjs`와 `test-content-blocking.mjs`는 선택한 개발 앱 내부의 실행 파일
(Helper 포함)이나 이번 검사의 정확한 `--user-data-dir`를 쓰는 개발 번들 프로세스만
정상 종료 요구·종료 대기 대상으로 삼는다. 관찰기는 공통 zsh 선택의 앱 경로를,
콘텐츠 차단 검사는 인자로 받은 실행 파일의 앱 경로를 사용한다.
`browser_bundle_executables.py --running --details`로 bundle ID `org.chromium.Chromium`의
본체·Helper와 macOS 커널의 실제 argv를 읽어 프로필 인수 값을 완전 일치로 비교한다.
공백으로 펼친 명령줄의 프로필 접두사를 일치로 취급하지 않는다.
다른 out·worktree의 무관한 프로세스에는 종료를 요구하지 않는다.
무관한 프로세스의 개수·실행 경로만 표준 출력과 관찰기의 `report.json`, 콘텐츠 차단 검사의
`yee-content-blocking-browser-evidence.json`에 남긴다. argv는 기록하지 않는다.
이미 끝난 PID는 상세 조회에서 제외하고, 다른 인수 조회 오류는 종료 요구 없이 검사를 중단한다.
기존 `--running`의 본체 목록과 출력 경로 인자의 번들 탐색 동작은 유지한다.
설치판 ID `io.github.yon-fandorin.yee`는 개발 번들 조회 대상이 아니다. 설치 검사는 앞서
설명한 검사 앱·검사 프로필만 종료 대상으로 삼는다.
앱 출력 오디오 helper는
Xcode 도구로 자동 빌드하며, 정확한 브라우저 PID의 자식 오디오 프로세스만
Core Audio tap에 포함한다. 마이크는 사용하지 않는다. 광고 구간의 PCM은 최대
90초만 보존하고 전체 출력은 초별 RMS로 집계한다. 측정 결과는
저장소의 `.local-build/youtube-review/`에 둔다(빌드 루트·출력 선택과 별도). OS의 오디오 캡처 권한이 없거나 출력이
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
