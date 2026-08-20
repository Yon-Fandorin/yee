# Groups

Group은 사용자가 Tab을 묶는 정리 UI다. Agent Task의 소유 단위가 아니고,
Tenant나 Workspace도 아니다.

## 헤더

세로 Sidebar에서는 가로 Chromium 그룹 헤더를 쓰지 않는다. 세로 헤더다.
높이는 Tab 행과 같은 32 DIP다. 스펙 heading은 30 DIP다. 지금은 32를 유지한다.

### 스펙이 정한 순서

1. disclosure
2. color mark
3. 이름
4. 개수
5. add action (`+`, hover/focus 시)

스펙에서 Group `+`는 Tab 또는 Note를 넣는다.

### 지금 native

1. color mark
2. 이름
3. ⋮ (그룹 에디터 버블)
4. 접기 아이콘 (trailing)

개수 라벨과 heading `+`는 아직 없다. 탭 추가는 에디터 버블 안 Chromium
명령이다. 스펙 `+`/개수/Note를 native 헤더에 얹기 전에 묻는다.

color mark는 브라우저 크롬에만 그린다. 페이지 위에 올리지 않는다.
8 DIP 원판은 접혀 있어도 크기가 같다.

마크 위에 그리는 것은 **브라우저 신호**뿐이다.

- 오디오 (탭 미디어 알림)
- 읽지 않음 점 (needs attention)
- 입력 필요 — 제품으로 쓸 브라우저 소스는 아직 없다. Agent waiting을
  여기 넣지 않는다.

Agent의 대기/접근 상태는 이 마크에 그리지 않는다.

헤더 모서리 8 DIP, mark 8 DIP, mark 슬롯 16 DIP.

## 목록

Group 아래 Tab은 일반 Tab 행과 같다. 접으면 그 Group의 Tab은 숨긴다.

드래그로 Group 헤더를 통째로 옮기는 것은 Chromium 그룹 드래그이고, 그 경우
Favorites 독으로 pin하지 않는다.

## 하지 않는 것

- Group을 Agent 컨테이너로 쓰기
- Group 이름을 Favorites 독 안에 넣기
- 스펙의 30 DIP heading을 native가 이미 다른 높이로 그린 채로, 문서만 조용히
  맞추거나 native를 예전 문단으로 되돌리기. 바꿀 거면 먼저 묻는다.
