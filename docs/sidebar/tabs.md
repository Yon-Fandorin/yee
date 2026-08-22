# Tabs

Tabs는 URL과 `WebContents`에 연결된 실제 브라우저 탭이다. Favorite이 아닌
항목은 세로 목록으로 보인다.

## 행

현재 native 행 높이는 32 DIP다. 스펙은 40 DIP에 title과 hostname 두 줄이다.
지금은 native 32 DIP를 유지한다. 40 DIP로 올릴지는 스펙과 함께 결정하고
묻는다.

- 아이콘 16 DIP
- 좌우 padding 8 DIP
- 행 간격 2 DIP
- 한 줄 제목만 그린다. 긴 제목은 끝에서 페이드한다. hostname/URL은 행이
  아니라 hover card에 둔다.
- 목록 행의 활성/hover는 native 세로 탭 페인트다. Favorites 칸의 dimmed
  카드와 섞지 않는다. 스펙의 3×20 mint indicator를 넣을지는 묻는다.

`Tabs` 섹션 제목은 두지 않는다.

## 드래그

목록 안에서는 Chromium 세로 탭 드래그를 쓴다. 미리보기는 제목 행 모양으로
마우스를 따른다. 다른 행과 Group은 빈자리를 만들며 밀린다.

Favorites 독으로 올리면 [favorites-drag.md](./favorites-drag.md)를 따른다.
목록 컨테이너를 독 컨테이너로 갈아끼우지 않고, 드롭 시 pin한다.

## 분할 드래그

분할을 구성하는 두 Tab 중 한쪽을 Sidebar의 분할 묶음 밖으로 드래그해 일반
Tab 빈자리에 놓으면, 두 WebContents는 모두 유지하고 분할만 해제한다. 드롭한
Tab은 보였던 빈자리로 이동하고 다른 한쪽은 기존 분할 위치에 일반 Tab으로
남는다. Group 내부 빈자리에 놓으면 분할을 해제한 뒤 해당 Tab만 Group 멤버가
된다.

드래그 중에는 분할 전체가 아니라 집은 한쪽 Tab만 따라가고, 분할 묶음 밖의
유효한 빈자리에는 `분할에서 분리` 상태를 단일 Tab 크기로 미리 보여 준다.
원래 분할 안으로 돌아오거나 드래그를 취소하면 순서·활성 화면·분할 비율을
포함한 기존 상태를 복원한다. 한쪽 Tab을 닫는 동작과 분할 해제를 같은 동작으로
취급하지 않는다.

분할 묶음 전체 이동은 개별 Tab 분리와 동일한 드래그 시작 영역을 공유하지
않는다. 전체 이동을 제공하려면 별도 묶음 affordance 또는 두 Tab 선택처럼
구분 가능한 시작 상태를 먼저 정한다. 화면의 divider를 끝으로 미는 동작은
크기 조절 의미를 유지하며 분할 해제로 사용하지 않는다.

### Native 제약 및 회귀 체크리스트

현재 Chromium `TabDragController`는 split 선택 모델로 두 Tab을 함께 끌고,
세로 Tab Strip의 두 child가 하나의 `SplitTabView` 안에 있어 개별 child drag와
직접 호환되지 않는다. 따라서 모델의 부분 이동이 split을 해제할 수 있다는
사실만으로 Sidebar UX가 지원된 것으로 보지 않는다.

- [ ] 분할의 왼쪽/오른쪽 어느 Tab에서 시작해도 집은 Tab 하나만 분리 대상으로
      판정한다.
- [ ] 분할 앞, 뒤, 목록 끝의 일반 빈자리에 놓으면 두 Tab을 보존한 채 분할만
      해제하고 미리보기와 같은 순서로 배치한다.
- [ ] 펼친 Group 내부에 놓으면 집은 Tab만 Group에 들어가며, 접힌 Group과의
      경계는 Group 밖 빈자리로 판정한다.
- [ ] 분리 미리보기 동안 주변 Tab과 Group이 단일 Tab 높이의 빈자리를 만들며
      밀린다.
- [ ] 취소하거나 원래 분할로 돌아오면 순서, 활성 pane, 분할 방향과 비율이
      바뀌지 않는다.
- [ ] 분할 묶음 전체 이동 affordance가 추가되면 개별 Tab 분리와 시각 및 hit
      target이 겹치지 않는다.
- [ ] 한쪽 Tab 닫기는 Tab을 실제로 닫고, `분할에서 분리` 드롭은 두 Tab을 모두
      유지한다.

### 분할뷰 표면 회귀 체크리스트

Yee shell은 Browser Surface 바깥 여백과 둥근 모서리를 이미 소유한다. 분할
상태에서도 이 표면은 하나로 이어져 보여야 하며, Chromium split pane이 별도의
흰색 카드 두 장이나 한 단계 더 안으로 들어간 중첩 surface처럼 보여서는 안
된다.

현재 native 레이아웃은 `MultiContentsView`에 좌우·하단 8 DIP split inset을
두고, 각 `ContentsContainerView`에도 outline을 위한 4 DIP padding과 1 DIP
border 공간을 추가한다. Yee의 기존 Browser Surface gutter 뒤에 이 두 단계가
연속 적용되는지 우선 확인한다. split inset으로 노출된
`MultiContentsBackgroundView`는 Chromium themed background를 사용하므로 Yee의
연속된 Browser Surface 색과 다르게 보일 수 있다.

- [ ] 분할 진입 전후 Browser Surface의 전체 배경색이 유지되고, pane 바깥이나
      사이에 흰색 또는 별도 theme 배경 띠가 생기지 않는다.
- [ ] 라이트·다크 테마와 활성·비활성 창에서 노출된 split 배경이 Yee shell의
      현재 Browser Surface 색을 따라 즉시 갱신된다.
- [ ] Yee가 소유한 바깥 gutter 외에 split 상태에서만 좌우·하단 여백이 한 단계
      더 생기지 않는다. pane 사이에는 의도한 divider 간격만 남긴다.
- [ ] 각 pane의 outline용 내부 padding이 필요하다면 바깥 gutter와 합산한 최종
      시각 여백을 하나의 계약으로 정하고, 동일 축에 두 번 더하지 않는다.
- [ ] 분할 진입·해제 및 비율 조절 중 좌우·하단 여백과 배경 띠가 튀거나 두께가
      바뀌지 않는다.
- [ ] 기본·활성·비활성·강조 상태에서 각 pane 외곽선의 위쪽을 포함한 네 변과
      네 모서리가 clip되지 않고 같은 두께로 보인다.
- [ ] Yee가 top separator를 숨긴 상태와 native의 조건부 top split inset 조합이
      위쪽 outline을 자르는지 실제 앱에서 확인한다.
- [ ] 외곽선 표현은 구현 전에 하나로 결정한다: 네 변 outline을 온전히
      유지하거나, pane 외곽선을 제거하고 active pane을 그림자로 강조한다.
      잘린 outline 위에 그림자를 중복해서 사용하지 않는다.
- [ ] 그림자를 선택하면 pane 경계와 active pane이 라이트·다크 테마 모두에서
      구분되고, 그림자가 parent clip에 잘리지 않도록 paint/clip 영역을 확보한다.
- [ ] 작은 창, fullscreen, infobar 표시, side panel 열림 상태에서도 배경 연속성,
      최종 gutter, 위쪽 outline 또는 그림자 계약이 동일하게 유지된다.

## 하지 않는 것

- Tab 목록을 Favorites와 같은 아이콘 그리드로 그리기
- 별도 탭 모델로 Sidebar를 다시 짜기
- 실행 URL을 만들어 빈 탭을 띄우기
