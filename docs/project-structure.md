# 프로젝트 루트 구조

제품 소스를 루트에서 역할별로 나눈다. 브라우저 셸과 Agent UI, 브라우저·렌더러
연결 코드, 공용 콘텐츠 차단 기능, 외부 Rust 의존성의 경계를 드러낸다.
Chromium 원본과 빌드 결과는 기존 `.local-build/`에 유지한다.

## Brave에서 참고한 경계

Brave의 `src`는 Chromium 작업 공간이다. `brave-core`는 그 안의 `src/brave`에
놓이고, 저장소 자체 루트에는 `browser/`, `renderer/`, `components/`, `app/`,
`build/`, `patches/`, `chromium_src/`, `script/`, `test/`, `third_party/`, `tools/`
등이 있다. `brave-browser`는 현재 이슈·배포·위키 저장소다.
[brave-core](https://github.com/brave/brave-core),
[brave-browser README](https://github.com/brave/brave-browser/blob/master/README.md).

Brave는 독립된 제품 코드와 작은 Chromium 연결을 선호하며, 필요할 때 패치와
`chromium_src` override를 사용한다.
[공식 패치 가이드](https://github.com/brave/brave-core/blob/master/docs/patching_and_chromium_src.md).

이 프로젝트는 이미 Yee 소스를 Chromium 경로에 동기화하고 GN에서 연결하는
구조다. 이 루트 정리에서는 그 연결 방식을 유지한다. Brave의 역할 구분을
반영하되, 별도의 `chromium_src` 시스템이나 `src/brave` 마운트 구조는 추가하지
않는다. 아래 구조는 우리 구현을 기준으로 선택한 배치다.

## 실제 디렉토리

```text
yee/
├── browser/
│   ├── ui/                           네이티브 셸·Sidebar·Agent UI
│   └── content_blocking/             브라우저 프로세스 연결
├── renderer/content_blocking/        렌더러 연결·주입 스크립트
├── components/content_blocking/      공용 콘텐츠 차단 기능
├── components/tasks/                 작업 sequence의 객체 생성·소유 어댑터
├── third_party/yee_adblock/           vendored Rust 의존성·라이선스·검증 manifest
├── patches/                          Chromium 원본 변경: 0001·0002·0003
├── branding/
│   ├── brand.json                    제품 표시 이름·로고 설정
│   └── surfaces.json                 브랜딩 범위와 검토 상태
├── build/
│   ├── args.gn                       Chromium 빌드 설정
│   └── overlay.json                  패치 순서·소스/적용 경로·새 glue 목록
├── tools/
│   ├── dev/                          checkout·빌드·실행·CLI·MCP·측정 도구
│   │   └── lib/                      경로·사전 검사·빌드·앱 수명 관리
│   └── overlay/                      패치 적용·소스 동기화·브랜딩·감사·vendoring
│       └── lib/                      공통 검사·동기화 구현
├── tests/
│   ├── tooling/                      브랜딩·오버레이·개발 도구의 Python 테스트
│   └── fixtures/                     실제 탭·Agent 시나리오·콘텐츠 차단 검증 입력
├── experiments/shell-prototype/      정적 브라우저 셸 탐색
├── native-pilot/                     설치된 Chromium의 격리 실행 체크포인트
├── assets/brand/                     선택한 브랜드 원본 이미지
├── docs/                             제품 명세·설계·검증 기록
├── .local-build/                     생성된 Chromium·depot_tools·빌드·캐시
└── .local-exclude/                   로컬 실험 산출물
```

`native-pilot/runtime/`은 실행할 때 다시 만드는 격리 프로필이며 Git에 보관하지 않는다.

제품명은 `branding/brand.json`에서 바꾼다. 디렉토리와 C++ namespace의 `yee`는
안정된 소스 식별자이므로, 표시 이름을 바꿀 때 이동하지 않는다.

## Chromium 적용 경로

`build/overlay.json`의 `source_roots`는 저장소의 `source`와 Chromium checkout의
`destination`을 명시한다. 동기화와 `0001` 재생성이 같은 목록을 사용한다.

| 저장소 소스 | Chromium src 적용 경로 |
| --- | --- |
| `browser/ui/` | `chrome/browser/ui/views/yee/` |
| `browser/content_blocking/` | `chrome/browser/yee_content_blocking/` |
| `renderer/content_blocking/` | `chrome/renderer/yee_content_blocking/` |
| `components/content_blocking/` | `components/yee_content_blocking/` |
| `components/tasks/` | `components/yee_tasks/` |
| `third_party/yee_adblock/` | `third_party/rust/yee_adblock/` |

소스의 내용, Chromium include 경로, GN label은 유지한다. 새 소유 디렉토리는
`source_roots`에 등록한다. 등록되지 않은 제품 소스는 복사 전에 검사에서 실패한다.
`browser/`, `renderer/`, `components/`, `third_party/`의 루트 `README.md`는
저장소 안내이므로 복사하지 않는다. 등록된 소스 디렉토리 안의 원본 README는 유지한다.
`.DS_Store`와 `__pycache__`는 제외하고 정상적인 vendored 숨김 파일은 보존한다.

`0001` 재생성은 모든 적용 대상 소스, 생성 자산, 다른 패치가 소유한 원본 파일을
제외한다. 새 Chromium glue 파일은 `new_glue`에 등록하여 untracked 상태에서도
패치에 포함되게 한다. UI 개선은 독립 소스와 기존 `0001`에 반영한다.

## 검증 진입점

이전 디렉터리의 호환 복사본이나 실행 wrapper는 유지하지 않는다. 현재 경로만 문서와
자동화에서 사용한다. 구조와 overlay를 빠르게 검사하는 명령은 아래와 같다.

```sh
./tools/dev/doctor.sh
./tools/overlay/apply.sh "$PWD/.local-build/chromium/src" --check --skip-brand-assets
./tools/dev/test-run-preflight.sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

이 명령들은 Chromium을 빌드하거나 브라우저를 실행하지 않는다. 제품 소스가 바뀌면
`tools/dev/build-ui.sh` 또는 `tools/dev/build.sh`를 실행하고, 화면 배치는
`tools/dev/test-browser-surface-layout.sh`의 해당 gate로 확인한다. C++ 테스트는 제품
소스 옆에 둔다. 특정 실행의 통과 수와 로컬 로그 경로는 구조 문서에 고정하지 않는다.
