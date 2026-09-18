# 테스트와 테스트용 자료

개발 도구의 테스트와 실제 브라우저에서 사용할 페이지·시나리오를 둔다.

- [`tooling/`](tooling/README.md): 브랜딩, 소스 복사 경로와 개발 명령 테스트
- [`fixtures/`](fixtures/README.md): 테스트용 페이지, Agent 시나리오와 콘텐츠 차단 입력

## 도구 테스트 실행

저장소 루트에서 실행한다.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

임시 파일과 가짜 빌드 도구를 사용하므로 Chromium을 빌드하거나 브라우저를 실행하지 않는다.
일부 브랜딩 테스트에는 기존 로컬 Chromium 소스의 도구가 필요하다.
없으면 해당 테스트를 건너뛴다.

C++ 테스트는 구현 파일 옆에 둔다. 실제 UI 테스트와 일부 CLI/MCP 테스트 명령은
[`tools/dev/`](../tools/dev/README.md)에 있다.
앱 빌드와 실제 화면 확인은 [`AGENTS.md`](../AGENTS.md)의 절차를 따른다.
