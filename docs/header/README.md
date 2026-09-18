# 주소·도구 영역 테스트

단일 화면의 Browser Surface Header와 분할 화면의 Pane Header를 테스트한다.
주소창의 배치, 색상, 입력 초점과 검색 결과 창의 위치를 확인한다.
제품의 색상·배치 규칙은 [셸 명세](../browser-shell-spec.md)를 따른다.

Chromium의 실제 Omnibox(주소창), 페이지 버튼과 `WebContents`를 사용한다.
색상 계산 테스트와 실제 UI를 옮겨 배치하는 테스트를 나눠 실행한다.

## 실행

저장소 루트에서 실행한다.

```sh
# 색상 안정화, 기본 테마와 크기·위치 규칙
./tools/dev/test-header.sh unit

# 단일·분할 화면의 실제 주소창 배치와 입력 초점
./tools/dev/test-header.sh interactive

# 빌드와 두 테스트를 순서대로 실행
./tools/dev/test-header.sh all
```

`interactive`와 `all`은 기존 개발 브라우저를 정상 종료한 뒤 테스트 창을 연다.
필요한 테스트 프로그램을 이미 빌드했다면 마지막에 `--no-build`를 붙인다.
자세한 테스트 항목과 수동 확인 범위는 [테스트 범위](./test-coverage.md)에 있다.
