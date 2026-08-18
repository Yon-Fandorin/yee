# Yee browser shell UI specification

이 문서는 정적 프로토타입의 시각적 의도를 실제 Chromium Views/WebUI 구현으로
옮길 때 사용하는 기준 문서다. CSS의 현재 모습보다 이 문서의 **불변 조건**을
우선한다. 치수나 구조를 바꾸면 프로토타입과 native 구현을 함께 갱신한다.

레이아웃 요소와 제품 범위의 표준 명칭은
[`browser-shell-layout-glossary.md`](./browser-shell-layout-glossary.md)를 따른다.

## 1. 제품 원칙

1. OS는 창 프레임을 소유하고 Yee는 제품 UI를 소유한다.
2. OS별 차이는 Title bar 재질, Caption controls, 단축키 표기에 한정한다.
3. Sidebar의 폭, 정보 구조, 행 높이, 간격과 선택 상태는 모든 OS에서 같다.
4. Title bar, Sidebar, Browser Content 주변 gutter는 하나의 연속된 chrome 면이다.
5. 실제 웹 페이지와 선택된 tab만 chrome보다 밝은 표면으로 분리한다.
6. Chromium의 native Omnibox, `TabStripModel`, `WebContents`, page actions와
   caption hit testing을 대체하거나 흉내 내지 않는다.

## 2. 레이아웃 불변 조건

| 항목 | 기준 | 구현 메모 |
| --- | ---: | --- |
| Expanded sidebar | 244 DIP | prototype과 native shell이 같은 기본 폭 token을 사용한다. |
| Sidebar header | Expanded sidebar와 동일 | Shell Controls와 Tab Sidebar가 하나의 Sidebar Column 경계를 공유한다. |
| Browser content gutter | 6 DIP | 우·하 및 sidebar와 content 사이에 사용한다. 상단은 Omnibox 하단 inset과 공유한다. |
| Shell inset | 6 DIP | Omnibox의 상·하 여백과 Browser Content 외부 gutter가 같은 token을 사용한다. |
| Sidebar 내부 좌우 padding | 8 DIP | section 자체는 추가로 좌우 4 DIP inset을 사용할 수 있다. |
| Sidebar section gap | 10 DIP | Favorites, Bookmarks, Group, Agent 영역 사이의 기본 리듬이다. |
| Group heading | 30 DIP | disclosure, mark, title, count의 수직 중심을 공유한다. |
| Tab row | 40 DIP | title + hostname 두 줄, favicon tile 포함. |
| Agent task row | 최소 50 DIP | 일반 tab과 구분되는 실행 상태 카드다. |
| Chrome surface radius | Windows 8 / macOS 10 / Linux 9 DIP | Omnibox와 Browser Content가 같은 OS별 radius를 사용한다. |
| Active indicator | 3 × 20 DIP | tab 왼쪽, 행 중앙 정렬. |

### 공통 수직 기준선

Sidebar가 펼쳐져 있을 때 Omnibox의 왼쪽 경계와 Browser Content의 왼쪽 경계는
항상 같은 x 좌표여야 한다.

```text
sidebar_column_width = sidebar_width
sidebar_header_width = sidebar_column_width
tab_sidebar_width = sidebar_column_width
content_column_start_x = window_left + sidebar_column_width
content_start_x = content_column_start_x + content_gutter
browser_toolbar_start_x = content_start_x
omnibox_start_x = browser_toolbar_start_x
                 + navigation_controls_width + navigation_omnibox_gap
```

Sidebar Toggle, New Tab과 Agent Control은 Sidebar Header의 `Shell Controls`에
배치한다. Back, Forward와 Reload는 Content Column의 Browser Toolbar에 배치한다.
창 크기나 OS Window Controls 때문에 공간이 부족하면 Shell Controls의 크기나
간격을 줄인다. Navigation Controls의 시작점을 오른쪽으로 밀어 Sidebar Column
경계를 깨지 않는다.

Sidebar가 닫히면 고정 기준선은 해제하고 Omnibox와 Browser Content가 남은 폭을
사용한다. Hover flyout은 페이지 폭을 변경하지 않는다.

### 공통 inset

Omnibox와 Browser Content는 동일한 `shell_inset`을 사용한다.

```text
shell_inset = 6 DIP
omnibox_height = titlebar_height - (shell_inset × 2)
content_margin = 0 shell_inset shell_inset
```

따라서 regular Title bar에서 Omnibox 높이는 Windows 36 DIP, macOS 40 DIP,
Linux 38 DIP다. Title bar 높이가 바뀌더라도 상·하 inset을 별도 상수로 복제하지
않고 같은 계산식을 사용한다.

Omnibox와 Browser Content가 수직으로 맞닿는 구간에서는 두 개의 6 DIP 여백을
더하지 않는다. Content의 top margin은 0이고 Omnibox의 bottom inset 6 DIP를
두 영역의 공유 gap으로 사용한다. 따라서 실제 표면 사이 거리는 12 DIP가 아니라
6 DIP다. Content의 right, bottom, sidebar-side gutter는 계속 6 DIP다.

좌표가 같더라도 두 표면의 radius나 border 색이 다르면 곡선의 접점 때문에
시각적으로 어긋나 보인다. Omnibox와 Browser Content는 `chrome_surface_radius`와
`chrome_line`도 공유해야 한다.

## 3. 표면과 색상

각 OS는 하나의 `chrome_bg`를 가진다. Browser frame, Title bar, pinned Sidebar와
content gutter는 동일한 값을 사용한다. Title bar 아래에 border, shadow 또는
별도 tint를 추가해 수평 seam을 만들지 않는다.

| OS | `chrome_bg` | `chrome_line` | 특징 |
| --- | --- | --- | --- |
| Windows | `#f3f3f3` | `#dfdfdf` | Fluent neutral, 불투명 |
| macOS | `#eef1f0` 상당의 glass tint | `rgba(54,93,85,.10)` | frame 영역에만 blur/saturation 허용 |
| Linux | `#f1f0ef` | `#d8d6d4` | Adwaita 계열의 불투명 neutral |

- Web content backing: `#ffffff`
- Active tab: 약 88% white, 얕은 1px/3px shadow
- Windows restored window: visible outline 없이 DWM의 둥근 시스템 shadow를
  유지한다. 1px transparent top-frame extension은 shadow를 위한 합성 힌트일
  뿐이며 client surface 안에 검은 선이나 별도 inset을 만들지 않는다.
- Hover: 해당 OS chrome보다 한 단계만 밝거나 어둡게 한다.
- 구분선은 Sidebar 내부 section과 white content outline에만 사용한다.
- Title bar와 workspace 사이의 수평 구분선은 금지한다.

## 4. OS별 허용 차이

### Windows

- Title bar: 48 DIP
- Caption controls: 오른쪽, native 순서 Minimize → Maximize → Close
- 각 caption button: 46 DIP 폭, Windows snap layout과 resize hit test 보존
- Omnibox: 36 DIP 높이, 8 DIP radius
- 단축키: `Ctrl K`, `Ctrl T`

### macOS

- Title bar: 52 DIP
- Traffic lights: 왼쪽, Close → Minimize → Zoom
- Traffic lights는 Sidebar Header의 Window Controls Safe Area에 두고 Shell
  Controls와 겹치지 않는다.
- Leading actions는 필요하면 26 DIP까지 압축할 수 있다.
- Glass/blur는 browser chrome에만 적용하고 `WebContents`는 불투명하게 유지한다.
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
│ Site info │ Page title · origin        │ Bookmark │
└───────────────────────────────────────────────────┘
```

순서와 기능은 모든 OS에서 같다. RTL에서는 native platform 규칙에 따라 mirror할 수
있지만 논리적 focus order는 Site info → Address → Bookmark를 유지한다.

### Omnibox geometry

| 항목 | 값 | 비고 |
| --- | ---: | --- |
| Outer height | `titlebar_height - 12 DIP` | regular: Windows 36, macOS 40, Linux 38 |
| Outer padding | vertical 2 / horizontal 3 DIP | border 안쪽 |
| Internal gap | 1 DIP | 각 control 사이 |
| Site info column | 30 DIP | 실제 button은 28 × 28 DIP |
| Address column | fluid, 최소 120 DIP | 유일하게 줄어드는 column |
| Bookmark column | 30 DIP | 실제 button은 28 × 28 DIP |
| Inner control radius | outer radius - 3 DIP | Windows 5, macOS 7, Linux 6 |
| Icon | 15 × 15 DIP | 1.45 DIP stroke |
| Page title | 11 DIP / 600–680 | 한 줄 ellipsis |
| Origin | 8–8.5 DIP mono | title 오른쪽, 한 줄 ellipsis |

Omnibox outer bounds는 Browser Content의 왼쪽 시작점과 맞춘다. control을 추가하기
위해 outer bounds를 이동하지 않는다. 공간이 좁아지면 먼저 origin을 줄이고, 그 다음
page title을 ellipsis 처리한다. Site info와 Bookmark는 숨기지 않는다.

### Site info

- 왼쪽 첫 control이며 Chromium의 현재 page identity/security state를 사용한다.
- secure page는 tune/site-controls icon을 사용하고 accessible name에 현재 상태를
  포함한다. 예: `사이트 정보 보기, 연결이 안전함`.
- button: 28 × 28 DIP, hover/expanded에서 `chrome_hover` 배경.
- popover: 286 DIP 폭, Omnibox 아래 6 DIP, 10 DIP padding, 12 DIP radius.
- header: 34 DIP status tile + `Connection is secure` + origin.
- 최소 row: Cookies and site data, Site permissions, Certificate.
- prototype의 값은 demo data다. native 구현은 `PageInfoBubbleView`, identity model,
  permission and certificate state를 그대로 사용한다.
- security state를 Yee service에서 재계산하거나 문자열로 복제하지 않는다.

### Address target

- fluid column 전체가 Address suggestions trigger다.
- 기본 상태는 page title과 origin을 보여준다.
- click 또는 `Ctrl/⌘ L`에서 URL edit model로 전환하고 현재 URL을 전체 선택한다.
- hover에서만 약한 white tint를 주며 별도 border를 추가하지 않는다.
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
| Keyboard focus | 2 DIP brand focus ring | 2 DIP ring | 2 DIP ring |

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
- 패널의 left와 width는 실제 Omnibox bounds를 따른다.
- Omnibox 하단의 6 DIP shared gap 뒤에 연결하며 별도 화면 dim은 사용하지 않는다.
- 주소·검색 제안, 방문 기록, bookmark, 열린 tab을 탐색 대상으로 삼는다.
- `Escape`는 패널을 닫고 페이지 상태를 변경하지 않는다.

#### Address suggestions geometry

| 항목 | 값 |
| --- | ---: |
| Left / width | 실제 Omnibox outer bounds와 동일 |
| Top | Title bar bottom, 즉 Omnibox bottom 이후 6 DIP |
| Radius | top 0 / bottom `chrome_surface_radius` |
| Search row | 46 DIP |
| Result row | 최소 44 DIP |
| Results padding | 10 DIP |
| Elevation | `0 14 30 / 13%`, `0 2 7 / 7%` 상당 |
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
| `shell_inset` | CSS custom property, 6px | 6 DIP layout constant |
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
- [ ] Expanded 상태에서 Omnibox와 Browser Content 왼쪽 경계가 1 DIP 이내로 맞는다.
- [ ] Omnibox와 Browser Content 사이의 공유 gap이 6 DIP다.
- [ ] Content의 우·하 및 Sidebar 쪽 gutter가 6 DIP다.
- [ ] Omnibox의 상·하 inset과 Browser Content 외부 gutter가 모두 6 DIP다.
- [ ] Omnibox와 Browser Content의 radius와 border token이 같다.
- [ ] Title bar와 workspace 사이에 선, 다른 tint, 1 DIP seam이 없다.
- [ ] Sidebar와 gutter가 같은 `chrome_bg`를 사용한다.
- [ ] Caption controls가 content나 extension dock 위로 겹치지 않는다.
- [ ] 창 resize, maximize/restore, DPI 100/125/150/200%에서 정렬이 유지된다.
- [ ] Sidebar collapse/expand 중 page viewport가 panel과 함께 연속 resize된다.
- [ ] Hover flyout은 page viewport를 resize하지 않는다.
- [ ] Favorites, Bookmarks, Group add, Agent context의 keyboard/focus 동작을 확인한다.
- [ ] Omnibox click과 `Ctrl/⌘ L`이 Omnibox 폭의 Address suggestions를 연다.
- [ ] toolbar의 Yee button click과 `Ctrl/⌘ K`가 같은 Yee Hub를 열고 네 가지 quick action이 실행된다.
- [ ] 두 launcher overlay가 `WebContents` 뒤로 가려지지 않는다.
- [ ] Omnibox control 순서가 Site info → Address → Bookmark다.
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
