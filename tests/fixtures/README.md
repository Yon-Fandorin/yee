# 테스트용 페이지와 시나리오

자동 테스트와 실제 브라우저 확인에 사용할 고정된 입력을 둔다.

| 파일·디렉터리 | 확인할 내용 |
| --- | --- |
| `theme-boundary-regression.html` | 테마와 Browser Surface 경계 |
| `agent-browser-prototype.html`, `agent-browser-form.html` | Agent의 페이지 관찰·입력·질문 |
| `scenarios/` | 비교 실험 시나리오와 준비된 프롬프트 |
| `content-blocking/fixture.html` | 요청·응답·CSS 차단 |

서버가 필요한 페이지는 해당 테스트 도구로 실행한다.
파일을 열어 본 것만으로 자동 테스트가 끝난 것은 아니다.

사용 방법은 [Agent 시나리오](../../docs/agent-browser-scenario-suite.md)와
[콘텐츠 차단 checkpoint](../../docs/content-blocking-checkpoint.md)에 있다.
새 빌드로 앱을 확인하기 전에는 [`AGENTS.md`](../../AGENTS.md)에 따라 기존 개발
브라우저를 정상 종료한 뒤 새 앱을 실행한다.
