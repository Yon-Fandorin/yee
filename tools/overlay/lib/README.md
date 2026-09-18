# 코드 적용 도구의 공통 코드

`overlay_tools.py`와 `overlay-tools.ps1`이
[`build/overlay.json`](../../../build/overlay.json)을 읽어 패치를 검사하고 소스를 복사한다.

- 변경 전에 패치 적용 가능 여부와 소스 경로 확인
- 패치 간 파일 중복, 등록되지 않은 소스와 잘못된 복사 경로 거부
- 내용이 달라진 파일만 Chromium에 복사
- Python에서 `0001`을 다시 만들 때 제외할 파일 목록 제공

`browser/`, `renderer/`, `components/`, `third_party/`의 루트 README는 복사하지 않는다.
등록된 소스 디렉터리 안의 원본 README는 복사한다.
등록되지 않은 하위 디렉터리를 README라는 이유로 검사에서 제외하지는 않는다.

사용 방법은 [상위 안내](../README.md), 경로와 파일 검사 테스트는
[도구 테스트](../../../tests/tooling/README.md)를 참고한다.
