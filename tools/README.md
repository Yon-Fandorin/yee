# 개발 도구

Chromium을 준비하고 제품 코드를 적용·빌드·실행하는 도구를 둔다.
브라우저 명령줄 도구(CLI), Agent 연동(MCP), 비교 실험 도구도 이곳에서 관리한다.

- [`dev/`](dev/README.md): 소스 준비, 빌드, 실행, 테스트와 Agent 연동
- [`overlay/`](overlay/README.md): 패치 적용, 소스 복사, 브랜딩 점검과 외부 라이브러리 갱신

명령은 저장소 루트에서 실행한다. 공통 코드는 각 디렉터리의 `lib/`에 둔다.
도구 테스트는 [`tests/tooling/`](../tests/tooling/README.md), 테스트용 페이지와
시나리오는 [`tests/fixtures/`](../tests/fixtures/README.md)에 있다.
일부 CLI/MCP 테스트 명령은 현재 `dev/`에 남아 있다.
전체 배치는 [프로젝트 구조](../docs/project-structure.md)를 참고한다.
