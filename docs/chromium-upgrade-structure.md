# Chromium 버전 이식 구조와 패치 래칫

153→157 Stable 이식에서는 Chromium 테스트 파일의 충돌 해결과 약 3일의
런타임·테스트 안정화에 시간이 많이 들었다. 컴파일 오류 해결이 주된 비용은 아니었다.
다음 이식에서는 원본 테스트 변경을 줄이고, Yee 상태의 수명과 Chromium 연결 경계를
명확히 한다. 패치 래칫과 테스트 이전 1단계를 구현하고 macOS에서 검증했다.
Windows 검증과 나머지 구조 변경은 후속 작업이다.

## 현재 패치 구성

테스트 이전 1단계 뒤 `0001-integrate-yee-shell.patch`의 래칫 측정 결과다.

| 구분 | 파일 | 추가 줄 | 삭제 줄 |
| --- | ---: | ---: | ---: |
| 수정한 제품 파일 | 296 | 12,335 | 1,652 |
| 수정한 Chromium 테스트 파일 | 31 | 5,664 | 134 |
| 새 파일 | 5 | 1,152 | 0 |

줄 수는 결합 정도의 근사치다. 실제 이식 비용은 충돌과 빌드·gate 소요 시간을
함께 기록해 판단한다.

## 구조 원칙

### 조립 지점과 수명

Chromium 157의 `EmbedderBrowserWindowFeatures`를 Yee의 composition root로 쓴다.
`chrome/browser/ui/browser_window/public/embedder_browser_window_features.h`와
`internal/embedder_browser_window_features.cc`는 현재 비어 있는 embedder 확장 지점이다.
이곳은 Yee 객체를 조립하고 수명 단계를 연결하며, 제품 정책과 상태 구현은 각 소유자에 둔다.

| 진입점 | Chromium 호출 지점과 역할 |
| --- | --- |
| `Init` | `BrowserWindowFeatures::Init`의 마지막, BrowserView 생성 전 조립 |
| `InitPostWindowConstruction` | window 객체가 필요한 연결 |
| `InitPostBrowserViewConstruction(BrowserView*)` | BrowserView 생성자에서 호출하는 View 연결 |
| `TearDownPreBrowserWindowDestruction` | teardown의 첫 단계에서 창 의존 상태 정리 |

Profile keyed-service factory 등록은 기존 프로필 초기화 단계에 남긴다.
상태는 다음 수명 소유자에 둔다.

| 수명 | Yee 소유자 |
| --- | --- |
| Profile | 프로필 서비스 |
| WebContents | WebContents controller |
| Pane | pane attachment |
| Window | 창 수준 coordinator 하나 |

Chromium host에는 opaque attachment를 최대 하나만 둔다.
`contents_container_view.h`의 기존 `YeePresentationOutlet`처럼 선언과 소유 포인터만
노출하고, 관련 상태와 연결은 attachment 안에서 관리한다.

### 계약과 분기

값 계약, Pimpl과 `From()`을 사용하는 좁은 concrete API, host adapter로 연결한다.
모든 기능을 하나의 virtual hook 계층으로 통합하지 않는다.
`BrowserViewLayoutImpl::ProposedLayout`은 protected이며 호출 중 stack에만 존재해야
하므로 Yee 공개 계약에 노출하거나 보관하지 않는다. adapter가 필요한 값만 전달한다.

| 분기 종류 | 판단 기준 |
| --- | --- |
| Process/Profile 정책 | `IsShellEnabled()` 등 제품 활성화 정책 |
| Host 적용 자격 | 해당 host의 attachment 존재 여부 |
| 동적 상태 | 해당 수명 소유자의 현재 상태 |

원본 동작을 덮어쓰는 API는 `optional<Override>`를 반환하고, 값이 없으면 native
경로를 따른다. 제품 활성화 여부를 모든 동적 상태 분기에 반복해서 묻지 않는다.

### View 참조와 트리

adapter가 타입 있는 View 참조를 유지하고, 재생성되는 View에는 `views::ViewTracker`를
쓴다. `ElementIdentifier`는 유지하며 `GetViewByID` 조회를 줄인다.

reparenting은 줄이되 native LocationBar 재호스팅은 유지한다.
[셸 명세](browser-shell-spec.md)는 분할뷰에서 같은 `LocationBarView`를 active pane
header로 옮기고, pane 전환·분할 해제 시 같은 View를 새 host나 Toolbar로 되돌리도록 한다.
clip root는 필요한 최소 구조를 유지하고, View 연결의 `CHECK`는 adoption 지점에만 둔다.

## 테스트와 패치 유지 방식

Yee 소유의 선택형 interactive 테스트 실행 파일을 두고
`chrome/test/base/interactive_ui_tests_main.cc`를 재사용한다.
`chrome/test/BUILD.gn`의 `captured_sites_interactive_tests`와
`accessibility_live_site_tests`가 같은 main을 쓰는 선례다. 오버레이에는 이미
[`browser/ui/BUILD.gn`](../browser/ui/BUILD.gn)의 `site_controls_browsertests`처럼
전체 테스트 목록과 독립된 실행 파일이 있다. macOS에서 실행 파일만 다시 만든 순수 링크
시간은 새 실행 파일 121–133초, `interactive_ui_tests` 125–128초로 차이가 없었다.
둘 다 브라우저 전체를 링크하므로 이 실행 파일의 이점은 링크 시간이 아니라
원본 테스트 파일 변경을 없애는 데 있다.

공통 대기 API는 `WaitForApplied(ticket, conditions)`로 두고 Chromium observer와
결합한다. 새 Yee 시나리오는 upstream 테스트 파일에 추가하지 않는다.
이전한 upstream 테스트 파일은 pristine upstream으로 복원한다.
Yee 셸을 켰을 때 실패하는 원본 테스트는 실행 filter로 제외하고 원본을 수정하지 않는다.

`.sh` gate는 Yee 실행 파일 하나만 빌드한다. 이 실행 파일은 이전한 Yee 테스트와 함께
gate가 고르는 원본 테스트 파일을 그대로 컴파일한다. 1단계에서 복원한 두 파일은 pristine
원본이고, drag·omnibox popup·Mac immersive 등 2단계 대상 파일은 아직 `0001`이 수정한
상태로 들어간다. 원본 테스트 중 Yee 셸에서 실패하는 것은 filter에서 빼고, 같은 시나리오를
Yee 셸 기준으로 고친 Yee 복사본을 실행한다. 예를 들어 원본
`BrowserViewTabbedLayoutImplTopContainerBackgroundUiTest`는 제외하고
`YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest`를 실행한다.

`0001`은 단일 패치 파일로 유지한다. 별도 유지보수 checkout에서 관심사별 commit
series로 관리한 뒤 `0001`로 export한다. 콘텐츠 차단 배포 코드의 패치 분리는 첫 범위
이후로 미루며, 그전에는 유지보수 커밋만 관심사별로 나눈다.

## 패치 래칫

[`patch_ratchet.py`](../tools/overlay/lib/patch_ratchet.py)가 패치를 읽고,
[`check-patch-ratchet.py`](../tools/overlay/check-patch-ratchet.py)가 검사·기준선 기록을
제공한다. 저장소 루트에서 실행한다.

```sh
python3 tools/overlay/check-patch-ratchet.py
python3 tools/overlay/check-patch-ratchet.py --update
```

기본 `--role`은 `shell`이다. `--update`는 줄어든 기준선만 저장하며 증가한 값은
올리지 않는다. `--init`은 해당 role의 기준선이 없을 때만 최초 기록에 쓴다.
검사 CLI의 종료 코드는 통과 `0`, 위반 `1`, 설정 오류 `2`다.

지표는 패치가 수정한 Chromium 파일의 추가 줄을 경로별로 센다.
테스트 파일은 파일명·디렉터리명으로 분류한다. 패치가 새로 만드는 파일은 지표에서
제외하고 크기 보고에만 포함한다. 줄 단위 근사이므로 스크립트 출력이 기준선의 기준이다.

| 지표 | 세는 내용 | 현재 합계 |
| --- | --- | ---: |
| `upstream_test_lines` | Chromium 테스트 파일의 추가 줄 | 5,664 |
| `shell_branches` | `IsShellEnabled(`가 있는 추가 줄 | 61 |
| `view_id_lookups` | `GetViewByID(`가 있는 추가 줄 | 30 |
| `yee_includes` | overlay 소유 목적지 또는 `new_glue` 헤더의 include 줄 | 206 |
| `yee_members` | 수정한 헤더의 Yee 이름·타입 멤버 선언(여러 줄 선언은 하나로 합침) | 55 |

[`build/patch-ratchet.json`](../build/patch-ratchet.json)은 `schema_version: 1`과
`roles`를 둔다. `roles.shell.baseline`은 `{metric: {path: count}}`,
`roles.shell.exceptions`는 `{metric, path, allowed, reason}` 항목의 목록이다.
`path`는 Chromium 상대 경로이며 기준선에 없는 경로는 0으로 본다.

전체 지표에 고정 상한을 두지 않고, 경로별 기준선의 증가를 막는다.
증가가 필요하면 검토한 예외에 지표·경로·허용 수·이유를 남긴다.
`allowed`는 증가분이 아닌 해당 지표·경로의 절대 허용 수이며 기준선보다 커야 한다.
다음 경우 검사가 실패한다.

- 현재 값이 경로별 기준선 또는 해당 예외의 허용 수를 초과한다.
- 감소한 값을 `--update`로 기준선에 잠그지 않았다.
- 예외가 불필요하거나 허용량 일부를 쓰지 않는다. `allowed`를 현재 값으로 낮추고,
  현재 값이 기준선 이하이면 예외를 제거한다. `--update`는 예외를 자동 정리하지 않는다.

종류별 파일 수·수정 위치·추가/삭제 줄·hunk 수는 보고용이며 통과 조건이 아니다.
`regenerate-shell-patch.py`는 `0001`을 먼저 저장한 뒤 래칫을 실행한다.
래칫 실패 시 저장한 패치를 유지하며 non-zero로 끝난다. 해당 role의 기준선이 없으면
재생성 단계의 래칫 검사만 건너뛴다. 독립 검사 명령은 기준선 누락을 설정 오류로 처리한다.

## 첫 작업 범위와 순서

### 1. 래칫과 측정 기준선

범위는 위의 도구·기준선과 패치 재생성 연결이다. 이 단계는 구현되어 있다.

- 완료 기준: 현재 패치의 지표·크기를 기록하고 읽기 전용 검사가 통과한다.
  초기 합계는 `14661 / 72 / 71 / 219 / 55`이며 검사 통과를 확인했다.
- 기대 효과: 연결 증가를 검토 대상으로 드러내고, 줄어든 값을 다시 늘리지 못하게 잠근다.

### 2. 테스트 이전

1단계는 독립 fixture가 있거나 작은 helper를 복사할 수 있는 suite부터 옮긴다.
`browser_view_tabbed_layout_impl_interactive_uitest.cc`에는 Yee 테스트 36개와
추가 줄 7,437개가 있고 upstream fixture도 수정한다.
`multi_contents_view_interactive_uitest.cc`에는 Yee 테스트 11개가 있다.
필요한 helper를 Yee 소유 fixture로 복사해 원본 fixture 변경을 없앤다.

1단계의 코드 이전은 완료했다. 추가·수정된 테스트를 모두 포함해
`browser/ui/browser_surface_interactive_uitest.cc`에 41개,
`browser/ui/split_pane_interactive_uitest.cc`에 17개를 두며 fixture 이름은 `Yee`로 시작한다.
두 원본 파일과 `chrome/test/BUILD.gn`은 pristine으로 복원했다.
`.sh` gate는 `//chrome/browser/ui/views/yee:yee_interactive_ui_tests`를 빌드·실행한다.
래칫 합계는 `5664 / 61 / 30 / 206 / 55`로 낮췄다.
2026-11-04 macOS에서 새 실행 파일과 기존 `interactive_ui_tests`가 모두 빌드됐고,
interactive gate는 header 기본 15/15·RTL 125% 4/4, sidebar 60/60,
레이아웃 기본 70/72·RTL 125% 43/43·통합 탭 스트립 4/4가 통과했다. 레이아웃 기본 단계의
실패로 gate 스크립트가 멈춰, RTL과 통합 단계는 같은 filter·인자로 따로 실행했다.
레이아웃의 실패 2건은 옮기지 않은 `ImmersiveModeControllerMacInteractiveTest`의
전체화면 Space 전환 대기 시간 초과다. 같은 조건에서 기존 `interactive_ui_tests`도 똑같이
실패해 이번 이전과는 무관하며, 2026-11-02에는 통과했던 원인은 확인하지 못했다.
Windows `.ps1` 전환과 검증은 후속 작업이다.

2단계는 Yee 테스트 26개가 있는 `tab_drag_interactive_uitest.cc`와
`tab_drag_controller_interactive_uitest.cc`의 drag suite다.
`VerticalTabDragTest`와 `DetachToBrowserTabDragControllerTest`는 `.cc` 안에 정의되어
다른 실행 파일에서 상속할 수 없다. 이 중 헤더에 선언된 기반 fixture는
`TabDragControllerTest`뿐이다. 대기 API로 다시 작성할 때까지 drag 변경은 현재
기준선에 남긴다. 1단계 이전은 5번의 대기 API 작업을 기다리지 않는다.

- 완료 기준: 이전한 upstream 파일을 원본으로 복원하고, 감소한 `upstream_test_lines`를
  `--update`로 잠근다. `chrome/test/BUILD.gn`의 약 40개 deps 추가 hunk가 사라진다.
  선택형 실행 파일과 `interactive_ui_tests`의 링크 시간을 같은 조건에서 측정한다.
  gate의 원본 guard 테스트를 보존하고 이전한 Yee 시나리오를 새 실행 파일에서 실행한다.
- 기대 효과: 테스트 충돌 범위를 줄인다. 1단계에서 Chromium 테스트 파일의 추가 줄이
  14,661에서 5,664로 줄었다. 측정 결과 링크 시간은 줄지 않았다.

### 3. ProjectNavigationController 생성 이동

생성·소유·정리를 embedder root로 옮기고 BrowserView 연결은
`InitPostBrowserViewConstruction`에서 수행한다. 제거 대상은
`browser_window_features.{h,cc}`의 해당 hunk와 `browser_view.cc` 약 1185행의 attach
hunk뿐이다. 157 원본 기준으로 이동 뒤에도 Chromium 파일 11개에
`ProjectNavigationController::` 참조 21개(`From()`, `IsHubShowing*()`,
`ShouldShowNativeTab()`, `kSidebarViewId`)가 남는다.
이 이동은 조립 패턴을 먼저 적용하는 단계이며 충돌 감소 범위는 작다.

초기화 순서에 따른 동작 차이를 검증해야 한다.

- 생성이 `BrowserWindowFeatures::Init` 시작부(현재 약 261행)에서 마지막 embedder
  `Init`(약 681행)으로 이동한다. 생성자가 `TabStripModel` observer를 등록하므로
  observer 등록 순서가 바뀐다.
- 다른 기능이 초기화되는 동안에는 `browser_command_controller.cc` 약 657행의
  `IsCommandEnabled` override가 존재하지 않고 native 경로를 따른다.
- reset은 embedder의 `TearDownPreBrowserWindowDestruction`에 유지한다.

- 완료 기준: interactive gate 통과와 실제 앱의 프로젝트 hub·command·session restore
  확인. observer 순서 변경의 영향을 이 검증에서 확인한다.
- 기대 효과: Chromium 공통 feature 소유자에서 Yee 조립을 분리한다. 남은 호출 지점은
  이 단계에서 정리하지 않는다.

### 4. yee_ui.h 분리

Chromium 파일 47개가 include하는 `yee_ui.h`를 metrics·policy·순수 color 계약으로
나눈다. `yee::kSidebarMetrics`는 계속 하나의 시각 계약으로 유지한다.

- 완료 기준: 각 호출자가 필요한 헤더만 참조하고, metrics 중복 없이 기존 빌드·gate가
  통과한다.
- 기대 효과: 헤더 변경 시 재빌드 전파를 줄인다. 충돌 감소를 주된 효과로 보지 않는다.

### 5. 대기 API 기반

`WaitForApplied(ticket, conditions)`와 Chromium observer의 연결 기반만 마련한다.
drag suite 전체 재작성은 이 단계의 완료 조건에 넣지 않는다.

- 완료 기준: 대표 적용 대기에서 요청 ticket과 conditions 충족을 observer와 함께
  판정하며 기존 gate 동작을 유지한다.
- 기대 효과: 후속 테스트 이전과 런타임 안정화에서 같은 적용 완료 조건을 재사용한다.

전체 상태 이전, View 트리 변경, 콘텐츠 차단 배포 코드의 패치 분리는 첫 범위에서 제외한다.

## 업그레이드 기록과 남은 결정

각 업그레이드의 [Stable 인수인계](chromium-stable-handoff.md)에 다음을 기록한다.

- rebase 중 충돌한 파일과 hunk 수
- 이식 시작부터 첫 빌드 성공까지의 시간
- 같은 시작점부터 필요한 gate가 모두 통과할 때까지의 시간

원본·대상 revision과 빌드 조건을 함께 남겨 서로 다른 업그레이드 결과를 비교한다.
새 Beta/Stable마다 유지보수 checkout에서 rebase를 예행해 네 milestone을 한 번에
넘기는 일을 피할지는 아직 결정하지 않았다.

구조 변경 단계의 빌드·링크·시간 검증은 수행하지 않았다. observer 순서 변경의 실제
영향도 미확인이다. 위 기대 효과는 검증 결과가 아니며 후속 구현에서 측정한다.
