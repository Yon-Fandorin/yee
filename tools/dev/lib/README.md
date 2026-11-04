# 개발 도구의 공통 코드

`common.zsh`와 `common.ps1`이 읽는 공통 코드와 플랫폼별 배포 구현을 둔다.
명령을 실행할 때는 상위 디렉터리의 스크립트를 사용한다.

| 파일 | 역할 |
| --- | --- |
| `paths.zsh`, `paths.ps1` | 저장소·Chromium·빌드 경로와 제품 이름 |
| `preflight.zsh`, `preflight.ps1` | 필요한 소스·도구·여유 공간 확인 |
| `build.zsh`, `build.ps1` | 소스·브랜딩 준비와 빌드 실행; zsh의 native 회귀 목록 검사·실행 |
| `native_test_discovery.py` | GTest 목록의 활성 사례·필터별 발견 수를 확인하는 순수 Python CLI |
| `native_gate_record.py` | zsh native gate의 소스·바이너리 식별, 목록·실행 기록과 XML 대조 |
| `background_test.py` | background 자식 세션·프로필 정리, macOS 관찰기 준비와 JSON 판정 |
| `test_tiers.py`, `test_tiers.zsh` | 사례별 manifest와 범위 필터의 tier 교집합, gate 옵션과 필터 출력 |
| `macos_installer_processes.mjs` | 설치 검사의 ps 해석과 설치·관찰 검사의 앱 경로·전용 프로필 기준 프로세스 분류 |
| `compile_preflight.py` | GN depfile 최신 여부 검사, overlay 기반 사전 컴파일 대상 분류·실패 파일·완료 범위 보고 |
| `runtime.zsh` | macOS 앱 실행·종료와 빌드 결과의 최신 여부 확인 |
| `metal.zsh` | macOS Metal 도구 경로 찾기 |
| `macos_release.py` | 복사한 Mac 설치판의 식별자·코드 서명·DMG·메타데이터 생성 |
| `macos-dmg-background.swift` | Finder 설치 창의 드래그 안내 이미지 생성 |

플랫폼마다 필요한 파일이 다르다. 제품 UI 규칙은 제품 소스에서 관리한다.
사용 방법은 [개발 도구](../README.md), 테스트는
[도구 테스트](../../../tests/tooling/README.md)를 참고한다.
