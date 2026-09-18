# 제품 문서

제품의 화면 배치, 기능 설계와 테스트 기록을 모은다.
디렉터리별 역할과 사용 방법은 각 README에 있다.

## 주제별 문서

| 주제 | 문서 |
| --- | --- |
| 파일 배치와 Chromium 적용 경로 | [프로젝트 구조](project-structure.md) |
| 화면 배치와 용어 | [셸 명세](browser-shell-spec.md), [레이아웃 용어](browser-shell-layout-glossary.md) |
| 사이드바의 제품 결정 | [Sidebar 문서](sidebar/README.md) |
| 주소·도구 영역 테스트 | [Header 문서](header/README.md) |
| 이름·로고 변경 | [제품 브랜딩](product-branding.md), [브랜딩 적용 범위](branding-coverage.md) |
| Brave에서 참고한 구조 | [Brave 브랜딩 분석](brave-branding-analysis.md) |
| 웹페이지 영역의 배치 | [레이아웃 설계](browser-surface-layout-design.md), [구조 검토](browser-surface-layout-design-audit.md) |
| Agent 구조와 사용 | [Agent 구조](agent-browser-architecture.md), [MCP 사용](agent-browser-mcp-usage.md), [검증 자료](agent-browser-validation/README.md), [UI 자동화](ui-automation-session.md) |
| 콘텐츠 차단 구현과 남은 작업 | [설계](content-blocking-design.md), [현재 checkpoint](content-blocking-checkpoint.md), [패키지·라이선스](content-blocking-private-core-filter-data.md) |

## 테스트 기록 읽기

[Browser Surface 기록](browser-surface-validation/README.md)과
[Agent 실행 기록](agent-browser-validation/README.md)은 당시 수행한 테스트의 근거다.
날짜, 통과한 범위와 확인하지 못한 부분을 함께 읽는다.
현재 코드의 결과를 확인하려면 해당 테스트를 다시 실행해야 한다.

제품 규칙은 명세와 주제별 결정 문서, 작업 절차는 [`AGENTS.md`](../AGENTS.md)를 따른다.
문서와 현재 구현이 충돌하면 사용자의 최신 요청을 확인하고 제품 명세를 임의로 바꾸지 않는다.
