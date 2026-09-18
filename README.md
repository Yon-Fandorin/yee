# 브라우저 프로젝트

Chromium 기반의 브라우저를 만든다. 탭과 웹페이지, 주소창의 기본 동작은
Chromium을 사용하고, 창의 UI와 Agent 연결은 별도 제품 코드로 구현한다.
표시 이름과 로고는 [`branding/brand.json`](branding/brand.json)에서 관리한다.

## 디렉터리 안내

| 디렉터리 | 역할 |
| --- | --- |
| [`browser/`](browser/README.md) | 브라우저 창의 UI, Agent 연결과 요청 차단 |
| [`renderer/`](renderer/README.md) | 웹페이지의 CSS·스크립트 처리 |
| [`components/`](components/README.md) | 공용 차단 엔진, 설정과 데이터 |
| [`third_party/`](third_party/README.md) | 외부 라이브러리 원본과 라이선스 |
| [`patches/`](patches/README.md) | Chromium 원본의 변경 내역 |
| [`branding/`](branding/README.md) | 표시 이름·로고 설정과 적용 목록 |
| [`build/`](build/README.md) | 빌드 설정과 소스 복사 경로 |
| [`tools/`](tools/README.md) | 개발·테스트·Agent 연동 도구 |
| [`tests/`](tests/README.md) | 도구 테스트와 테스트용 페이지 |
| [`experiments/`](experiments/README.md) | 화면 배치와 동작 실험 |
| [`native-pilot/`](native-pilot/README.md) | 설치된 Chrome으로 실제 탭 동작 확인 |
| [`assets/`](assets/README.md) | 로고 등 원본 이미지 |
| [`docs/`](docs/README.md) | 제품 명세, 설계와 테스트 기록 |

파일 배치는 [프로젝트 구조](docs/project-structure.md), 화면 배치와 용어는
[브라우저 셸 명세](docs/browser-shell-spec.md)와
[레이아웃 용어](docs/browser-shell-layout-glossary.md)를 따른다.
작업 절차는 [`AGENTS.md`](AGENTS.md)에 있다.

## 빌드 없이 확인

저장소 루트에서 실행한다. 첫 명령에는 기존 Chromium 소스가 필요하다.
패치 적용 가능 여부와 도구 테스트를 확인하며 브라우저를 빌드하거나 실행하지 않는다.

```sh
./tools/overlay/apply.sh "$PWD/.local-build/chromium/src" --check --skip-brand-assets
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

이름과 로고 변경은 [브랜딩 안내](branding/README.md)를 참고한다.

## 빌드와 실행

Chromium 소스, 개발 도구, 캐시와 빌드 결과는 Git에서 제외된 `.local-build/`에 둔다.
빌드를 진행할 때는 아래 명령을 사용한다.

macOS:

```sh
./tools/dev/doctor.sh
./tools/dev/checkout.sh
./tools/dev/setup-metal.sh
./tools/dev/configure.sh
./tools/dev/build-ui.sh
./tools/dev/build.sh
./tools/dev/smoke-test.sh
./tools/dev/run.sh
```

Windows PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\dev\doctor.ps1
.\tools\dev\checkout.ps1
.\tools\dev\configure.ps1
.\tools\dev\build-ui.ps1
.\tools\dev\build.ps1
.\tools\dev\smoke-test.ps1
.\tools\dev\run.ps1
```

`build-ui.*`는 `yee_ui`만 컴파일한다. 실제 앱을 다시 만들려면 `build.*`를 실행한다.
Windows 개발 실행 파일은 `chrome.exe`다.
환경 준비, 디스크 여유 공간과 실행 방법은 [개발 도구](tools/dev/README.md)에 있다.

## 화면 실험과 실제 앱 확인

[화면 프로토타입](experiments/shell-prototype/README.md)은 HTML·CSS로 배치와 동작을 비교한다.
[설치된 Chrome 확인 환경](native-pilot/README.md)은 별도 프로필로 실제 탭을 실행한다.
직접 구현한 기능의 테스트 범위는 [제품 코드](browser/README.md)와
[테스트 기록](docs/README.md)에 있다.
