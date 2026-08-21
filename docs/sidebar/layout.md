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
| footer | Tenant / Workspace | 안 보임. 예약 |

지금 펼친 Sidebar에 실제로 쌓이는 것은 **Favorite이 있을 때의 Favorites 독,
그 아래 하나의 unpinned 목록**이다. Groups 블록을 Tabs 위에 따로 모으지
않는다. Favorite이 0개인 유휴 상태에서는 목록만 쌓인다.
Bookmarks를 독과 목록 사이에 끼워 넣지 않는다.

`Tabs` 같은 중복 섹션 제목은 쓰지 않는다. 화면 자체가 tab sidebar다.
Favorites에도 `FAVORITES` 라벨을 두지 않는다. 아이콘 독이 곧 그 영역이다.

Favorites와 Tab 목록 사이에 1px 구분선을 두지 않는다. 독 칸의 dimmed 카드가
영역을 나눈다.

Title Bar의 New Tab / Sidebar Toggle을 Sidebar 안쪽에 다시 두지 않는다.

## 세 상태

| 상태 | 폭 | 페이지 |
| --- | --- | --- |
| Pinned Sidebar | 244 DIP + content gutter 6 DIP | 페이지가 폭을 내준다 |
| Collapsed Edge Rail | 8 DIP 가장자리 target + gutter 6 DIP | 남은 폭은 Browser Content |
| Sidebar Flyout | 펼친 표현이 페이지 위에 뜬다 | 페이지 폭을 바꾸지 않는다 |

접힌 8 DIP는 Chromium 기본 56 DIP 아이콘 레일이 아니다. 펼쳐 고정했거나
hover로 드러난 동안에는 펼친 표현 계약을 쓴다 (`UsesExpandedSidebarPresentation`).

Sidebar Flyout은 페이지 위에 뜨는 유리 면이다. 페이지 폭을 바꾸지 않는다.
가장자리에서 안쪽으로 떠서 네 모서리가 둥글고, Browser Content 프레임보다
큰 반지름을 쓴다. Edge Rail과 플라이아웃은 하나의 hover 대상이다.

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
- Favorites 칸 최소 32, 상하 padding 8, 칸 간격 8, 독 inset 6
