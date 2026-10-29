# Chromium 157 중간 체크포인트와 빌드 인수인계

2026-10-29 · 사용자가 MacBook에서도 빌드를 시작하려고 **검수 완료 전 소스 체크포인트의
커밋·푸시를 요청했다.** Windows 전체 앱 빌드는 진행 중이다. Windows 실제 앱 검수와
macOS 빌드·검수는 미완료이며, 이 체크포인트는 배포 승인 기록이 아니다.

## MacBook에서 빌드 시작하기

Yee 저장소에서 기존 작업을 보존한 뒤 브랜치를 받는다.

```sh
git status --short
git fetch origin
git switch --track origin/codex/chromium-stable-157
```

같은 로컬 브랜치가 이미 있다면 마지막 명령 대신 `git switch codex/chromium-stable-157`,
`git pull --ff-only`를 사용한다. 미커밋 변경 때문에 전환이 거부되면 먼저 기존 작업을
보존한다. 강제 초기화하거나 기존 Chromium 소스를 새 버전으로 덮어쓰지 않는다.

다음은 **같은 터미널에서**, Yee 저장소 루트에 위치한 채 실행한다.

```sh
export YEE_LOCAL_BUILD_ROOT="$PWD/.local-build/stable157"
export YEE_OUT_NAME=YeeStable157

./tools/dev/checkout.sh
./tools/dev/setup-metal.sh
python3 tools/dev/chromium-version.py verify "$YEE_LOCAL_BUILD_ROOT/chromium/src" --platform macos
./tools/dev/configure.sh
./tools/dev/build.sh
```

- 새 원본은 `.local-build/stable157/chromium/src`에 준비한다. 도구·캐시도 새 루트에
  생긴다. 이 루트는 Git에서 제외되며 기존 `.local-build/chromium`의 153 환경을 보존한다.
- 새 앱은 `.local-build/stable157/chromium/src/out/YeeStable157/Yee.app`이다.
  Mac에서는 `YEE_LOCAL_BUILD_ROOT`가 원본 루트를 고른다. Windows의
  `YEE_CHROMIUM_ROOT`를 대신 지정하지 않는다.
- 병렬 수는 `YEE_BUILD_JOBS`로 조정하며 기본값은 3이다. Mac의 메모리를 확인해 정한다.
  최초 준비에는 도구의 120/115 GiB 여유 공간 검사가 있다. 부분 다운로드는 출력된
  재개 명령으로 이어가며 삭제하지 않는다.
- 위 명령은 저장소 기본 `build/args.gn`을 사용한다. 기본은 static 빌드,
  `is_debug=false`, symbol 0, `dcheck_always_on=false`, AAC/H.264 지원이다.
  Windows에서 진행 중인 component 후보와 빌드 형태를 구분한다.
- 다른 터미널에서 이어갈 때도 위 두 환경 변수를 다시 지정한다.

앱 빌드 성공 후 같은 환경에서 실행한다.

```sh
./tools/dev/smoke-test.sh
./tools/dev/run.sh
```

`run.sh`는 새 루트의 `runtime-profile`을 사용한다. 기존 실사용 프로필이나 설치된 앱을
자동 교체하지 않는다. smoke 검사는 기본 렌더러 확인이며 실제 UI·미디어 검수를 대신하지
않는다. 자세한 환경 준비는 [개발 도구](../tools/dev/README.md)를 따른다.

## 공통 기준과 커밋 범위

| 항목 | 값·상태 |
| --- | --- |
| 브랜치 | `codex/chromium-stable-157` |
| 기반 | `7e9443dc52cbae5dea1241cb6bddd38099a1d64d` — 기존 AAC/H.264 지원 작업 |
| 공통 원본 | `157.0.8103.27` / `f9d7b13a569b352a0267234ea5740fdecc83ffa2` |
| 버전 기준 | `build/chromium-version.json` 하나를 Windows·macOS가 공유 |
| Windows 전체 앱 빌드 | **진행 중, 완료 판정 전** |
| Windows native·실제 앱·미디어 | **이번 157 바이너리로 미검증** |
| macOS 빌드·실제 앱·CLI/MCP | **미검증** |
| 설치판·실사용 프로필 | 기존 환경 보존. 이번 체크포인트에서 교체·배포하지 않음 |

커밋에는 현재 제품 소스, 정식 개발 도구·도구 검사, 브랜드 생성물, canonical 패치와
문서를 담는다. Chromium 체크아웃·빌드 캐시·실사용 데이터·Windows PC 전용 임시 검수
실행기는 Git에 포함하지 않는다. Mac 빌드에 필요한 제품 소스의 별도 미커밋 묶음은 없다.

## 반영한 소스 작업

- 153→157의 패치 충돌과 API 차이를 해결했다. GN 공개 헤더와 구현 의존을 분리해
  새 frame/layout/contents 대상 사이의 순환을 해소했다.
- 프로젝트·Site Controls는 `BrowserWindowInterface`, 공유 제품명은 작은 `brand`
  대상으로 연결한다. Chromium 탭 모델의 소유권을 유지한다.
- Favorites 전용 420ms 시계, 배치별 드래그·그룹 표시와 Side Panel 반올림을 보완했다.
  Mac glass의 전체 창 경로와 서비스 워커 factory의 새 sandbox 검사를 보존했다.
- 157의 WebUI 그림자 경로에서 AIM 여백과 native host 투명도 계약을 맞췄다.
  C++ 회귀 사례를 추가했지만 아직 새 검사 바이너리로 실행하지 않았다.
- 생성 자산·문구와 패치의 중복 소유를 제거했다. 실제 파일 헤더가 패치 선언과 다르면
  적용 전에 거부한다. 개발 명령은 공통 핀을 먼저 검사한다.
- native 목록에서 필수 패턴별 실제 사례가 없으면 실행을 차단한다. 세 회귀 명령의
  고정 이름 117개를 원본·OS 조건과 대조하고 Windows/Mac 필터를 분리했다.

충돌에서 확인한 연결 경계와 후속 후보는
[빌드 문서](../build/README.md#이식에서-유지할-연결-경계)에 있다.
후속 후보까지 구현했다고 기록하지 않는다.

## 확인한 결과와 미검증 범위

| 검사 | 결과 |
| --- | --- |
| 전체 도구 검사 | 179개 중 157 통과·22 건너뜀·실패 0 — 앞선 완료점의 결과 |
| Windows native `build/util/version_test.py` | 12개 통과. 변경 없는 RC 파일 재작성 보완 |
| 공통 핀·GN 생성·관련 GN 헤더 의존 검사 | 통과. Mac 컴파일 통과를 뜻하지 않음 |
| owned 소스·브랜드 자산 | 동기화 대기 0개, 브랜드 86개 일치 |
| canonical 패치 | 0001 320경로·0002 66경로·0003 2경로, 소유 중복 없음 |
| 0001·0002 역방향 적용·원본 공백 검사 | 통과 |
| 제품 소스 독립 검토 | 발견 사항 수정 후 최종 범위별 검토에서 추가 조치 사항 없음 |
| Windows 전용 임시 검수 도구 | 가짜 프로세스·입력 검사 26/21/67개와 별도 독립 검토 통과 |
| 새 Windows·Mac 실제 실행 | **미완료. 위 소스·도구 검사로 대체하지 않음** |

22개 생략에는 Windows의 zsh 부재 등이 포함된다. Mac에서 해당 검사를 확인해야 한다.
별도 실행의 수를 합쳐 하나의 전수 통과로 기록하지 않는다. 사용자가 Mac 빌드를 병행하려고
요청한 중간 체크포인트이며, 검수 완료 전 커밋이라는 점을 유지한다.

## Windows에서 이어갈 작업

작업 저장소는 `F:/yee/worktrees/project-navigation-handoff`, 원본은
`F:/yee/chromium-stable157/src`, 출력은 `out/YeeStable157`이다.
기존 `F:/yee/chromium/src/out/YeePilot`과 설치판·실사용 프로필을 보존한다.

같은 PC의 `.local-build/chromium-stable-upgrade-20261029/handoff-state.json`과
`build-result.json`을 먼저 확인한다. **이미 실행 중인 출력에 Ninja/Siso를 추가로 띄우지
않는다.** 현재 후보는 component 개발 빌드, symbol 0, jobs 4이며 설치용 릴리즈가 아니다.

임시 검수 도구는 같은 작업 폴더에 있다.

- `source-fingerprint.py`·`capture-build-provenance.py`·두 PowerShell 빌드 실행기:
  원본·활성 Git 의존성 156개·설정·실행 ID·실행 파일/DLL을 연결한다.
- `run-windows-native-gates.py`와 자체 검사: 사례별 실행, 목록·XML, PRE 프로필과 재개
  기록을 관리한다. 손상된 기록과 이전 성공 결과를 통과로 재사용하지 않는다.
- `diagnose-live.mjs`와 자체 검사: 요청 플레이어의 본영상만 측정하며 광고·소스 전환,
  오래된 표본, 잘린 AAC 정보와 정상 종료 실패를 구분한다.

이 보조 도구는 이 PC의 절대 경로를 가정하며 Mac용 정식 명령이 아니다. 과거
`handoff-validation-tools.zip`은 결함 수정 전의 역사적 초안이므로 현재 실행기 대신
사용하지 않는다. 자체 검사와 실제 native·앱 실행 통과를 구분한다.

**이번 중간 커밋은 진행 중인 컴파일을 중단하지 않는다.** 저장소 HEAD·문서가 바뀌면
시작 당시 고정한 저장소 지문과 달라져, 현재 실행의 마지막 출처 검사가 이를 거부할 수
있다. 이 경우 컴파일 오류와 구분한다. 검토한 새 커밋에 지문을 다시 고정하고 같은 캐시에서
앱 증분 빌드를 실행해 성공 기록을 새 커밋에 연결한다. 이전 기록을 수동으로 성공 처리하거나
지문 확인을 끄거나 빌드 캐시를 지우지 않는다.

그 뒤 후속 native 대상 빌드, 실제 목록·검사·캡처, 격리 프로필의 실제 앱과 미디어를
검증한다. 작업 수는 실행마다 집계되고 캐시 판정에 따라 분모도 변하므로 이전 실행과
단순 합산하지 않는다.

## 남은 검수와 보존

[전수 검사 행렬](../build/README.md#stable-이식-후-전수-재검사)을 OS별로 수행한다.
Layout all, Header·Sidebar, 프로젝트 전체·서비스, Site Controls·내부 페이지·차단·Agent
계약을 포함한다. unified 조건부 사례와 RTL 125%를 기본 조건과 구분하고, 서비스 워커
새 사례 3개도 실제 `content_unittests`에서 실행한다.

실제 앱의 탭·그룹 접기/펼치기·첫 탭 닫기·Favorites 드래그·분할·맞춤설정·창 버튼·주소창·
미디어·전체화면·정상 종료와 복원을 확인한다. 기본 속도 모션과 촬영용 느린 모션,
초기 프로필 검사와 실사용 데이터 이전 검사를 구분한다.

새 실행 파일로 직접 MP4·MSE의 AAC/H.264 디코딩을 확인한다. 요청 영상
`https://www.youtube.com/watch?v=NS322Aijayo`는 2026-10-29 공개 조회에서 종료된
다시보기였으므로 현재 재생 가능 상태를 다시 확인하고 별도 진행 중 라이브도 검증한다.
현재 라이브 ID는 실행 직전 조회하며 과거 YTN ID를 고정해 재사용하지 않는다.

Mac에서는 CLI/MCP의 새 앱 연결·관찰·입력·탭 전환·취소도 확인한다. Windows의 현재
transport 비활성을 통과로 세거나 이번 이식에서 지원 범위를 조용히 넓히지 않는다.

작업 규칙은 [AGENTS.md](../AGENTS.md), [CONTRIBUTING.md](../CONTRIBUTING.md)와
두 필수 스킬 `codebase-architecture`·`codewright`다. 이후 코드 수정도 검사·별도 독립 검토·
영향받는 실제 실행 검수를 거친다.

기존 원본·출력·설치판·실사용 프로필·빌드 캐시와 필요한 UX 시안을 보존한다.
Windows 작업 폴더의 `native-before.tar.gz`·JSON·기존 stash는 복구 자료다.
`resolve-batch*.py`, `port-*.py`, `refresh-canonical-patches.py`, `fix-webui-shadow.py`,
`finish-webui-shadow-test.py`는 일회성 도구이므로 다시 실행하지 않는다.
0001은 정식 `tools/overlay/regenerate-shell-patch.py --chromium-src ...`를 사용하고,
0002에는 생성 BRANDING·GRD·owned 소스를 다시 넣지 않는다.

검수 후 현재 작업이 만든 임시 프로필·원시 캡처·로그는 필요한 요약을 남겨 정리한다.
이 체크포인트의 미완료 상태를 최종 검수 완료나 배포 승인으로 사용하지 않는다.
