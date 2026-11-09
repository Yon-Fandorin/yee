# Chromium 157 Windows 인수인계

## 5. 외부 제어 도구의 URL 판별

- [ ] Computer Use에서 Yee의 현재 URL과 전체 창을 확인한다.

2026-11-09에 개발 출력 `out/YeeStable157/chrome.exe`를 새 격리 프로필로 열었다.
시작 주소로 `https://example.com/`을 전달하고 `--force-renderer-accessibility`를 사용했다.
Computer Use가 정확한 실행 경로의 `Example Domain - Yee` 창을 찾았다. 이어 URL과 화면을
요청한 `get_window_state`는 정책 적용에 필요한 URL 판별 단계에서 중단됐다. 도구는
접근성 상태와 화면을 반환하지 않았다. 판별에 사용한 값과 내부 실패 조건은 확인되지 않았다.

Yee의 Windows 표준 접근성 경로는 [Omnibox 검사](header/test-coverage.md)에서 다룬다.
실제 HWND의 `Document`가 노출하는 문서 URL과 `Edit`의 편집값을 각각 읽는다.
다음 검증은 새 개발 빌드와 별도 프로필에서 동일한 Computer Use로 진행한다. 창 상태에서
실제 문서의 전체 URL과 Yee 전체 화면이 함께 반환되면 이 항목을 닫는다. 앱 식별자, URL,
접근성 역할은 실제 값과 Chromium 표준을 유지하며 도구의 판별 절차를 따른다.

이 PC의 실행 파일 해시와 재현 결과는
`.local-build/windows-external-url-20261109-000249-69f6df/`에 있다.
