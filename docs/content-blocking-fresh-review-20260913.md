# 차단 통합 독립 재검수 — 2026-09-13

이전 완료 판단을 전제로 삼지 않고 현재 제품 소스, 적용된 Chromium glue,
생성 헤더, 실제 bundle의 소스 묶음과 검증 도구를 다시 확인했다.
**추가로 재현한 결함은 5개이며 아직 수정하지 않았다.** 별도로 리소스 검증의
잠재 결함 1개를 코드에서 확인했다. 기존 테스트 통과만으로 이 경계를 보장할 수 없다.

이번 변경은 검토 문서와 ignored 재현 자료뿐이다. 제품 코드, upstream 엔진,
Chromium glue를 수정하지 않았다. 실제 Yee를 종료하거나 실행하지 않았다.
사용자가 지정한 MCP 실험 종료 후 실앱 검증 순서를 유지한다.

동기화·패키징·process inventory 재현은
`python3 .local-build/content-blocking-fresh-tooling-repro.py`로 다시 실행할 수 있다.
이 스크립트는 제품 파일을 읽고 독립 temporary fixture만 변경한다. inventory 사례는
실제 process 실행 대신 입력 PID row를 사용하는 모형이다. 재현 대상 도구의 hash는
`.local-build/content-blocking-fresh-tooling-repro-inputs.json`에 기록했다.

## 새로 확인한 결함

P2는 수정해야 할 기능·도구 결함, P3는 현재 빈 production 리소스에는 영향을 주지
않지만 이후 기능 확장 전에 해결해야 할 검증 결함으로 사용한다.

### 1. [P2] YouTube 정리 후 보관된 객체에 광고 필드가 복귀한다

위치: `renderer/content_blocking/youtube.js:14`, `:34`, `:40`, `:173`.

`clean()`은 root와 알려진 player container의 광고 필드를 보호하지만 일반 child는
이미 존재하는 광고 필드만 보호한다. player container setter도 이미 존재하는
configurable/writable 데이터 속성에만 설치한다. 광고 접근자는 configurable이어서
삭제한 뒤 일반 속성으로 다시 만들 수 있다.

현재 JS 원문을 VM에서 실행하고 작은 mutable 객체로 다음 네 조건을 재현했다.
frozen 객체나 작업량 제한에 의한 의도적인 생략 사례가 아니다.

| 최초 정리 후 동작 | 결과 |
| --- | --- |
| 보관한 global 응답의 기존 child에 새 `adSlots` 추가 | 광고 필드가 남음 |
| XHR에서 받은 기존 child에 뒤늦게 `playerResponse = {adPlacements: [1]}` 설정 | 광고 필드가 남음 |
| root의 보호된 `adPlacements`를 삭제하고 같은 필드에 다시 값 설정 | 삭제 성공, 새 광고 필드가 남음 |
| `Response.json()`에서 받은 child 참조에 새 `playerAds` 추가 | 광고 필드가 남음 |

global/XHR getter를 다시 읽으면 재정리되는 경우가 있다. 그러나 page가 이미 보관한
참조로 처리하면 getter가 호출되지 않는다. 기존 retained-reference 테스트는 보호가
설치된 필드 변경을 확인하며 위 조건 전체를 확인하지 않는다.

재현 자료: `.local-build/content-blocking-fresh-youtube-repro.mjs`와 같은 이름의 JSON.
실행: `node .local-build/content-blocking-fresh-youtube-repro.mjs`.
대상 JS SHA-256: `c025e6ee1911a2b68f6cfd553cc0e3a25f168e8bb5ff67aec5fe19cc23b9726f`.

수정 기준: 새 child/key/container와 delete/redefine의 지원 경계를 정하고 회귀 사례를
포함해야 한다. 알려진 player 소비 경계에서 재정리하는 방법도 검토할 수 있다.
모든 필드를 무조건 nonconfigurable로 바꾸면 strict-mode 삭제 등에 예외가 생기므로
객체 identity와 page 호환성 검증이 함께 필요하다.
**실제 YouTube가 이 변경 순서를 사용한다는 증거는 아직 없다.** 데이터 hook의
구체적인 누락이며 실계정 광고 재생 실패를 직접 관측한 결과는 아니다.

### 2. [P2] 대소문자만 바뀐 소스 파일을 overlay 동기화가 삭제한다

위치: `tools/overlay/lib/overlay_tools.py:177`, `:202`, `:213`.

Python의 `Path` 집합은 macOS에서 문자열 대소문자를 구분하지만 현재 작업 volume은
파일 이름 대소문자를 구분하지 않는다. source의 `rule.cc`와 기존 destination의
`Rule.cc`가 같은 디스크 파일인데도 stale 판정에서 서로 다른 경로로 취급된다.

독립 temporary source/checkout fixture에서 실제 `source_plan()`과 `sync_sources()`를
호출했다. 기존 `Rule.cc`를 source의 `rule.cc`로 바꿨을 때:

- 내용이 같으면 copy 없이 `Rule.cc` 삭제만 계획하며 원하는 파일이 사라진다.
- 내용이 다르면 `rule.cc`에 copy한 뒤 `Rule.cc`를 삭제해 방금 복사한 파일이 사라진다.

두 조건 모두 최종 destination 파일 목록은 비었다. 실제 제품 mirror는 수정하지 않았다.
증거: `.local-build/content-blocking-fresh-sync-repro.json`의
`case_only_same`, `case_only_changed`.

수정 기준: stale 삭제 판정에 실제 filesystem 파일 identity를 반영해야 한다.
전 플랫폼의 이름을 일괄 소문자로 바꾸면 case-sensitive volume의 별개 파일을
합칠 수 있으므로 그것을 해결책으로 삼으면 안 된다.
PowerShell의 expected map은 Python과 다르며 이 대소문자 결함을 동일하게 재현했다고
주장하지 않는다.

### 3. [P2] owned mirror의 파일↔디렉터리 구조 전환이 실패한다

위치: `tools/overlay/lib/overlay_tools.py:181`, `:184`, `:202`.
PowerShell 대응 위치: `tools/overlay/lib/overlay-tools.ps1:173`, `:184`, `:216`.

source plan은 stale 파일을 제거하기 전에 destination 형태를 검사한다.
기존 destination의 `node` 파일을 새 source의 `node/new.h`로 바꾸면 parent가
파일이라는 이유로 실패한다. 반대 방향인 기존 `node/old.h`에서 새 `node` 파일로
전환해도 destination이 디렉터리라는 이유로 실패한다. stale 파일만 제거하고 빈
디렉터리를 남기는 처리 역시 디렉터리→파일 전환을 완료하지 못한다.

Python에서 두 방향 모두 실제 fixture로 재현했다. 증거는 같은 sync JSON의
`file_to_directory`, `directory_to_file`이다. PowerShell은 동일한 선행 검사와
파일 전용 stale prune를 코드에서 확인했으며 Windows 실행 검증은 하지 않았다.

수정 기준: owned root 안의 형태 충돌, stale 파일 및 빈 디렉터리를 모두 사전 검증한
뒤 안전한 제거→생성→복사 순서를 계획해야 한다. original/generated root와 링크 거부
정책은 보존해야 한다. warm checkout을 수동 삭제해야만 구조 변경이 적용되는 상태다.

### 4. [P2] 소스 패키징이 manifest 밖의 GN 등록 소스를 누락해도 성공한다

위치: `components/content_blocking/package_notices.py:28`, `:61`.
관련 입력 선언: `components/content_blocking/BUILD.gn:59`.

packager는 manifest에 있는 파일의 hash만 검사하고 같은 whitelist로 묶음을 만든다.
실제 upstream source tree 또는 GN에 등록된 소스가 whitelist와 일치하는지 검사하지 않는다.

별도 temporary vendor fixture에 원본 `adblock_0_13_3`를 복사하고
`src/added_mpl.rs`를 추가했다. 해당 fixture의 `BUILD.gn`에도 그 소스를 등록했다.
기존 manifest 파일과 hash는 유지한 채 실제 packager를 실행했다.
결과는 exit 0, archive 생성 성공, archived `BUILD.gn`에 새 파일 참조 존재,
새 `.rs` 파일은 archive에 없음이었다.
증거: `.local-build/content-blocking-fresh-package-repro.json`.
fixture 자체를 GN으로 컴파일하지는 않았다.

**현재 bundle에서 누락을 발견한 것은 아니다.** 현재 원본 파일 2,206개는 모두
archive에 있고 hash가 일치한다. 이 결함은 이후 소스 추가·GN 입력 변경을 packaging
gate가 차단하거나 검증하지 못하는 문제다. 새 파일의 입력과 공개 소스 묶음이 함께
갱신되도록 검사를 추가하고, 미등록 upstream 파일은 성공으로 처리하지 않아야 한다.

### 5. [P2] 디스크의 앱 정보 변경 후 기존 실행 PID를 종료 가드가 놓친다

위치: `tools/dev/browser_bundle_executables.py:19`, `:26`, `:32`, `:49`.

실행 process 목록을 검사하는 `browser_processes()`가 현재 디스크의 bundle 탐색
함수를 재사용한다. 실행 중인 PID의 정체를 현재 `Info.plist`와 binary 파일 존재에
의존시킨다. 앱을 지우거나 executable 이름을 바꾼 후에도 이전 프로세스는 존재할 수 있다.

동일한 가상 `ps` PID row를 유지한 temporary bundle fixture에서 valid bundle은
1개로 발견했다. binary를 삭제하면 0개, 기존 binary를 남겨도 plist의
`CFBundleExecutable`을 새 이름으로 바꾸면 이전 PID를 0개로 판정했다.
증거: `.local-build/content-blocking-fresh-inventory-repro.json`.
실제 실행 앱을 삭제하거나 변경한 실험은 하지 않았다.

따라서 모든 Yee 종료 확인 없이 새 validation을 시작할 수 있는 조건이 남아 있다.
일반적인 동일 경로 rebuild나 이전 이름의 bundle을 그대로 보존하는 조건은 기존
테스트가 확인한다. 수정 시 실행 프로세스의 identity/PID를 찾는 경로와 디스크의
launchable bundle을 찾는 경로를 분리해야 한다. macOS 실행 앱 API 활용은 후보이며
실제 기존 PID를 유지한 rename/remove fixture로 최종 검증해야 한다.

## 추가 코드상 결함과 문서 정정

### [P3] `resources_valid()`는 저장소 등록 성공까지 보장하지 않는다

`components/content_blocking/rust/src/lib.rs:44`에서 valid는 JSON 역직렬화가
실패할 때만 false가 된다. 원본 `Engine::use_resources()`가 사용하는
`resources/resource_storage.rs:122`의 convenience constructor는 `add_resource()`
오류를 소비한다. 잘못된 base64, 중복 name/alias, 허용되지 않는 dependency 같은
등록 오류가 있어도 Yee의 valid는 true로 유지되는 코드 경로다.

현재 production `resources.json`은 빈 목록이며 기존 fixture는 정상 resource다.
이번에는 invalid-resource 네이티브 실행을 추가하지 않았고 코드 경로로 판정했다.
리소스 도입 전에 fallible `add_resource()`로 모든 항목을 검증하는 저장소 구성과
오류 전달을 연결하거나, 이름을 JSON 검증으로 한정하고 동등한 build gate를 추가해야 한다.
upstream convenience 함수를 수정할 필요 없이 Yee 연결에서 해결할 수 있다.

checkpoint의 한계 설명 중 `$csp`와 inherited `about:blank/srcdoc`을 미구현으로
적은 문장은 앞쪽 구현 기록 및 현재 코드와 모순됐다. 해당 부분을 정정하고 이번
검토의 미수정 결함을 checkpoint에 연결했다. 과거 build/test 통과 기록은 보존한다.

## 재확인한 정상 경로와 검증 범위

| 경로 | 이번 확인 |
| --- | --- |
| 요청·navigation | `ResourceType` 우선 분류, opaque navigation factory 설치, trusted top origin, main target first-party, iframe initiator, start/server/client redirect 재평가를 현재 Chromium 호출 지점과 대조 |
| 응답·Mojo 수명 | response/body/metadata/progress 전달, factory/clone/request 분리 소유권, 차단·완료·disconnect 테스트 30개 재실행 통과 |
| CSP | 기존 parsed 보안 metadata 보존 및 추가 enforce 정책 연결 확인. 정상 Network Service 응답이 `PopulateParsedHeaders()`를 거치는 경로 확인 |
| renderer | document reset·weak pointer, user-origin sheet 교체/dedup, isolated origin/empty CSP, inherited origin fallback 확인. 실제 frame lifecycle은 미검증 |
| 엔진·설정·CSS 저장소 | 기존 네이티브 테스트 13개 재실행 통과 |
| tooling | `python3 -m unittest discover -s tests/tooling`: 60개, 18.518초, 통과 |
| JS | 기존 YouTube adapter/lossless text 71개 조합, generic collector 및 fake runtime/CDP suites 통과. 별도의 새 YouTube mutation 재현은 위 4개 누락을 확인 |
| 적용·생성 입력 | 전체 overlay preview 0개. JS와 filter 헤더를 temporary output에 재생성해 빌드 gen 헤더와 byte 비교 일치 |
| upstream·배포 묶음 | 61개 crate의 manifest hash 2,206개, 현재 Cargo.lock checksum과 manifest 일치. 실제 bundle archive 2,277 entries에서 원본 2,206개 hash·root LICENSE·필터 데이터 일치 |
| patch·whitespace | Chromium diff check, shell patch reverse apply check, patch 제외 root diff check 각각 exit 0 |
| 빌드 | 기존 native binary 재실행. 두 target에 `autoninja -n -C out/YeePilot -j 3` dry-run exit 0. 제품 코드 변경과 전체 앱 재빌드는 없음 |

현재 archive SHA-256은 `edf86d7e64136f5c46eaa26cf5f75a5a41311860490724e80adb682860413499`다.
무결성 상세는 `.local-build/content-blocking-fresh-integrity.json`, 실행 기록은
`.local-build/content-blocking-fresh-{tooling,core,factory}.log`에 있다.

Brave 대조는 [기존 파일별 표](content-blocking-brave-comparison-20260913.md)의 pinned
18개 관련 파일과 resource/list 기준을 이용했다. 저장한 18개 파일 hash를 다시
검증했다. Brave 저장소 전체의 모든 코드 경로를 전수 확인했다는 의미는 아니다.
WebSocket/CNAME, procedural/action/Shadow DOM, resource redirect/removeparam,
updater/프로필 설정/전용 interstitial은 기존에 기록한 미지원 기능으로 유지한다.
shared/service worker 복수 client, BFCache/prerender/fenced frame, download/partition,
strict CSP와 inherited frame의 실제 동작, Windows 빌드 및 실제 YouTube 재생은
추가 통합 검증 대상이다. 이번 정적 검토와 headless 테스트로 완료 판정하지 않는다.
