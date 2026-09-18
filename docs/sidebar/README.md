# 사이드바의 제품 결정

Tab Sidebar의 배치와 동작을 주제별로 기록한다.
전체 화면의 규칙과 용어는 [셸 명세](../browser-shell-spec.md)와
[레이아웃 용어](../browser-shell-layout-glossary.md)를 따른다.
작업 절차는 [`AGENTS.md`](../../AGENTS.md)에 있다.

실제 구현의 치수는 `yee::kSidebarMetrics`를 기준으로 한다.
결정이 바뀌면 관련 문서와 공통 치수를 함께 수정한다.
제품 명세와 충돌할 때는 먼저 확인하고 임의로 명세를 바꾸지 않는다.

## 주제별 문서

| 문서 | 내용 |
| --- | --- |
| [ownership.md](./ownership.md) | 제품 UI와 Chromium 탭 모델의 역할, 고정 탭 연결과 예약 기능 |
| [layout.md](./layout.md) | 영역 순서, 펼침·접힘, 명세와 현재 구현의 차이 |
| [favorites.md](./favorites.md) | Favorites 영역의 모양, 용량과 빈 상태 |
| [favorites-drag.md](./favorites-drag.md) | 탭 이동·재배치, 영역 전환과 새 창 분리 |
| [groups.md](./groups.md) | 그룹 제목·색상 표시와 Agent 상태의 구분 |
| [tabs.md](./tabs.md) | 탭 행의 모양과 목록 드래그 |
| [footer.md](./footer.md) | 작업 공간 전환, 브라우저 도구와 하단 메뉴 |
| [test-coverage.md](./test-coverage.md) | 자동 테스트와 실제 화면 확인 범위 |
| [dark-theme-audit.md](./dark-theme-audit.md) | 어두운 테마의 사이드바 색상과 검토 결과 |

테마의 구조와 검증 범위는 [테마 구조 검토](../theming-structural-audit.md)를 참고한다.
중간 구현 보고서의 현재 결정은 위 주제별 문서와 테스트 범위에 합치고 제거했다.

## 테스트 방법

규칙에 맞는 테스트를 선택한다.

| 확인할 내용 | 방법 |
| --- | --- |
| 용량, 고정·해제, 크기·위치, 클릭·드래그 판정, 좌우 반전 | `favorites_unittest.cc` 같은 단위 테스트 |
| 실제 UI 배치, `TabStripModel` 순서, 그룹 소속과 취소 복원 | Chromium `interactive_ui_tests` |
| 색 대비, 화면 잘림과 애니메이션 | 실제 개발 브라우저의 화면 확인 |

입력과 결과만으로 확인할 수 있는 규칙은 단위 테스트로 검사한다.
실제 UI나 탭 상태의 변경이 중요하면 브라우저 UI 테스트를 사용한다.
색감과 움직임처럼 실제 화면이 필요한 항목은 수동 확인 목록을 유지한다.

저장소 루트에서 실행한다.

```sh
# 브라우저 창을 열지 않는 규칙·배치·UI 단위 테스트
# macOS 화면 세션 접근은 필요
./tools/dev/test-sidebar.sh unit

# 실제 창의 배치·드래그·그룹·스크롤 테스트
./tools/dev/test-sidebar.sh interactive

# 빌드와 두 테스트를 순서대로 실행
./tools/dev/test-sidebar.sh all
```

`interactive`와 `all`은 기존 개발 브라우저를 정상 종료한 뒤 테스트 창을 연다.
테스트 창이 입력 초점을 가져갈 수 있다.
필요한 프로그램을 이미 빌드했다면 마지막에 `--no-build`를 붙인다.

## 아직 결정하지 않은 항목

DIP는 화면 배율과 독립된 UI 크기 단위다.

- Arc Favorites와 Arc Pinned Tabs를 함께 제공할지:
  현재는 Chromium 고정 탭을 Favorites로 사용하며 `kPins`는 비어 있는 예약 영역이다.
- Bookmarks, Chat, Agent History를 언제 켤지:
  Bookmarks는 명세의 두 번째 영역이지만 현재 화면에는 없다.
- Agent 상태를 Sidebar에 어떻게 보여줄지:
  그룹 색상 표시에는 넣지 않는다.
- 탭 행을 현재 32 DIP에서 명세의 40 DIP로 바꿀지:
  호스트 이름을 행에 넣을지, 마우스를 올렸을 때 나오는 카드에 넣을지도 정해야 한다.
- 그룹 제목 영역에서 명세의 `+`·개수와 현재 ⋮ 편집 메뉴 중 무엇을 사용할지, Note를 붙일지
- 그룹을 Favorites 아래에 모을지, 현재처럼 탭 순서에 섞을지
- 명세의 3×20 민트색 활성 표시와 현재 세로 탭의 채워진 표시 중 무엇을 사용할지
- Split Favorite만 두 칸 너비를 사용할지
- Title Bar Create 메뉴에 Chat과 명세의 New note 중 무엇을 넣을지
