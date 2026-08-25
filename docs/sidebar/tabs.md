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
상태는 일반 한 탭 상태와 구분되는 별도 presentation이다. 주소 표시줄 아래에
불투명한 중립 그레이 canvas를 놓고, 두 `ContentsContainerView`를 각각 네
모서리가 둥근 카드로 배치한다. canvas는 theme에서 계산하되 page 표면 색이나
바탕 화면을 비치지 않는다. 이 불투명 배경이 카드의 잘린 모서리, 바깥 inset,
두 pane 사이 resize 영역을 모두 채운다.

분할 중에는 공용 Browser Toolbar 행과 그 예약 높이를 제거하고 두 카드를 상단 6 DIP
Content Gutter까지 올린다. 두 카드는 각각 42 DIP 높이의 Pane Header를 내부 상단에
가진다. Chromium의 실제 native Omnibox는 하나만 유지하고 active pane의 Pane
Header로 재배치한다. active Header에는 Sidebar Toggle과 Back·Forward·Reload를,
inactive Header에는 Back·Forward·Reload를 호버와 무관하게 항상 표시한다. inactive
pane에는 favicon, `origin | title`, alert와 닫기 버튼을 담은 읽기 전용 주소 표면을
같은 위치와 높이로 표시한다. 이 표면을 클릭하면 해당 pane을 활성화하고 native
Omnibox로 한 번에 focus를 넘긴다. native Omnibox나 주소 편집 model을 두 개 만들지
않는다.

각 42 DIP Pane Header 전체는 해당 WebContents의 page-aware 색을 독립적으로
따른다. Omnibox나 origin label의 안쪽 pill만 색칠하지 않는다. 따라서
서로 다른 두 페이지의 Header 색은 달라질 수 있다. active pane은
카드 전체의 얕은 그림자와 한 단계 진한 1 DIP 외곽선으로 구분하고, 주소 표면의
focus stroke는 실제 편집 중일 때만 표시한다. 분할 canvas 자체는 page 색과 관계없는
불투명 중립 그레이를 유지한다. 라이트 theme에서도 canvas가 흰 카드와 합쳐져
보이지 않을 정도로 밝아지지 않아야 하며, outline을 진하게 만들어 이를 보정하지
않는다. 공용 Browser Surface Header가 사라지는 분할 상태에서는 이 canvas가
Browser Surface의 상단까지 노출되므로 상단을 포함한 네 모서리를 12 DIP로
클리핑한다. 단일 Tab으로 돌아오면 Header 아래에 이어지는 기존 하단 두 모서리
클립만 복원한다. canvas 바깥에는 단일 Browser Surface와 같은 계산식의 1 DIP
outline과 낮은 two-stage shadow를 유지한다. 이 외곽 깊이는 전체 Browser Surface를
셸에서 분리하는 역할이고, 내부 active Pane shadow는 선택된 카드만 구분한다.

각 카드는 Yee의 6 DIP split inset 안에 놓고 12 DIP 곡률로 네 모서리를 모두
클리핑한다. 두 카드 사이에는 Chromium의 resize hit target과 divider를 그대로
유지하고 canvas가 그 간격에 보이게 한다. Chromium 기본 4 DIP padding + 1 DIP
일반 outline은 중복 적용하지 않는다.

Pane Header의 page-aware fill은 카드 외곽 좌표부터 칠하지 않는다. 1 DIP outline
안쪽의 x/y=1 DIP에서 시작하고 좌우도 1 DIP inset하며, 외곽 12 DIP에서 stroke를
제외한 11 DIP 상단 곡률로 hard clip한다. Header의 논리 영역은 42 DIP를 유지하고
WebContents가 시작되는 y=42 DIP 직전에 Browser Surface와 같은 계산식의 조용한
1 DIP separator를 그린다. 따라서 Header fill이 외곽선을 덮거나 상단 모서리로
튀어나오지 않고, 페이지와 Header가 선 하나로 이어진다.

곡률은 배경만 둥글게 칠하는 장식이 아니라 `WebContents` 합성 subtree의 실제
클립이다. 일반 한 탭 상태에서는 결합된 Browser Surface와 공유하는 하단 두
모서리를 hard clip한다. 분할 상태에서는 카드 outline이 네 모서리 12 DIP를
소유하고, WebContents는 42 DIP Pane Header 아래에서 시작해 하단 두 모서리를
11 DIP로 clip한다. 1 DIP 차이는 카드 외곽선을 위한 실제 내부 경계이며 중복
padding이 아니다. outline은 별도로 inset한 두 번째 곡선을 만들지 않고, 바깥
12 DIP clip과 안쪽 11 DIP clip 사이의 동일한 1 DIP 경계를 소유해야 한다.
renderer가 그리는 overlay scrollbar, DevTools, NTP footer,
immersive/actor/AI overlay와 enterprise watermark도 이 Browser Content 경계를
넘어가면 안 된다. native renderer host에는 rounded radius뿐 아니라 subtree
`masks-to-bounds`를 함께 적용한다. 고속 rounded-corner 경로는 단색 canvas,
scrim처럼 자식 합성 레이어가 없는 장식 표면에만 사용한다.

hard clip의 소유 범위는 Browser Content와 page overlay까지다. 카드 outline,
active shadow, Pane Header, close chip과 semantic highlight는 Yee chrome이므로
같은 clip 안에 넣어 모서리나 그림자를 잘라내지 않는다. 분할을 해제하면 inactive pane의
곡률과 clip을 비우고, active pane에는 일반 Browser Surface의 하단 clip을 즉시
복원한다.

메인 `ContentsWebView`는 자신의 backing layer와 실제 renderer를 호스팅하는
`NativeViewHost`에 한 번의 rounded-corner 계약을 함께 적용한다. 특히 macOS native
surface에서 바깥 backing만 hard clip하고 host를 fast clip으로 남기지 않는다.
DevTools·footer 같은 보조 WebView도 동일한 clip mode를 전달받는다.

일반 active pane은 레이아웃 크기를 바꾸지 않는 낮은 elevation 그림자로만
강조한다. Chromium의 permission/security attention처럼 의미가 있는 강조
상태에서는 native outline을 유지하고 일반 그림자는 끈다. 두 표현을 겹치지
않는다. 좌우 분할과 위아래 분할 모두 같은 카드, canvas, 강조 원칙을 적용한다.

분할 진입·해제 애니메이션 중 Chromium이 임시 inset을 다시 요청하더라도 Yee
경로에서는 같은 6 DIP를 유지한다. 이 규칙은 Chromium의 split 모델, resize
동작, semantic highlight를 바꾸지 않는 presentation 계약이다.

Chromium의 native mini toolbar가 가진 favicon, domain, alert와 닫기 기능은
Pane Header의 inactive 주소 표면으로 옮긴다. Yee Pane Header는 Chromium command를
호출하는 탐색 facade를 함께 배치하고 active일 때 native Omnibox를 같은 Flex 행에
host한다. 따라서 WebContents 우하단에 별도 rail이나 notch를 만들지 않으며
scrollbar와 닫기 affordance도 겹치지 않는다.
permission/security semantic highlight 중에는 native 동작대로 숨긴다.

- [ ] 분할 진입 시 두 카드 상단에 같은 42 DIP Pane Header와 34 DIP 주소 표면이
      나타나고 WebContents는 그 아래에서 시작한다.
- [ ] 공용 Browser Toolbar의 빈 상단 행이 분할에서 남지 않고 카드가 6 DIP 상단
      gutter에 맞닿는다. 일반 단일 Tab으로 돌아오면 기존 Toolbar 행과 inset이
      정확히 복원된다.
- [ ] active Header의 Sidebar Toggle·Back·Forward·Reload와 inactive Header의
      Back·Forward·Reload가 pointer hover 전에도 보이며 활성·비활성 상태가 해당
      WebContents의 탐색 기록을 따른다.
- [ ] active pane에는 실제 native Omnibox, inactive pane에는 favicon과
      `origin | title` 읽기 전용 표면이 보이며 inactive 표면 click은 pane 활성화와
      주소 편집을 한 번에 수행한다.
- [ ] 두 주소 표면은 각자의 WebContents 상단 색을 독립적으로 따라가고, page
      스크롤 중 색상 전환은 일반 단일 Tab과 같은 안정화·보간 규칙을 사용한다.
- [ ] page-aware 색이 Omnibox pill에만 머물지 않고 42 DIP Pane Header 전체를
      채우며 버튼과 텍스트 대비도 같은 surface에서 계산된다. Header 자식은 카드의
      상단 두 모서리 hard clip 밖으로 나오지 않는다.
- [ ] Pane Header fill이 1 DIP 카드 outline 안쪽에서 11 DIP 상단 곡률로 잘리고,
      밝고 어두운 page 색 모두에서 외곽선을 덮거나 모서리로 튀어나오지 않는다.
- [ ] Pane Header와 WebContents 사이에는 resolved Header surface에서 도출한 조용한
      1 DIP separator만 보이며 중복 gap이나 별도 pill 외곽선이 생기지 않는다.
- [ ] active pane의 카드 전체에만 강화된 1 DIP outline과 얕은 shadow가 보이며,
      주소 focus stroke는 실제 편집 중일 때만 추가된다.
- [ ] 라이트·다크 테마와 활성·비활성 창에서 split canvas가 완전히 불투명하고,
      shell 또는 바탕 화면이 카드 모서리와 pane 사이로 비치지 않는다. 라이트
      theme의 흰 카드와도 구분되는 중립 명도 차이가 유지된다.
- [ ] 공용 Header가 없는 분할 상태에서 split canvas의 상단 좌우도 Browser
      Surface와 같은 12 DIP로 잘리고, 단일 Tab 복귀 시 상단에 중복 곡률이나
      빈 틈 없이 기존 하단-only 클립으로 돌아간다.
- [ ] 단일 Browser Surface와 split canvas 바깥에 같은 surface-derived 1 DIP
      outline과 낮은 two-stage shadow가 보이며, active Pane shadow와 겹쳐 두꺼운
      테두리나 이중 카드처럼 보이지 않는다.
- [ ] 각 카드 바깥에 6 DIP inset이 네 변 모두 유지되고, pane 사이에는 native
      resize 영역만 남는다.
- [ ] 각 pane에 일반 상태용 내부 outline/padding이 다시 생기지 않고, 바깥
      split card inset과 동일 축에 여백을 두 번 더하지 않는다.
- [ ] 분할 진입·해제 및 비율 조절 중 네 변 inset과 canvas가 튀거나 두께가
      바뀌지 않는다.
- [ ] 두 카드의 네 모서리 모두 12 DIP로 보이고, 특히 하단 좌우가 사각형으로
      돌아가거나 page 배경이 카드 밖으로 넘치지 않는다.
- [ ] 스크롤 가능한 일반 한 탭에서 우측 scrollbar와 하단 scrollbar corner가
      Browser Surface의 하단 12 DIP 곡률 또는 Content Gutter로 뚫고 나오지 않는다.
- [ ] 좌우·위아래 분할의 두 pane을 각각 스크롤해도 overlay scrollbar가 네 모서리,
      split canvas 또는 중앙 resize 영역 위로 나오지 않는다.
- [ ] DevTools를 좌·우·하단에 dock하거나 NTP footer, immersive reading, actor/AI,
      enterprise watermark overlay가 표시돼도 해당 pane의 최종 곡률을 공유한다.
- [ ] 분할 진입·해제와 일반 Tab 전환 뒤 inactive WebContents의 이전 hard clip이
      남지 않고, active WebContents가 일반 하단 곡률과 분할 네 모서리 사이를 한 번만
      전환한다.
- [ ] 기본·비활성 pane에는 과한 외곽선이 없고, active pane의 그림자는 네 변과
      네 모서리에서 clip되지 않는다.
- [ ] permission/security attention이 활성화되면 일반 그림자가 사라지고 native
      semantic outline만 온전히 보인다. 강조 해제 시 일반 active 그림자로
      돌아온다.
- [ ] 좌우·위아래 분할 모두 각 pane의 divider 쪽을 포함한 네 모서리가 같은
      카드 곡률을 유지한다.
- [ ] active pane 그림자는 라이트·다크 테마 모두에서 pane 경계를 구분하되
      별도 카드처럼 보일 정도로 강하지 않다.
- [ ] pane을 전환하면 native Omnibox와 inactive 주소 표면이 같은 bounds에서
      교대하고, 높이 변화·깜빡임·이전 outline 조각이 남지 않는다.
- [ ] active pane에서 Address suggestions를 열면 단일 Tab과 같은 연결형 popup이
      실제 Omnibox outer bounds의 좌우 2 DIP 범위에 맞춰 나타난다. popup이 Pane
      전체 폭의 별도 sheet로 늘어나거나 Pane Header를 불투명한 빈 영역으로 덮지
      않는다.
- [ ] Address suggestions가 열린 동안 native Omnibox, 탐색 control과 close chip이
      계속 보이고 일반 active card의 조용한 outline·shadow가 유지된다. 이 상태를
      permission/security attention으로 취급해 semantic highlight 색을 Pane 전체에
      표시하지 않는다.
- [ ] 일반 URL, 긴 domain, NTP, file/blob URL과 alert 표시에서 Pane Header의
      favicon, origin/title 생략·말줄임, alert, 닫기 버튼이 겹치거나 카드 밖으로
      나가지 않는다.
- [ ] close chip의 tooltip·접근성 이름과 semantic highlight 중 숨김 동작은
      Chromium 기본 기능을 그대로 유지한다.
- [ ] 분할 해제와 창 종료 전에 native Omnibox가 `ToolbarView`로 복귀하고,
      suggestions anchor와 `Ctrl/⌘ L` focus가 정상 동작한다.
- [ ] 작은 창, fullscreen, infobar 표시, side panel 열림 상태에서도 불투명
      canvas, 최종 inset, 네 모서리 곡률과 그림자 계약이 동일하게 유지된다.
- [ ] Sidebar pinned 상태를 숨기거나 다시 표시할 때 split WebContents가 매 frame
      재배치되지 않고 최종 폭으로 clip되어, 두 pane과 divider가 끊김 없이 움직인다.

## 하지 않는 것

- Tab 목록을 Favorites와 같은 아이콘 그리드로 그리기
- 별도 탭 모델로 Sidebar를 다시 짜기
- 실행 URL을 만들어 빈 탭을 띄우기
