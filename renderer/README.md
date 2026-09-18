# 웹페이지 처리 코드

웹페이지가 열릴 때 CSS와 스크립트를 적용하는 코드를 둔다.
현재 `content_blocking/`이 콘텐츠 차단을 위한 문서 처리를 담당한다.

| 파일 | 역할 |
| --- | --- |
| `document_filter_agent.*` | 페이지와 프레임의 상태에 맞춰 차단 기능 적용 |
| `generic_cosmetic.js` | 동적으로 추가·변경된 요소의 class/id 수집 |
| `youtube.js` | 확인된 YouTube 응답 데이터 처리 |
| `embed_scripts.py`, `BUILD.gn` | 스크립트를 빌드에 포함 |

공용 엔진과 설정은 [`components/`](../components/README.md),
네트워크 요청 처리는 [`browser/`](../browser/README.md)에 둔다.
페이지 처리와 Agent/MCP의 스크립트는 서로 다른 실행 환경을 사용한다.

`content_blocking/`은 Chromium의 `chrome/renderer/yee_content_blocking/`에 복사된다.
경로는 [`build/overlay.json`](../build/overlay.json)에서 관리하며 이 README는 복사하지 않는다.

실제 웹사이트의 차단 동작과 YouTube 재생은 별도의 앱 테스트로 확인해야 한다.
[콘텐츠 차단 checkpoint](../docs/content-blocking-checkpoint.md)와
[테스트용 페이지](../tests/fixtures/README.md)를 참고한다.
