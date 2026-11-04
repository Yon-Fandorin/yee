# pilot Chromium 157 통합 체크포인트와 빌드 인수인계

2026-11-02 KST · 사용자가 Windows 실제 앱 검수가 막힌 상태에서 Mac으로 인계하고
현재 수정본을 커밋·푸시하도록 요청했다. 전달 대상은 **`origin/pilot`**이다.
Windows 앱·검사 대상 빌드, native 검사와 기술적 미디어 검증은 아래 범위까지 완료했다.
Windows 실제 앱 검수 항목 18개는 미완료다. macOS 결과는 2026-11-03 갱신한 아래 표와
[macOS 검증 결과](#macos-검증-결과-2026-11-03)를 따른다. 이 체크포인트는 배포 승인 기록이 아니다.

2026-11-04 KST 갱신: Windows 검수는 `a62a2a9`에 후속 변경을 적용한 상태에서 수행했다.
전달 전에 원격의 추가 3개 커밋을 `07002c0`까지 반영했다. 최신 상태는
[표시 설정 수정과 중간 인계](#windows-표시-설정-수정과-중간-인계-2026-11-04)를 따른다.
사용자 요청으로 실제 앱 검수 미완료 상태를 기록해 전달하며, 이전 실행의 통과 수를 합산하지 않는다.

## 현재 상태

| 항목 | 값·상태 |
| --- | --- |
| 브랜치 | `pilot`; `07002c0`까지 반영하고 Windows 후속 변경을 함께 전달 |
| 초기 통합 | Stable157 `2483ff9` + Mac 프로필 메뉴 `f1565ce`; 공통 기반 `7e9443d` |
| 공통 Chromium | `157.0.8103.27` / `f9d7b13a569b352a0267234ea5740fdecc83ffa2` |
| 버전 기준 | [chromium-version.json](../build/chromium-version.json) 하나를 Windows·macOS가 공유 |
| Windows 최신 앱·검사 대상 빌드 | **완료**, 전체 URL 표시 수정 후 소스 지문·EXE/DLL 564개 연결 (2026-11-04) |
| Windows 도구·Footer native 8개 gate | **369 통과·1 조건부 생략·실패 0**, 실제 XML 370개; 전체 URL 표시 수정 전 실행 |
| Windows Yee 자체 URL 검사 | **18 통과·생략 0·실패 0**, HWND 주소창·문서 URL 확인; 전체 URL 표시 수정 전 실행 |
| Windows 전체 URL 표시 설정 회귀 | **4 통과·생략 0·실패 0**; 설정을 켜고 끌 때 표시·값·Ctrl+L 확인. 일반 앱 검수 18개와 다른 검사 |
| Windows 이전 native 24개 gate | **818 통과·1 조건부 생략·실패 0** (2026-11-02); 기록 검증의 별도 복구 절차 보존 |
| Windows 이전 추가 native / 집중 검사 | **40 통과 / 21 통과** (2026-11-02); 최신 결과와 합산하지 않음 |
| Windows 이전 기술적 앱·미디어 | **5단계 통과** (2026-11-02); 이번 실행에서 재검하지 않음 |
| Windows 실제 앱 검수 항목 18개 | **0 통과·18 미검증**; 2026-11-04 새 빌드에서도 외부 제어 도구의 URL 판별 중단 |
| macOS 빌드·집중 native | **완료**, 기본 10/10·RTL 125% 10/10 (2026-11-03) |
| macOS 실제 앱 검수 항목 18개 | **14 수동 통과·4 native 대체**; 드래그 계열 4개는 사용자 결정으로 native 회귀로 대체 |
| macOS 미디어·CLI/MCP | 코덱 2/2·전면 live·VOD 청취 확인; CLI 통과·MCP 9개 사례 통과 |
| Windows 설치판·실사용 프로필 | 기존 환경 보존. 이번 체크포인트에서 교체·배포하지 않음 |
| macOS 설치판 | 사용자 승인으로 `/Applications/Yee.app`을 157 ad-hoc 테스트 패키지로 교체, 프로필·북마크 보존 |

2026-11-02 Windows 검증은 Yee HEAD `3593947320557f732b39e993b7b74b28cf48b5d2`에 검토된 13개
변경 파일을 적용한 상태에서 수행했다. 전달 문서 갱신은 그 뒤의 문서 변경이다.
전달 커밋의 런타임 소스와 검증한 파일의 일치를 별도 확인하며, 과거 기록의 source commit이나
지문을 새 전달 커밋으로 소급 변경하지 않는다.

## 2026-11-02 수정한 원인과 미확정 문제

| 분류 | 확인한 원인·변경 | 근거와 한계 |
| --- | --- | --- |
| 제품 수명 | Widget에 붙기 전 그룹 Header의 테마 조회를 막고, 부착 뒤 inset을 다시 계산 | `tab_group_presentation.cc`, 0001의 `tab_group_header_view.cc`; unattached/attached 단위 판정과 최종 실제 그룹 복원에서 dump 0건 |
| 제품 초기화 | 프로젝트 coordinator factory를 Chromium의 프로필 생성 전 등록 단계에서 준비 | `project_tab_coordinator.{h,cc}`, 0001의 profile factory 목록; 실제 서비스 생성은 기존 lazy 경로 유지 |
| 테스트 입력 | Windows 가상 키 전송에서 방향키의 `KEYEVENTF_EXTENDEDKEY` 누락 보완 | 0001의 `ui_controls_internal_win.cc`; 제품 Omnibox의 Alt·숫자 키패드 처리는 변경하지 않음 |
| 드래그 fixture | 실제 활성화·배치·Favorites 준비 뒤 현재 탭 handle과 source 제외 슬롯으로 드롭 위치 결정 | 0001의 그룹 드래그 검사; 원래 정확한 순서·그룹·URL 판정 유지, 집중 3개와 전체 gate의 해당 사례 통과 |
| URL 관측 | native Omnibox·문서 URL·편집 초안·Escape·탭/분할 전환·실제 탐색 검사 추가 | [Omnibox 검사](../browser/ui/omnibox_accessibility_browsertest.cc), GN, [공개 집중 실행기](../tools/dev/test-omnibox-accessibility.sh); 외부 도구의 URL 판별 성공을 뜻하지 않음 |
| 앱 복원 검사 | 정상 종료 뒤 새 검수 프로필의 Crashpad `.dmp`를 해시와 함께 확인하고 존재하면 실패 | [그룹 세션 복원 검사](../tools/dev/test-group-session-restore.mjs); Windows 전용이며 Mac 실행기로 사용하지 않음 |

이전 153→157 이식의 API·GN 의존·브랜딩·WebUI·프로필 메뉴 변경과 이전 17개 커밋도
함께 전달한다. 소유 소스는 `browser/` 등에 두고 Chromium 연결은
`patches/0001-integrate-yee-shell.patch`에 유지했다. 최종 0001은 332개 구간이며,
마지막 수명 수정에서 변경한 구간은 그룹 Header와 profile factory 두 곳이다.

회귀 전체를 Chromium Stable 자체, Yee 테마 또는 AI 에이전트 기능 하나에 귀속할
근거는 없다. 실제 제품 수명 문제와 검사 입력·fixture 문제를 구분한다.
Shell 표시를 끈 비교도 Yee 패치가 없는 순정 Chromium 대조 실험은 아니다.
프로젝트의 대화·작업 provider는 여전히 후속 범위다.

과거 COM callback 충돌은 현재 해당 사례가 통과했지만 근본 원인은 미확정이다.
입력 간섭이나 AI 기능 문제로 단정하지 않는다. queue drain·1 DIP·double-click 경고도
원시 로그에 남아 있으며 assertion 실패와 구분한다.

## 2026-11-02 Windows에서 실제로 확인한 범위

| 검사 | 결과 |
| --- | --- |
| 앱·후속 빌드 | initial `1a830e05-6df2-45d4-a21a-ddabf51cfa1d`, followup `4b6c3e7a-b900-487d-8fad-e7ff7d029323` 통과 |
| native 24개 gate | 실제 XML 819개: 818 통과·1 생략, 재시도·실패 0. 필수 29개·프로필 필수 3개·PRE/UIA·unified 조건과 RTL 125% 57개 확인 |
| 조건부 생략 | `projects-default`의 `ProjectNavigationBrowserTest.UnifiedHorizontalLayoutToleratesMissingNativeAllocation`; unified gate의 대응 사례는 통과 |
| 추가 native 40개 | provider 12·loader 27·fallback 1 통과. method별 subresource wrapper 전체 coverage를 뜻하지 않음 |
| 집중 native 21개 | Omnibox 18·그룹 드래그 3 통과. 실제 URL 값 46개와 HTTP/HTTPS 탐색·초안 취소·history 확인 |
| 기술적 앱·미디어 5단계 | Toolbar flags, 정상 그룹 세션 복원, 직접 MP4+MSE AAC/H.264, 요청 다시보기, 별도 실제 공개 라이브 통과 |
| 최종 그룹 세션 복원 | 실제 앱 3회 정상 시작·종료, 탭·그룹 복원 및 사용자 시작 설정 유지, dump 0건 |
| 요청 영상 `NS322Aijayo` | 다시보기 64.388초·1935 frame 진행. 혼합 Opus/AAC 기록이므로 이 영상의 AAC 디코딩은 별도 입증되지 않음 |
| 당시 공개 라이브 `0sfDth4lMLc` | 2026-11-02 06:24 KST 검수: live 메타데이터·62.380초·1872 frame·AAC 디코딩 확인. 다음 검수에서는 live 여부를 새로 확인 |
| 영상 표시 | 두 영상의 페이지 viewport PNG에서 영상 확인. 전체 native 창·실제 청취·실제 앱 검수 항목 18개 통과로 확대하지 않음 |
| 독립 검토 | 최종 소유 소스 13개·native glue·0001, 위 native·미디어 원시 결과에서 추가 조치 사항 없음 |

native 사례 실행 자체는 중단 없이 끝났지만, 그 뒤 실행기의
`expectedPriorGateCaseCounts` 누락으로 후처리가 실패했다.
원래 실패 기록과 원시 XML을 보존하고, 별도 완료 검증으로 6,723개 고정 파일·사례 수·
실행 소유권·바이너리·후속 소스 지문을 다시 확인했다. 이 복구는 native를 재실행하지
않았으며 **단일 연속 end-to-end 통과가 아니다.** 초기 도구 157 통과·22 생략 같은
이전 결과도 현재 native 수에 합산하지 않는다.

## URL 제어 중단과 남은 실제 앱 검수 항목 18개

Windows native UIA 진단에서는 실제 주소창 Edit와 Document가 URL을 노출했고,
새 native 테스트에서도 URL·초안·문서·활성 탭의 관계를 확인했다.
2026-11-04 보강한 [Yee 자체 URL 검사](#url-처리-책임과-yee-자체-검사-2026-11-04)도
18개 모두 통과했다. 이 결과와 외부 도구의 판독 성공은 구분한다.
그러나 외부 Windows 제어 도구는 새 전용 프로필·영어 locale·전체 URL 표시·
`https://example.com/`에서도 현재 URL을 정책 적용에 충분한 확신으로 판별하지 못했다.
새 앱은 `Example Domain - Yee` 창으로 확인했지만, 도구의 상태 캡처 단계에서 중단됐다.

따라서 제품 접근성 결함이나 제어 도구 내부 resolver의 정확한 원인은 확정하지 못했다.
브라우저 정체성·실제 URL·AX 역할이나 도구 정책을 위장·변경하지 않았으며,
도구가 중단한 뒤 다른 입력 경로로 우회하지 않았다. 테스트 코드의 Omnibox 18개와
아래 실제 앱 검수 항목 18개는 서로 다른 목록이다. 이 숫자는 앱 개수가 아니라 Yee에서 확인할 동작의 개수다.

| 실제 앱 항목 | 남은 확인 |
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

전 항목은 현재 미검증이다. 기술적 그룹 복원·미디어 검사나 native 결과를
자동으로 이 목록의 통과로 올리지 않는다. 초기 프로필 검수와 실사용 데이터 이전,
기본 속도 모션과 촬영용 느린 모션을 구분한다.

## Mac에서 받기·빌드·검수

기존 작업을 보존하고 Yee 저장소 루트에서 받는다.

```sh
git status --short
git fetch origin pilot
git switch pilot
git merge --ff-only origin/pilot
```

로컬 `pilot`이 없으면 마지막 두 명령 대신 `git switch --track origin/pilot`을 사용한다.
전환·fast-forward가 거부되면 기존 변경·커밋을 보존하고 차이를 검토한다.
강제 초기화·강제 푸시·별도 원격 브랜치 재생성을 하지 않는다.

같은 터미널에서 기존 157 빌드 루트를 선택한다. 아래 경로는 기본 예시다.
다른 157 루트를 이미 사용했다면 그 경로를 유지하고 캐시를 재사용한다.

```sh
export YEE_LOCAL_BUILD_ROOT="$PWD/.local-build/stable157"
export YEE_OUT_NAME=YeeStable157

# 이 루트가 없는 최초 준비에서만 실행한다.
./tools/dev/checkout.sh
./tools/dev/setup-metal.sh

python3 tools/dev/chromium-version.py verify "$YEE_LOCAL_BUILD_ROOT/chromium/src" --platform macos
./tools/dev/configure.sh
./tools/dev/build.sh
./tools/dev/smoke-test.sh
./tools/dev/run.sh
```

- 새 앱은 `$YEE_LOCAL_BUILD_ROOT/chromium/src/out/YeeStable157/Yee.app`이다.
  `run.sh`는 해당 루트의 `runtime-profile`을 사용한다. 설치판·실사용 프로필을 보존한다.
- macOS 원본 선택에는 `YEE_LOCAL_BUILD_ROOT`를 사용한다.
  Windows의 `YEE_CHROMIUM_ROOT`를 대신 지정하지 않는다.
- 기본 Mac 설정은 static·symbol 0·release/AAC/H.264이며 Windows component 후보와 다르다.
  병렬 수는 `YEE_BUILD_JOBS`로 조절한다. 새 최초 준비의 공간 검사도 유지한다.
- 이미 같은 출력에서 빌드 중이면 추가 Ninja/Siso를 띄우지 않는다.
  새 터미널에서는 두 환경 변수를 다시 지정한다.

빌드 뒤 URL 비교는 같은 환경에서 실행할 수 있다.

```sh
./tools/dev/test-omnibox-accessibility.sh
```

이 명령은 검사 대상을 빌드하고 native 테스트 코드를 실행한다. macOS에서는
`Presentation/OmniboxAccessibilityBrowserTest.*` 16개를 확인하며, Windows/Linux용
Alt+방향키 탐색 2개는 생략된다. 이미 같은 소스의 검사 바이너리를 빌드했을 때만
`--no-build`를 사용한다. [Header 검사 범위](header/test-coverage.md)를 함께 읽는다.

그다음 [Browser Surface 단계별 gate](../AGENTS.md#verify), Header·Sidebar·프로젝트·
프로필 gate를 변경 범위에 맞춰 실행하고, 별도 프로필에서 실제 앱 검수 항목 18개를 확인한다.
먼저 주소창의 접근성 역할·값·문서 URL을 확인한 뒤 제어 도구 관측·입력을 대조한다.
native 값 노출과 외부 도구의 URL 인식 성공을 별도 결과로 남긴다.
어두운 실제 두 pane·테마 전환·비활성 Header는 테스트 색상 주입 없이 확인한다.

Windows 전용 `test-group-session-restore.mjs`, `test-media-codecs.mjs`와
PC 전용 `diagnose-live.mjs`를 Mac 실행기로 사용하지 않는다.
Mac에서는 실제 앱에서 정상 종료·복원, 직접 MP4와 MSE의 AAC/H.264,
요청 다시보기와 검수 시점의 실제 공개 라이브를 별도로 확인한다.
과거 live ID의 재생 성공으로 현재 라이브 검증을 대신하지 않는다.
CLI/MCP의 새 앱 연결·관찰·입력·탭 전환·취소도 확인한다.
Windows 결과를 Mac 통과로 기록하거나 transport 비활성을 통과로 세지 않는다.

## macOS 검증 결과 (2026-11-03)

`7777daf`에 검토한 Mac 후속 변경을 적용한 상태로 `out/YeeStable157`을 빌드해 검수했다.
그 변경은 `b462c19`로 커밋했으며, 검수 뒤 바뀐 것은 테스트 주석과 검사 범위 문서뿐이다.

| 검사 | 결과 |
| --- | --- |
| 집중 native | 기본 10/10·RTL 125% 10/10, 재시도·생략 0. 분할 경계 실제 마우스 드래그, native 창 크기 변경, Favorites·그룹 드래그 포함 |
| 미디어 | 현재 앱 AAC/H.264 직접 MP4·MSE 2/2, 전면 유지 strict live 90초 통과, 사용자 VOD 음성 청취 확인 |
| 실제 앱 검수 항목 18개 | 14 수동 통과. 그룹 중간 위·아래 드래그, Favorites 재정렬, 분할 경계 드래그 4개는 native 대체 |
| CLI/MCP | CLI 범위 검사 통과. MCP 9개 사례 통과, 첫 탭 연결 승인이 만료돼 재시도했으므로 중단 없는 단일 통과는 아님 |
| 설치 | `package-macos.sh --skip-build`와 `test-macos-installer.mjs` 복원 검사 통과. ad-hoc 서명이며 Developer ID·공증 별도 |

드래그 계열 4개는 2026-11-03 사용자 결정으로 수동 확인을 생략하고, 같은 동작의 실제 포인터 native
회귀(기본·RTL 125% 통과)로 대체했다. 수동 통과로 기록하지 않는다.
이 Mac 대체 결정은 Windows 실제 앱 검수 항목 18개에 적용하지 않는다.
원시 기록은 Mac 로컬 `.local-exclude/`에 있으며 Git으로 전송하지 않는다. Windows 결과를 Mac 통과로,
Mac 결과를 Windows 통과로 기록하지 않는다. 다음 Chromium 이식은 [개발 도구](../tools/dev/README.md)의
사전 컴파일 점검과 빌드 루트·출력 선택 규칙을 따른다.

## 2026-11-02 Windows 증거 위치와 재개 조건

제품 기준 저장소는 `F:/yee/pilot-repo`의 `pilot`, 검수 작업 폴더는
`F:/yee/worktrees/project-navigation-handoff`다.
Chromium은 `F:/yee/chromium-stable157/src`, 출력은 `out/YeeStable157`다.
검증한 Windows 후보는 component·symbol 0·jobs 4이며 설치용 릴리즈가 아니다.

원시 기록은 검수 폴더의 `.local-build/chromium-stable-upgrade-20261029/`에 있다.
아래 경로는 이 Windows PC의 로컬 기록이며 Git으로 전송하지 않는다.
Mac에서 확인할 소스·패치·정식 검사 코드는 커밋에 포함한다.

| 기록 | SHA-256 |
| --- | --- |
| `source-review-frozen.json` | `da5a58337d4f1b1514154de4b49ef77e65a2d6144d6331ae684fcb10b097bc25` |
| `build-provenance-4b6c3e7a-b900-487d-8fad-e7ff7d029323.json` | `ad1f7b87ea6d82ff2ed1bde773b8c9499feb591c4777d6ebfdf0ea9e32328986` |
| 원래 `core24-current-3593947-cc0209d2.json` | `b039a768c35058da0c4020baf7d2598632f2a8c3afe4fae715932fe60c1b0bfb` |
| 별도 `native819-completion-cf68aed4.json` | `5866e0ca2166278a5795dd0e04ce1d759034ead84dba092258f8946728144650` |
| `additional-native-3593947-da5a583-a43e639e/run.json` | `29d56a17b12627194cf10b31bfbcea20891ef7cc93a1ec5728077a909a37f508` |
| `acceptance-input-21-current-3593947-fd07f684.json` | `2ba0b73ba7ae1f856248291312a711377d2a578b235dff2a71de302ff5a0773c` |
| `current-app-media-3593947-509656d7/run.json` | `e2a3bffa8800f2f468861e366f2690dae6b1c2079c32557da922e0355b126c92` |
| `current-media-3593947-result.json` | `79524698aa52c0c1ef1257ab472acd7f2ef111026c0d45b9936d82b9172b8649` |

2026-11-02 Windows 입력 검수는 **사용자 요청에 따른 인계 상태**로 남겼으며,
2026-11-03 사용자의 최신 지시로 재개했다. 재개 후 결과와 새 증거 위치는 아래 최신 절을 따른다.
최신 `handoff-state.json`·실제 결과·프로세스를 먼저 확인하고, 오래된 실행 ID·커밋을
고정해서 재사용하지 않는다. PC 전용 보조 실행기는 Mac 명령이 아니다.
`source-fingerprint.py`·`capture-build-provenance.py`·`run-windows-native-gates.py`의
출처 검증을 유지하고, 원래 실패·이전 지문·복구 전 기록을 수동 성공 처리하지 않는다.
이후 런타임 변경은 새 검토·명시적 지문 고정·캐시 재빌드·영향받는 재검을 거친다.

기존 원본·출력·설치판·실사용 프로필·캐시·실패 증거를 보존한다.
일회성 `resolve-batch*.py`·`port-*.py` 등 과거 이식 초안을 다시 실행하지 않는다.
0001 갱신은 정식 `tools/overlay/regenerate-shell-patch.py --chromium-src ...`를 사용한다.
제품 소스와 canonical 패치의 일치를 유지하고, [작업 규칙](../AGENTS.md)과
[기여 절차](../CONTRIBUTING.md)의 소유권·독립 검토·실제 앱 확인을 따른다.

## Windows 후속 작업 (2026-11-03 Mac에서 넘김)

Mac에서 아래 도구 개선을 zsh 쪽에만 적용했다. Windows 작업은 이 목록을 기준으로 Windows PC에서
구현·검증하도록 인계했다. 항목 1·2는 Windows에서 구현·검증했고 항목 3의 출처 규약은 유지했다.
항목 4는 제어 도구 중단으로 계속 미검증이다. 항목 5는 2026-11-04 Mac에서 추가한 미착수 작업이다.
Mac 결과를 Windows 통과로 기록하지 않는다.

1. **빌드 루트·출력 선택 가드의 `.ps1` 대응.** `tools/dev/lib/paths.ps1`은 `YEE_OUT_NAME`이 없으면
   `YeePilot`, `YEE_CHROMIUM_ROOT`가 없으면 `LocalBuildRoot\chromium`으로 조용히 정한다. Mac의
   `tools/dev/lib/paths.zsh`·`preflight.zsh`와 같은 규칙을 적용한다.
   - 출력: `YEE_OUT_NAME` 미지정, 기본 출력에 `build.ninja` 없음, 같은 `out\*\build.ninja` 후보 존재 →
     후보와 지정 방법을 출력하고 종료 코드 18. `configure.ps1`(gn gen 전), `build.ps1`·`build-ui.ps1`
     (디스크 검사 전), `lib/build.ps1` 공통 빌드, `run.ps1`·`smoke-test.ps1`·`package-windows.ps1`
     등 부작용 전에 확인한다.
   - 루트: `YEE_CHROMIUM_ROOT`·`YEE_LOCAL_BUILD_ROOT` 모두 미지정, 기본 위치에 depot_tools·소스 없음,
     저장소 `.local-build\*` 아래에 `chromium\src\BUILD.gn` 또는 `depot_tools\gclient.bat`
     후보가 존재할 때 종료 코드 17. `F:\yee\...`처럼
     저장소 밖 루트는 찾지 않으므로 안내에 `YEE_CHROMIUM_ROOT` 지정 방법을 넣는다.
   - 명시값은 항상 우선하고, 후보가 없는 새 장비의 기존 동작은 유지한다.
   - `tests/tooling`의 PowerShell 조건부 테스트로 검증한다. Mac 동작 기준은
     `tests/tooling/test_build_selection.py`다.
2. **플랫폼 미컴파일 파일의 Windows 점검.** Mac의 `tools/dev/check-compile.sh --list`는 현재 graph에서
   object가 없는 13개 파일을 따로 보고한다. 그중 `*_win.cc`, Aura, `ui_controls_internal_win.cc` 등
   Windows 전용 파일은 다음 Chromium 이식 때 긴 Windows 빌드 전에 먼저 컴파일한다.
   Windows 대응은 [`check-compile.ps1`](../tools/dev/check-compile.ps1)로 구현했다.
3. **native 실행 기록.** Mac `run_native_test_gate`의 실행 기록은 Windows 실행기에 적용되지 않는다.
   Windows는 기존 `run-windows-native-gates.py`의 출처 검증을 유지한다.
4. **Windows 실제 앱 검수 항목 18개.** 위 [URL 제어 중단과 남은 실제 앱 검수 항목 18개](#url-제어-중단과-남은-실제-앱-검수-항목-18개)의
   미검증 상태 그대로다.
5. **테스트 이전 1단계의 Windows gate 전환 (2026-11-04 Mac에서 넘김).** Yee 레이아웃·분할 시나리오
   58개를 `browser/ui/browser_surface_interactive_uitest.cc`와 `split_pane_interactive_uitest.cc`로
   옮겼다. 이 시나리오는 새 실행 파일 `//chrome/browser/ui/views/yee:yee_interactive_ui_tests`에서만
   실행되고 fixture 이름은 `Yee`로 시작한다(예: `YeeBrowserViewTabbedLayoutImplUiTest`).
   Yee 셸에서 실패하는 원본 `TopContainerBackground`는 필수 목록에서 뺐다.
   PC 로컬 `run-windows-native-gates.py`의 layout 목록이 `interactive_ui_tests`의 이전 이름을 쓰면
   해당 사례를 찾지 못하므로, `.sh` gate의 대상과 filter를 기준으로 바꾼다. 새 target의
   Windows 전용 소스는 아직 Windows에서 빌드하지 않았다.
   [구조 문서](chromium-upgrade-structure.md)의 테스트 이전 절을 따른다.

## Windows 후속 검증 결과 (2026-11-04)

이 절의 8개 gate는 아래 전체 URL 표시 수정 전의 Windows 도구·Footer 체크포인트다.
Windows 요청은 2026-11-03 KST에 시작했고 최종 native 실행은 2026-11-04 KST에 끝났다.
Yee `a62a2a9`에 이번 Windows 도구·fixture 변경을 적용한 소스를 검수했다.
Chromium은 기존 `F:/yee/chromium-stable157/src`와 `out/YeeStable157` 캐시를 재사용했으며,
component·symbol 0·jobs 4 설정과 공통 원본 핀을 유지했다. 이전 설치판과 실사용 프로필은 보존했다.

### 구현과 검사

| 항목 | 결과 |
| --- | --- |
| PowerShell 경로 선택 | 루트 미지정은 후보가 있을 때 17, 출력 미지정은 다른 graph가 있을 때 18로 부작용 전에 중단. 명시값·유효한 기본값·후보 없는 새 장비 동작 보존 |
| Windows 사전 컴파일 | `check-compile.ps1 -IncludeUnbuilt` 성공. 298개 파일·303개 object, 미빌드 7개 포함. 현재 Windows graph에 없는 Mac 전용 10개 파일은 미컴파일로 남김 |
| 실행 경계 | Windows는 `.obj`를 직접 지정해 `cmd`의 caret 소비를 피하고, 복수 대상은 실제 Python의 `autoninja.py` 호출로 배치 명령줄 8191자 한도를 피함 |
| 도구 검사 | 전체 258개: **150 통과·108 환경 조건 생략·실패 0**. Windows 집중 15개 통과; PowerShell 5.1·7의 native stderr·실패 코드·UTF-8 기록 포함 |
| native 실행기 | 기존 프로필 소유권·직렬 실행·목록/XML·출처 검증 유지. 가짜 자식 자체 검사 35개 통과. 현재 Sidebar 프로필 목록의 필수 사례 수는 4개 |
| 최종 빌드 | initial `4c1b878e-e23e-4eb8-bb3c-b13dfdd84f64`, followup `f8f7d228-6048-4a4d-8206-10bbf08d6745` 통과. build 전후 소스 지문과 EXE/DLL 564개 연결 |
| 독립 검토 | Windows 도구·fixture 수정과 로컬 실행기 범위·출처 규약에서 최종 추가 조치 사항 없음 |

새 그래프의 목록 조회만으로 컴파일 통과를 기록하지 않았다. 실제 사전 컴파일은 의존 단계까지
2,256개를 처리했고 1시간 14분 41.95초가 걸렸다. 앱·검사 바이너리의 링크와 실행은 별도로 검증했다.
이 측정으로 cold 빌드 시간 단축을 입증하지 않는다.

### 최종 native 범위

| gate | 통과 | 조건부 생략 | 실패 |
| --- | ---: | ---: | ---: |
| layout-native | 76 | 0 | 0 |
| layout-geometry | 18 | 0 | 0 |
| layout-default | 75 | 0 | 0 |
| layout-rtl125 | 54 | 0 | 0 |
| sidebar-unit | 101 | 0 | 0 |
| sidebar-profiles | 4 | 0 | 0 |
| projects-default | 40 | 1 | 0 |
| projects-unified | 1 | 0 | 0 |
| 실행 합계 | **369** | **1** | **0** |

각 실행의 실제 목록과 XML 370개를 확인했다. 재시도·미실행·차단·증거 오류는 0이다.
조건별 중복 사례가 있으므로 합계를 고유 테스트 수나 제품 전체 통과 수로 해석하지 않는다.
기본 설정에서는 `TabStripUnification`이 꺼져 있어
`UnifiedHorizontalLayoutToleratesMissingNativeAllocation` 한 개가 스스로 생략된다.
같은 사례를 기능을 켠 `projects-unified`에서 별도로 실행해 통과했다.
원시 summary의 `all_selected_cases_passed=false`는 이 생략을 포함한 원래 판정으로 보존했다.

첫 native 실행은 **367 통과·2 실패·1 생략**이었다. 두 실패는 Footer Escape 단위 검사의
새 `ScopedRunLoopTimeout(10s)`가 `MOCK_TIME`에서 Windows native 활성화 메시지보다 먼저
만료된 fixture 문제였다. 첫 검사의 제한은 Windows에서만 제외하고, 두 번째는 기존 Mac 제한을
유지했다. traversal 이유·포커스 링·owner 활성화·분리 후 대상 focus·pointer exit 판정은 유지했다.
집중 진단 2개가 통과한 뒤 새 소스 지문·빌드 출처로 위 8개 gate 전체를 다시 실행했다.
제품의 Windows 포커스 동작은 바꾸지 않았다.

검사 바이너리 빌드 중 도구 세션이 중단된 실행도 별도로 보존했다. 이전 `running` 기록을 성공으로
바꾸지 않고, 남아 있는 빌드 프로세스가 없음을 확인한 뒤 새 실행 번호로 재개했다.
중단 빌드·첫 실패 실행·집중 2개·최종 370개·2026-11-02 결과를 합산하거나 단일 연속 통과로 묶지 않는다.

### 일반 앱 창 검수의 한계

최종 빌드의 `chrome.exe`를 새 전용 프로필로 실행했고, 정확한 실행 경로·프로필과
`Example Domain - Yee` 창을 확인했다. 영어 locale·전체 URL 표시·renderer 접근성 노출을 사용했으나,
Windows Computer Use 도구는 창 상태를 읽는 단계에서 현재 URL을 정책 적용에 충분한 확신으로
판별하지 못해 제어를 중단했다. 도구 중단 뒤 추가 입력이나 다른 제어 경로로 우회하지 않았다.

따라서 위 **실제 앱 검수 항목 18개는 0 통과·18 미검증**이다. native 369개 통과를 이 목록의 통과로
옮기지 않는다. 이 오류만으로 Yee 접근성 결함이나 도구 내부 판별기의 원인을 확정하지 않는다.
이번 검수에서 새 미디어·실제 청취·설치·실사용 프로필 이전 검증도 수행하지 않았다.

### URL 처리 책임과 Yee 자체 검사 (2026-11-04)

이 절은 이후 전체 URL 표시 수정 전의 별도 검사 기록이다.
현재 URL은 Yee의 Chromium 탭·`WebContents`·내비게이션·주소창 코드가 관리한다.
Codex는 이번 검수에서 그 값을 읽으려 한 외부 도구이며, Yee의 URL 처리에 필요한 실행 의존성이 아니다.
외부 도구의 URL 판별 중단을 Yee의 URL 처리 실패로 해석하지 않는다.

[`omnibox_accessibility_browsertest.cc`](../browser/ui/omnibox_accessibility_browsertest.cc)를
보강해 기존의 직접 provider 검사에 더해, Windows HWND의 Control View에서 native 주소창과
보이는 문서를 찾아 실제 URL 값을 읽었다. 별도 MTA 스레드의 표준 UIA 호출을 사용하며
Codex 연결이나 Computer Use 창 제어를 사용하지 않는다. 검사 범위는
[Header 검사 문서](header/test-coverage.md)를 따른다.

HTTP·HTTPS, Yee Shell·native Toolbar, 탭·분할 전환과 해제, 편집 초안·두 단계 Escape,
Enter 이동·뒤로·앞으로의 **18개 실행이 모두 통과**했다. 각 실행의 실제 XML 18개에
HWND에서 찾은 주소창 수와 문서 URL 기록을 확인했고 생략·실패·미실행·증거 오류는 0이다.
실행 뒤 소스 지문 대조도 통과했다. 기존 369 통과·1 생략과 합산하지 않는다.

당시 변경은 검사 코드와 문서에 한정되며 제품의 URL 값·역할·정체성을 바꾸지 않았다.
당시 제품 `chrome.exe`·`chrome.dll`은 앞서 검수한 바이트와 동일했다. 독립 검토에서 추가 조치 사항은 없었다.
이 자체 검사 통과로 위 일반 앱 검수 항목 18개나 외부 제어 도구의 판독 성공을 통과 처리하지 않는다.

새 기록은 Windows 로컬 `F:/yee/pilot-repo/.local-build/windows-url-boundary-20261104/`에 보존한다.
이 절의 문서 갱신은 실행·소스 대조 뒤에 했으며 검수 당시 HEAD·지문을 소급 변경하지 않는다.

| 기록 | SHA-256 |
| --- | --- |
| `source-review-frozen.json` | `794687fb0d579333265fcde3a58ddbc9db44ece347c84add9f5ccfba9ba7c76e` |
| `build-provenance.json` | `8d17ad87f591bf5d285d27bea07730e1d82f7d7526d238fa7b2a7173fbd91c3a` |
| `native-window-url/summary.json` | `d1eada704ec0a93498fe2e33e137511709308ca9cfd61889e531bfcf0d773f17` |

### 도구·Footer 체크포인트 증거와 다음 Mac 세션

원시 자료는 Windows 로컬 `F:/yee/pilot-repo/.local-build/windows-followup-20261103/`에 보존한다.
Git으로 원시 로그·프로필을 전송하지 않는다.

| 기록 | SHA-256 |
| --- | --- |
| `source-review-frozen.json` | `8f77d3f1d5fbc66aa462160dd6a8faf3f5b6e2de67339ae34d7c51da31f27f05` |
| `build-provenance-footer-clock-fixed.json` | `53d47281e4dcc356252f1df56542bfda4166d9904d8aee0a02ac14954b54b391` |
| `native-eight-final-footer-clock/summary.json` | `fae6daf7df139c074926af60e5eb60d7ce587bf27689e67ae40ffc15db97acff` |
| `actual-app-result.json` | `521a91b53882803e93e956f343484454cbd0de7b148403ba0ab6fd84087cf449` |

최종 native 뒤 소스 지문 대조도 통과했다. 이 인수인계 문서 갱신은 그 뒤의 문서 변경이며,
검수 당시 HEAD·지문을 전달 커밋으로 소급 변경하지 않는다.

Mac 쪽은 추가 편집 계획이 없다는 사용자 안내를 받았다. 다음 Mac 세션에서는 `pilot`을
`git pull --ff-only`로 받은 뒤 Mac 로컬 `current-state.md`에 이 Windows 결과와 남은 실제 앱
검수 항목 18개 미검증 상태를 맞춘다. Mac의 14 수동 통과·4 native 대체 결과와 구분해 기록한다.

## Windows 표시 설정 수정과 중간 인계 (2026-11-04)

사용자는 현재까지 작업을 정리해 인수인계 문서를 갱신하고 커밋·푸시하도록 요청했다.
실제 앱 검수 항목 18개는 **0 통과·18 미검증**인 중간 체크포인트다.
빌드·자동 검사 통과를 실제 앱 검수 완료나 배포 승인으로 해석하지 않는다.

### 추가로 확인하고 수정한 제품 표시 문제

전용 검수 프로필의 `omnibox.prevent_url_elisions=true`에도 Yee의 대기 표시 레이어가
native URL을 `origin | title`로 덮는 경로가 있었다. 기존 `LocationBarView`에서 이 설정을
관찰하고, 켜졌을 때 장식 레이어를 숨겨 native 전체 URL을 그대로 보이도록 수정했다.
설정을 끄면 기본 표시가 복원된다. URL 모델·접근성 역할·브라우저 정체성은 변경하지 않았다.
이 표시는 사용자 설정 호환 문제이며, 외부 도구 중단의 근본 원인으로 확정하지 않는다.

Chromium glue는 정식 재생성 도구로 `0001`에 반영했다. 전체 335개 섹션 중 내용 변경은
`chrome/browser/ui/views/location_bar/location_bar_view.cc` 한 곳이며 기존 섹션 순서를 보존했다.
역적용 검사와 새 패치 래칫 검사를 통과했다. 소유권·설정 갱신·native 동작과 패치 일치의
독립 검토에서 추가 조치 사항은 없었다.

| 검사 | 결과·범위 |
| --- | --- |
| 최종 앱 빌드 | initial `a2183485-c66e-4253-9539-7b5e1629d381` 통과 |
| 최종 검사 대상 빌드 | followup `e49c1637-901c-48c4-864f-34aab262335b` 통과 |
| 전체 URL 표시 회귀 | HTTP·HTTPS × Yee Shell·native Toolbar **4/4 통과**, 재시도·생략·실패 0. 같은 문서에서 설정 false→true→false, 표시·값·문서 URL·Ctrl+L 확인 |
| 실행 전 소스 대조 | 새 빌드·회귀 검사 뒤 소스 지문 대조 통과 |
| 전달 전 통합 도구 검사 | 원격 `07002c0` 반영 후 전체 282개: **163 통과·119 환경 조건 생략·실패 0**. 패치 래칫도 통과 |
| 일반 앱 실행 | 새 전용 프로필·영어 locale·전체 URL 표시로 프로세스 실행. `Example Domain - Yee` 창 식별 |
| 외부 창 제어 | 새 제어 연결에서도 첫 화면 관측 전에 같은 URL 판별 중단. 성공한 상태 캡처·시나리오 조작 없음 |

위 4개는 제품 수정의 회귀 검사이며, 앞선 자체 URL 검사 18개·native 369개와 합산하지 않는다.
전체 URL 표시 수정 후 8개 gate 전체를 다시 실행한 것은 아니다.
원격 통합 뒤 첫 도구 검사는 macOS용 `sysctl` 모킹에서 Windows에 없는 `os.sysconf` 속성을
생성하지 못해 1개 오류로 끝났다. 테스트에 `create=True`만 추가해 읽기 전용 PID·인수·커널 오류
판정을 유지했고, 위 재검은 통과했다. 제품 코드 변경이 아니며 최초 실패 로그도 보존했다.
이 테스트 호환성 문제를 외부 창 제어 중단의 원인으로 판단하지 않는다.

### 외부 제어 중단의 확인 범위와 이전 기록

Windows 제어 연결을 초기화하고 새 빌드의 실행 파일 해시·전용 프로필을 확인해 실행했다.
`list_apps`에는 개발 빌드가 없었고 `list_windows`에서 정확한 실행 경로의 창 하나를 찾았다.
반환된 창은 ID `2425550`, 제목 `Example Domain - Yee`였다.
`get_window`는 반환됐지만, 뒤따르는 활성화·첫 상태 요청 과정에서 도구가 다음 오류로 중단했다.

> Computer Use has been stopped for this turn because it could not determine the current browser URL on Windows with enough confidence to enforce policy.

성공한 화면·접근성 상태를 받지 못했다. 활성화와 상태 요청 중 어느 내부 단계에서 중단됐는지는
기록만으로 확정할 수 없다. 중단 뒤 앱 입력이나 다른 제어 경로를 사용하지 않았다.
검수용 fixture 페이지는 준비했지만 로컬 서버는 시작하지 않았고, 실제 앱 18개 동작을 수행하지 못했다.

확인한 Windows 157 인계 기록에는 2026-10-30에도 주소창을 사용자가 준비한 뒤 동일한 중단이 있다.
기존 native·미디어 검사 성공은 해당 외부 제어 경로의 성공을 입증하지 않는다.
이번 확인 범위에서는 같은 도구의 과거 성공 기록을 찾지 못했으나, 더 이전의 성공을 부정하거나
도구·플랫폼 호환성 및 회귀 가능성을 배제할 근거도 없다. 원인은 미확정이다.
Codex 업데이트나 이번 표시 수정을 확정 해결책으로 인계하지 않는다.

### 최신 로컬 증거와 재개 조건

원시 기록은 `F:/yee/pilot-repo/.local-build/windows-manual18-20261104/`에 보존하며 Git에 넣지 않는다.
시작 프로세스는 PID `36628`, 실행 파일은 `F:/yee/chromium-stable157/src/out/YeeStable157/chrome.exe`,
프로필은 `F:/yee/.local-build/manual-ui-profiles/yee157-ui18-fullurl-1042a0b8`였다.
이 PID가 다음 세션에도 유효하다고 가정하지 말고 정확한 실행 경로·프로필과 현재 프로세스를 다시 확인한다.
인계 정리 중 2026-11-04 10:34 KST에는 같은 PID·실행 경로·검수 프로필의 브라우저가 남아 있음을
읽기 전용으로 확인했다. 창 입력·종료는 하지 않았다.

| 기록 | SHA-256 |
| --- | --- |
| `source-review-frozen.json` | `4d1c56c8423e3838c61f189d690633537da4e1183737cbba6d202f130ea6ae67` |
| `build-provenance.json` | `b952451716f5b78392295f7052b446d4139e4bd85c7a6848dd80570d3b9a8777` |
| `native-window-url/summary.json` | `da46513bbc621b1d188170ce713d46358629c28ccfb0af51da1fabdfaec2ac9d` |
| `actual-app-launch.json` | `1b950de4141ef9b2cf099cb4718d63748246d3446b356d1aab76d7b82e29d073` |
| `actual-ui18-results.json` (인계 시 중단 상태 반영) | `401e68967e1ab1201e82d5cc533d2b051294aa5068596dbcf9a13effba5feca5` |
| `computer-use-stop-reconciliation.json` | `a1ff5a7457b3c6b1dd00544e4dabece06cfa6a60de7ac08e8374ebda6ed8b252` |

검증 기준은 `a62a2a9`와 Windows 변경이며, `07002c0`의 추가분은 문서·개발/검사 도구다.
Chromium 핀이나 앱 제품 소스의 추가 변경은 없다. 전달 전의 문서 정리·Git 통합 결과를
과거 소스 지문이나 실행 번호로 소급해 덮어쓰지 않는다.

다음 Windows 세션은 외부 제어 중단 원인의 조사부터 재개하고, 실제 화면을 관측한 뒤
18항목을 개별 실행·기록한다. 사용자는 자동 검사로의 대체를 승인하지 않았다.
Mac에서는 `git pull --ff-only` 뒤 로컬 `current-state.md`를 이 Windows 상태에 맞춘다.
