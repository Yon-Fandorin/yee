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
| [footer.md](./footer.md) | Context Switcher, 선별된 Browser tools, Yee Workspace 기능의 통합 Footer 흐름 |
| [test-coverage.md](./test-coverage.md) | 자동 회귀 범위와 실제 화면 검수의 경계 |
| [dark-theme-audit.md](./dark-theme-audit.md) | 다크 Sidebar 표면 계층 감사와 검증 근거 |

## 검증 배치

문서의 동작 규칙을 한 종류의 테스트에 몰아넣지 않는다. 구현과 같은 변경에서
아래 가장 낮은 계층에 회귀 근거를 둔다.

| 규칙 | 검증 위치 |
| --- | --- |
| 용량, pin/unpin 의도, 레이아웃·삽입·hit geometry, RTL | `favorites_unittest.cc` 같은 Yee 유닛 테스트 |
| 실제 View 배치·`TabStripModel` 순서·Group 소속·취소 복원 | Chromium `interactive_ui_tests` |
| 색 대비, 클리핑, hover·drag motion의 시각적 연속성 | 각 문서의 Native 회귀 체크리스트를 실제 Yee 앱에서 검수 |

새 규칙이 순수 입력과 출력으로 표현되면 유닛 테스트를 함께 추가한다. 실제
Chromium View 생명주기나 모델 커밋이 핵심이면 억지로 mock 유닛 테스트를 만들지
않고 interactive browser test를 둔다. 체감 모션처럼 픽셀과 시간의 조합이 핵심인
항목은 자동화 가능한 불변 조건만 테스트하고 최종 시각 검수 항목을 유지한다.

저장소 루트에서 아래 배치로 같은 범위를 반복 실행한다.

```sh
# 브라우저 창을 열지 않는 정책·geometry·View 유닛 테스트
# (native View 테스트이므로 macOS GUI 세션 접근은 필요)
./chromium-dev/test-sidebar.sh unit

# 실제 Yee 창의 Sidebar 배치·drag·Group·scroll 통합 테스트
./chromium-dev/test-sidebar.sh interactive

# 빌드와 두 배치를 순서대로 실행
./chromium-dev/test-sidebar.sh all
```

`interactive`와 `all`은 기존 Yee 프로세스에 정상 종료를 요청한 뒤 새 테스트 창을
열기 때문에 실행 중 포커스를 가져갈 수 있다. 이미 해당 target을 빌드했다면 마지막에
`--no-build`를 붙인다. native 체크리스트는 자동화가 놓치는 페이드의 리듬, 색의 체감
대비, 클리핑 가장자리처럼 실제 화면으로 판단해야 하는 항목만 남긴다.

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
