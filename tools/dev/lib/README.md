# 개발 도구의 공통 코드

`common.zsh`와 `common.ps1`이 읽는 공통 코드를 둔다.
명령을 실행할 때는 상위 디렉터리의 스크립트를 사용한다.

| 파일 | 역할 |
| --- | --- |
| `paths.zsh`, `paths.ps1` | 저장소·Chromium·빌드 경로와 제품 이름 |
| `preflight.zsh`, `preflight.ps1` | 필요한 소스·도구·여유 공간 확인 |
| `build.zsh`, `build.ps1` | 소스·브랜딩 준비와 빌드 실행 |
| `runtime.zsh` | macOS 앱 실행·종료와 빌드 결과의 최신 여부 확인 |
| `metal.zsh` | macOS Metal 도구 경로 찾기 |

플랫폼마다 필요한 파일이 다르다. 제품 UI 규칙은 제품 소스에서 관리한다.
사용 방법은 [개발 도구](../README.md), 테스트는
[도구 테스트](../../../tests/tooling/README.md)를 참고한다.
