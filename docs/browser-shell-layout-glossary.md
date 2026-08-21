# Yee browser shell layout glossary

이 문서는 Yee 브라우저 셸을 논의하고 설계하며 구현할 때 사용하는 표준 용어를
정의한다. 화면에서 보이는 제품 용어를 우선하고, 필요한 경우 Chromium Views의
구현 용어를 함께 표기한다.

새 문서와 코드 주석에서는 이 문서의 **표준 용어**를 사용한다. 아직 위치가
확정되지 않은 요소는 이름과 역할만 정의하고 배치를 의미에 포함하지 않는다.

## 1. 전체 구조

```text
Window Frame
├─ Title Bar
│  ├─ Sidebar Header [Sidebar Column]
│  │  ├─ Window Controls Safe Area [leading platform controls]
│  │  └─ Shell Controls
│  │     ├─ Sidebar Toggle
│  │     ├─ New Tab
│  │     └─ Agent Control
│  ├─ Browser Toolbar [Content Column]
│  │  ├─ Navigation Controls
│  │  │  ├─ Back
│  │  │  ├─ Forward
│  │  │  └─ Reload
│  │  ├─ Omnibox
│  │  ├─ Extension Dock
│  │  └─ Toolbar Actions
│  └─ Window Controls Safe Area [trailing platform controls]
└─ Browser Body
   ├─ Tab Sidebar [Sidebar Column]
   │  ├─ Favorites
   │  ├─ Bookmarks
   │  ├─ Groups
   │  ├─ Tabs
   │  ├─ Agent Activity
   │  └─ Context Switcher
   └─ Content Column
      ├─ Content Gutter
      └─ Browser Content
```

이 구조는 정보 관계를 설명한다. Context Switcher나 Agent Control의 최종 위치는
별도 레이아웃 결정에 따라 달라질 수 있다.

### 두 열의 표준 명칭

| 표준 용어 | 의미 | 폭 규칙 |
| --- | --- | --- |
| **Sidebar Column** | Sidebar Header와 Tab Sidebar가 공유하는 고정 열 | 펼쳐진 상태에서 `Sidebar Header width = Tab Sidebar width` |
| **Content Column** | Browser Toolbar와 Browser Content가 공유하는 가변 열 | Sidebar Column 다음부터 창 오른쪽 Safe Area 전까지 확장 |
| **Sidebar Header** | Shell Controls가 배치되는 Sidebar Column의 상단 영역 | Sidebar Column과 동일한 폭 |

```text
sidebar_header_width = tab_sidebar_width = sidebar_column_width
content_column_start_x = window_left + sidebar_column_width
browser_toolbar_start_x = browser_content_start_x
                        = content_column_start_x + content_gutter
omnibox_start_x = browser_toolbar_start_x
                 + navigation_controls_width + navigation_omnibox_gap
```

`Content Gutter`는 Sidebar Column의 폭에 포함하지 않는다. Content Column 안쪽에서
Browser Content 둘레에 적용하며, 상단 간격은 Omnibox의 bottom inset과 공유한다.
Omnibox는 Navigation Controls 다음에 놓이므로 Browser Content와 직접 정렬하지
않는다. Collapsed Edge Rail은 페이지 폭을 줄이지 않는 가장자리 target이고 Sidebar
Flyout은 Browser Content 위에 표시된다.

## 2. Window Frame과 Title Bar

| 표준 용어 | 의미 | Chromium·OS 대응 |
| --- | --- | --- |
| **Window Frame** | OS 창과 Yee 셸을 포함하는 최외곽 창 구조 | `BrowserFrameView`, native window frame |
| **Title Bar** | 창 최상단의 Window Controls와 Browser Toolbar가 놓이는 영역 | native/custom title bar |
| **Sidebar Header** | Title Bar 안에서 Sidebar Column과 폭을 공유하며 Shell Controls를 담는 영역 | 현재는 `ToolbarView`의 leading segment |
| **Window Controls** | 닫기·최소화·확대 또는 최대화 버튼의 통칭 | macOS Traffic Light Buttons, Windows/Linux Caption Buttons |
| **Window Controls Safe Area** | Window Controls와 제품 UI가 겹치지 않도록 프레임이 예약한 영역 | `BrowserLayoutParams.leading_exclusion`, `trailing_exclusion` |
| **Window Controls Gutter** | Safe Area와 첫 제품 컨트롤 사이의 의도적인 간격 | toolbar inset 또는 control gap |
| **Drag Region** | 클릭 컨트롤이 없으며 창을 이동할 수 있는 Title Bar 영역 | caption hit-test region |
| **Leading Area** | 읽기 방향상 Title Bar의 시작 부분 | macOS에서는 일반적으로 Traffic Lights 다음 영역 |
| **Trailing Area** | 읽기 방향상 Title Bar의 끝 부분 | Windows에서는 일반적으로 Caption Buttons 앞 영역 |

### OS별 Window Controls 명칭

| OS | 명칭 | 일반적인 위치 | 버튼 |
| --- | --- | --- | --- |
| macOS | **Traffic Light Buttons** | 왼쪽 | Close, Minimize, Zoom/Fullscreen |
| Windows | **Caption Buttons** | 오른쪽 | Minimize, Maximize/Restore, Close |
| Linux | **Caption Buttons** 또는 **Window Controls** | 데스크톱 설정에 따라 좌·우 | Minimize, Maximize/Restore, Close |

Yee는 OS별 위치를 하드코딩하지 않는다. 프레임이 제공하는 Leading/Trailing
exclusion을 Window Controls Safe Area로 사용한다. 전체화면이나 서버 측 장식처럼
앱 내부 컨트롤이 없는 경우 Safe Area는 축소되거나 비어 있을 수 있다.

## 3. Browser Toolbar

**Browser Toolbar**는 Content Column 상단에서 탐색 버튼, 주소창과 확장 기능을
제공하는 인터랙티브한 한 줄이다. Yee에서는 Title Bar 안에 통합되지만 Title Bar
자체와 같은 말은 아니다. Sidebar 버튼과 Agent 버튼은 인접한 Sidebar Header의
Shell Controls에 속한다.

| 표준 용어 | 의미 | Chromium 대응 |
| --- | --- | --- |
| **Browser Toolbar** | Content Column에서 브라우저 탐색과 페이지 기능을 제공하는 상단 컨트롤 행 | 현재 `ToolbarView`의 main segment |
| **Shell Controls** | Sidebar Header에서 Yee 셸 자체를 제어하는 버튼 묶음 | Sidebar Toggle, New Tab, Agent Control |
| **Sidebar Toggle** | Tab Sidebar의 pinned/open 상태를 전환하는 버튼 | vertical tab/sidebar toggle |
| **New Tab** | 현재 Group 또는 기본 위치에 새 브라우저 Tab을 만드는 버튼 | new-tab action |
| **Agent Control** | Agent 상태를 간결하게 표시하고 상세 화면을 여는 버튼 | Yee agent `ToolbarButton` |
| **Navigation Controls** | 현재 Tab의 탐색 기록과 로딩을 제어하는 버튼 묶음 | Back, Forward, Reload/Stop |
| **Omnibox** | URL, 검색, 페이지 정보가 결합된 주소 입력 영역 | `LocationBarView`, Omnibox model |
| **Extension Dock** | 표시가 허용된 확장 프로그램 action의 고정 영역 | extensions toolbar container |
| **Toolbar Actions** | 공유, 다운로드, 메뉴처럼 페이지나 브라우저에 적용되는 후행 action | page/browser actions |

`Agent Control`은 작은 진입점이자 상태 표시다. 상세 실행 상태와 task 목록은
`Agent Activity`에 둔다. 두 용어를 서로 바꾸어 쓰지 않는다.

## 4. Tab Sidebar와 Browser Content

| 표준 용어 | 의미 | Chromium 대응 |
| --- | --- | --- |
| **Browser Body** | Title Bar 아래의 Tab Sidebar와 Browser Content를 포함하는 영역 | BrowserView의 주 콘텐츠 영역 |
| **Sidebar Column** | Sidebar Header와 Tab Sidebar가 공유하는 고정 폭 열 | Yee shell layout column |
| **Content Column** | Browser Toolbar와 Browser Content가 공유하는 가변 폭 열 | Yee shell layout column |
| **Tab Sidebar** | 탭과 관련된 사용자 도구가 놓이는 세로 사이드바 | `VerticalTabStripRegionView` 기반 |
| **Favorites** | 자주 여는 사이트를 아이콘 독으로 두는 영역. Chromium pinned tab이 백킹이다 | pinned tab container, Yee dock |
| **Pins** | Arc 스타일 pinned tabs용 예약 슬롯. Favorites와 같은 말이 아니다 | `SidebarSection::kPins`, 아직 뷰 없음 |
| **Vertical Tab Strip** | 실제 브라우저 Tab을 세로로 배치하는 내부 컴포넌트 | Chromium 구현 용어 |
| **Pinned Sidebar** | Browser Content의 폭을 확보한 채 고정 표시되는 Sidebar 상태 | expanded/pinned vertical tabs |
| **Collapsed Edge Rail** | Sidebar가 닫혔을 때 다시 열 수 있도록 남는 얇은 가장자리 target | collapsed vertical tab strip |
| **Sidebar Flyout** | Edge Rail에 접근했을 때 Browser Content 위로 떠서 열리는 Sidebar | expand-on-hover state |
| **Group** | 사용자가 여러 Tab을 묶는 UI 정리 도구 | tab collection presentation |
| **Tab** | URL과 `WebContents`에 연결된 실제 브라우저 탭 | `TabStripModel` entry |
| **Agent Activity** | Agent Task의 상태, 결과와 연결된 Context Tabs를 보여주는 상세 영역 | Yee product UI; runtime 연동 예정 |
| **Browser Content** | 현재 선택된 Tab의 페이지가 렌더링되는 표면 | `MultiContentsView`, `WebContents` |
| **Content Gutter** | Browser Content와 셸 사이에 남기는 6 DIP 외부 간격 | Yee content layout inset |
| **Content Boundary** | Browser Content와 Gutter의 경계 | 고정 12 DIP 곡률, theme-derived 1 DIP outline과 얕은 shadow |
| **Chrome Surface** | Title Bar, Tab Sidebar와 Gutter가 공유하는 브라우저 셸 재질 | Browser chrome background |

제품 문서에서는 전체 영역을 `Tab Sidebar`라고 부른다. Chromium 코드나 클래스와
직접 대응할 때만 `Vertical Tab Strip`을 사용한다. Favorites, Bookmarks, Group,
Agent Activity까지 포함하는 전체 Sidebar를 `Tab Strip`이라고 부르지 않는다.

## 5. Context와 실행 단위

다음 용어는 레이아웃 요소이면서 제품의 상태 범위를 나타낸다.

| 표준 용어 | 의미 | 주의사항 |
| --- | --- | --- |
| **Tenant** | 계정·조직·데이터 격리의 최상위 경계 | 단순한 시각적 Group이 아니다. |
| **Workspace** | Tenant 안에서 Tab과 작업 맥락을 나누는 논리적 작업 공간 | Browser Content 영역의 별칭으로 사용하지 않는다. |
| **Context Switcher** | 현재 Tenant와 Workspace를 표시하고 전환하는 컨트롤 | 위치는 레이아웃 결정 사항이다. |
| **Profile** | 개인 사용자 신원과 개인 설정을 나타내는 컨트롤 | Context Switcher와 같은 개념이 아니며 결합 여부는 미정이다. |
| **Group** | 사용자가 Tab을 시각적으로 묶는 UI 도구 | 권한·데이터 경계나 Agent Task 소유 단위가 아니다. |
| **Agent Task** | 시작·진행·입력 대기·완료 상태를 갖는 실행 단위 | Group과 별도로 분류한다. |
| **Context Tab** | Agent Task가 참조하거나 조작하는 실제 Tab | 별도 가상 탭이 아니라 최종적으로 resolve된 Tab이다. |

범위 관계는 다음과 같이 표현한다.

```text
Tenant
└─ Workspace
   ├─ Groups (사용자 UI 정리)
   │  └─ Tabs
   └─ Agent Tasks (실행 단위)
      └─ Context Tabs → 실제 Tabs 참조
```

## 6. 혼동하기 쉬운 용어

| 피해야 할 표현 | 사용할 표현 | 이유 |
| --- | --- | --- |
| 상단 **Status Bar** | **Browser Toolbar** | Status Bar는 일반적으로 화면 하단의 링크·로딩 상태 영역을 뜻한다. |
| Sidebar 버튼 영역 | **Shell Controls** | Sidebar Toggle과 Agent Control 등의 역할을 함께 설명할 수 있다. |
| 주소창 | **Omnibox** | Yee에서는 URL 입력뿐 아니라 검색과 페이지 정보도 포함한다. |
| 왼쪽 탭바 | **Tab Sidebar** | RTL과 다양한 Sidebar 구성을 고려하고 탭 외 요소도 포함한다. |
| 맥 버튼 영역 | **Window Controls Safe Area** | 제품 UI가 피해야 할 영역이라는 레이아웃 의미를 포함한다. |
| Agent 버튼/Agent 영역 혼용 | **Agent Control** / **Agent Activity** | Toolbar 진입점과 Sidebar 상세 영역을 구분한다. |
| 작업 화면을 Workspace라고 부름 | **Browser Content** | Workspace는 제품의 논리적 범위다. |

`Command Runway`는 초기 디자인 탐색에서 사용한 개념어다. 실제 레이아웃 요소를
지칭할 때는 `Browser Toolbar`, `Omnibox`, `Yee Hub` 중 정확한 이름을 사용한다.

## 7. 문서와 코드에서의 표기 규칙

- 제품·디자인 문서에서는 표준 영문 용어를 첫 등장에 쓰고 필요한 경우 한글 설명을
  덧붙인다. 예: `Browser Toolbar(브라우저 툴바)`.
- 코드에서는 OS 이름보다 역할을 사용한다. 예: `window_controls_safe_area`는 좋고
  `mac_traffic_light_padding`은 피한다.
- `leading`과 `trailing`을 사용하고 `left`와 `right`는 실제 좌표 계산에만 사용한다.
- 상태와 컴포넌트를 구분한다. 예: `Pinned Sidebar`는 상태이고 `Tab Sidebar`는
  컴포넌트다.
- 새로운 별칭을 만들기 전에 이 문서의 기존 용어로 표현할 수 있는지 확인한다.

치수, 색상, 인터랙션의 구체적인 계약은
[`browser-shell-spec.md`](./browser-shell-spec.md)를 따른다. Tab Sidebar에서
이미 고른 UX 결정은 [`sidebar/`](./sidebar/)를 따른다.
