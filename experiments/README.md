# 화면과 동작 실험

[`shell-prototype/`](shell-prototype/README.md)에 HTML·CSS·JavaScript로 만든 화면을 둔다.
브라우저 배치와 간단한 동작을 빠르게 비교하는 용도다.
제품 명세와 실제 앱 테스트는 별도로 확인해야 한다.

## 화면 열기

저장소 루트에서 실행한다.

```sh
python3 -m http.server 4173 --bind 127.0.0.1
```

`http://127.0.0.1:4173/experiments/shell-prototype/`을 연다.
설치된 Chrome으로 실제 탭을 확인하는 환경은
[`native-pilot/`](../native-pilot/README.md)에 있다.
