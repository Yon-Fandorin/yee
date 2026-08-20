# Ownership

Tab Sidebar는 새 탭 모델이 아니다. Chromium이 탭의 존재를 소유하고, Yee가
사이드바에 보이는 방식을 소유한다.

## 모델은 Chromium

다음을 대체하거나 우회하지 않는다.

- `TabStripModel`
- `TabView`
- `WebContents`
- Omnibox
- page actions

사이드바를 새 트리로 다시 짜지 않는다. 탭을 만들고, 고정하고, 그룹하고, 닫는
권한은 Chromium에 남긴다.

## 표현은 Yee

제품 UI는 `chromium-overlay/yee-ui/`에 둔다. Chromium 원본 파일은 Yee 헤더를
포함하고 헬퍼를 호출하는 최소 접착만 가진다.

Yee 제품 정책은 `TabStripModel`에 넣지 않는다. 해당하는 예:

- Favorites 개수 상한
- 독 기하
- Group color mark 그리기

Views와 command glue는 Yee 헬퍼를 호출해도 된다.

치수는 `yee::kSidebarMetrics` 한곳이다. 같은 숫자를 Chromium layout constant에
복사하지 않는다.

## Favorites는 Chromium pin이다

자주 쓰는 사이트는 별도 URL 목록이 아니다. Chromium의 pinned tab이 Favorites
독으로 보인다.

- pin 등록 = Favorites에 넣기
- unpin = 독에서 Tab 목록으로 내리기
- 세션 복원과 API는 상한을 넘긴 pin을 복구할 수 있다. 상한은 사용자가 새로
  넣을 때만 막는다.

실행 URL을 지어내지 않는다. 탭이 이미 연 페이지가 곧 Favorite이다.

## 예약 슬롯

사이드바 열 계약은 위에서 아래로 고정한다. 아직 켜지 않은 슬롯은 자리를
차지하지 않는다. 나중에 끼워 넣지 않도록 이름만 잡아 둔다.

| 슬롯 | 상태 | 의미 |
| --- | --- | --- |
| Pins | 예약 | 코드 `kPins`. Arc 스타일 pinned tabs. 화면의 Favorites 독이 아니다. 켜기 전에 묻는다. |
| Favorites | 호스팅 | Chromium pin 컨테이너를 아이콘 독으로 보여 준다. 섹션 enum 이름이 아니다. |
| Bookmarks | 예약 | Bookmark manager 항목. 독이 아니다. |
| Groups | 호스팅 | `TabStripModel` tab group의 세로 헤더 |
| Tabs | 호스팅 | 고정되지 않은 브라우저 Tab 목록 |
| Chat | 예약 | 켜기 전에 묻는다. |
| Agent History | 예약 | Agent Activity와 혼동하지 않는다. 켜기 전에 묻는다. |

Pins를 켜는 것은 Favorites를 없애거나 옮기는 결정이다. 추측하지 않는다.
