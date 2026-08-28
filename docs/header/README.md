# Browser Surface Header regression guide

이 디렉터리는 단일 Browser Surface Header와 분할 Pane Header의 자동 회귀 범위를
정리한다. 제품 색상·배치 계약은
[`browser-shell-spec.md`](../browser-shell-spec.md)가 소유하고, 여기서는 그 계약을
어느 계층에서 검증하는지만 기록한다.

Chromium은 native Omnibox, page action, `WebContents`를 계속 소유한다. Yee 테스트는
이를 대체한 mock 모델을 만들지 않고 색상 정책의 순수 상태 전이와 실제 View
재호스팅을 나눠 검증한다.

## 검증 배치

```sh
# 색상 안정화, 테마 fallback, palette와 geometry 계약
./chromium-dev/test-header.sh unit

# 실제 native Omnibox의 단일/분할 재호스팅, focus와 popup anchor
./chromium-dev/test-header.sh interactive

# 빌드와 두 배치를 순서대로 실행
./chromium-dev/test-header.sh all
```

`interactive`와 `all`은 실행 중인 Yee에 정상 종료를 요청한 뒤 테스트 창을 연다.
이미 target을 빌드했다면 마지막에 `--no-build`를 붙일 수 있다. 전체 계약과 수동
검수 경계는 [test-coverage.md](./test-coverage.md)에 둔다.
