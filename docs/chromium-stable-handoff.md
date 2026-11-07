# pilot Chromium 157 Windows 인수인계

2026-11-07 KST 기준. 공통 원본은 Chromium `157.0.8103.27`이며
[버전 핀](../build/chromium-version.json)을 Windows·macOS가 공유한다.
macOS 구조 개편과 전체 gate는 완료했다. Windows는 새 소스의 컴파일·검사와 실제 앱
검수 항목 18개가 남아 있다. 이 기록은 배포 승인이 아니다.

## 다음 Windows 세션 작업

시작점은 `2210e67` 또는 이를 포함하는 이후 `pilot`이다. 기존 변경을 보존한 뒤
`git pull --ff-only`로 받는다. 다음 순서로 진행한다.

1. [ ] 최신 pilot 사전 컴파일·빌드.
   [11-04 이후 변경 범위](#windows에서-처음-빌드할-변경-범위)를 Windows에서 처음 컴파일한다.
   기존 루트·출력을 명시하고 `configure.ps1`로 graph를 갱신한다.
   `check-compile.ps1 -List -IncludeUnbuilt`로 Mac graph에 object가 없던 Windows
   전용 파일을 먼저 확인하고, `check-compile.ps1 -IncludeUnbuilt` 뒤 앱·검사 대상을 빌드한다.
   완료 조건은 해당 소스의 컴파일·링크 성공과 새 소스·바이너리 출처의 일치다.
   목록 조회와 플랫폼 미컴파일은 통과로 세지 않는다.
   [사전 컴파일 규칙](../tools/dev/README.md#chromium-이식-사전-컴파일-점검)과
   [Windows 빌드 명령](../tools/dev/README.md#windows에서-작업하기)을 따른다.

2. [ ] Windows 전용 테스트·지원 코드 이전.
   [남은 원본 경로 표](chromium-upgrade-structure.md#2-테스트-소유권)의
   `interactive_test_utils_win.cc` 211줄, `ui_controls_internal_win.cc` 6줄,
   `install_unittest.cc` 11줄, `taskbar_util_unittest.cc` 157줄,
   `ui/aura/window_unittest.cc` 54줄과 `interactive_test_utils.h` 22줄을 다룬다.
   Mac P3/P4처럼 Yee 소유 fixture·지원 코드로 옮기고 원본을 pristine으로 복원한다.
   기존 assertion·tier·OS·parameter·입력 계약을 유지한다.
   완료 조건은 이전한 소스의 Windows 컴파일·발견·실행 성공과 원본 복원이다.
   `0001`을 재생성하고 사라진 경로를
   [관심사 지도](../build/patch-concerns.json)에서도 제거한다. 불필요한 래칫 예외를
   정리하고 `check-patch-ratchet.py --update`로 감소분을 잠근 뒤 기본 검사를 통과한다.
   [패치 유지 방식](chromium-upgrade-structure.md#테스트와-패치-유지-방식)과
   [래칫 규칙](chromium-upgrade-structure.md#패치-래칫)을 따른다.

3. [ ] Windows 검사 실행기·필터 전환.
   저장소에는 Windows 테스트 gate `.ps1`이 없다. Windows 검사는 PC 로컬
   `run-windows-native-gates.py`로 실행한다.
   레이아웃·분할은 1단계에서 58개를 이전했고, 중복 `ResizesToMinSize` 삭제
   (`732c6a1`) 뒤 현재 선언은 57개다. drag 52개와 함께
   `yee_interactive_ui_tests.exe`의 Yee fixture를 선택해야 한다.
   [전체 gate 기준표](#windows-검사-gate-기준)의 `tools/dev/test-*.sh` 전체를 기준으로
   Windows 적용 gate의 모드·대상·필터를 전환한다. Windows 조건부 사례와
   tier·기능 플래그·RTL·배율 인수도 유지한다.
   `setup_unittests --gtest_filter=TaskbarPinningTest.*` 7개와 layout fast의
   `yee_aura_unittests`를 연결·실행한다. 이전 후 바뀐 이름·경로도 반영한다.
   [tier manifest](../tests/tooling/test_tiers.json)의 OS·parameter·tier와
   [Windows 미검증 예외](../tests/tooling/gate_coverage_allowlist.json) 18개를 대조한다.
   caption·clip·분할 테마·그룹 drag·Footer popup·Customize Chrome popup·UIA도 포함한다.
   Yee main의 UIControls 초기화, background/foreground 정리와 무관한 창 보존을 확인한다.
   완료 조건은 Windows 적용 gate 전부의 대상·필터 전환과 발견·실행 확인이다.
   목록·XML·출처 일치와 Windows 예외의 미검증 사유 해소도 실제 결과로 확인한다.
   [gate 연결 규칙](../tools/dev/README.md#변경-후-테스트)과
   [coverage 검사](../tests/tooling/README.md#yee-테스트-gate-누락-검사)를 따른다.

   결정 필요: 세션 시작 때 사용자가 실행기 위치를 정한다.

   - PC 로컬 실행기 유지: 기존 출처 검증을 유지하며 대상·필터를 갱신한다.
   - 저장소 `.ps1` gate로 이전: 공통 실행·발견·XML·출처 규약을 연결하고 Windows에서 검증한다.

4. [ ] Windows Yee 테스트의 applied-wait 확인.
   [대기 규칙](../tools/dev/README.md#ui-테스트-작성과-applied-wait)과
   [정확한 예외 목록](../tests/tooling/legacy_ui_wait_allowlist.json)을 따른다.
   2026-11-07 정적 검사에서 Windows 조건부 본문을 포함한 owned UI 테스트·지원 코드의
   미분류 위반은 없었다. Windows 실행 통과를 뜻하지 않는다.
   원본에서 옮기는 지원 코드도 입력 전에 ticket을 등록하고
   `WaitForApplied(ticket, conditions)`로 적용 완료를 판정한다.
   완료 조건은 `test_legacy_ui_waits.py` 통과와 Windows 실제 실행에서 조건·수명·deadline 확인이다.

5. [ ] 외부 제어 도구의 URL 판별 중단 조사.
   [확인된 중단 범위](#외부-제어-중단의-확인-범위)를 기준으로 원인을 조사한다.
   브라우저 정체성·실제 URL·AX 역할을 위장하지 않는다. 필요하면 Chromium 표준 방식으로
   URL을 노출한다. 다른 입력 경로로 우회하지 않는다.
   완료 조건은 새 빌드의 실제 URL과 화면을 외부 도구가 정상 관측하고,
   원인·수정 또는 남은 한계를 구분해 기록하는 것이다.

6. [ ] 새 빌드의 실제 앱 18항목 검수.
   [18항목 표](#windows-실제-앱-검수-18항목)를 각각 실행하고 결과를 남긴다.
   11-04 기록은 `a62a2a9` 기반의 예전 빌드이므로 새 빌드에서 다시 확인한다.
   사용자는 자동 검사 대체를 승인하지 않았다.
   완료 조건은 새 실행 파일·격리 프로필에서 18항목을 개별 확인하는 것이다.
   막힌 항목은 미검증으로 남기며 native·기술적 미디어 결과를 수동 통과로 옮기지 않는다.

## Windows 검사 gate 기준

기준은 `tools/dev/test-*.sh` 14개 전체다. Windows native 적용 11개, macOS 전용
1개, opt-in 1개와 공통 도구 보조 1개다. Windows 실행 파일에는 `.exe`가 붙는다.
각 스크립트의 모든 적용 모드와 필터·tier·기능 인수를 함께 맞춘다.
`test-run-preflight.sh`는 native 사례 발견 대상이 없는 도구 fixture다.

| gate 스크립트 | 실행 파일 | Windows 적용 여부·비고 |
| --- | --- | --- |
| [test-agent-browser.sh](../tools/dev/test-agent-browser.sh) | `multi_contents_geometry_unittests` | Windows 적용. Agent 계약·입력·탭 activity·ledger·권한 |
| [test-agent-prompt.sh](../tools/dev/test-agent-prompt.sh) | `agent_bridge_prompt_unittests` | Windows 적용. `AgentBridgePromptTest.*`·`AgentBridgePointerGeometryTest.*` |
| [test-applied-wait.sh](../tools/dev/test-applied-wait.sh) | `multi_contents_geometry_unittests`, `yee_interactive_ui_tests`, `site_controls_browsertests` | Windows 적용. core·interactive·browser·all과 `YeeAppliedWait*` 사례 |
| [test-browser-surface-layout.sh](../tools/dev/test-browser-surface-layout.sh) | `yee_layout_unittests`, `multi_contents_geometry_unittests`, `yee_aura_unittests`, `yee_interactive_ui_tests`, `site_controls_browsertests`, `browser_tests` | Windows 적용. fast·horizontal·interactive·browser·all. Aura는 Windows, glass·immersive는 macOS 분기 |
| [test-content-blocking-native.sh](../tools/dev/test-content-blocking-native.sh) | `yee_content_blocking_unittests`, `yee_filtering_factory_unittests`, `yee_service_worker_unittests` | Windows 적용. SW 필터는 `YeeServiceWorkerContainerHostTest.WrapSubresourceLoaderFactory_*` |
| [test-header.sh](../tools/dev/test-header.sh) | `yee_header_unittests`, `yee_interactive_ui_tests` | Windows 적용. unit·interactive·all, RTL 125% |
| [test-internal-pages.sh](../tools/dev/test-internal-pages.sh) | `internal_urls_unittests`, `site_controls_browsertests`, `mac_identity_unittests` | Windows 적용. `mac_identity_unittests`만 macOS 전용. UI·tier·filter 선택 유지 |
| [test-macos-install.sh](../tools/dev/test-macos-install.sh) | `install_sh_test` | macOS 전용. 임시 DMG·설치 템플릿 |
| [test-omnibox-accessibility.sh](../tools/dev/test-omnibox-accessibility.sh) | `site_controls_browsertests` | Windows 적용. foreground·all에서 주소·문서 URL·탐색 검사 |
| [test-projects.sh](../tools/dev/test-projects.sh) | `project_service_unittests`, `site_controls_browsertests` | Windows 적용. unit·browser·all과 unified 전용 사례 |
| [test-run-preflight.sh](../tools/dev/test-run-preflight.sh) | 없음(zsh 임시 fixture) | 공통 도구 보조. Framework 최신성(macOS)·소유 소스 동기화 계약. GTest 필터 없음 |
| [test-sidebar.sh](../tools/dev/test-sidebar.sh) | `yee_layout_unittests`, `yee_interactive_ui_tests` | Windows 적용. unit·interactive·all, Windows Footer popup 포함 |
| [test-site-controls.sh](../tools/dev/test-site-controls.sh) | `site_controls_browsertests` | Windows 적용. Windows UIA·시작 설정·favicon·그룹 이름·Tab Search 포함 |
| [test-youtube-live.sh](../tools/dev/test-youtube-live.sh) | `site_controls_browsertests` | opt-in. Windows에서도 off·on을 명시해 실행하며 일반 필수 gate와 구분 |

## Windows에서 처음 빌드할 변경 범위

| 변경 | Windows 확인 범위 |
| --- | --- |
| 테스트 정리 batch 3~5b | 공유 치수 계약·단언, 제품 updater 구성·경계 검사, tier·headless main·격리 실행 |
| 대기 API P1~P4 | ticket·적용값·observer·Kombucha 연결, drag 52개 이전, Mac·공용 원본 pristine 복원 |
| 그룹 capsule 검사 | native animation clock에 맞춘 중간 높이 판정 |
| PNC 조립 (`afa5957`) | embedder root 생성·View 연결·정리 |
| `yee_ui.h` 분리 (`c2f6eb2`) | metrics·정책·색상·View helper별 직접 include, Windows frame 소스의 선언·의존성 |
| Customize Chrome | Yee `TabLayoutHandler`의 문서·pipe 수명과 기존 권한 검사 |
| Site Controls (`fcaf86d`) | 소유 탭 비활성화·다른 창 분리 시 popup 닫기 |
| Service Worker·관심사 지도 | factory wrap 유지 결정(`1ad4084`), 물리 분리 없이 경로별 분류(`2210e67`) |

구현 경계와 유지 결정은 [이식 구조](chromium-upgrade-structure.md#구현된-소유-경계)를 따른다.
Windows의 기존 Chromium 경로는 `F:/yee/chromium-stable157/src`, 출력은
`out/YeeStable157`이다. 이전 후보는 component·symbol 0·jobs 4였으며 설치용 릴리즈가 아니다.
새 세션에서 현재 루트·graph·프로세스를 다시 확인하고 설치판·실사용 프로필을 보존한다.

## Windows에서 완료한 결과

11-04 검증은 `a62a2a9`에 Windows 도구·fixture·전체 URL 표시 수정을 적용한 범위다.
전달 전 `07002c0`의 문서·개발/검사 도구 변경도 반영했다. 아래 결과는 새 `pilot`의
Windows 통과 증거가 아니며 서로 다른 검사 범위를 합산하지 않는다.

| 날짜·범위 | 결과 |
| --- | --- |
| 11-02 제품 수명·초기화 | 부착 전 그룹 Header 테마 조회 방지·부착 뒤 inset 재계산, 프로필 초기화 단계의 coordinator factory 등록 완료 |
| 11-02 입력·fixture | 방향키 확장 표시와 활성 창·배치·드롭 위치 준비 보완. 정확한 순서·그룹·URL 판정 유지 |
| 11-02 native | 24개 gate: 818 통과·1 조건부 생략·실패 0. 추가 native 40개·집중 검사 21개 통과 |
| 11-02 기술적 앱·미디어 | 정상 그룹 복원, 직접 MP4·MSE AAC/H.264, 요청 다시보기·당시 공개 라이브 포함 5단계 통과 |
| 11-04 PowerShell 도구 | 루트·출력 선택 가드(17·18), `check-compile.ps1`, object·명령줄 길이·stderr·UTF-8 처리 구현·검증 완료 |
| 11-04 사전 컴파일 | 당시 Windows graph의 298개 파일·303개 object 성공. graph에 없는 Mac 전용 10개는 미검증 |
| 11-04 native | 도구·Footer 8개 gate: 369 통과·1 조건부 생략·실패 0. unified 대응 사례는 별도 통과 |
| 11-04 URL 관측 | HWND Control View의 native 주소창·문서 URL, 초안·Escape·탭/분할·실제 탐색 18개 통과 |
| 11-04 전체 URL 표시 | `omnibox.prevent_url_elisions`를 존중해 장식 레이어를 숨김. HTTP·HTTPS × Shell·native Toolbar 회귀 4개 통과 |
| 11-04 빌드·도구 검사 | 앱·검사 대상 빌드와 소스·바이너리 출처 대조 완료. 통합 tooling 163 통과·119 환경 조건 생략·실패 0 |

Footer Escape 검사는 Windows의 `MOCK_TIME` timeout이 native 활성화 메시지보다 먼저
만료하던 fixture를 보완했다. 제품 포커스 동작과 기존 단언은 유지했다.
전체 URL 표시 수정 후 8개 gate 전체를 다시 실행하지는 않았다.

원시 증거는 Windows PC의 `F:/yee/pilot-repo/.local-build/{windows-followup-20261103,windows-url-boundary-20261104,windows-manual18-20261104}/`에, 11-02 자료는 `F:/yee/worktrees/project-navigation-handoff/.local-build/chromium-stable-upgrade-20261029/`에 있다. Git으로 전송하지 않는다.

## 외부 제어 중단의 확인 범위

새 전용 프로필·영어 locale·전체 URL 표시·renderer 접근성 노출로 개발 빌드를 실행했다.
외부 도구가 정확한 실행 경로와 `Example Domain - Yee` 창을 찾고 `get_window`를 반환했지만,
활성화·첫 상태 요청 과정에서 현재 URL을 정책 적용에 충분한 확신으로 판별하지 못해 중단했다.
성공한 화면·접근성 상태 캡처와 18항목의 조작은 없었다. 어느 내부 단계에서 중단됐는지는
기록만으로 확정할 수 없다. 중단 뒤 추가 입력이나 다른 제어 경로를 사용하지 않았다.

native UIA에서는 주소창 Edit와 Document의 실제 URL이 노출됐다.
[Yee 자체 URL 검사](header/test-coverage.md)의 18개 통과와 전체 URL 표시 회귀 4개 통과는
외부 도구의 URL 판독 성공을 입증하지 않는다. 현재 URL은 Chromium 탭·`WebContents`·
내비게이션·주소창이 관리하며 외부 도구는 Yee의 실행 의존성이 아니다.
전체 URL 표시 수정은 사용자 설정 호환 문제를 해결했으며 외부 중단의 원인으로 확정되지 않았다.

확인한 10-30 기록에도 같은 중단이 있다. 도구·플랫폼 호환성 또는 회귀 가능성은 미확정이다.
제품 접근성 결함·AI 기능·Codex 업데이트를 원인이나 확정 해결책으로 단정하지 않는다.

## Windows 실제 앱 검수 18항목

현재 0 통과·18 미검증이다. 새 빌드에서 실제 포인터·키보드와 전체 앱 창을 확인한다.

| 실제 앱 항목 | 확인할 동작 |
| --- | --- |
| 1. 실행 식별 | 새 앱·전용 프로필·전체 창·시작 URL |
| 2. 주소 탐색 | 주소 편집·이동·뒤로·앞으로 |
| 3. 수평 탭 | 생성·전환·닫기 |
| 4. 수직 탭 | 배치 전환·행과 Header 표시 |
| 5. 그룹 접기 | 만들기·접기·펼치기 |
| 6. 그룹 첫 탭 닫기 | 남은 멤버·그룹·순서 유지 |
| 7. 그룹 편집 | 편집기 열기·닫기·표시 |
| 8. 위에서 드래그 | 실제 포인터로 그룹 중간 진입 |
| 9. 아래에서 드래그 | 실제 포인터로 그룹 중간 진입 |
| 10. Favorites | 추가·선택·재정렬·일반 탭 복귀 |
| 11. 분할 | 생성·활성 pane 전환·divider 조절·한쪽 닫기 |
| 12. 프로젝트 | A→B→A와 현재 창 탭 보존 |
| 13. 프로필 | 전용 로컬 프로필 전환·다른 창 생성/재사용 |
| 14. 맞춤설정 | 테마·배치, 실제 어두운 두 pane와 비활성 Header 전환 |
| 15. 창 버튼 | 최대화·복원·최소화·복귀 |
| 16. 전체화면 | 실제 진입·복귀 |
| 17. 정상 종료·복원 | 같은 전용 프로필에서 탭·그룹 복원 |
| 18. 미디어 표시 | 실제 앱 전체 창에서 영상·재생 진행·현재 공개 라이브 |

초기 프로필 검수와 실사용 데이터 이전, 기본 속도 모션과 촬영용 느린 모션은 구분한다.
기술적 복원·코덱·native 검사를 이 표의 통과로 대신 기록하지 않는다.

## macOS 완료 범위와 검증 한계

구조 개편의 macOS 전체 gate(`--tier=all`)가 통과했다.
Site Controls 수정(`fcaf86d`) 뒤 표시형 all·background·foreground도 각각 통과했다.
Customize Chrome 탭 배치는 새 검수 프로필의 실제 앱에서 DevTools 입력과 화면 캡처로 확인했다.
반복 flake 측정과 Linux·Windows 컴파일·실행은 이 결과에 포함하지 않는다.

11-03의 이전 macOS 앱 검수는 14 수동 통과·4 native 대체였다. 그룹 중간 위·아래 드래그,
Favorites 재정렬과 분할 경계 드래그는 당시 사용자 결정으로 기본·RTL 125%의 실제 포인터
native 회귀로 대체했다. 이 결정은 Windows에 적용하지 않는다.
직접 MP4·MSE 코덱 2개, 전면 공개 라이브, 사용자 VOD 청취, CLI 범위 검사·MCP 9개 사례와 설치 복원도
당시 범위에서 통과했다. 설치판은 ad-hoc 테스트 패키지이며 Developer ID·공증은 별도다.

사전 컴파일은 graph의 대상만 검사한다. 미빌드 생략·플랫폼 미컴파일·전체 링크·실제 실행을
각각 구분한다. 소스 지문과 바이너리 기록만으로 빌드 출처를 증명하지 않는다.
`source-fingerprint.py`·`capture-build-provenance.py`·`run-windows-native-gates.py`의
PC 로컬 출처 검증을 유지한다. 런타임 변경은 새 검토·빌드·영향받는 검사를 거친다.
foreground 검사는 전용 화면에서 직렬 실행하며 무관한 앱·창·프로필을 보존한다.

과거 COM callback 충돌의 근본 원인은 미확정이다. 해당 사례 통과를 원인 규명으로 해석하지
않는다. 다음 이식에서 사전 컴파일의 시간 효과와 관심사별 충돌·gate 비용을
[업그레이드 기록 기준](chromium-upgrade-structure.md#업그레이드-기록과-남은-결정)으로 측정한다.
