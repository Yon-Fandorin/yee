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

## 하지 않는 것

- Tab 목록을 Favorites와 같은 아이콘 그리드로 그리기
- 별도 탭 모델로 Sidebar를 다시 짜기
- 실행 URL을 만들어 빈 탭을 띄우기
