# Yee 광고·트래커 및 YouTube 광고 차단 조사

조사일: 2026-09-12. 공식 문서, upstream 소스, 현재 Yee checkout을 기준으로 한다.
조사와 구현 제안이며, Yee에서 차단을 구현하거나 실제 YouTube 재생을 검증한 결과는 아니다.
기존 MCP·토큰·성능 작업의 코드와 실행 중인 브라우저는 변경하지 않았다.

후속 설계에서는 사용자의 “소스 제공 의무가 없는 의존성 우선” 선호를 반영해
Chromium matcher와 Yee 자체 C++ 구현을 추천안으로 삼았다.
아래 Rust 추천은 해당 선호 확인 전의 조사 결론이며, 최신 구현 방향은
[`content-blocking-design.md`](content-blocking-design.md)의 대안 비교와
[`content-blocking-checkpoint.md`](content-blocking-checkpoint.md)의 후속 통합 기록을 따른다.

## 권장 방향

**Brave의 `adblock-rust`를 재사용하는 네이티브 차단 기능이 Yee의 장기 방향으로 적합하다.**
필요한 구성은 요청 차단, 페이지 요소 필터링, 사이트별 scriptlet 실행,
브라우저 릴리스와 독립적인 필터 업데이트다. 엔진 라이브러리 하나를 링크하는 것으로
Brave Shields 전체가 완성되지는 않는다.

Brave Shields는 Chromium에 직접 통합되어 MV2/MV3 확장 프로그램에 의존하지 않는다.
`adblock-rust`는 재사용 가능한 라이브러리로 공개되어 있으며, 네트워크 필터,
cosmetic filter, 리소스 대체와 uBO 문법 확장을 제공한다.
[Brave의 통합 방식](https://brave.com/blog/brave-shields-manifest-v3/),
[엔진 README](https://github.com/brave/adblock-rust).

빠른 비교 실험에는 uBlock Origin Lite 또는 AdGuard MV3를 사용할 수 있다.
이후 네이티브 구현의 차단 품질·사이트 호환성을 비교하는 기준으로도 유용하다.
이는 확장 프로그램을 Yee의 최종 제품 기능으로 채택했다는 결정은 아니다.

## 접근법 비교

아래 평가는 upstream 기능을 Yee에 적용할 때의 엔지니어링 판단이다.

| 방법 | 일반 광고·트래커 | YouTube 광고 | Yee 적용 비용과 제약 |
| --- | --- | --- | --- |
| DNS/hosts 차단 | 별도 광고·추적 도메인에 효과적 | 단독 접근으로 부적합 | 가장 간단하지만 URL 경로, 응답 데이터, 페이지 실행 상태를 구분하지 못함 |
| MV3 확장: uBOL/AdGuard | 실용적인 빠른 검증 수단 | 사이트별 필터와 scriptlet로 대응 가능; 실제 계정·영상별 검증 필요 | 기존 확장 시스템 활용 가능. DNR 문법·쿼터, 스크립트 패키징, 업데이트 방식의 제약이 남음 |
| MV2 uBO 지원 또는 내부 component 확장 | 기존 uBO 기능 재사용 후보 | 기존 필터 체계 재사용 후보 | 현재 checkout의 내부 예외를 실험할 수 있지만, API 호환성과 미래 Chromium 유지 비용이 미확인 |
| 네이티브 `adblock-rust` 통합 | 브라우저가 직접 제어 | 별도 renderer 처리와 필터 갱신을 갖추면 대응하는 구조를 만들 수 있음 | 초기 작업이 가장 큼. 장기적으로 Yee 설정·프레임·프로필 수명주기와 통합하기 좋음 |

YouTube는 광고와 본 영상이 같은 서버에서 전달될 수 있어 도메인 차단으로
둘을 안정적으로 구별하기 어렵다.
[Pi-hole의 설명](https://discourse.pi-hole.net/t/how-do-i-block-ads-on-youtube/253).

MV3가 광고 차단 자체를 불가능하게 만드는 것은 아니다. DNR은 네트워크 규칙을
브라우저에서 처리하지만 표현 가능한 조건과 규칙 수에 제약이 있다.
uBOL 제작자도 일부 필터의 DNR 변환 불가와 anti-adblock 대응의 차이를 설명한다.
2026년 6월 FAQ에는 사용자 필터와 외부 목록 구독 지원이 추가되었다고 명시되어 있으므로,
“uBOL은 외부 필터를 전혀 추가할 수 없다”는 과거 설명을 현재 제약으로 사용하면 안 된다.
[Chrome DNR 문서](https://developer.chrome.com/docs/extensions/reference/api/declarativeNetRequest),
[현재 uBOL FAQ](https://github.com/uBlockOrigin/uBOL-home/wiki/Frequently-asked-questions-(FAQ)).

Chrome Web Store로 배포하는 MV3 확장의 실행 코드는 확장 패키지에 포함해야 한다.
필터 데이터 갱신과 새 JavaScript 리소스 배포는 구분해야 한다. 이 배포 정책을
Yee 네이티브 기능의 제약으로 그대로 적용할 이유는 없다.
[원격 실행 코드 문서](https://developer.chrome.com/docs/extensions/develop/migrate/remote-hosted-code).

## 일반 웹 차단에 필요한 층

1. **요청 차단**: URL뿐 아니라 요청 유형, HTTP method, 요청을 만든 origin,
   first/third-party 관계, 예외 규칙을 함께 평가한다. 광고 스크립트·이미지·추적 요청이
   전달되기 전에 차단해야 다운로드와 실행을 함께 줄일 수 있다.
2. **Cosmetic filtering**: 광고 영역과 빈 placeholder를 CSS 등으로 숨긴다.
   동적으로 추가되는 요소도 처리한다. 숨기는 것만으로 네트워크 추적이 차단되지는 않는다.
3. **Scriptlet 및 대체 리소스**: 필요한 사이트에서 광고 데이터를 정리하거나
   광고·감지 스크립트의 동작을 제한한다. 단순 취소가 사이트를 깨뜨리는 경우
   호환용 대체 리소스가 필요할 수 있다.

Brave의 C++ 엔진 wrapper는 요청 문맥을 Rust 엔진으로 전달하고,
사이트별 cosmetic 리소스와 동적 class/id 필터를 조회한다. 라이브러리 API도
필터와 리소스를 로드한 뒤 호출자가 실제 페이지 적용을 수행하는 구조다.
[Brave C++ wrapper](https://github.com/brave/brave-core/blob/master/components/brave_shields/content/browser/ad_block_engine.cc),
[Rust 엔진 API](https://github.com/brave/adblock-rust/blob/master/src/engine.rs).

## YouTube 대응

현재 uAssets Quick fixes에는 YouTube 광고 관련 JSON 필드 처리,
fetch/XHR 응답 또는 요청 처리, 페이지 스크립트 처리, 재생 오류와 광고 상태에 대한
대응이 포함되어 있다. 파일의 갱신 주기 선언은 8시간이다.
이는 단일 CSS selector나 고정된 광고 도메인 목록으로 해결되는 작업이 아니라는
직접적인 근거다.
[현재 YouTube 관련 Quick fixes](https://github.com/uBlockOrigin/uAssets/blob/master/filters/quick-fixes.txt).

Yee에서는 다음을 별도 수용 기준으로 둔다.

- 일반 watch 페이지에서 시작 전 광고와 중간 광고의 영상·소리가 재생되지 않는지 확인한다.
- 광고 차단 감지 화면, 무한 로딩, 검은 화면, 본 영상 누락이 발생하지 않는지 확인한다.
- 새로고침뿐 아니라 영상 간 SPA 이동, 재생목록, 뒤로 가기에서도 계속 적용한다.
- 로그인/비로그인, 일반 영상/Shorts/라이브/임베드, 자막·탐색·화질 전환을 나누어 검증한다.
- 서버가 광고를 스트림에 결합하는 변형은 별도 문제로 취급한다. 광고 요청 취소만으로
  모든 변형을 해결한다고 가정하지 않는다.

Brave는 YouTube 광고 차단을 제품 기능으로 제공한다고 안내한다. 그러나 그 안내만으로
현재 Yee, 특정 계정, 특정 YouTube 실험군에서의 성공이 증명되는 것은 아니다.
[Brave의 YouTube 기능 안내](https://brave.com/did-you-know/brave-blocks-youtube-ads/).

영상 제작자가 본 영상 안에 직접 넣은 협찬 멘트는 플랫폼 광고와 구분한다.
동일한 요청 차단 기능의 완료 기준에 섞지 않는다.

## Yee 소스에서 확인한 연결 지점

현재 로컬 Chromium은 `153.0.8005.0`, HEAD는
`25189dfe0a49b3f8324586374b8251b8d12ad1c2`다.
Yee overlay 검색에서는 전용 광고·트래커 차단 모듈이 확인되지 않았다.
Agent bridge의 element fingerprint와 Views의 ViewTracker는 이 기능에 해당하지 않는다.

| 역할 | 현재 소스 | 적용 시 고려사항 |
| --- | --- | --- |
| Browser 측 요청 처리 | `chrome/browser/chrome_content_browser_client.cc`: `CreateURLLoaderThrottles`, `WillCreateURLLoaderFactory` | 기존 Safe Browsing·확장·인증 처리와 공존하는 얇은 호출 지점 후보 |
| Renderer/worker 요청 처리 | `chrome/renderer/url_loader_throttle_provider_impl.cc`: `CreateThrottles` | Browser 쪽 콜백 하나가 모든 subresource를 처리한다고 가정하지 않음 |
| 문서 초기 실행 | `chrome/renderer/chrome_content_renderer_client.cc`: `RunScriptsAtDocumentStart`; `content/public/renderer/render_frame_observer.h`: `DidCreateScriptContext` | 사이트 스크립트보다 먼저 필요한 처리가 준비되어야 함. 실제 삽입 순서·CSP·execution world는 prototype에서 검증 |
| C++/Rust 연결 | `build/rust/rust_static_library.gni`: `cxx_bindings` | 현재 Chromium 빌드 체계에 CXX bridge 경로가 있음. 외부 crate와 의존성 도입은 별도 GN 작업 |
| 프로필 서비스 | `BrowserContextKeyedServiceFactory`/`Profile` | 전역 필터 데이터와 프로필별 설정·예외, 시크릿 상태의 소유권을 구분 |

특히 `content/public/browser/content_browser_client.h`에는 문서·worker·service worker
등 factory 유형이 구분되어 있고, `PrefetchContainer`에
`CreateURLLoaderThrottles()`가 적용되지 않는다는 설명이 있다.
한 콜백에 차단 코드를 추가한 뒤 전체 네트워크 보호가 끝났다고 판단하면 안 된다.
iframe, worker, service worker fetch, redirect, prefetch, 캐시 경로와
WebSocket의 적용 범위를 명시하고 검증해야 한다.

MV2도 단순히 “관련 코드가 없다”는 상태는 아니다. 현재
`extensions/browser/manifest_v2_util.cc`는 component 확장을 deprecation 대상에서
제외하고, `_permission_features.json`에는 MV2 `webRequestBlocking` 정의가 남아 있다.
반면 일반 legacy 확장은 `manifest_v2_handler.cc`에서 차단하며, component loader에는
allowlist가 있다. **내부 uBO 탑재는 실험 후보라는 추론이며 로딩·정상 동작을 검증하지 않았다.**
일반 사용자용 MV2 설치 방법을 제품 경로로 삼기는 어렵다.
[Chrome의 MV2 종료 일정](https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline).

차단 서비스와 renderer 코드는 각각 별도 GN 타깃으로 두는 것이 적합하다.
renderer가 Views 기반 `yee_ui`에 의존하거나 MCP bridge에 차단 정책을 넣지 않는다.
현재 `install-yee-ui-sources.sh`는 Views의 Yee 폴더 바로 아래 파일만 복사하므로,
backend/Rust/renderer 소스를 추가하려면 overlay 동기화 범위도 설계해야 한다.
`TabStripModel`, `TabView`, `WebContents`는 기존 소유권을 유지한다.

## 필터·리소스 운영 제안

- 일반 광고는 EasyList, 추적은 EasyPrivacy, 사이트별 보완은 검증된 uBO/Brave 목록을
  시작 후보로 삼는다. 여러 목록의 예외 우선순위도 함께 검증한다.
- 한국 사이트 대응에는 지역 목록을 별도 검토한다. List-KR은 공식적으로 AdGuard와
  uBO만 지원하므로 `adblock-rust`에서 그대로 완전 호환된다고 가정하지 않는다.
  문법 변환, 미지원 규칙, 필요한 scriptlet을 먼저 조사한다.
  [List-KR 지원 범위](https://github.com/List-KR/List-KR).
- 규칙과 scriptlet/redirect 리소스를 함께 버전 관리한다. 엔진의 uBO 문법 지원이
  최신 uAssets의 모든 scriptlet과 동작을 자동으로 보장하지 않는다.
  [Brave 전용 리소스 저장소](https://github.com/brave/adblock-resources).
- 앱에 초기 규칙을 포함하고, 검증된 갱신본을 원자적으로 교체한다. 갱신 실패 시 마지막
  정상 버전을 유지하고, 특정 사이트 대응을 되돌릴 수 있게 한다.
- 새 페이지의 초기 실행 시점에 필터 조회나 다운로드가 늦지 않도록 준비한다.
  임의의 사용자 구독 목록이 임의의 원격 JS 실행 권한을 얻는 구조로 만들지 않는다.
- 엔진·필터·scriptlet은 출처와 라이선스를 각각 기록한다. 엔진은 MPL-2.0이며,
  재사용 리소스의 조건은 해당 upstream 자료를 별도로 확인한다.
  [엔진 라이선스](https://github.com/brave/adblock-rust/blob/master/LICENSE).

## 차단과 추적 방지의 범위

광고·추적 요청 차단은 Brave 수준의 전체 privacy 보호 중 일부다. 서드파티 쿠키,
스토리지 격리, fingerprinting 방어, CNAME을 이용한 추적 등은 다른 계층에 걸친다.
Yee에 광고 필터만 붙인 상태를 “Brave와 동일한 추적 방지”라고 부르면 안 된다.
[Brave Shields가 다루는 보호 범위](https://brave.com/shields/).

첫 구현 범위는 광고·알려진 추적 요청·페이지 광고 요소·YouTube 대응으로 명확히 하고,
쿠키·스토리지·fingerprinting 강화는 현재 Chromium 동작부터 확인하는 후속 범위로 제안한다.
전체 JavaScript 또는 쿠키 차단을 일괄 기본값으로 적용하는 방식은 제품 결정을 대체하지 않는다.

사이트별 해제, 보호 수준, 상태 표시 위치는 구현 전에 확정할 제품 항목이다.
특히 Omnibox의 Site info → Address → Bookmark 계약을 바꾸는 shield 버튼이나
예약 Sidebar 슬롯을 이 조사만으로 추가하지 않는다.

## 구현 순서와 검증

1. **엔진 통합 feasibility**: 고정된 upstream 버전, CXX/GN 연결, 필터 파싱·예외·리소스
   조회를 작은 타깃에서 확인한다. 지원하지 않는 규칙을 진단 가능하게 한다.
2. **일반 웹 차단**: 프로필 서비스와 요청 경로, CSS 처리, 필터 갱신을 연결한다.
   로컬 fixture에서는 광고/추적 요청의 서버 도착 여부와 정상 요청 보존을 확인한다.
3. **YouTube 대응**: 필요한 scriptlet 리소스와 문서 초기 실행을 붙이고 위의 실제
   재생 수용 기준을 검증한다. 최신 규칙을 가져오는 것과 정상 재생은 별도 확인이다.
4. **제품 설정과 배포**: 사이트 예외·상태 표시·복구 경로를 구현하고 실제 Yee에서
   로그인·결제·영상 사이트의 회귀를 확인한다.

성능 이득의 숫자는 아직 제시할 근거가 없다. 차단으로 전송량·광고 스크립트 실행이
줄어드는 효과와 엔진 메모리·매칭 지연·DOM 처리 비용을 함께 측정해야 한다.
동일 사이트와 동일 규칙, cold/warm cache 조건에서 차단 off/on을 비교하고,
페이지 로딩·CPU·RSS·전송량·재생 시작 지연·프레임 끊김을 기록한다.
전체 DOM을 반복 탐색하는 처리나 요청마다 UI thread에서 목록을 파싱하는 구조를 피한다.

MCP 비교 실험과는 프로필·기능 설정·규칙 버전을 분리해 결과를 섞지 않는다.
차단 때문에 광고 요소와 페이지 타이밍이 바뀔 수 있으므로 기존 실험의 설정을 임의로
바꾸지 않는다. 차단 기능은 사용자 탐색과 에이전트 탐색에 동일한 브라우저 경로로 적용한다.

실제 코드 변경 시에는 해당 작은 단위/통합 테스트와 `build.sh`를 실행하고,
새 빌드의 실앱 검증 전에는 저장소 규칙대로 모든 Yee 브라우저를 graceful shutdown한다.
Browser Surface 레이아웃을 변경하는 경우에만 해당 tier의 layout gate도 적용한다.
현재 산출물은 조사 문서이므로 빌드나 브라우저 재시작은 실행하지 않았다.
