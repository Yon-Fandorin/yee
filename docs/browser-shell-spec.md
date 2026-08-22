# Yee browser shell UI specification

이 문서는 정적 프로토타입의 시각적 의도를 실제 Chromium Views/WebUI 구현으로
옮길 때 사용하는 기준 문서다. CSS의 현재 모습보다 이 문서의 **불변 조건**을
우선한다. 치수나 구조를 바꾸면 프로토타입과 native 구현을 함께 갱신한다.

레이아웃 요소와 제품 범위의 표준 명칭은
[`browser-shell-layout-glossary.md`](./browser-shell-layout-glossary.md)를 따른다.
Tab Sidebar에서 이미 고른 UX 결정은
[`sidebar/`](./sidebar/)를 따른다.

## 1. 제품 원칙

1. OS는 창 프레임을 소유하고 Yee는 제품 UI를 소유한다.
2. OS별 차이는 Title bar 재질, Caption controls, 단축키 표기에 한정한다.
3. Sidebar의 폭, 정보 구조, 행 높이, 간격과 선택 상태는 모든 OS에서 같다.
4. Sidebar와 Browser Surface 바깥 gutter는 하나의 연속된 chrome 면이다.
5. Browser Toolbar와 실제 웹 페이지는 하나의 외곽 Browser Surface 안에서
   theme-derived 1 DIP separator로만 나뉜다.
6. Chromium의 native Omnibox, `TabStripModel`, `WebContents`, page actions와
   caption hit testing을 대체하거나 흉내 내지 않는다.

## 2. 레이아웃 불변 조건

| 항목 | 기준 | 구현 메모 |
| --- | ---: | --- |
| Expanded sidebar | 244 DIP | prototype과 native shell이 같은 기본 폭 token을 사용한다. |
| Sidebar header | Expanded sidebar와 동일 | New Tab·Agent Control과 Tab Sidebar가 하나의 Sidebar Column 경계를 공유한다. |
| Browser surface gutter | 6 DIP | 결합된 Browser Surface의 상·우·하 및 Sidebar 쪽 외부 간격이다. |
| Browser Surface Header | 42 DIP | 48 DIP Title Bar에서 상단 6 DIP gutter를 제외한 실제 표면 높이다. |
| Browser Toolbar | 40 DIP | Browser Surface Header 안에서 수직 중앙 정렬한다. |
| Omnibox | 34 DIP | Browser Surface Header 안에서 위·아래 4 DIP 여백을 갖는다. |
| Sidebar 내부 좌우 padding | 8 DIP | section 자체는 추가로 좌우 4 DIP inset을 사용할 수 있다. |
| Sidebar section gap | 10 DIP | Favorites, Bookmarks, Group, Agent 영역 사이의 기본 리듬이다. |
| Group heading | 30 DIP | disclosure, mark, title, count의 수직 중심을 공유한다. |
| Tab row | 40 DIP | title + hostname 두 줄, favicon tile 포함. |
| Agent task row | 최소 50 DIP | 일반 tab과 구분되는 실행 상태 카드다. |
| Browser surface radius | 12 DIP | 결합 표면의 외곽 outline과 shadow가 사용하며 WebContents는 하단 모서리만 공유한다. |
| Active indicator | 3 × 20 DIP | tab 왼쪽, 행 중앙 정렬. |

### 공통 수직 기준선

Sidebar가 펼쳐져 있을 때 Browser Surface Header와 Browser Content의 왼쪽 경계는
항상 같은 x 좌표여야 한다.

```text
sidebar_column_width = sidebar_width
sidebar_header_width = sidebar_column_width
tab_sidebar_width = sidebar_column_width
content_column_start_x = window_left + sidebar_column_width
browser_surface_start_x = content_column_start_x + content_gutter
browser_toolbar_start_x = browser_surface_start_x
content_start_x = browser_surface_start_x
omnibox_start_x = browser_toolbar_start_x
                 + sidebar_toggle_width
                 + navigation_controls_width
                 + navigation_omnibox_gap
```

New Tab과 Agent Control은 Sidebar Header에 남긴다. Sidebar Toggle은 Content
Column의 Browser Surface Header 첫 컨트롤이며, 그 뒤에 Back, Forward, Reload와
Omnibox가 이어진다. 창 크기나 OS Window Controls 때문에 공간이 부족하면 컨트롤
간격을 줄이되 Browser Surface의 시작 경계를 깨지 않는다.

Sidebar가 닫히면 고정 기준선은 해제하고 Omnibox와 Browser Content가 남은 폭을
사용한다. Hover flyout은 페이지 폭을 변경하지 않는다.

### 공통 inset과 Browser Surface 곡률

Omnibox는 창의 Title Bar가 아니라 상단 gutter를 제외한 Browser Surface Header를
기준으로 수직 정렬한다.

```text
content_gutter = 6 DIP
browser_surface_header_height = titlebar_height - content_gutter = 42 DIP
browser_toolbar_height = 40 DIP
browser_toolbar_top = content_gutter + 1 DIP = 7 DIP
omnibox_height = 34 DIP
omnibox_top = content_gutter + 4 DIP = 10 DIP
omnibox_bottom = titlebar_height - 4 DIP = 44 DIP
browser_surface_margin = content_gutter
content_top = titlebar_height
toolbar_content_separator = 1 DIP
```

현재 native checkpoint는 모든 OS에서 48 DIP Title Bar, 40 DIP Browser Toolbar,
34 DIP Omnibox를 사용한다. 상단 gutter와 Omnibox 사이에는 4 DIP가 남으므로 hover나
focus 상태도 Browser Surface 외곽을 침범하지 않는다. 이 값은 Chromium layout에
복제하지 않고 `yee::kSidebarMetrics`의 한 계산 계약을 사용한다.

Browser Surface는 y=6 DIP에서 시작해 Toolbar와 Content를 함께 감싼다. Content는
Title Bar 바로 아래에서 시작하며 두 영역 사이에 별도 gap이나 중복 outline을 두지
않는다. 현재 theme의 content separator 색으로 1 DIP 내부 선만 그린다.

현재 native checkpoint는 Browser Surface 외곽 네 모서리에 12 DIP 고정 곡률을
사용한다. WebContents는 결합 표면 안에서 이어지므로 상단 곡률은 0이고 하단 두
모서리만 같은 `yee::kSidebarMetrics.content_corner_radius`를 사용한다.

OS 창 모서리와 Content 곡률을 자동으로 맞추는 방식은 보류한다. macOS Zoom,
Windows/Linux 최대화·전체 화면·타일링처럼 창 상태의 의미가 서로 달라, 실제
WebContents 합성 결과까지 검증한 뒤 별도 결정으로 추가해야 한다.

## 3. 표면과 색상

각 OS는 하나의 `chrome_bg`를 가진다. Browser frame, pinned Sidebar와 Browser
Surface 바깥 gutter는 동일한 값을 사용한다. 결합된 Browser Surface는 Toolbar
theme 색의 header, 불투명 WebContents, 외곽 theme-derived 1 DIP outline과 얕은
1px/3px shadow로 구성한다.

| OS | `chrome_bg` | `chrome_line` | 특징 |
| --- | --- | --- | --- |
| Windows | 현재 theme frame 색, 기본 `#f3f3f3` | `#dfdfdf` | Fluent 계열, 불투명 |
| macOS | 현재 theme frame 색의 native glass tint | theme-derived separator | Light 약 43% / Dark 약 74% 유효 불투명도 |
| Linux | 현재 theme frame 색, 기본 `#f1f0ef` | `#d8d6d4` | Adwaita 계열, 불투명 |

- Browser Surface Header backing: navigation·load·tab 전환·페이지 색 변경 뒤 실제
  WebContents 최상단의 얇은 띠를 저해상도로 샘플링한다. navigation 중에는 직전에
  확정한 Header 색을 지우지 않고 새 결과를 candidate로만 유지한다. load가 끝난 뒤
  같은 평면색이 최소 150ms 동안 세 sample 연속 안정적일 때 새 색을 한 번 확정한다.
  각 WebContents는 마지막으로 확정한 색을 자신의 수명 동안 보관한다. 이미 샘플링한
  Tab으로 돌아오면 다른 Tab의 색을 유지하지 않고 해당 색을 즉시 복원한 뒤 새 sample로
  검증한다.
  이전 확정 색이 없는 창은 이 구간에 현재 theme의 `kColorToolbar`를 사용한다. 사용자
  스크롤이 시작되면 매 frame을 캡처하지 않고 약 140ms 간격의 제한된 sample burst를
  실행한다. 연속한 두 결과가 안정적일 때만 현재 viewport 최상단 색으로 전환하고,
  확정된 색 사이는 약 200ms 동안 연속 보간한다. 전환 중 새 결과가 확정되면 현재
  화면에 보이는 색에서 새 목표색으로 이어가며, 중간색을 단계적으로 확정해 보이는
  계단식 전환은 만들지 않는다.
  bounded attempt 뒤에도 샘플이 이미지·gradient처럼 불균일하거나 안정되지 않으면
  페이지 CSS background, `theme-color`, 현재 theme의 `kColorToolbar` 순서로
  fallback한다. 선택한 페이지 색은
  Toolbar 쪽으로 보정하지 않고 그대로 사용해 WebContents와 Header의 시각적 색 경계를
  만들지 않는다.
- Omnibox는 Rest에서 Browser Surface Header와 같은 resolved color를 사용하고
  상시 outline을 그리지 않는다. Hover에서만 해당 표면의 최대 대비색을 약 2%
  혼합하며, 편집·키보드 focus는 suggestions가 열린 동안에도 유지되는 native focus
  ring으로 구분한다. 주소·제목·중립 control은 고정 theme 색을 사용하지 않고 resolved
  surface에서 primary(읽기 대비), secondary(비텍스트 가시 대비), disabled 역할색을
  계산한다. 위험·보안·제품 상태의 의미 색은 이 중립 팔레트보다 우선한다.
- Address suggestions의 중립 배경·텍스트·아이콘도 열리는 시점의 같은 resolved
  surface에서 계산한다. hover·선택 행은 최대 대비색을 6%만 혼합하고, 위험·보안
  상태색과 고대비 모드는 Chromium의 native 팔레트를 유지한다.
- Glass 활성화, 전체 창 적용 범위, tint 불투명도는 Yee 코드의 제품 기본값이다.
  실행 스크립트와 강제 theme seed에 의존하지 않는다.
- macOS 26 미만, Windows, Linux와 macOS의 ‘투명도 줄이기’ 환경에서는 현재 theme
  frame 색을 불투명하게 그린다. 지원되는 macOS의 활성 창만 native material을
  사용하며 비활성 창은 같은 색의 불투명 표면으로 전환한다.
- Active tab: 약 88% white, 얕은 1px/3px shadow
- Windows restored window: visible outline 없이 DWM의 둥근 시스템 shadow를
  유지한다. 1px transparent top-frame extension은 shadow를 위한 합성 힌트일
  뿐이며 client surface 안에 검은 선이나 별도 inset을 만들지 않는다.
- Hover: 해당 OS chrome보다 한 단계만 밝거나 어둡게 한다.
- Browser Surface 외곽 outline과 Toolbar/Content 내부 separator는 resolved surface의
  최대 대비색에서 낮은 alpha로 도출한다. 내부 separator는 외곽 outline보다 조용하다.
- 외곽 outline·shadow와 WebContents 하단 clip은 같은 12 DIP corner token을 사용한다.

## 4. OS별 허용 차이

### Windows

- Title bar: 48 DIP
- Caption controls: 오른쪽, native 순서 Minimize → Maximize → Close
- 각 caption button: 46 DIP 폭, Windows snap layout과 resize hit test 보존
- Omnibox: 34 DIP 높이, 12 DIP radius
- 단축키: `Ctrl K`, `Ctrl T`

### macOS

- Title bar: 52 DIP
- Traffic lights: 왼쪽, Close → Minimize → Zoom
- Traffic lights는 Sidebar Header의 Window Controls Safe Area에 두고 Shell
  Controls와 겹치지 않는다.
- Leading actions는 필요하면 26 DIP까지 압축할 수 있다.
- Glass/blur는 지원되는 macOS의 browser chrome에만 적용하고 `WebContents`는
  불투명하게 유지한다. 시스템의 ‘투명도 줄이기’ 설정에서는 즉시 불투명 셸로
  전환한다.
- 단축키: `⌘K`, `⌘T`

### Linux

- Title bar: 50 DIP
- Caption controls: 오른쪽 원형 Adwaita 계열, Minimize → Maximize → Close
- Headerbar는 불투명하며 하단 shadow나 seam을 만들지 않는다.
- 단축키: `Ctrl K`, `Ctrl T`

## 5. Sidebar 정보 구조

위에서 아래 순서를 유지한다.

1. **Favorites**: 자주 쓰는 앱의 4열 quick-launch grid
2. **Bookmarks**: 기본은 30 DIP 한 줄로 접고, 펼치면 folder별 항목과 개수를 표시
3. **User groups**: 사용자 정의 tab collection
4. **Agent activity**: 실행 중인 task와 연결된 context tabs. 현재 native sidebar 체크포인트에서는 제외한다.
5. **Tenant / Workspace**: Sidebar footer에 고정

`Tabs` 같은 중복 섹션 제목은 사용하지 않는다. 화면 자체가 tab sidebar이므로
추가 제목은 정보를 늘리지 않는다.

Favorites가 0개면 유휴 상태에서 독과 빈 영역을 모두 숨긴다. 실제 Tab
드래그가 시작된 동안에만 빈 Favorites 드롭존을 표시하고, 드래그가 끝나거나
취소되면 다시 숨긴다. 드롭존은 160ms 동안 0에서 76 DIP로 펼쳐지고 표면은
30ms 뒤에 나타나기 시작한다. 취소할 때는 높이와 표면 투명도를 120ms 동안
함께 접는다. Group 드래그에는 표시하지 않는다. 마지막 Favorite을 집으면
즉시 같은 76 DIP 드롭존으로 교체한다. 포인터가 Tab 영역으로 넘어가도 드래그가
끝날 때까지 드롭존과 레이아웃 공간을 유지한다. 모델의 unpin 커밋 중에도 한 칸
독으로 바꾸지 않는다. 커밋이 끝나면 드롭존의 레이아웃 공간은 즉시 제거하고,
레이아웃에 참여하지 않는 표면 잔상만 90ms 동안 사라지게 한다. 삭제 애니메이션을
위해 남은 View는 Favorite 존재 여부에 포함하지 않아, 드롭존과 한 칸 독이
순서대로 사라지는 이중 전환을 만들지 않는다. 빈 드롭존의 hit magnet은 아래로
늘리지 않아 첫 Tab의 위쪽을 첫 번째 Tab 삽입 위치로 보존한다. 모션 감소 설정에서는
드롭존의 모든 전환을 즉시 반영한다. 드롭존의 안내색은 고정 RGB를 쓰지 않는다.
현재 Chromium 테마의 frame과 label 색을 Yee shell tint 및 드롭존 fill 위에 합성해
매 paint마다 도출한다. 제목은 최소 7:1, 설명은 최소 4.5:1 대비를 유지하고,
별과 점선은 계산된 제목색에서 파생한다.
Favorites와 Tab 목록 사이를 넘는 드롭은 보였던 삽입 빈자리의 순서를 그대로
모델에 커밋한다. 독 바로 아래 경계는 첫 Tab 앞이며, 첫 Tab 아래에 놓은
Favorite을 첫 번째 Tab으로 되돌리지 않는다. 목록 끝 빈자리는 원본 위치와
무관하게 실제 마지막 순서로 커밋한다.
영역을 넘어 도착한 Favorite과 Tab은 처음부터 최종 크기와 레이아웃 공간을
차지한다. 도착 요소의 레이어만 최종 위치의 왼쪽 10 DIP에서 시작해 240ms 동안
빠르게 감속하며 오른쪽으로 이동하고 동시에 불투명해진다. 같은 영역 안의
재배치와 주변 요소 이동은 기존 컨테이너 애니메이션을 유지한다.
Expanded Sidebar의 Tab 영역은 남은 세로 공간을 모두 채우며, 마지막 Tab 아래
빈 공간도 목록 끝 드롭 타깃으로 유지한다.
탭이 하나뿐인 창에서 Tab을 드래그하면 창 이동보다 Sidebar 내부 정리를 먼저
시작한다. 같은 Sidebar 안에서는 Favorite 등록·해제와 Tab 배치를 처리하고,
Sidebar를 벗어나면 지연 없이 Chromium의 기존 창 이동·다른 창 연결 경로로
전환한다. 빈 바탕화면에서는 현재 단일 Tab 창 자체를 이동하며, 다른 창의
Favorites 독에 놓으면 목적지 창의 Favorite으로 등록한다. Group 헤더 드래그와
여러 Tab 전체 선택 드래그에는 이 예외를 적용하지 않는다.

### Group contract

- Heading 순서: disclosure → color mark → name → count → add action
- Group의 `+`는 heading 오른쪽 끝에 두고 hover/focus 시 드러낸다.
- `+`는 해당 group에 Tab 또는 Note를 추가한다.
- Global `+`는 New tab, New note, New group을 제공한다.
- Group은 사용자의 정리 도구이며 Agent Task의 소유자가 아니다.

### Agent contract

- Toolbar에는 30 DIP compact status button만 둔다.
- 상세 task 상태는 Sidebar의 Agent activity section에 둔다.
- 상태는 최소 `Ready`, `Working`, `Needs input`을 지원한다.
- Context tabs는 task 아래에 연결된 형태로 표시하되 실제 browser tab이다.

## 6. Omnibox 구현 계약

Omnibox는 하나의 click target이 아니다. 하나의 외곽 surface 안에 세 개의 독립된
control을 다음 순서로 배치한다.

```text
┌───────────────────────────────────────────────────┐
│ Site info │ origin | Page title        │ Bookmark │
└───────────────────────────────────────────────────┘
```

순서와 기능은 모든 OS에서 같다. RTL에서는 native platform 규칙에 따라 mirror할 수
있지만 논리적 focus order는 Site info → Address → Bookmark를 유지한다.

### Omnibox geometry

| 항목 | 값 | 비고 |
| --- | ---: | --- |
| Outer height | 34 DIP | 42 DIP Browser Surface Header 안에서 상·하 4 DIP |
| Outer radius | 12 DIP | Browser Surface와 Address suggestions cutout이 공유 |
| Outer padding | vertical 2 / horizontal 3 DIP | border 안쪽 |
| Internal gap | 1 DIP | 각 control 사이 |
| Site info column | 30 DIP | 실제 button은 28 × 28 DIP |
| Address column | fluid, 최소 120 DIP | 유일하게 줄어드는 column |
| Bookmark column | 30 DIP | 실제 button은 28 × 28 DIP |
| Inner control radius | outer radius - 3 DIP | 현재 9 DIP |
| Icon | 15 × 15 DIP | 1.45 DIP stroke |
| Origin | 11 DIP / medium | 왼쪽의 primary label, 최대 180 DIP, 한 줄 ellipsis |
| Page title | 11 DIP / regular | origin 오른쪽의 secondary label, 한 줄 ellipsis |

Omnibox는 Browser Surface Header에서 Sidebar Toggle과 Navigation Controls 뒤의
가변 폭을 사용한다. control을 추가하더라도 Browser Surface 자체의 시작 경계는
이동하지 않는다. 공간이 좁아지면 page title을 먼저 ellipsis 처리하고, origin은
최대 180 DIP까지만 사용한다. Site info와 Bookmark는 숨기지 않는다.

Rest의 origin은 scheme과 선행 `www.`를 생략하되 의미 있는 subdomain은 유지한다.
Origin과 Page title 사이는 글자 `|`가 아니라 1 DIP 세로 divider로 나누며, Page
title은 origin보다 낮은 대비를 사용한다. Focus가 들어오면 이 표시 layer를 즉시
숨기고 첫 pointer 활성화부터 native Omnibox의 전체 URL과 편집 UI를 그대로 노출한다.

### Site info

- 왼쪽 첫 control이며 Chromium의 현재 page identity/security state를 사용한다.
- secure page는 tune/site-controls icon을 사용하고 accessible name에 현재 상태를
  포함한다. 예: `사이트 정보 보기, 연결이 안전함`.
- button: 28 × 28 DIP. 중립 상태의 Rest 배경은 Browser Surface Header와 같고,
  hover/focus/expanded에서 native ink drop과 상태 배경을 사용한다. 위험 상태와
  delegate가 지정한 의미 색은 Header 동화보다 우선한다.
- popover: 286 DIP 폭, Omnibox 아래 6 DIP, 10 DIP padding, 12 DIP radius.
- header: 34 DIP status tile + `Connection is secure` + origin.
- 최소 row: Cookies and site data, Site permissions, Certificate.
- prototype의 값은 demo data다. native 구현은 `PageInfoBubbleView`, identity model,
  permission and certificate state를 그대로 사용한다.
- security state를 Yee service에서 재계산하거나 문자열로 복제하지 않는다.

### Address target

- fluid column 전체가 Address suggestions trigger다.
- 기본 상태는 `origin | page title`을 보여준다. Origin은 Chromium의 security display
  formatter를 사용하고 항상 title과 별도 label로 남겨 긴 title에도 가려지지 않는다.
- origin을 primary 대비와 medium weight로, page title을 한 단계 낮은 대비와 regular
  weight로 그려 URL을 기준 정보로 읽게 한다.
- 기본 표시는 실제 native Omnibox 위의 non-interactive presentation layer이며,
  접근성 tree와 hit target은 native Omnibox가 계속 소유한다.
- click 또는 `Ctrl/⌘ L`에서 URL edit model로 전환하고 현재 URL을 전체 선택한다.
- hover에서만 resolved surface의 2% contrast tint를 주며 별도 border를 추가하지
  않는다.
- focus stroke는 Address suggestions가 열린 동안에도 유지하되 Omnibox의 34 DIP
  외곽에서 사방 2 DIP 안쪽에 1 DIP로 그린다. 따라서 focus 선은 30 DIP 높이와
  10 DIP 곡률을 갖는 내부 edit-state 표시로 읽히며, 12 DIP 외곽 surface와 native
  hit target은 바꾸지 않는다. 현재 Header 배경에서 더 진한 색을 계산하고, 진한
  색으로 3:1 비텍스트 대비를 만들 수 없는 어두운 배경에서만 밝은 방향으로
  전환한다.
- security icon과 Bookmark click은 주소 편집을 열지 않는다.

### Bookmark page

- Address 오른쪽의 28 × 28 DIP 별 button이다.
- off: outline star + `ink_faint`.
- on: `#e7ad48` fill, `#bd842b` foreground, `aria-pressed=true`.
- accessible name은 `이 페이지 북마크`와 `이 페이지 북마크에서 삭제` 사이에서
  상태와 함께 변경한다.
- native 구현은 `BookmarkModel`과 기존 edit bubble을 사용한다. prototype처럼
  local boolean로 끝내지 않는다.

### Yee Hub discoverability

- Omnibox에는 native browser 기능만 둔다. Yee Hub 진입점은 Sidebar Header의
  Shell Controls에 배치해 주소·보안·bookmark 기능과 제품 명령을 분리한다.
- 모든 폭에서 30 × 30 DIP의 icon-only button을 사용한다. 제품명 label을 toolbar에
  반복하지 않는다. macOS는 traffic light 영역 때문에 28 × 28 DIP를 쓴다.
- click과 `Ctrl/⌘ K`는 완전히 같은 Yee Hub를 연다. tooltip과 accessible name에
  OS별 shortcut을 포함한다.
- finding 수는 count badge, 연결 계정의 사용량 주의는 작은 amber status dot으로
  표시한다. 숫자를 하나로 합치거나 진행률을 toolbar에 직접 그리지 않는다.
- 상태 우선순위는 `needs input` → `findings ready` → `usage warning` → `normal`이다.
  복수 상태는 count와 dot을 함께 보여줄 수 있으나 button 크기는 바뀌지 않는다.
- hover, focus, expanded state를 제공하고 expanded는 `aria-expanded=true`다.

### Omnibox state matrix

| 상태 | Site info | Address | Bookmark |
| --- | --- | --- | --- |
| Rest | security icon | title + origin | outline/on star |
| Hover | 28 DIP hover tile | inner tint | 28 DIP hover tile |
| Address editing | unchanged | selected URL + suggestions | unchanged |
| Site info open | expanded tile | unchanged | unchanged |
| Yee Hub open | unchanged | unchanged | unchanged |
| Keyboard focus | native focus state | page-aware 1 DIP inner stroke | native focus state |

동시에 두 개의 popup을 열지 않는다. Site info, Address suggestions, Yee Hub
중 하나를 열면 나머지를 닫는다. Bookmark toggle은 현재 popup을 닫을 수 있지만
새 popup을 만들지 않는다.

## 7. 인터랙션과 접근성

- `Ctrl/⌘ T`: 새 tab
- `Ctrl/⌘ L`: Address suggestions와 주소 편집
- `Ctrl/⌘ K`: Yee Hub
- `Ctrl/⌘ W`: 현재 tab 닫기
- `Ctrl/⌘ B`: Sidebar pinned 상태 전환
- 모든 disclosure는 `aria-expanded`와 실제 visibility를 함께 갱신한다.
- icon-only action은 접근 가능한 이름과 tooltip을 가진다.
- native caption controls와 Omnibox의 keyboard, accessibility, security UI를 보존한다.
- Windows high contrast와 OS reduced motion 설정을 존중한다.

### Address suggestions

- Omnibox click 또는 `Ctrl/⌘ L`로 연다.
- 현재 주소를 전체 선택하고 즉시 입력 가능한 상태로 만든다.
- 패널 top은 실제 Omnibox top과 맞춰 Browser Surface 상단을 침범하지 않는다. 좌우와
  아래만 Omnibox bounds 바깥으로 2 DIP 확장해 하나의 연결된 표면처럼 감싼다. 투명
  cutout은 실제 Omnibox와 같은 12 DIP 곡률을 사용하고 결과 목록은 Omnibox 아래
  2 DIP부터 시작한다. cutout은 Omnibox의 page-aware 1 DIP focus outline을 덮지
  않는다. compact shell의 open/close opacity 전환은 140ms easing을 사용하며 별도
  화면 dim은 사용하지 않는다.
- 중립 배경·텍스트·아이콘은 열리는 시점의 Browser Surface Header 색 역할을
  공유하고, hover·선택 행은 6% 대비 tint만 더한다. semantic 색과 고대비 모드는
  native 값을 보존한다. 패널이 열린 뒤 페이지 surface가 확정되거나 바뀌면 현재
  Widget의 color provider를 닫기·재열기 없이 교체하고, Omnibox 배경·결과 영역·
  텍스트·아이콘을 같은 palette로 함께 갱신한다.
- 주소·검색 제안, 방문 기록, bookmark, 열린 tab을 탐색 대상으로 삼는다.
- `Escape`는 패널을 닫고 페이지 상태를 변경하지 않는다.

#### Address suggestions geometry

| 항목 | 값 |
| --- | ---: |
| Left / width | 실제 Omnibox outer bounds에서 좌우 각 2 DIP 확장 |
| Top | Omnibox top과 동일, 결과는 Omnibox bottom보다 2 DIP 아래 |
| Radius | outer / Omnibox cutout 모두 12 DIP |
| Search row | 46 DIP |
| Result row | 최소 44 DIP |
| Results padding | 10 DIP |
| Elevation | native MD elevation 4 상당의 얕은 shadow |
| Workspace dim | 없음 |

### Yee Hub

- `Ctrl/⌘ K`로 열며 단일 `K` shortcut은 웹 입력과 충돌하므로 사용하지 않는다.
- 상단 중앙의 독립된 620 DIP 패널과 약한 workspace dim을 사용한다.
- 첫 화면은 Agent summary와 연결 구독 계정 사용량을 나란히 보여준다. Agent summary는
  결과 수, 상태, 최근 작업과 `Review` action을 포함한다.
- 사용량은 계정별 서비스명, 남은 비율, reset 시점, 갱신 시점을 표시한다. 서로 다른
  서비스의 quota를 합산하거나 총량처럼 표현하지 않는다.
- 검색어 입력 시 overview 사용량 card는 숨기고 command와 열린 tab 결과에 집중한다.
- Quick actions와 열린 tab을 한 입력으로 검색하며 `↑`/`↓`와 `Enter`를 지원한다.
- 최소 action은 New tab, New group, New note, Toggle sidebar다. Agent summary의
  `Review`는 Sidebar의 Agent activity로 이동한다.
- Address suggestions와 input/result token은 공유하지만 위치, elevation, 결과 model은
  명확히 분리한다.
- 두 overlay는 native `WebContents`와 Sidebar flyout보다 위에서 paint되고 hit test된다.

연결 계정 데이터는 마지막 동기화 시점을 함께 표시하고 계정 연결을 해제하면 즉시
제거한다. 경고 기준은 provider별 quota semantics를 따르며 prototype의 수치는 demo다.

#### Yee Hub geometry

| 항목 | 값 |
| --- | ---: |
| Width | 620 DIP, viewport edge 최소 24 DIP |
| Top | Title bar bottom + 14 DIP |
| Radius | 16 DIP |
| Search row | 52 DIP |
| Overview | 단일 outline surface 내부 2 columns; narrow viewport는 1 column |
| Agent / usage section | 최소 116 DIP, section 사이 1 DIP divider |
| Quick action grid | 4 columns, 6 DIP gap; 700 DIP 미만 viewport는 2 columns |
| Quick action | 최소 58 DIP, title 중심이며 description은 시각적으로 숨긴다. |
| Open tab result | 최소 40 DIP |
| Workspace dim | `rgba(20,29,27,.06)` |
| Elevation | `0 22 56 / 18%`, `0 3 10 / 8%` 상당 |

### Overlay and focus order

Prototype의 paint order 기준은 Workspace 1, dim 12, Title bar 20, Launcher 40,
Site info 55다. native 구현에서 숫자를 그대로 복사할 필요는 없지만 다음 관계는
반드시 지킨다.

```text
WebContents < Sidebar/Workspace < dim < Title bar < Launcher < Site info
```

Popup open 시 focus는 search/input 또는 첫 actionable row로 이동한다. `Escape`는
popup을 닫고 해당 trigger로 focus를 복원한다. Pointer outside click도 닫지만 underlying
page action을 두 번 실행하지 않는다.

## 8. Chromium 구현 경계

| 제품 개념 | Chromium 구현 대상 |
| --- | --- |
| Toolbar / Omnibox | `ToolbarView`와 native LocationBar |
| Site info | LocationBar page action + `PageInfoBubbleView` |
| Bookmark page | native bookmark star + `BookmarkModel` / edit bubble |
| Yee Hub entry | Yee `ToolbarButton`, native accelerator `Ctrl/⌘ K` |
| Address suggestions | native Omnibox edit model and popup contents |
| Yee Hub | Yee-owned bubble/widget; native focus and accelerator system |
| Open tabs | native vertical tab view + `TabStripModel` |
| Page surface | `MultiContentsView` / `WebContents` holders |
| Caption controls | 플랫폼 native frame/container |
| Pinned Sidebar geometry | `BrowserView` proposed layout의 단일 owner |
| Favorites / Bookmarks / Groups | Favorites는 실제 pinned tabs, Bookmarks는 실제 manager entry, Groups는 `TabStripModel`의 tab group |
| Agent activity | Toolbar status View + Sidebar task model contract |
| Tenant / Workspace | Sidebar footer View; Title bar에 중복 금지 |

레이아웃 계산은 한 곳에서만 소유한다. Title bar가 nominal sidebar width를 다시
계산하거나, content가 layout 후 수동 이동되는 구현은 금지한다. 최종 proposed
bounds가 Sidebar, gutter, `MultiContentsView`, renderer viewport를 함께 결정한다.

### Prototype-to-native token map

| Product token | Prototype | Native requirement |
| --- | --- | --- |
| `content_gutter` | CSS `shell_inset`, 6px | `yee::kSidebarMetrics.content_gutter` |
| `chrome_surface_radius` | OS CSS variable | platform-specific LayoutProvider token |
| `chrome_line` | OS CSS variable | theme/system derived separator color |
| Omnibox anatomy | CSS grid | LocationBar child Views/FlexLayout |
| Yee Hub action model | DOM dataset | typed command/action model, not strings |
| Popup state | `hidden`, `aria-expanded` | Widget/Bubble lifetime + AX state |

CSS selector와 DOM dataset은 시각 prototype의 도구일 뿐 native architecture가 아니다.
native 구현은 Chromium의 model, accelerator, theme, focus manager와 accessibility tree를
사용한다.

## 9. 구현 완료 체크리스트

각 native 변경 PR에서 아래 항목을 확인한다.

- [ ] Windows, macOS, Linux에서 Sidebar 폭과 내부 행 위치가 같다.
- [ ] Expanded 상태에서 Browser Surface Header와 Browser Content의 왼쪽 경계가 1 DIP 이내로 맞는다.
- [ ] Sidebar Toggle이 Browser Surface Header의 첫 컨트롤이고 New Tab·Agent는 Sidebar Header에 남는다.
- [ ] Toolbar와 Browser Content 사이에는 gap 없이 theme-derived 1 DIP separator만 보인다.
- [ ] Browser Surface의 상·우·하 및 Sidebar 쪽 외부 gutter가 6 DIP다.
- [ ] Browser Surface 외곽 네 모서리가 12 DIP이고 WebContents 하단 clip과 일치한다.
- [ ] 외곽 outline은 theme-derived 1 DIP이고 shadow는 얕은 1px/3px다.
- [ ] Caption controls가 content나 extension dock 위로 겹치지 않는다.
- [ ] 창 resize, maximize/restore, DPI 100/125/150/200%에서 정렬이 유지된다.
- [ ] Sidebar collapse/expand 중 page viewport가 panel과 함께 연속 resize된다.
- [ ] Hover flyout은 page viewport를 resize하지 않는다.
- [ ] Favorites, Bookmarks, Group add, Agent context의 keyboard/focus 동작을 확인한다.
- [ ] Omnibox click과 `Ctrl/⌘ L`이 Omnibox 폭의 Address suggestions를 연다.
- [ ] toolbar의 Yee button click과 `Ctrl/⌘ K`가 같은 Yee Hub를 열고 네 가지 quick action이 실행된다.
- [ ] 두 launcher overlay가 `WebContents` 뒤로 가려지지 않는다.
- [ ] Omnibox control 순서가 Site info → Address → Bookmark다.
- [ ] Rest 표시가 `origin | Page title` 위계를 가지며 선행 `www.`는 생략한다.
- [ ] CSS 문서 배경과 실제 상단 띠가 다른 페이지에서도 Header와 보이는 페이지
      상단 색이 이어지고, 다른 단색 section으로 스크롤하면 약 140ms sampling으로
      Header·주소·중립 버튼 색이 약 200ms 동안 함께 연속 전환되며 중간 frame에서
      계단식 변화나 깜빡임이 없다.
- [ ] Yee button은 findings count와 usage warning을 button bounds를 바꾸지 않고 표시한다.
- [ ] Yee button은 모든 폭에서 icon-only이며 accessible name과 OS별 shortcut tooltip을 유지한다.
- [ ] Yee Hub가 Agent summary와 계정별 remaining/reset/updated 값을 표시하고 quota를 합산하지 않는다.
- [ ] Site info가 실제 security, cookie, permission, certificate state를 사용한다.
- [ ] Bookmark 상태가 실제 `BookmarkModel`과 동기화되고 edit bubble을 연다.
- [ ] Address, Site info, Yee Hub popup은 동시에 두 개 이상 열리지 않는다.
- [ ] 각 popup을 `Escape`로 닫으면 원래 trigger로 focus가 복원된다.
- [ ] High contrast, reduced motion, screen reader name을 확인한다.
- [ ] 실제 native Omnibox와 real `WebContents`를 사용하며 imitation control이 없다.

### Visual parity evidence

Native 변경 PR에는 아래 캡처를 동일 window size로 첨부한다.

1. Resting Omnibox
2. Site info open
3. Bookmark on
4. Address suggestions open
5. Yee Hub open: Agent summary + connected usage
6. Yee Hub search results
7. Sidebar collapsed + Yee Hub open
8. OS별 Yee icon button과 badge/status dot 정렬

Windows는 100%, 125%, 150% DPI를 확인하고 macOS는 Retina/non-Retina logical bounds,
Linux는 100%, 200% scale을 확인한다. 주요 edge와 inset은 목표의 ±1 DIP까지 허용한다.

## 10. 프로토타입 확인 URL

```text
/prototype/?titlebar=regular&tenant=offset&sidebar=open&os=windows
/prototype/?titlebar=regular&tenant=offset&sidebar=open&os=mac
/prototype/?titlebar=regular&tenant=offset&sidebar=open&os=linux
```

프로토타입은 의도 확인용이다. native 구현 완료 판정은 위 체크리스트와 실제
Chromium 화면 캡처를 기준으로 한다.

## 11. 변경 관리

UI 결정을 변경할 때는 다음 순서를 따른다.

1. 이 문서의 원칙, token 또는 정보 구조를 먼저 갱신한다.
2. `prototype/`에서 Windows, macOS, Linux 변형을 함께 수정한다.
3. native Chromium overlay를 수정하고 실제 browser capture로 비교한다.
4. PR 설명에 9절 체크리스트 결과와 확인한 OS/DPI를 기록한다.

프로토타입과 native 구현이 즉시 같아질 수 없다면 임시 값을 조용히 남기지 않는다.
이 문서의 관련 표에 **현재 native 값**, **목표 값**, **후속 작업**을 함께 기록한다.
새로운 overlay patch는 이 사양서에 링크하고, 시각 클래스나 CSS selector가 아니라
제품 개념과 Chromium 구현 대상을 기준으로 설명한다.
