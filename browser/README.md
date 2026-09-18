# 브라우저 코드

브라우저 창의 UI, Agent 연결, 네트워크 요청 차단을 담당한다.
탭 상태와 웹페이지, 주소창의 기본 동작은 Chromium 코드를 그대로 사용한다.

## 구성

| 디렉터리 | 역할 |
| --- | --- |
| `ui/` | 창의 UI, Sidebar, 고정 탭, 분할 화면, Agent 상태와 입력 요청 |
| `content_blocking/` | 네트워크 요청을 검사하고 허용된 요청을 Chromium에 전달 |

새 제품 UI는 `ui/`에 둔다. Favorites 용량이나 그룹 표시 같은 제품 규칙은
Chromium의 탭 모델에 넣지 않는다. 원본에 필요한 연결 변경은
[`0001` 패치](../patches/0001-integrate-yee-shell.patch)에 반영한다.

## Chromium에 적용되는 위치

| 저장소 경로 | Chromium 경로 |
| --- | --- |
| `ui/` | `chrome/browser/ui/views/yee/` |
| `content_blocking/` | `chrome/browser/yee_content_blocking/` |

경로는 [`build/overlay.json`](../build/overlay.json)에서 관리한다.
이 README는 저장소 안내이므로 Chromium에 복사하지 않는다.
C++ 테스트는 구현 파일 옆에 둔다.

## 관련 문서

- [셸 명세](../docs/browser-shell-spec.md): 화면 배치와 제품 규칙
- [Sidebar 결정](../docs/sidebar/README.md), [Header 테스트](../docs/header/README.md)
- [Agent 구조](../docs/agent-browser-architecture.md)
- [콘텐츠 차단 checkpoint](../docs/content-blocking-checkpoint.md)
- [개발 도구](../tools/dev/README.md), [작업 규칙](../AGENTS.md)
