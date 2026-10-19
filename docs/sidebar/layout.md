# Layout

Tab Sidebar는 Sidebar Column 안에서 위에서 아래로 쌓인다. 폭·섹션 리듬은 OS가
같아도 되고, native 체크포인트가 스펙 문단과 이미 다른 곳이 있다.

## 섹션 순서

스펙이 정한 열 계약은 다음 순서를 유지한다. 예약 슬롯은 아직 공간을
차지하지 않는다.

| 순서 | 슬롯 | 지금 화면에 |
| --- | --- | --- |
| — | Pins | 안 보임. Arc pinned tabs 예약. Favorites가 아님 |
| 1 | Favorites | Favorite이 있으면 보임. 0개면 Tab 드래그 중에만 빈 드롭존 표시 (`kPins` 슬롯이 아님) |
| 2 | Bookmarks | 안 보임. 예약 |
| 3–4 | Groups + Tabs | 보임. **한 목록**. 그룹과 비그룹 탭이 모델 순서로 섞인다 |
| 5 | Agent activity | 안 보임. 체크포인트에서 제외 |
| 6 | Chat / Agent History | 안 보임. 예약 |
| footer | Context Switcher | 보임. Tenant / Workspace / Account, 선별된 Browser tools, Yee Workspace 기능 진입점 |

지금 펼친 Sidebar에 실제로 쌓이는 것은 **Favorite이 있을 때의 Favorites 독,
그 아래 하나의 unpinned 목록, 맨 아래 Context Switcher Footer**다. Groups 블록을
Tabs 위에 따로 모으지 않는다. Favorite이 0개인 유휴 상태에서는 목록과 Footer만
쌓인다.
Bookmarks를 독과 목록 사이에 끼워 넣지 않는다.

`Tabs` 같은 중복 섹션 제목은 쓰지 않는다. 화면 자체가 tab sidebar다.
Favorites에도 `FAVORITES` 라벨을 두지 않는다. 아이콘 독이 곧 그 영역이다.

Favorites와 Tab 목록 사이에 1px 구분선을 두지 않는다. 독 칸의 dimmed 카드가
영역을 나눈다.

New Tab과 Agent Control은 Sidebar Header가 소유하며 Sidebar 본문에 중복으로
두지 않는다. 고정 Sidebar에서는 두 컨트롤을 창 버튼과 같은 높이에 두고,
hover 패널에서는 툴바 아래의 패널 첫 줄에 둔다. 두 컨트롤은 오른쪽 조작 열에서
하나의 묶음으로 정렬한다. Sidebar Toggle은 Browser
Surface Header의 첫 컨트롤로 옮긴다. 이 묶음은 `ToolbarView`가 아니라 Sidebar
Header가 한 번만 소유한다. 따라서 분할 상태에서 공용 Toolbar가 사라지고 native
Omnibox가 active Pane Header로 이동해도 New Tab과 Agent는 펼친 Sidebar에 그대로
남고 각 pane에 복제되지 않는다.

## 세 상태

| 상태 | 폭 | 페이지 |
| --- | --- | --- |
| Pinned Sidebar | 244 DIP + content gutter 6 DIP | 페이지가 폭을 내준다 |
| Collapsed Edge Rail | 8 DIP 가장자리 target + gutter 6 DIP | 남은 폭은 Browser Content |
| Sidebar Flyout | 펼친 표현이 페이지 위에 뜬다 | 페이지 폭을 바꾸지 않는다 |

접힌 8 DIP는 Chromium 기본 56 DIP 아이콘 레일이 아니다. 펼쳐 고정했거나
hover로 드러난 동안에는 펼친 표현 계약을 쓴다 (`UsesExpandedSidebarPresentation`).

Sidebar Flyout은 페이지 위에 뜨는 독립 패널이다. 페이지 폭을 바꾸지 않는다.
창 버튼이 있는 48 DIP Title Bar 아래에서 6 DIP 떨어져 시작한다. 왼쪽·아래는
12 DIP 안쪽으로 떠서 네 모서리에 Browser Content와 같은 12 DIP 반지름을 쓴다.
접힌 Edge Rail도 Title Bar 아래에서 시작하며 창 버튼 영역은 hover 대상에서 제외한다.
전체 화면에서 Toolbar가 숨겨지면 상단 외곽도 12 DIP를 사용한다.
macOS immersive 전체화면에서도 일반 창의 Sidebar Column·Content Column과
하나의 Browser Surface를 유지한다. native overlay 창은 Sidebar Header와 Toolbar의 표시·입력을
함께 소유한다. Toolbar 항상 표시 상태에서는 본문이 상단 카드의 바탕·윤곽을
이어 받고 Header 행을 중복 예약하지 않는다. 숨김 상태에서 잠시 드러나는 Toolbar는
본문 위에 겹친다. Sidebar hover 패널에서는 같은 Header를 패널 안으로 옮기며,
전체화면 해제 시 기존 Header parent와 합성 순서를 복구한다. 분할에서는 각 Pane Header가
주소줄을 소유하므로 빈 상단 native 행을 예약하지 않고 팝업도 본문 창에 연결한다.
Edge Rail과 플라이아웃 사이의 이동 통로는 하나의 hover 대상이다.
목록의 행 높이·아이콘·좌우 inset과 Header·Footer는 고정 Sidebar와 같다.
임시 패널은 배경 흐림 없이 불투명한 native 바탕에 프레임색을 은은하게 남긴다.
native 모서리를 따라 이어지는 한 겹의 가는 중성색 윤곽과 부드러운 그림자로 고정
Sidebar보다 위에 뜬 면을 구분한다. 윤곽은 native 중성 Outline 색을 사용해 밝고 어두운
배경을 따른다. 고정과 hover는 같은 글자 역할을 쓰고 대비를 유지한다.
Header의 Create 메뉴와 Footer 팝업을 조작하는 동안에는 현재 펼침 상태를 유지한다.
팝업이 닫히면 원래 hover·포커스 조건으로 돌아간다. hover에서 고정으로 전환하거나
도중에 접힘으로 되돌려도 폭과 불투명도가 중간 프레임에서 갑자기 바뀌지 않는다.

## 스펙과 native

스펙은 제품 문서다. 프로토타입 CSS는 제품 문서가 아니다. native 체크포인트는
불완전하고 이미 스펙과 다른 곳이 있다.

| 항목 | 스펙 | 현재 native |
| --- | --- | --- |
| Tab 행 높이 | 40 DIP, title + hostname | 32 DIP, 한 줄 |
| Favorites | 4열 quick-launch grid | Chromium pin 아이콘 독, 최대 4열 |
| Group heading | 30 DIP | 세로 헤더 + color mark |
| Agent activity | Sidebar 섹션 | 체크포인트에서 제외 |
| Collapsed rail | Edge target | 8 DIP, 56 DIP 레일 아님 |

현재 native UI와 사용자의 마지막 요청을 맞춘다. 스펙과 싸우는 변경은 먼저
말하고 묻는다. 스펙을 조용히 고치지 않고, native를 예전 스펙 문단으로 조용히
되돌리지도 않는다.

## 시각 계약

치수의 단일 출처는 `yee::kSidebarMetrics`다. 같은 값을 Chromium layout
constant에 다시 적지 않는다.

자주 쓰는 현재 값:

- Sidebar 펼침 폭 244, 접힘 8
- 섹션 좌우 inset 8
- Favorites 독과 Tab 목록 사이 8 (칸 간격과 같음). Favorite이 0개면 평소에는
  독 공간도 없고, Tab 드래그 중 나타나는 빈 드롭존 뒤에만 섹션 간격 10을
  더한다
- Expanded Sidebar의 Tab ScrollView는 마지막 행에서 끝나지 않고 남은 세로
  공간 전체를 채운다. 마지막 Tab 아래의 빈 공간도 목록 끝 드롭 타깃이다.
- Tab 행 32, 행 간격 2, 아이콘 16, Group 헤더 높이도 32
- Tab 행 곡률은 6 DIP이고, 평평한 선택 면은 지속 Favorite 카드보다 옅게 표시한다.
  Header의 Create·Agent는 오른쪽에 한 묶음으로 두며 Agent·Tab 닫기·Footer
  disclosure의 중심은 같은 열이다. Footer와 Tab 제목의 시작 열도 같다.
- hover 패널은 상단 54 DIP, 가로·하단 12 DIP 외곽을 쓴다. 본문 카드는 원래
  윤곽과 그림자를 유지하고 불투명 패널이 더 높은 native layer에서 겹친 부분을
  가린다. 접힌 8 DIP 가장자리 target도 Title Bar 아래에 둔다. Header는42 DIP로
  줄여 Create·Agent를 패널 안에서 위아래6 DIP 간격으로 배치한다. 첫 Tab과
  조작의 화면 기준 높이는 고정 상태보다48 DIP 내려간다. 고정/hover 두 전환
  트랙에서 위치와 높이를 연속 계산하고 창 버튼 영역까지 hit target을 넓히지 않는다.
- Favorites 칸 최소 32, 상하 padding 8, 칸 간격 8, 독 좌우 inset 8 / 상단
  inset 0. Favorite 독은 Sidebar Header 바로 아래에서 시작하고 Favorite이 없는
  평상시 Tab 목록만 상단 8 DIP 여백을 유지한다. Favorite과 Tab 행의 좌우
  정렬선은 같다.
- Footer 지속 행은 50 DIP이고 Sidebar 좌우 inset 8을 공유한다. Tab ScrollView는
  Footer 위까지의 남은 공간을 채우며 Footer를 덮지 않는다. 계정 표식은 20 DIP다.
- Sidebar 분할바는 10 DIP resize target과 4 × 24 DIP 손잡이를 사용한다.
  손잡이는 Tab 행의 실제 trailing 경계와 Browser Surface 사이에 중앙 정렬한다.
  Tab 행 inset 8 DIP와 Content Gutter 6 DIP를 포함해 양옆 간격은 각각 5 DIP다.
- Sidebar 손잡이는 평소에는 숨기고 경계 hover, 드래그, 키보드 focus에서
  표시한다. 멀티탭 분할바는 기존 dimmed 중앙 표시를 유지한다.
