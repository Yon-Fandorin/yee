# Chromium 버전 이식 구조와 패치 래칫

Yee 소유 코드와 테스트는 overlay에서 관리하고, Chromium 원본의 변경은 `0001`에 둔다.
패치 래칫은 원본 파일에 남는 Yee 연결과 상태의 증가를 경로별로 제한한다.

## 현재 패치 구성

`0001-integrate-yee-shell.patch`에 포함된 변경이다. overlay 소스 전체의 크기는
이 표에 포함하지 않는다.

| 구분 | 파일 | 추가 줄 | 삭제 줄 |
| --- | ---: | ---: | ---: |
| 수정한 제품 파일(GN 연결 포함) | 296 | 12,363 | 1,656 |
| 수정한 Chromium 테스트 파일(GN 연결 포함) | 9 | 507 | 40 |
| 새 파일 | 4 | 792 | 0 |

줄 수는 결합 정도의 근사치이며, 패치 크기 자체는 래칫의 통과 조건이 아니다.

## 구조 원칙

### 조립 지점과 수명

Chromium 157의 `EmbedderBrowserWindowFeatures`를 Yee의 composition root로 쓴다.
`chrome/browser/ui/browser_window/public/embedder_browser_window_features.h`와
`internal/embedder_browser_window_features.cc`는 embedder 확장 지점이다.
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
Yee 소유 `browser/ui/interactive_ui_tests_main.cc`를 컴파일한다.
foreground는 upstream 초기화·전면화 동작을 유지하고, background는 headless를 사용하며
전면화 등록·modifier 정리·always-on-top 창 종료를 제외한다.
`site_controls_browsertests`도 Yee 소유 `browser/ui/browser_tests_main.cc`를 사용한다.
두 main은 macOS background에서만 테스트 전용 target의 NSApplication 활성화 차단을
설치한다. Chromium 원본 main과 Widget의 창 표시 경로는 수정하지 않는다.
[`browser/ui/BUILD.gn`](../browser/ui/BUILD.gn)은 Yee 소유 실행 파일과 테스트 지원
target을 정의한다. 브라우저 테스트 실행 파일은 Chromium 브라우저 의존성도 링크하므로
소스 목록을 분리해도 브라우저 전체의 링크 비용은 남는다.

공통 대기 API는 `WaitForApplied(ticket, conditions)`로 두고 Chromium observer와
결합한다. 새 Yee 시나리오는 upstream 테스트 파일에 추가하지 않는다.
이전한 macOS·공용 테스트 원본은 pristine으로 복원했다.
Yee 셸을 켰을 때 실패하는 원본 테스트는 실행 filter로 제외하고 원본을 수정하지 않는다.

`.sh` gate는 선택한 범위에 필요한 실행 파일을 빌드한다. Yee 테스트는 소유 fixture에서,
원본 guard는 pristine 소스를 그대로 컴파일해 실행한다. 원본 테스트 중 Yee 셸에서
실패하는 것은 filter에서 빼고, 같은 시나리오를 Yee 셸 기준으로 고친 복사본을 선택한다.
예를 들어 원본
`BrowserViewTabbedLayoutImplTopContainerBackgroundUiTest`는 제외하고
`YeeBrowserViewTabbedLayoutImplTopContainerBackgroundUiTest`를 실행한다.

Sidebar에서 Yee 셸이 쓰지 않는 compact rail 너비를 강제하는 원본
`TabGroupHeaderViewTest.FocusModeVisibility`는 필터에서 제외한다.
확장 헤더의 focus mode 검사는 overlay의
`YeeTabGroupHeaderViewTest.FocusModeMarkAndExpandedButtons`로 실행한다.

`0001`은 Chromium 연결을 담는 단일 패치 파일로 유지한다. Yee 소유 코드는
[`build/overlay.json`](../build/overlay.json)의 경로 매핑으로 동기화한다.

콘텐츠 차단 연결을 별도 패치로 물리 분리하는 안은 검토 후 채택하지 않았다.
이동 범위가 41개 경로의 +581/-100줄(`0001`의 약 4%)에 그치고, 같은 Chromium
파일의 업그레이드 충돌과 래칫 총량은 그대로인데 적용·재생성 도구 비용은 커진다.
셸 UI가 콘텐츠 차단에 의존해 해당 패치만 보류할 수도 없다.

대신 [`build/patch-concerns.json`](../build/patch-concerns.json)에 파일별 관심사를
기록한다. 콘텐츠 차단만 따로 보류하거나 rebase해야 할 때 물리 분리를 다시 검토하며,
그때는 UI의 콘텐츠 차단 의존부터 끊는다.

## 테스트 gate 누락 방지

`tests/tooling/test_gate_coverage.py`는 overlay의 GTest 선언과 `0001`의 추가·수정
테스트를 gate의 빌드 타깃·필터에 대조한다. patch의 기본 3줄 문맥을 유지하며
checkout의 테스트 선언 범위와 hunk 변경 줄을 대조한다. checkout이 없으면 upstream
검사만 이유를 출력하고 건너뛴다. overlay 검사는 계속 수행한다.
원본 fixture는 그대로 두며 새 Yee 시나리오는 계속 overlay에 작성한다.

브라우저 수준 favicon·그룹 rename 접근성·Tab Search split 사례는 Yee 소유 소스로
`site_controls_browsertests`에 연결한다. layout의 `browser` 범위는 Yee 소유
Side Panel·selection 사례를 같은 실행 파일에서, 나머지 원본 guard를 `browser_tests`에서
실행한다. Service Worker의 factory wrapping 사례는 `yee_service_worker_unittests`,
Windows Aura의 layer 위치 guard는 `is_win && use_aura`의 `yee_aura_unittests`에 연결한다.
전체 gate와 Windows 소유 예외는 [개발 도구](../tools/dev/README.md#변경-후-테스트)와
[tooling 안내](../tests/tooling/README.md#yee-테스트-gate-누락-검사)를 따른다.

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
| `upstream_test_lines` | Chromium 테스트 파일의 추가 줄 | 507 |
| `shell_branches` | `IsShellEnabled(`가 있는 추가 줄 | 52 |
| `view_id_lookups` | `GetViewByID(`가 있는 추가 줄 | 17 |
| `yee_includes` | overlay 소유 목적지 또는 `new_glue` 헤더의 include 줄 | 222 |
| `yee_members` | 수정한 헤더의 Yee 이름·타입 멤버 선언(여러 줄 선언은 하나로 합침) | 55 |

[`build/patch-ratchet.json`](../build/patch-ratchet.json)은 `schema_version: 1`과
`roles`를 둔다. `roles.shell.baseline`은 `{metric: {path: count}}`,
`roles.shell.exceptions`는 `{metric, path, allowed, reason}` 항목의 목록이다.
`path`는 Chromium 상대 경로이며 기준선에 없는 경로는 0으로 본다.

[`build/patch-concerns.json`](../build/patch-concerns.json)은 `schema_version: 1`과
관심사 설명(`concerns`), 경로별 관심사 목록(`paths`)을 둔다. `shell` role의 모든
패치 경로를 명시하며, 누락·남은 경로, 미정의 관심사와 빈 목록은 설정 오류다.
관심사별 파일 수·추가/삭제 줄·hunk 수·5개 지표 합계를 보고한다.
혼합 경로는 `shell+content_blocking`처럼 별도 행으로 세어 중복 합산하지 않는다.

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
미분류 경로나 래칫 위반이 있으면 저장한 패치를 유지하며 non-zero로 끝난다.
해당 role의 기준선이 없으면 재생성 단계의 지표 검사만 건너뛴다.
`shell` role의 관심사 지도는 필수이며, 지도와 경로 집합은 기준선 유무와 무관하게 검사한다.
독립 검사 명령은 기준선 누락을 설정 오류로 처리한다.

## 구현된 소유 경계

### 1. 래칫과 기준선

패치 재생성은 경로별 기준선과 검토한 예외를 검사한다. 원본에서 제거한 변경은
`--update`로 낮은 값에 잠그며, 증가에는 경로별 검토가 필요하다.

### 2. 테스트 소유권

macOS·공용 범위의 Yee 사례와 셸 적응 guard는 `browser/ui`,
`browser/content_blocking`, `components/branding` 등 소유 코드 곁의 fixture에 있다.
이전 대상 Chromium 테스트 원본은 pristine으로 복원했다. 원본 fixture가 `.cc` 내부에
있는 사례는 공개 기반과 필요한 helper로 재조립했다.

| 검사 범위 | 실행 파일 |
| --- | --- |
| BrowserView·split pane·drag·Omnibox·Mac immersive/glass | `yee_interactive_ui_tests` |
| 시작 설정·favicon·Tab Search·Side Panel·selection·Customize Chrome·UIA | `site_controls_browsertests` |
| native geometry·animation | `yee_layout_unittests` |
| Service Worker factory wrapping | `yee_service_worker_unittests` |
| macOS 설치 정책·임시 DMG | `install_sh_test` |

native geometry unittest는 `new_glue`에서 제거하고
`browser/ui/yee_browser_view_tabbed_layout_native_geometry_unittest.cc`로 옮겼다.
Mac trace와 소유 창의 focus 준비는 Yee test support가 맡는다.

원본 테스트·지원 경로에 남은 추가 줄은 다음과 같다. Windows 경로는 이전을 기다리는
기준선이며, Service Worker fake와 Content GN 연결은 검토한 예외다.

| Chromium 경로 | 추가 줄 | 유지 이유 |
| --- | ---: | --- |
| `chrome/test/base/interactive_test_utils_win.cc` | 211 | Windows 세션에서 이전 |
| `ui/base/test/ui_controls_internal_win.cc` | 6 | Windows 세션에서 이전 |
| `chrome/installer/setup/install_unittest.cc` | 11 | Windows 세션에서 이전 |
| `chrome/installer/setup/taskbar_util_unittest.cc` | 157 | Windows 세션에서 이전 |
| `ui/aura/window_unittest.cc` | 54 | Windows 세션에서 이전 |
| `chrome/test/base/interactive_test_utils.h` | 22 | 남아 있는 Windows 구현의 선언·include |
| `content/renderer/service_worker/service_worker_provider_context_unittest.cc` | 9 | client capability와 IPC 수명 보존 |
| `content/renderer/service_worker/service_worker_subresource_loader_unittest.cc` | 9 | client capability와 IPC 수명 보존 |
| `content/test/BUILD.gn` | 28 | Content 내부 fixture를 허용된 testonly 경계에서 컴파일 |

Windows 전용 경로 439줄, 공용 header의 Windows 선언 22줄, SW fake 18줄과
Content GN 연결 28줄이 `upstream_test_lines`의 507줄을 이룬다.
SW fake의 `WrapSubresourceLoaderFactory`를 별도 IPC로 분리하려면 client capability,
Clone→Wrap→Connector 순서, browser의 sandbox·StorageKey 검사와 disconnect 수명을
함께 재설계해야 한다. Content testonly 연결은 소형 실행 파일 하나만 소비하며
private header의 visibility를 넓히지 않는다.

Customize Chrome 탭 배치는 Yee 소유 `TabLayoutHandler`를 `CustomizeChromeUI`에만
바인딩한다. `DocumentService`가 문서와 양쪽 pipe의 수명을 연결하고 기존 설정 helper가
권한을 검사한다. 원본 Customize Chrome Mojo와 mock은 수정하지 않는다.
이 endpoint 등록에 필요한 `chrome_browser_interface_binders_webui.cc`의 owned Mojom
include 1줄도 경로별 예외다.

### 3. ProjectNavigationController 조립

`ProjectNavigationController`의 생성·소유·정리는 embedder root가 맡는다.
생성은 `BrowserWindowFeatures::Init` 마지막에, BrowserView 연결은
`InitPostBrowserViewConstruction`에서 수행한다.
`browser_window_features.{h,cc}`는 pristine이며 `browser_view.cc`의 별도 attach
코드는 제거했다. 명령·표시 여부를 조회하는 Chromium 호출 지점은 좁은 API를 사용한다.

### 4. 헤더별 계약

| 헤더 | 책임 |
| --- | --- |
| `sidebar_metrics.h` | `SidebarMetrics`·`kSidebarMetrics`·검증식·View ID |
| `shell_policy.h` | Sidebar·Favorites 정책 |
| `yee_colors.h` | SkColor 기반 순수 색상 계약 |
| `yee_ui.h` | Views·테마 helper |

각 소비자는 필요한 계약의 헤더를 직접 include한다. `yee_ui.h`는 다른 계약을
재노출하지 않는다.

### 5. 대기 API

`WaitForApplied(ticket, conditions)`와 Chromium observer를 Yee UI 테스트 전반과
drag 테스트에 적용했다. fixture가 세션·observer 연결을 소유하며, 제품 소유자는
적용값과 구독을 제공한다. 적용 상태를 기다리는 테스트는 입력 전에 ticket을 등록하고
해당 요청의 조건이 충족됐는지 판정한다. 원본 guard와 이전한 사례의
tier·OS·parameter 연결은 유지한다.

관측 read, Kombucha 시퀀스의 평가 시점, 상태·배치 조건과 정상 콜백 대기는
[UI 테스트 작성 규칙](../tools/dev/README.md#ui-테스트-작성과-applied-wait)을 따른다.

## 업그레이드 기록과 남은 결정

각 업그레이드의 [Stable 인수인계](chromium-stable-handoff.md)에 다음을 기록한다.

- rebase 중 충돌한 파일과 hunk 수. 파일은 관심사별로 나누고 혼합·미분류도 따로 기록한다.
- 이식 시작부터 첫 빌드 성공까지의 시간
- 같은 시작점부터 필요한 gate가 모두 통과할 때까지의 시간

원본·대상 revision과 빌드 조건을 함께 남겨 서로 다른 업그레이드 결과를 비교한다.
새 Beta/Stable마다 유지보수 checkout에서 rebase를 예행해 네 milestone을 한 번에
넘기는 일을 피할지는 아직 결정하지 않았다.

각 구조 변경의 기대 효과는 검증 결과가 아니며 다음 업그레이드에서 측정한다.

## 검증과 후속 작업

macOS에서 전체 gate(`--tier=all`)가 통과했다.

Windows 전용 테스트·지원 코드 이전, `.ps1` gate의 실행 파일·필터 전환과 검증은
후속 작업이다. SW IPC 분리, 전체 상태 이전과 View 트리 재설계도 별도 설계 대상으로 남긴다.
