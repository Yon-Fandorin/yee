# 개발 도구 테스트

브랜딩 설정과 적용, 소스 복사 경로, 개발 명령이 예상대로 동작하는지 확인한다.

## 실행

저장소 루트에서 실행한다.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

임시 파일, 임시 Git 저장소와 가짜 빌드 도구를 사용한다.
실제 Chromium 컴파일이나 브라우저 실행은 수행하지 않는다.
일부 브랜딩 테스트는 `.local-build/chromium/src`의 도구를 읽는다.
해당 소스가 없으면 테스트를 건너뛰므로 통과와 건너뜀을 구분해서 보고한다.

[`tools/dev/`](../../tools/dev/README.md)의 기존 브랜딩·앱 번들 테스트 명령도
이곳의 테스트를 실행한다. C++ 테스트는 구현 옆에 두며 실제 UI 테스트 절차는
[`AGENTS.md`](../../AGENTS.md)를 따른다.
