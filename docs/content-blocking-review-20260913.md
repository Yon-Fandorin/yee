# 네이티브 차단 통합 재검토

검토일: 2026-09-13. 요청·문서·필터·배포 경로를 검토하고 아래 문제를 수정했다.
엔진 원본은 변경하지 않았다. 실제 광고 차단 성공을 선언하는 문서는 아니다.
MCP 실험용 앱은 종료하지 않고 실앱 검증을 실험 종료 이후로 미룬다.

## 발견해 수정한 문제

| 문제 | 실제 영향 | 수정과 근거 |
| --- | --- | --- |
| 미연결 Mojo receiver의 disconnect handler 설정 | 첫 차단/허용 요청에서 DCHECK 종료 가능 | upstream receiver를 bind한 뒤 handler 설정. 처음 factory 테스트에서 재현했고 수정 후 14개가 모두 통과 |
| sendBeacon을 fetch/XHR로 분류 | `$ping` 규칙이 tracker beacon을 놓침 | Blink의 실제 `ResourceType::kPing`을 먼저 확인. 같은 host의 beacon 차단·fetch 허용 테스트 통과 |
| 동적 작업 큐의 overflow를 버림 | 수백 요소가 동시에 추가되면 마지막 광고 class/id가 matcher에 전달되지 않음 | 큐 초과를 하나의 증분 재순회로 합침. 600개 추가 요소의 마지막 class/id까지 확인 |
| 요소 하나의 class가 batch보다 많음 | 뒤쪽 class가 다음 batch로 이어지지 않음 | 요소·class 순회 위치를 유지. 요소 하나의 700개 class 전달 확인 |
| 긴 UTF-8 이름이 native batch 전체를 거부시킴 | 다른 정상 광고 selector까지 조회되지 않음 | 수집 단계에서 byte 길이를 제한. 긴 한국어 이름 옆의 정상 항목 전달 확인 |
| fixture의 generic 규칙을 production에 포함 | 일반 페이지의 `.yee-generic-*` 요소가 숨겨질 수 있음 | 별도 test 규칙·resource bundle 및 opt-in switch. 기본 bundle에서 host 차단·generic selector·scriptlet이 없는지 확인 |
| 예외 host의 대소문자 및 origin 판단 | 대문자 예외 미적용, 내부 initiator에서 시작한 web factory 제외, 일부 worker metadata의 보호 누락 | 정확 host 비교는 대소문자 비구분. 알려진 HTTP(S) top origin 우선, opaque top의 web initiator fallback. entirely opaque/internal factory는 제외 |
| source packaging action이 crate 원본을 입력으로 추적하지 않음 | 변경된 engine을 컴파일해도 이전 source archive가 남을 수 있음 | 모든 원본·생성 GN 파일을 `sources.gni`에 연결하고 action 입력에 추가 |
| checksum을 기록만 하고 registry source를 직접 검증하지 않음 | 수정된 Cargo cache 파일을 원본으로 기록할 위험 | 61개 `.crate`의 Cargo.lock SHA-256과 원본 파일 2,145개 비교. vendor source가 실제 배포 archive와 같은지 확인 |
| 필터 provenance hash와 실제 입력을 비교하지 않음 | 다른 필터를 빌드하면서 이전 attribution/hash를 배포할 수 있음 | header 생성·고지 packaging 모두 hash 불일치 시 실패. 변조 fixture를 출력 전에 거부하는 것을 확인 |
| source archive에 독립 Yee 연동부까지 포함 | 불필요하게 자체 구현 소스를 제공하게 됨 | 제공 범위를 원본 engine/dependencies와 외부 filter 데이터로 제한. archive whitelist로 자체 Rust/C++/JS·test 규칙 제외 |
| 두 crate의 license 전문 누락 | license 식별자만 고지에 들어감 | FlatBuffers·SeaHash의 공식 원문을 별도 데이터에 추가. 원본 crate는 그대로 두고 출처·hash 보존 |
| YouTube traversal이 object 수만 제한 | 큰 배열/객체 하나에서 무제한 property 처리와 큐 증가 가능 | property 방문·pending 수까지 제한. 15,000개 getter 객체의 작업량 테스트 통과 |
| page-defined getter/Proxy 오류 전파 | player 데이터 설정에 추가 오류를 유발할 수 있음 | getter·enumeration 실패는 원래 데이터를 보존하며 건너뜀. 본 영상 필드와 설정 동작 보존 확인 |

구현 위치:

- [`filtering_url_loader_factory.cc`](../browser/content_blocking/filtering_url_loader_factory.cc)
- [`generic_cosmetic.js`](../renderer/content_blocking/generic_cosmetic.js)
- [`youtube.js`](../renderer/content_blocking/youtube.js)
- [`settings.cc`](../components/content_blocking/settings.cc)
- [`vendor-yee-adblock.py`](../tools/overlay/vendor-yee-adblock.py)
- [`package_notices.py`](../components/content_blocking/package_notices.py)

## 코드 경로에서 확인한 수명과 범위

factory와 loader는 독립적인 pipe를 소유한다. factory를 먼저 닫아도 이미 시작한
요청은 완료할 수 있으며, Clone이 남아 있으면 정책을 유지한다. loader 취소와
upstream client disconnect도 회귀 테스트로 확인했다. 서버 redirect 및 client URL
변경을 각각 다시 평가한다. 정상 response는 head/status/body를 유지한다.

renderer의 페이지 scriptlet 실행 뒤에는 weak pointer로 frame 파괴를 확인한다.
generic callback은 closure에 frame pointer를 저장하지 않고 실행 context에서 확인한다.
같은 문서의 중복 설치는 막고 새 문서에서는 적용 상태를 초기화한다.

Chromium의 `RunScriptsAtDocumentStart` 호출은 document element available 경로에 있다.
Blink main-world `ExecuteScript`는 scripts-disabled 기본 정책을 사용하고, isolated
world 실행 API는 scripts-disabled에서도 실행하는 정책이다. 페이지 API hook과
관리 world의 차이를 소스에서 확인했다. CSP·sandbox·실제 타이밍은 아직 실앱에서
확인하지 않았으므로 소스 검토만으로 통과 처리하지 않는다.

## 남아 있는 중요한 차이

| 범위 | 현재 상태 | 후속 확인/구현 |
| --- | --- | --- |
| YouTube 실제 pre/mid-roll | 데이터 fixture만 통과 | 광고가 있는 비로그인/실계정 표본에서 광고 영상·소리 없음과 본 영상 재생을 함께 확인 |
| stream/arrayBuffer 경유 데이터, 다른 endpoint, 영상에 결합된 광고 | 현재 wrapper 대상이 아님 | 실제 응답 경로를 확인한 뒤 필요한 경로만 추가. CSS로 player를 가리는 것을 성공으로 세지 않음 |
| 광고 차단 감지와 A/B 변형 | 감지 회피나 모든 변형 성공을 보장하지 않음 | 오류·재생 정지·광고와 본 영상 경계를 기록해 판단 |
| 일반 필터의 scriptlet resource | production resource는 빈 목록 | 필요 사이트의 리소스·라이선스를 확인하거나 작은 자체 모듈 추가. 엔진 재사용만으로 scriptlet 대응이 생기지 않음 |
| `about:blank/srcdoc`, inherited/opaque document | cosmetic은 HTTP(S) 문서만 적용 | iframe origin·sandbox와 사이트 예외를 보존하는 방식으로 확장/검증 |
| shadow DOM | document observer/user CSS가 내부를 관통하지 않음 | 필요 사이트의 open/closed root 보호 범위를 별도로 정함 |
| WebSocket/WebTransport | Yee 차단 hook 없음 | 별도 Chromium 생성 경로에 연결해야 함. URLLoaderFactory 보호로 세지 않음 |
| service worker 자체 cache 응답 | HTTP factory를 안 거치면 현재 proxy 밖 | service/shared worker 및 여러 client의 top-site 예외 포함 실앱 확인 |
| preconnect/DNS hint, prefetch/early hints/prerender | 모든 사전 연결 차단을 입증하지 않음 | 해당 factory/별도 네트워크 경로별 요청 증거 확인 |
| redirect resource·removeparam·CSP directive·procedural/action CSS | 반환된 모든 엔진 동작을 consumer가 실행하는 것은 아님 | 구체적인 사이트 실패를 근거로 추가. unsupported option을 없애 규칙을 넓히지 않음 |
| 한국어 사이트 목록 | EasyList·EasyPrivacy와 초기 Yee 규칙 | 지역 목록의 필요성·라이선스 및 실제 국내 사이트를 검증 |
| 목록 업데이트·긴급 예외 | 앱 rebuild로만 갱신 | 서명/검증·rollback을 갖춘 updater와 빠른 사이트 해제 경로 필요 |
| 사용자 제어 | 정확 host CLI 예외, restart 필요 | UI·영구 프로필 설정은 다음 제품 범위. 위치/UX를 임의로 만들지 않음 |
| native 성능 | 최초 bundle 생성은 집중 테스트에서 약 90–100 ms | renderer 첫 페이지 지연, warm 요청 지연, RSS와 탭 수별 증가를 feature off/on으로 비교 |
| release credits/source 안내 | 현재 개발 빌드의 `chrome://credits`는 Chromium sample HTML. 차단 고지·source는 app Resources에 있음 | 외부 배포 전에 사용자가 찾을 수 있는 credits/source 안내와 release packaging 포함을 별도로 확인 |
| Windows | installer/GN 경로만 작성 | 실제 Windows Rust/GN/app bundle 배포 확인 |

## 검증 상태와 다음 gate

- 엔진·설정 native 테스트 7개 통과.
- 실제 Mojo factory native 테스트 14개 통과.
- generic collector와 YouTube Web API/데이터 회귀 테스트 통과.
- 변조 filter/source를 별도 임시 입력에서 거부하고 불완전 출력이 없는 것 확인.
- 기본 35 GiB disk guard로 전체 macOS 앱 빌드 통과.
- 최종 앱 framework Resources의 source/data 파일 2,277개와 원본 crate 61개 hash 검증.
  독립 Yee source가 제외되고 고지·두 추가 license 전문·업데이트된 renderer script가 포함되는 것 확인.
- Chromium source diff whitespace 및 현재 `0001` reverse apply 통과.

실앱 gate는 아직 실행하지 않았다. 실험 종료 후 모든 Yee의 graceful shutdown을
확인하고 새 앱의 별도 profile로 [`test-content-blocking.mjs`](../tools/dev/test-content-blocking.mjs)를 실행한다.
feature off/on 및 top-site 예외를 비교하며 첫 inline script 이전 처리, fetch·redirect·worker,
beacon 도착 여부, 허용 response, 600개 동적 요소 이후의 광고를 검증한다. 이후 실제
YouTube 광고와 본 영상 재생, CSP·iframe·BFCache·캐시 경로를 확인한다.

## 보완한 라이선스 원문 출처

- FlatBuffers 25.12.19: published crate의 VCS commit
  [`7e163021…/LICENSE`](https://github.com/google/flatbuffers/blob/7e163021e59cca4f8e1e35a7c828b5c6b7915953/LICENSE).
- SeaHash 4.1.0: 해당 published crate/VCS tree에는 전문이 없었다. 같은 공식 프로젝트가
  `fix: add missing MIT license text`로 추가한
  [`3088c5c9…/LICENSE`](https://gitlab.redox-os.org/redox-os/seahash/-/blob/3088c5c912b70b586d27bf553fbe964e025a2c89/LICENSE)를 보완했다.
  4.1.0 원본에 이미 포함된 파일로 가장하지 않고 이 차이를 notices에 명시한다.

MPL 코드를 담지 않은 독립 파일의 source까지 제공해야 하는 것은 아니다.
[Mozilla MPL FAQ Q8/Q11](https://www.mozilla.org/en-US/MPL/2.0/FAQ/)를 기준으로,
원본 engine을 유지하고 독립 작성한 Yee 연동부는 배포용 source archive에서 제외한다.
MPL 코드를 옮기거나 재작성해 포함하는 변경이 생기면 해당 범위를 다시 검토해야 한다.

전체 보호의 결정과 원본 엔진의 라이선스는 별개다. 최신 구현 구조는
[`content-blocking-checkpoint.md`](content-blocking-checkpoint.md)를 따른다.
