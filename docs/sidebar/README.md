# Tab Sidebar UX decisions

이 디렉터리는 Tab Sidebar의 **제품 UX/UI 결정**을 주제별로 둔다. 셸 전체의
불변 조건과 용어는 여기에 복제하지 않는다.

- 셸 불변 조건: [`../browser-shell-spec.md`](../browser-shell-spec.md)
- 표준 용어: [`../browser-shell-layout-glossary.md`](../browser-shell-layout-glossary.md)
- 구현 작업 규칙: [`../../AGENTS.md`](../../AGENTS.md)

수치는 이 문서의 문장보다 `yee::kSidebarMetrics`가 앞선다. 결정이 바뀌면 해당
주제 파일과 메트릭을 함께 고친다. 스펙과 충돌하면 조용히 스펙을 고치지 않고
먼저 묻는다.

## 문서

| 파일 | 다루는 결정 |
| --- | --- |
| [ownership.md](./ownership.md) | Yee 표현 vs Chromium 모델, Favorites의 pin 백킹, 예약 슬롯 |
| [layout.md](./layout.md) | 섹션 순서, 펼침/접힘, 스펙과 native 체크포인트의 차이 |
| [favorites.md](./favorites.md) | Favorites 독의 모양, 용량, 빈 상태 |
| [favorites-drag.md](./favorites-drag.md) | 드래그로 옮기기, 재배치, 영역 전환, 새 창 분리 |
| [groups.md](./groups.md) | Group 헤더, color mark, Agent와의 구분 |
| [tabs.md](./tabs.md) | Tab 행 표현, 세로 목록 드래그 |

## 아직 묻기

아래는 추측하지 않고 물을 질문이다.

- Arc Favorites와 Arc Pinned Tabs를 동시에 둘지. 지금은 Chromium pin = Favorites
  독이고, `kPins`는 비어 있는 예약 슬롯이다.
- Bookmarks, Chat, Agent History를 언제 켤지. Bookmarks는 스펙 2번째지만
  지금은 화면에 없다.
- Agent 상태를 Tab Sidebar에 어떻게 보여줄지. Group color mark에는 넣지 않는다.
- 스펙의 Tab 행 40 DIP로 native 32 DIP를 되돌릴지. hostname을 행에 둘지
  hover card에 둘지도 같이 정한다.
- Group 헤더 chrome: 스펙 `+`/개수 vs 지금 ⋮ 에디터. Note를 붙일지.
- Groups를 Favorites 아래 한 블록으로 모을지, 지금처럼 목록 순서에 섞을지.
- 스펙 3×20 mint 활성 표시 vs native 세로 탭 필.
- Split Favorite이 그 칸만 두 칸 폭인지.
- 드래그 중 나타나는 빈 Favorites 한글 카피를 잠글지.
- Title Bar Create 메뉴의 Chat vs 스펙 New note.
