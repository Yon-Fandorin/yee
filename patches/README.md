# Chromium 변경 패치

Chromium 원본에 필요한 변경 내역을 패치 파일로 관리한다.
순서와 적용 방식은 [`build/overlay.json`](../build/overlay.json)에 정의한다.

| 패치 | 역할 |
| --- | --- |
| `0001-integrate-yee-shell.patch` | 셸·Agent·콘텐츠 차단 코드와 Chromium의 연결 |
| `0002-brand-yee-application.patch` | 브랜딩 설정과 제품명 리소스·템플릿의 연결 |
| `0003-fix-windows-protoc-python-aliases.patch` | Windows protoc의 Python 별칭 문제 처리 |

제품 기능은 [`browser/`](../browser/README.md), [`renderer/`](../renderer/README.md),
[`components/`](../components/README.md)에 작성한다.
UI 개선은 기존 `0001`에 반영하며 새 패치를 추가하지 않는다.
이름과 로고는 [`branding/`](../branding/README.md)에서 관리한다.

## 수정과 검사

원본 연결을 수정한 뒤 실제 Chromium 변경 내역에서 `0001`을 다시 만든다.
별도 제품 소스와 다른 패치가 관리하는 파일은 제외한다.
명령은 [오버레이 도구](../tools/overlay/README.md)에 있다.

패치에서 원본 문맥을 나타내는 공백 한 칸은 정상 형식이다.
자세한 검사 방법은 [패치 작업 규칙](AGENTS.md)과 [루트 작업 규칙](../AGENTS.md)을 따른다.
