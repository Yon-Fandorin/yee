# Chromium에 제품 코드 적용

제품 소스, 패치와 브랜딩 설정을 Chromium 소스에 반영한다.
이 적용 구성을 오버레이라고 부르며,
[`build/overlay.json`](../../build/overlay.json)에서 패치 순서와 복사 경로를 관리한다.

## 도구 안내

| 도구 | 역할 |
| --- | --- |
| `apply.sh`, `apply.ps1` | 패치·소스·브랜딩·아이콘 적용 |
| `install-yee-ui-sources.*` | 등록된 제품 소스 디렉터리 복사 |
| `install-branding.*` | 제품 이름 설정과 관련 패치 적용 |
| `install-brand-assets.*` | 로고에서 플랫폼별 아이콘 생성 |
| `audit-branding.py` | 브랜딩 적용 대상과 남은 문구 점검 |
| `regenerate-shell-patch.py` | 실제 변경 내역에서 `0001` 다시 만들기 |
| `vendor-yee-adblock.py` | 외부 Rust 원본 확인과 빌드 파일 생성 |

공통 코드는 [`lib/`](lib/README.md)에 있다.
`install-yee-ui-sources`는 UI뿐 아니라 등록된 요청·페이지 처리 코드와 Rust 원본도 복사한다.
소스 위치와 복사 경로는 [프로젝트 구조](../../docs/project-structure.md)를 참고한다.

## 변경 전에 확인

저장소 루트에서 기존 Chromium 소스의 절대 경로를 전달한다.

```sh
./tools/overlay/apply.sh "$PWD/.local-build/chromium/src" --check --skip-brand-assets
```

```powershell
.\tools\overlay\apply.ps1 -ChromiumSrc F:\chromium\src -CheckOnly -SkipBrandAssets
```

파일을 수정하지 않고 패치·소스·브랜딩 적용 가능 여부를 확인한다.
아이콘 생성과 macOS 아이콘 생성 도구의 컴파일도 생략한다.
브라우저 빌드나 실행은 수행하지 않는다.

## 실제 적용

```sh
./tools/overlay/apply.sh "$PWD/.local-build/chromium/src"
```

```powershell
.\tools\overlay\apply.ps1 -ChromiumSrc F:\chromium\src
```

먼저 설정값, 패치와 소스 경로를 검사한다.
이미 적용된 패치는 건너뛰고 내용이 달라진 제품 파일만 복사한다.
서로 다른 패치가 같은 원본 파일을 수정하거나, 등록되지 않은 소스와 잘못된 경로가
있으면 거부한다. 심볼릭 링크와 파일·디렉터리 충돌도 검사한다.

제품 소스 영역의 루트 README는 복사하지 않는다.
등록된 소스 안의 원본 README와 숨김 파일은 유지하며 `.DS_Store`와 `__pycache__`는 제외한다.

기본 적용에는 아이콘 생성도 포함된다.
macOS는 `sips`와 ICNS 생성 도구, Windows는 `System.Drawing`을 사용한다.
브라우저 빌드와 실행은 [개발 도구](../dev/README.md)의 별도 단계다.

## 이름·로고 설정과 점검

값은 [`branding/brand.json`](../../branding/brand.json)에서 바꾼다.
`0002`는 그 값을 제품명 리소스와 템플릿에 연결한다.
모든 문구·번역·자산·OS 식별자의 연결이 끝난 것은 아니다.

```sh
./tools/overlay/install-branding.sh "$PWD/.local-build/chromium/src" --check
python3 tools/overlay/audit-branding.py "$PWD/.local-build/chromium/src"
python3 tools/overlay/audit-branding.py "$PWD/.local-build/chromium/src" --json
```

점검 도구는 [`surfaces.json`](../../branding/surfaces.json)의 목록과 Chromium 문구
파일(GRD·GRDP), 직접 작성한 UI의 고정 이름, 생성된 이름 설정을 확인한다.
실제 앱 화면이나 모든 번역·아이콘을 테스트하지는 않는다.
`--require-complete`는 남은 작업과 수행하지 않은 테스트를 실패로 보고한다.

변경 절차는 [제품 브랜딩](../../docs/product-branding.md),
남은 작업은 [브랜딩 적용 범위](../../docs/branding-coverage.md)에 있다.

## 원본 연결 패치 다시 만들기

Chromium 연결 코드를 수정한 뒤 실행한다.
이 명령은 기본 `.local-build/chromium/src`를 사용하며 `0001` 패치 파일을 바꾼다.

```sh
python3 tools/overlay/regenerate-shell-patch.py
```

제품 소스, 생성 자산과 다른 패치가 관리하는 파일은 제외한다.
새 연결 파일은 `overlay.json`의 `new_glue`에 등록한다.
패치를 교체하기 전에 역방향 적용을 검사하며 Git staging은 하지 않는다.
UI 개선은 기존 `0001`에 반영한다.

패치의 원본 문맥을 나타내는 공백 한 칸은 정상 형식이다.
실제 Chromium 소스의 공백 검사와 패치 역방향 검사를 수행한다.
저장소 루트 공백 검사에서는 패치 파일을 제외한다.
상세 절차는 [패치 작업 규칙](../../patches/AGENTS.md)과
[루트 작업 규칙](../../AGENTS.md)을 따른다.

현재 UI와 기능은 [브라우저 코드](../../browser/README.md),
[Sidebar 결정](../../docs/sidebar/README.md), [Header 테스트](../../docs/header/README.md),
[Agent 구조](../../docs/agent-browser-architecture.md),
[콘텐츠 차단 checkpoint](../../docs/content-blocking-checkpoint.md)에 있다.
