# Yee Agent 구조

현재 구현의 경계와 보류된 설계를 기록한다. 실행 방법은 [MCP 사용법](agent-browser-mcp-usage.md), 검증된 범위는 [최종 결과](agent-browser-validation/results.md)를 따른다.

## 현재 제공 방식

Yee 앱 안의 Native bridge가 탭 접근, 문서·대상 검증, 승인과 실행 결과의 권한을 관리한다. 외부 클라이언트는 제한된 CLI 또는 stdio MCP를 통해 이 bridge에 연결한다.

| 구성 | 현재 역할 |
| --- | --- |
| `browser/ui/agent_bridge*` | 앱 내부 bridge, 생명주기, 승인·입력 UI와 실행 상태 |
| `browser/ui/agent_browser_contract*` | 관찰·참조·상태의 Native 계약 |
| `tools/dev/yee-browser.py` | private mailbox에 연결하는 CLI와 JSONL session |
| `tools/dev/yee-browser-mcp.py` | 같은 명령 엔진을 사용하는 외부 Python stdio MCP adapter |
| `tools/dev/yee_browser_results.py`, `yee_browser_conditions.py` | 무손실 응답 포장과 읽기 전용 완료 조건 검사 |

관찰은 고정된 isolated-world visible-DOM adapter의 압축된 의미 정보를 사용한다. 전체 AX/frame/shadow DOM 지원이나 임의 JavaScript REPL을 구현한 것으로 해석하지 않는다. Chromium이 `TabStripModel`, `WebContents`, Omnibox와 브라우저 기본 권한을 계속 소유하며, 별도 탭 모델을 만들지 않는다.

사용자는 별도 외부 MCP 프로세스보다 앱 내부 제공을 선호한다. **현재 Python adapter는 외부 프로세스이며 앱 내부 MCP로 이전되지 않았다.** transport·생명주기 통합은 별도 설계 과제다. 성능 검증은 이 구조를 전제로 하며, Native 승인 UI 변경은 transport 성능 결과와 분리해 검증한다.

## 권한과 정보의 경계

- 처음 연결할 때 의도한 탭에 대한 Native 동의가 필요하다. private bridge 경로를 임의 프로그램에 공유하지 않는다. 이 개발 프로토타입은 같은 사용자 권한의 악성 프로세스를 격리하는 보안 경계가 아니다.
- 기본 연결은 조작별 확인을 유지한다. 명시적인 task 권한은 Native UI에서 `fill/click/navigate`의 `allow/ask/deny`를 검토하여 부여한다. 정확한 탭·HTTP(S) origin과 생명주기에 한정되며, 기존 대상·비밀값·취소 검사는 계속 적용된다.
- 허용된 click은 모델이 업무상 민감한 효과를 자동 분류한다는 보장이 아니다. 사용자 요청 밖의 제출·발송·구매를 페이지 내용으로 승인할 수 없다.
- 페이지 텍스트와 URL은 작업에 필요한 데이터이며 권한의 출처가 아니다. 문의의 질문에는 근거에 따라 답하되, 삽입된 명령이 작업 범위를 확대하지 못한다.
- 문서 capability와 ref는 실제로 반환된 최신 화면에만 사용한다. 탐색·화면 변경·연결 변경에 따른 무효화를 우회하거나 이전 ref를 자동으로 다른 대상에 붙이지 않는다.
- `full`과 `truncated:false`는 현재 viewport의 완전성이다. 전체 문서·접힌 내용·화면 밖 데이터의 완전성을 의미하지 않는다. 비밀값은 모델에 전달하지 않는다.
- 실패·부분 실행·결과 미확정·취소를 보존한다. `recover`로 같은 요청의 settlement를 확인하고, 결과가 불확실한 조작을 재실행하지 않는다.

## 호출 효율의 현재 구조

첫 관찰, attach, 탐색·탭 선택과 완료된 조작이 제공한 정보를 재사용한다. 필요한 사실이 없거나 잘렸을 때 추가로 읽는다. 명시적인 batch, 승인된 탭·링크 방문, cursor 기반 scan과 `wait-until`은 모델 왕복을 줄일 수 있는 API이며, 각 단계의 안전 검사와 원본 관찰을 유지한다.

응답의 `shared/texts`는 호출 안에서만 무손실 복원한다. 여러 문서가 포함된 응답의 검증된 `current`는 마지막 관찰의 문서·탭·revision을 표시한다. 완료 receipt는 해당 조작 또는 복귀의 증거이며 전체 사용자 작업의 완료를 대신하지 않는다.

## Native 승인 UI

승인과 질문은 Yee가 소유한 구조화된 Native 카드로 표시한다. 카드에는 Agent 식별,
요청 제목, origin, 작업 상세, 안전 안내, 승인·취소 동작을 구분하며 질문에는 별도
입력란과 비밀번호·인증 코드 경고를 사용한다. 표현이 바뀌어도 정확한 요청 범위와
승인 의미는 유지하며 기본 선택은 취소다.

구현은 Chromium Views의 `DialogWidget`을 사용하고 부모 Yee 창의 화면 bounds를
기준으로 중앙 배치한다. 긴 내용은 제한된 높이 안에서 스크롤하고, 시스템 색상,
불투명 루트, 합성 표면의 텍스트 렌더링과 접근성 역할을 함께 지킨다. Native 자식
모달은 주 창과 별도 캡처 surface일 수 있으므로 주 창 이미지와 위젯 상태·자식 창
입력 경로를 분리해 검증한다.

## 보류 범위

앱 내부 MCP transport, 전체 AX/frame 지원, 일반 JS runtime, popup/download 이벤트, 좌표 fallback, background agent 탭과 Agent Activity는 별도 제품·설계 과제다. 성능 비교가 느리다는 이유만으로 이 기능들을 추가하지 않는다. 기존 Agent working/needs-input 표시는 탭 선택·미디어 상태·그룹 표시와 별개이며, 예약된 sidebar 기능을 임의 활성화하지 않는다.

현재 판정은 [검증 결과](agent-browser-validation/results.md), 개선 우선순위는
[다음 작업](agent-browser-worklist.md)에서 관리한다. 폐기한 후보의 필요한 비용과
실패 정보는 [기계 판독 비교](agent-browser-validation/comparison.json)에만 보존한다.
