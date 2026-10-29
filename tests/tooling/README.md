# 개발 도구 테스트

브랜딩 설정과 적용, 소스 복사 경로, 개발 명령이 예상대로 동작하는지 확인한다.

## 실행

저장소 루트에서 실행한다.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

임시 파일, 임시 Git 저장소와 가짜 빌드 도구를 사용한다.
실제 Chromium 컴파일이나 브라우저 실행은 수행하지 않는다.
일부 브랜딩 테스트는 `.local-build/chromium/src`의 도구를 읽는다.
해당 소스가 없으면 테스트를 건너뛰므로 통과와 건너뜀을 구분해서 보고한다.

`test_chromium_version.py`는 공통 원본 핀, OS별 업데이트 조회와 Windows 개발 명령의
사전 검사를 다룬다. `test_overlay_tools.py`는 별도 후보 원본에서 패치를 다시 만들 때의
소유권 경계도 확인한다. macOS 개발 명령 검사는 zsh가 없으면 건너뛴다.
브랜딩 적용 테스트는 Windows에서 PowerShell, macOS에서 zsh 진입점을 사용한다.
새 원본에 적용·이름 변경·반복 적용과 생성 경로의 패치 재진입 거부를 검사한다.

`test_native_test_discovery.py`는 일반·타입·값 매개변수 목록, 양수·음수 필터,
비활성 테스트, 패턴 일부 누락과 순수 Python CLI를 가짜 목록으로 확인한다.
zsh가 있으면 가짜 바이너리로 목록 조회·실제 실행의 인수 일치와 실패 코드 전파도 확인한다.
실제 네이티브 바이너리나 GUI는 실행하지 않으며 zsh가 없으면 셸 검사는 건너뛴다.
`test_development_tools.py`의 통합 fixture는 다른 OS 전용 사례를 목록에서 빼 두어
레이아웃의 `horizontal`·`interactive`·`all`과 Sidebar의 플랫폼 필터 혼합을 확인한다.

[`tools/dev/`](../../tools/dev/README.md)의 기존 브랜딩·앱 번들 테스트 명령도
이곳의 테스트를 실행한다. C++ 테스트는 구현 옆에 두며 실제 UI 테스트 절차는
[`AGENTS.md`](../../AGENTS.md)를 따른다.
