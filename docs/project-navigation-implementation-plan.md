# 프로젝트 탐색 상세 구현 계획

2026-10-27 · 기준 브랜치 `codex/project-navigation-handoff`, 커밋 `2874d6c`

승인 후 첫 native 구현은 [`browser/projects/`](../browser/projects/README.md)와
[`browser/ui/projects/`](../browser/ui/projects/)에 반영했다. 1~7절은 최초 계획
수립 당시의 조사·제안·검증 기록이며, 구현 연결 지점은 8절에 정리한다.

[UX 인계](project-navigation-handoff.md)의 다음 산출물인 상세 계획이다.
아래 신규 책임·저장 정책·파일 배치는 구현 제안이며, 제품 명세를 변경하거나
프로젝트 기능을 구현한 결과가 아니다. 첫 구현은 프로젝트 생성 → 실제 탭 연결 →
허브 왕복 → 정상 종료 후 복원으로 제한한다.

## 1. 확인한 기반

- 브랜치의 제품 코드 기반은 `95f3e45`다. 기존 시안 인계 커밋은 `bdbdb1f`로
  리베이스됐고, `2874d6c`는 통합 후 인계 내용과 검증 범위를 갱신했다.
- 시안 원본·독립 HTML·캡처 4개의 해시를 manifest와 대조했고 캡처 4개를 직접 확인했다.
  이 자료는 UX 의도이며 실제 브라우저 수명·저장의 증거로 사용하지 않는다.
- 현재 Mac의 `pilot` 작업은 원래 체크아웃에 보존했다. 이 계획은 별도 작업 트리의
  커밋된 제품 코드를 조사했으며, Mac 패키징 변경을 프로젝트 기능에 합치지 않았다.
- 앞서 별도 작업이었던 UI 13개 파일은 `95f3e45`에 반영돼 이 브랜치의 기반에 포함된다.
  `omnibox_popup_style.cc/.h`, 주소줄 hover와 제안 목록, 셸 조작·창 버튼, 관련 검사·명세를
  현재 소스로 읽는다. 그 변경을 별도로 전달받는 선행 조건은 없다.
- `baseline-working-tree.json`은 최초 분기 당시의 역사적 해시 기록이다. 통합 과정의 보완과
  줄바꿈 차이가 있을 수 있으므로 현재 파일을 그 바이트로 되돌리거나 미통합으로 판정하지 않는다.
- 통합 커밋의 Windows 검사 기록은 원격에서 수행한 결과다. 인계에 명시된 최종 실제 앱 검수,
  WebUI 대비 행렬 실행, macOS·Linux 빌드·검수는 완료된 것으로 합산하지 않는다.
  이후 native 구현은 이 통합 소스 위에서 진행하고 플랫폼별 검증을 별도로 기록한다.

확인에 사용한 제품 진입점은 다음과 같다.

| 현재 코드 | 확인한 사실과 구현에서 쓸 경계 |
| --- | --- |
| [Sidebar 소유권](sidebar/ownership.md), [슬롯](../browser/ui/sidebar_sections.h) | 탭·그룹은 native 모델이 소유한다. Chat/Agent History와 Pins는 예약 상태다. |
| [Surface 배치](../browser/ui/browser_surface_layout.h), [수평 표현](../browser/ui/horizontal_tab_presentation.h) | 카드/연결형 탭 모드와 기존 본문·Side Panel 배치 계약을 재사용한다. |
| [Footer](../browser/ui/sidebar_footer.h) | Workspace·Account 표시를 프로젝트 식별자나 저장소로 사용하지 않는다. |
| [프로필 서비스 사례](../browser/content_blocking/content_blocking_service_factory.cc) | ProfileKeyedServiceFactory, Pref 등록, OTR 분리를 따를 기존 패턴이 있다. |
| [Agent 상태](../browser/ui/agent_tab_activity.h), [실행 구조](agent-browser-architecture.md) | 활동 표시는 작업 이력 저장소가 아니며 실제 대화 provider를 제공하지 않는다. |
| [WorkerOwned](../components/tasks/worker_owned.h) | sequence 소유 어댑터다. 프로젝트나 Agent Task 모델의 근거가 아니다. |

적용된 Chromium checkout에서도 `BrowserView`가 `MultiContentsView`를 소유하고,
`ContentsContainerView`가 실제 `ContentsWebView`를 호스팅하는 경로를 확인했다.
세션에는 `SessionService::AddTabExtraData`, `SessionTab::extra_data`,
`chrome::AddRestoredTab`가 있다. 쓰기 API 하나를 호출하는 것만으로 전체 복원이
완성되지는 않는다. 아래 4절의 세션 재구축·닫은 탭 복원 경로까지 연결해야 한다.

## 2. 첫 구현의 동작 계약

1. 일반 브라우저 창의 Create 메뉴에서 프로젝트를 만들고 이름을 입력한다.
   기존 일반 탭·그룹·Favorites와 탭 방향은 유지한다.
2. 프로젝트의 ‘현재 탭 연결’로 해당 창의 실제 HTTP(S) 탭 하나를 연결한다.
   일반 새 탭 생성은 계속 프로젝트 밖에서 동작한다.
3. 프로젝트 이름은 허브를 열고, 옆 화살표는 목록만 열고 닫는다.
   첫 구현의 목록은 실제로 연결한 페이지를 표시한다.
4. 허브에서 페이지를 고르거나 복귀를 누르면 원래 native 탭을 선택한다.
   탭을 탐색·재생성하지 않으므로 허브 왕복 자체가 폼 내용을 버리지 않는다.
5. 정상 종료 후 Chromium이 복원한 탭을 안정적인 항목 ID로 다시 연결한다.
   브라우저의 시작 설정을 강제로 ‘이전 세션 복원’으로 바꾸지 않는다.
   이전 세션을 복원하지 않은 경우 프로젝트 기록은 남고 페이지는 보관 항목으로 보인다.

대화 입력·AI 실행·작업 이력·닫은 웹페이지의 본문 보관은 첫 구현에 넣지 않는다.
현재 provider가 없는 항목을 가짜 실행 결과나 동작하지 않는 입력란으로 채우지 않는다.
인계의 혼합 목록과 허브 입력 방향은 후속 provider 연결 단계에서 같은 데이터 책임 위에
구현한다. 예약 Chat/Agent History 슬롯은 활성화하지 않는다.

첫 구현에서는 프로젝트와 tab group을 동일 개념으로 만들지 않는다. 페이지를 연결해도
그 탭의 기존 그룹·색·순서가 바뀌지 않는다. 일반 그룹의 프로젝트 전환, 그룹 단위 drag,
프로젝트 헤더로 기존 native 그룹을 대체하는 작업은 탐색 목록 단계로 분리한다.

## 3. 책임·배치와 의존성 제안

프로젝트는 영속 데이터와 여러 창의 소비자를 가지므로 `browser/projects/`를 새 소유자로
제안한다. 저장 규칙을 View나 공통 UI 치수 파일에 넣지 않는다.
`sidebar/ownership.md`의 오래된 `src/` 표기 대신, 현재 `AGENTS.md`와 overlay가
정한 `browser/ui/`를 제품 UI 경로로 따른다.

| 제안 파일/타깃 | 책임과 소비자 |
| --- | --- |
| `browser/projects/project_service.h/.cc` | 프로필별 프로젝트·페이지 레코드와 순서, 검증, Pref 변경·관찰. native 연결부와 UI가 소비한다. |
| `browser/projects/project_service_factory.h/.cc` | 일반 프로필의 서비스 생성·Pref 등록. 시스템 프로필 제외, OTR 정책 적용. |
| `browser/projects/project_tab_helper.h/.cc` | 현재 WebContents의 항목 UUID 전달과 유효한 navigation 메타데이터 관찰. 포인터·native 세션 ID를 저장 키로 쓰지 않는다. |
| `browser/projects/project_tab_coordinator.h/.cc` | 프로필의 TabInterface 수명 관찰, UUID→현재 탭 연결, contents 교체·닫기·이동·복원·재열기 중복 조정. 기존 탭 명령을 호출한다. |
| `browser/ui/projects/project_navigation_controller.h/.cc` | 창 단위 페이지/허브 상태와 복귀 대상, 키보드 명령·초점 전환. 방향별 View가 함께 소비한다. |
| `browser/ui/projects/project_hub_view.h/.cc` | native Views 허브와 페이지 목록. 서비스 관찰 결과를 그리고 controller에 사용자 의도를 전달한다. |
| `browser/ui/projects/project_navigation_view.h/.cc` | 세로 이름/목록 조작과 수평 이름/팝업의 공통 모델 소비. 탭 수명과 저장을 소유하지 않는다. |

첫 데이터 계약은 service 헤더에 둔다. 독립적으로 쓰이지 않는 DTO별 파일이나
별도 repository 인터페이스를 미리 늘리지 않는다. 검사는 각 소유자 옆에 둔다.

의존 방향은 `프로젝트 UI → 창 controller/탭 coordinator → 프로젝트 service`다.
service는 Views·DOM·Agent bridge·TabStripModel에 의존하지 않는다.
tab helper/coordinator는 실제 Chromium 탭 API를 사용하지만 UI 컴포넌트를 알지 않는다.
Chromium 원본에는 생성·명령·세션 직렬화·호스트 배치의 작은 연결만 둔다.

구현 시 필요한 wiring:

- `build/overlay.json`: `browser/projects → chrome/browser/yee_projects` source root 등록.
  `browser/ui/projects`는 기존 UI source root에 포함되므로 별도 중복 매핑을 만들지 않는다.
- 새 `browser/projects/BUILD.gn`의 service 및 tab 연결 타깃을 분리하고,
  기존 `browser/ui/BUILD.gn`에 UI 타깃을 연결한다.
- 프로필 factory는 기존 콘텐츠 차단 서비스와 같이
  `chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc`의 factory 구성에 연결한다.
  helper 생성은 `TabHelpers::AttachTabHelpers` 경로에서 일반 프로필 탭에 한정한다.
- 창 controller는 기존 `BrowserWindowFeatures::Init`의 창 수명에 맞춰 소유하고,
  `InitPostBrowserViewConstruction`에서 실제 host를 연결하는 안이다.
  `BrowserWindowInterface::GetUnownedUserDataHost`로 두 방향의 View가 같은 상태를 찾게 한다.
  단순히 파일을 GN sources에 넣은 것으로 초기화가 완료됐다고 판단하지 않는다.
- Chromium 연결은 기존 `patches/0001-integrate-yee-shell.patch`에 반영한다.
  새 Chromium glue 파일을 만들게 되면 overlay의 `new_glue`에도 등록한다.

이 트리와 신규 타깃은 제안이다. 이 문서 작성으로 제품 소스 디렉터리를 생성하거나
배치 승인을 대체하지 않는다.

## 4. 데이터와 세션 복원

### 프로필 데이터

첫 버전은 하나의 비동기 저장 경로를 사용한다. `PrefService`의
`yee.projects.state` dictionary에 `schema_version=1`, 프로젝트·페이지 UUID 레코드,
각 프로젝트의 명시적 항목 순서를 저장하는 안이다. 신규 JSON 파일과 Pref에 같은
데이터를 이중 기록하지 않는다. Pref의 실제 디스크 쓰기는 Chromium 소유다.

- 프로젝트: UUID, 이름, 설명, 정렬된 page item UUID 목록.
- 페이지 항목: UUID, 소속 project UUID, 마지막 유효한 committed URL·제목.
- runtime 연결: page UUID → 현재 native TabInterface. 프로필 안 여러 창을 조회하고
  `RegisterWillDetach`로 파괴·다른 창 이동을 구분해 참조와 복귀 상태를 갱신한다.
  WebContents는 호출할 때 `GetContents()`로 구하며 장기 캐시하지 않는다.
  이 runtime 참조나 tab index, SessionID, tab group ID는 영속 식별자가 아니다.
- 같은 URL의 서로 다른 탭은 서로 다른 항목이다. URL로 전역 중복 제거하지 않는다.
- 열린/닫힌 상태는 복원 완료 후 실제 연결에서 계산한다. 앱 종료로 탭이 사라졌다는 이유로
  모든 프로젝트 레코드를 닫힘 처리하거나 삭제하지 않는다.
- OTR/Guest에서는 첫 버전의 프로젝트 기능을 제공하지 않고 일반 프로필 자료도 노출하지 않는다.
  기존 창의 일반 탐색은 유지한다.

서비스는 단일 Pref 변경 안에서 레코드와 순서를 검증·갱신한다. 새 스키마 버전이나
손상된 데이터는 기본값으로 덮어쓰지 않고 변경을 막고 복구 가능한 원본을 유지한다.
없는 UUID 참조는 진단 대상이며, 그것을 근거로 실제 탭을 닫지 않는다.

### native 세션에 항목 ID 전달

사용할 세션 key는 예를 들어 `yee.project_page_id`다. 프로젝트 자체의 소유자는 service이며
세션에는 탭을 재연결할 항목 UUID만 넣는다.

| 연결 지점 | 필요한 처리 |
| --- | --- |
| live 연결/해제 | helper 갱신과 `SessionService::AddTabExtraData` 기록. 해제는 빈 값으로 명시하고 읽을 때 미연결로 해석한다. |
| `SessionService::BuildCommandsForTab` | 전체 세션 command 재구축 때도 helper의 항목 UUID를 다시 내보낸다. 현재 구현은 임의 extra_data 전체를 자동 재구축하지 않는다. |
| `chrome/browser/ui/browser_tabrestore.cc`의 복원 WebContents 생성 | `extra_data`의 유효한 UUID를 제품 helper에 전달한다. 허용된 프로필·기존 레코드만 연결한다. |
| `BrowserLiveTabContext::GetExtraDataForTab` | 닫은 탭 복원에도 같은 UUID를 포함한다. 현재 제품용 수집 hook이 없으므로 명시적으로 추가한다. |
| 복원 완료와 탭 이동/파괴 | coordinator가 실제 native 탭에 연결하고 창 간 이동을 갱신한다. native 그룹 ID가 복원 중 재생성될 수 있음을 유지한다. |

메모리 절약을 위한 discard는 탭 닫기가 아니다. `TabInterface`는 유지되지만
`DiscardWebContentsAt`이 WebContents를 교체하므로, coordinator가
`RegisterWillDiscardContents(old, new)`에서 UUID를 새 helper로 전달하고 관찰 대상을
바꾼다. 이전 contents의 파괴로 항목을 보관 상태로 바꾸거나 연결을 지우지 않는다.
세션 재구축도 현재 contents에서 같은 UUID를 내보내야 한다. discard 직후 reload 전과
reload 후 모두 연결·저장·재시작 복원을 검사한다.

Pref와 Chromium 세션 파일 사이에 하나의 디스크 트랜잭션이 있다고 가정하지 않는다.
정상 종료 복원이 첫 완료 조건이다. 비정상 종료 시 양쪽 저장 시점이 달라질 수 있으므로
알 수 없는 UUID는 미소속 탭으로 유지하고, 레코드만 남은 항목은 보관 자료로 남긴다.
불일치를 URL 매칭이나 자동 탭 생성으로 덮지 않는다.

### 닫기·재열기·중복·삭제

- 같은 탭을 같은 프로젝트에 다시 연결하면 기존 항목 UUID를 반환한다. 새 레코드나 순서
  변경을 만들지 않는다. 한 실제 탭의 연결은 첫 버전에서 한 프로젝트로 제한한다.
- 다른 프로젝트에 이미 연결된 탭은 즉시 덮어쓰지 않는다. 기존/새 프로젝트 이름을
  보여 주고 사용자가 이동을 선택했을 때 기존 UUID를 유지한 채 부모·양쪽 항목 순서를
  한 service Pref 변경으로 갱신한다. 취소하면 어느 쪽도 변경하지 않는다.
  확인 중 탭·기존 소속이 달라지면 stale 요청을 거부한다. 그룹·URL·실제 탭 수명은 유지한다.
- 사용자가 native 닫기를 완료한 뒤에도 페이지 레코드는 남긴다. beforeunload 취소 중에는
  열린 항목 상태를 유지한다. ‘보관’은 제목·URL 참조 보존이며 페이지 본문 저장이 아니다.
- 항목 재열기는 복원 중이면 완료를 기다리고 UUID별 pending 요청을 합친다.
  이미 연결된 탭이 있으면 그 탭과 창을 활성화한다. 없으면 native 탐색 명령으로 만든
  정확한 새 WebContents에 UUID를 붙인다. 같은 URL의 다른 탭을 찾아 붙이지 않는다.
- native ‘탭 복제’는 별도 탭이다. 최초 구현에서는 복제본을 프로젝트 미소속으로 두고
  명시적으로 연결할 때 새 항목 UUID를 발급하는 안이다. 현재 `DuplicateTabAt`은
  `WebContents::Clone` 후 탭을 넣는다. `WebContentsObserver::DidCloneToNewWebContents`와
  helper 초기화를 함께 확인해 세션 복원·닫은 탭 복원에서 받은 UUID를 복제하지 않는다.
- 복원 자료에 같은 UUID를 주장하는 탭이 둘 이상 있으면 하나의 연결만 채택하고
  나머지 실제 탭은 미소속으로 보존한다. 탭을 자동 병합·삭제하지 않는다.
- 프로젝트에서 항목 제거는 참조와 연결 해제다. 실제 탭을 닫거나 방문 기록을 지우지 않는다.
  프로젝트 삭제도 같은 정책을 사용하며 삭제 확인에서 범위를 명확히 표시한다.
- 수동 목록 순서가 기본이며 최근 활동은 보조 정보다. native 탭 drag가 프로젝트 목록의
  순서를 암묵적으로 다시 쓰지 않는다. 후속 혼합 목록에서도 한 종류별 섹션을 강제하지 않는다.

## 5. 허브의 native 호스팅 제안

첫 구현은 추가 웹탭이나 실제 탭의 navigate 없이 native Views 화면을 쓰는 안이다.
허브는 BrowserView가 소유하는 제품 host로 두고 기존 MultiContentsView와 같은 본문
배치 영역에서 전환한다. 기존 ContentsWebView의 WebContents를 허브 내용으로 교체하거나
TabStripModel에서 탭을 빼지 않는다.

현재 `browser_view_tabbed_layout_impl.cc`는 MCV가 BrowserView의 보이는 자식임을
`CHECK`하고 무조건 배치한다. MCV에 단순히 `SetVisible(false)`를 호출하는 구현은
이 계약에 맞지 않는다. 실험에서 Page/Hub 상태를 native 배치 입력에 연결해 이 검사와
본문 배분을 함께 조정하는 안을 검증한다. popup·app·DevTools에는 기존 계약을 유지한다.
MCV 컨테이너를 유지하는 대안을 쓰더라도 실제 웹 자식의 표시·입력·AX 차단을 확인한다.

창 controller는 `Page` 또는 `Hub(project_id)`, runtime 복귀 TabInterface 참조를 관리한다.
허브를 표시할 때도 native 탭 선택은 남아 있다. 세로·수평 전환은 이 상태를 소비하며
별도 프로젝트 저장소를 만들지 않는다.

- 허브와 MCV는 같은 기존 본문 allocation을 사용한다. Side Panel 폭·Header·InfoBar·
  immersive layout 치수를 복제하지 않고 `BrowserSurface`의 최종 배치에서 받는다.
- 숨긴 웹페이지가 클릭·AX 탐색·키보드 입력을 받지 않도록 native visibility와
  accessibility 노출을 함께 전환한다. DOM 입력값과 native 탐색 이력은 유지한다.
- split 상태에서는 두 pane과 활성 pane·분할 비율을 보존하고 왕복 시 같은 상태를 복구한다.
  허브가 split 탭 각각으로 복제되지 않게 한다.
- 주소 영역은 허브에서 프로젝트 이름·복귀 조작을 표시한다. 실제 사이트의 URL·자물쇠·
  사이트 권한 버튼을 허브의 신원처럼 보이지 않게 한다. 허브 표시는 임의 URL이 아니다.
- 허브 복귀 조작은 웹페이지의 history back과 구분한다. Ctrl/Cmd+L은 허브에서 나와
  실제 탭의 주소 입력으로 이동하는 안이다. 웹탭 선택도 허브를 닫고 해당 native 탭을 활성화한다.
- 탭 modal·인증·beforeunload 같은 사용자 조작을 가리는 상태에서는 허브 진입을 막거나
  해당 탭으로 돌아간다. native 권한·보안 조작을 프로젝트 View가 대신 승인하지 않는다.
- 첫 구현은 현재 Side Panel을 프로젝트 대화로 바꾸지 않는다. 후속 대화는 기존 window
  SidePanelRegistry/SidePanelEntry 경로를 검토하되 선택한 대화와 명시적 참고 자료를
  소유하고, 활성 웹탭 변경만으로 참고 자료를 교체하지 않는다.
- 첫 버전의 허브 표시·복귀 탭은 창의 runtime 상태다. 재시작은 Chromium이 선택한 실제
  페이지에서 시작하고, 프로젝트 이름으로 복원된 기록의 허브에 다시 들어간다.
  ‘허브를 보고 종료한 창의 화면까지 복원’은 별도 window extra_data 계약 후 추가한다.

이 호스팅은 아직 검증되지 않았다. 데이터·탐색 연결 전에 작은 native 실험으로 visibility,
초점, 폼 유지, split, modal과 Side Panel 공존을 확인한다. 통과하지 않으면 탭을 임의로
탐색시키는 대안으로 바꾸지 않고 실패 원인을 계획에 반영한다.

## 6. 단계와 검증

| 변경 단위 | 완료 조건과 검사 |
| --- | --- |
| 기반 확인 | `95f3e45` 위의 통합된 브랜치를 사용한다. 기존 Mac 작업을 보존하며, 이후 빌드에 적용할 때 UI 검사·패치 및 플랫폼별 검증 범위를 확인한다. |
| 서비스·Pref | 생성/이름/삭제·순서, 손상/새 스키마, 프로필 분리·OTR 제외를 서비스 단위 검사로 확인한다. 구현 시 새 배치와 타깃을 확정한다. |
| 탭·세션 연결 | 동일 탭 반복 연결·다른 프로젝트로 명시적 이동/취소, command reset, 정상 재시작, 닫은 탭 복원, 복제, 같은 URL 두 탭, UUID 충돌, 창 간 이동, discard→reload→재시작을 실제 native 탭으로 검사한다. |
| 허브 host 실험 | 폼 입력 후 허브 왕복, 주소 신원·Tab/Shift+Tab·Cmd/Ctrl+L, split/Side Panel/modal, 기본·최소 폭·전체화면을 확인한다. |
| 첫 사용자 흐름 | 실제 Create→탭 연결→프로젝트 이름/화살표→허브 왕복→재시작 후 기록·재연결을 양쪽 탭 방향에서 검증한다. |
| 후속 혼합 탐색 | 일반 그룹·미소속 탭을 유지하며 프로젝트 목록 표현, 그룹 전환과 drag 정책을 구체화한다. 기본 탭 방향은 바꾸지 않는다. |
| 대화·작업 | 실제 provider·저장·권한 계약을 먼저 정하고 혼합 항목과 패널을 연결한다. 탭 활동·Workspace·예약 슬롯을 실행 모델로 전용하지 않는다. |

데이터와 controller 검사는 해당 소스 옆에 두고, 실제 세션 직렬화·복원은 browser test로
검증한다. Header/Sidebar/Surface를 수정한 checkpoint에는 기존 관련 gate를 사용한다.
순수 기하는 `test-browser-surface-layout.sh fast`, 실제 적용 배치는 `interactive`,
Side Panel coordinator 자체 변경은 필요할 때 `browser`를 추가한다.

실제 UI 완료 전에는 별도 문맥 검토와 해당 앱 빌드를 수행한다. 검수 실행 파일이나
검수 프로필을 사용하는 프로세스만 정상 종료하고 새 프로세스의 격리 프로필로 검증한다.
실행 파일·프로필이 분리된 설치 릴리즈는 계속 사용한다. 기본·hover·선택·누름·비활성·키보드 focus를 실제 Yee에서
확인하며, 공통 native 색상과 `yee::kSidebarMetrics`를 재사용한다.

## 7. 이번 산출물의 검증 범위

이번 작업은 기반 조사와 계획 문서 작성이다. 고정 자료의 해시와 캡처, 위 제품 코드와
Chromium 세션·호스트 진입점을 확인했다. 문서 링크와 변경 범위를 검사하고 독립 검토한다.
프로젝트 native 코드, 영속 저장·재시작, 새 UI 빌드·실행을 완료했다고 주장하지 않는다.
기존 명세와 고정 시안은 변경하지 않는다.

## 8. 승인 후 첫 native 구현

- `ProjectService`가 일반 프로필의 프로젝트·페이지 UUID와 Pref를 소유한다.
  `ProjectTabCoordinator`와 `ProjectTabHelper`가 실제 native 탭의 수명·내용 교체·
  세션 UUID를 연결한다. helper 관찰은 `TabFeatures::Init`에서 시작한다.
- `BrowserWindowFeatures`가 창 controller를 소유한다. 세로 Sidebar와 수평 Header는
  같은 서비스·controller를 소비한다. 세로 프로젝트 행 전체는 일반 그룹처럼 폴딩하고
  오른쪽 허브 버튼은 기본 화면을 연다. 수평 이름은 허브, 목록 버튼은 선택기를 연다.
- 허브 본문과 Header는 native 배치 결과와 표면색을 공유하는 Views 화면이다.
  둥근 모서리를 합성할 수 있도록 레이어의 opaque 최적화를 끈다.
  MCV 컨테이너를 유지하며 실제 WebView의 표시와 루트의 입력·AX 노출을 전환한다.
  복귀 시 자식 WebView의 원래 접근성 상태와 실제 페이지를 복원한다.
- 생성·이름/설명 편집·현재 탭 추가·다른 프로젝트로 명시적 이동·참조 제거·프로젝트 삭제를
  제공한다. 새 탭·그룹·Favorites의 소유권과 일반 브라우저 시작 설정은 유지한다.
- session rebuild·정상 세션 복원·닫은 탭 복원에 항목 UUID를 전달한다.
  목록에서 열린 페이지를 고르면 현재 창에 있는 native 탭만 재사용한다.
  다른 창에 열려 있어도 현재 창에 별도 탭을 열고 재열기 예약·복원 중복 방지도 창별로 한다.
  자료는 프로필에서 공유하지만 live 제목·주소·닫기·메뉴는 현재 창의 탭을 따른다.

검사 진입점은 [프로젝트 소유자 문서](../browser/projects/README.md)에 있다.
배치 검사는 기존 Browser Surface gate를 사용한다. 실제 앱 검수와 각 실행 결과는
정적 시안 검증과 구분해 작업 결과로 기록한다.

### 프로젝트 탐색 표시 보완

- 세로 Sidebar는 전역 Favorites → 프로젝트 → 미소속 native 탭 순서로 표시한다.
  세로 Sidebar가 펼쳐져 프로젝트 목록을 제공하는 동안 연결된 일반 탭의 중복 행을 생략한다.
  탭·그룹·분할의 실제 모델과 순서는 유지하며, 참조를 해제하면 일반 native 행으로 돌아온다.
  Sidebar를 접거나 수평 탭으로 바꾸면 native 탭 표시를 유지한다. 전역 Favorites 정책은 유지한다.
- 프로젝트 페이지의 파비콘은 열린 WebContents의 favicon driver를 관찰하고,
  닫힌 참조는 프로필의 기존 로컬 favicon 저장소에서 읽는다. 별도 이미지 저장소나
  임의 원격 favicon 조회를 추가하지 않는다.
- 프로젝트 행의 닫기·가운데 클릭과 우클릭은 실제 native 탭의 닫기 delegate와
  컨텍스트 메뉴에 연결한다. 닫은 탭 복원과 native 확인 절차를 그대로 사용한다.
- 허브의 프로젝트 편집·삭제는 더보기 메뉴, 참조 제거는 명시적인 tooltip이 있는
  작은 아이콘으로 제공한다. 프로젝트 참조의 drag/reorder 정책은 아직 추가하지 않았다.


## 상태별 표현과 폴딩 조작 보완

프로젝트 행 전체는 폴딩하고 오른쪽 허브·더보기 버튼은 별도 조작으로 분리한다. 포인터 진입은 강제 초기 focus ring을 남기지 않으며 키보드 진입은 허브의 초기 초점을 유지한다. 폴딩은 허브 진입으로 전파되지 않고 허브 왕복도 목록 접힘 상태를 바꾸지 않는다. hover/focus에서 앞 아이콘을 같은 위치의 화살표로 바꾸며 제목·보조 조작의 칸을 유지한다.

행 상태 면은 이름·파비콘·보조 조작을 포함하는 부모가 소유한다. 실제 enter/exit 통지를 저장해 exit callback 시점의 이전 hover 값이 잔상으로 남지 않게 한다. native Sidebar 상태색과 font/inset을 사용하고, 닫기·제거는 hover/active/행 내부 focus에서 표시한다. 행 본문의 macOS pressed 배경과 중첩 InkDrop은 제거하고 보조 버튼은 native 상태·포커스 경로를 유지한다. 창의 실제 paint-active 통지를 따른다. 닫기는 원본 TabCloseButton을 재사용한다. metadata 갱신 후 숨겨진 보조 조작의 논리 초점도 복원하고 custom focus ring을 갱신한다. 본문은 행 ring을, 폴딩·닫기는 각 버튼 ring을 써 키보드 조작 위치를 구분한다. native 그룹 집중 모드처럼 접기가 금지된 상태에서는 폴딩 화살표도 표시하지 않는다.

허브는 native MCV 곡률·공통 Header palette·outline 순서를 공유한다. 검사에는 행 상태·고정 슬롯·행 폴딩/오른쪽 허브 분리·native group leading paint·초점 갱신·공통 표면 계약을 포함한다. 실제 새 앱에서 상태별로 검수한 결과와 미검수 범위는 작업 검수 기록에 구분한다.

폴딩과 허브의 metadata 갱신은 같은 실제 자식 배치 높이를 사용한다. 허브 진입·복귀로 프로젝트 아래 일반 탭의 시작 위치가 달라지지 않는다. 허브가 보이는 동안 native 세로 탭의 실제 모델 선택은 유지하되 선택 면·선택 접근성·상시 닫기 표시를 숨기며 복귀하면 함께 복원한다. 일반·Favorite·그룹 및 Favorite 분할 바탕이 같은 표시 상태를 소비한다.

프로젝트와 native 그룹의 leading 화살표는 오른쪽과 아래 방향 사이를 160ms 동안 회전한다. 빠른 반대 입력은 현재 각도에서 이어지고 RTL에서는 회전 방향을 반전한다. hover/focus의 고정된 아이콘 칸을 유지하며 시스템 동작 줄이기에서는 최종 방향을 바로 표시한다.

허브 Header는 프로젝트 이름·전환만 표시하고 Back·Sidebar 토글·맞춤설정·본문 fallback Back은 두지 않는다. 키보드 초기 초점은 남은 이름에 두고 상단이 없으면 본문 조작으로 옮긴다. 허브와 Sidebar 보조 조작은 공통 native 버튼 설정을 사용하며 가로 배치의 중앙 높이를 맞춘다.

### 목록 갱신과 native 입력 경계

자료의 제목·주소·열림 상태를 갱신할 때 허브와 Sidebar는 읽던 스크롤 위치를
유지한다. 새 contents의 배치가 끝난 뒤 위치를 복원하고, 목록이 짧아졌으면
유효한 범위로 제한한다. 다른 프로젝트로 전환한 허브는 처음부터 표시한다.
폴딩 후에는 자신의 ScrollView contents layer와 공통 부모를 다시 그려
포인터 이동이나 화살표 애니메이션 없이 제목·파비콘을 표시한다.

세로 native 목록의 URL 드롭은 실제 보이는 행만 대상으로 한다. 프로젝트로
숨긴 행의 이전 bounds는 사용하지 않고, 분할 탭의 보이는 멤버가 하나면
그 멤버의 native identity를 반환한다. 그룹과 미고정 탭 컨테이너의 재귀 탐색도
같은 가시성 규칙을 따른다.
