# Brave 경로 대조 및 Yee 개선 기록

검토일: 2026-09-13. [구조 변경 후 검토](content-blocking-structure-review-20260913.md)의
수정 후속 기록이다. 실제 Yee와 YouTube 재생 검증은 MCP 실험 종료 후 진행한다.

이 기록 이후 [독립 재검수](content-blocking-fresh-review-20260913.md)에서 YouTube 후속
객체 변경, overlay 구조 전환 및 검증 도구의 추가 결함을 확인했다. 아래는 당시 수정과
검증 기록이며 새로 확인한 조건의 해결을 뜻하지 않는다.

광고·트래커 및 YouTube 포팅에 한정한 후속
[반복 검수](content-blocking-brave-port-audit-20260913.md)는 실제 CRX 생성기와 pinned
uBlock 함수·Privacy/First Party 목록까지 대조 범위를 확장했다. 동작 결함 3개와
YouTube 포팅 누락 6개 범주를 추가 확인했으며 아직 수정하지 않았다.

이어진 [추가 최종 회차](content-blocking-brave-final-round-20260913.md)에서 초기 빈
iframe의 hook lifecycle 누락 F4와 CSS 입력 격리의 예방 개선 H1을 확인했다.
초기 iframe 후보에 대한 이전 보류 판정은 이 후속 기록으로 갱신한다.

## 검토 범위와 기준 소스

Brave 전체 저장소를 전수 검증했다는 뜻은 아니다. Yee가 연결하는 네트워크 차단,
내비게이션, 엔진, 문서 시작, cosmetic 처리에 해당하는 아래 18개 파일의 관련 함수를
대조했다. 브라우저의 광고 차단은 엔진 하나로 끝나지 않으므로 리소스 조립 진입점,
목록, YouTube 보정 스크립트도 별도로 읽었다.

- brave-core: `11a6bbbc91a941dccca9ae997fb9b5d85991698a`, 커밋 시각
  2026-09-12 14:12:19 UTC.
- adblock-resources: `6da9b247c3368baa8b4f24cd02046e18702a6527`.
- adblock-lists: `0154f33b9d4e1a2630739fd02ba0f2e4d5dc7a3b`.
- Yee 엔진: registry 원본 `adblock 0.13.3`, 현재 `Cargo.lock`의 archive checksum.

GitHub의 brave-core 전체 recursive tree는 `truncated=true`였다. 이 결과를 완전한
파일 목록으로 취급하지 않고 관련 디렉터리의 별도 subtree를 받았다. `browser/net`,
`brave_shields/content/browser`, `cosmetic_filters`, `brave_shields/core/browser`,
`components/third_party/adblock`의 각 목록은 `truncated=false`를 확인했다.
읽은 brave-core 18개 파일의 SHA-256도 확인했다. 원본과 manifest는 ignored
`.local-build/brave-content-blocking-reference/`에 있으며 재조회 가능한 고정 링크는 아래에 있다.

Brave의 구현 코드를 Yee 파일로 복사하거나 이름만 바꾸지 않았다. 변경한 Yee 연결
코드는 독립 작성했고, vendored upstream 엔진 파일은 그대로 보존한다.

## 파일별 대조표

| 기준 파일 | 읽은 경로·동작 | Yee의 처리와 판단 |
| --- | --- | --- |
| [brave_content_browser_client.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/brave_content_browser_client.cc) | `WillCreateURLLoaderFactory`, `CreateWebSocket`, navigation throttle 등록 | Chromium의 기존 factory builder에 Yee를 추가한다. Navigation/subresource 용도를 명시적으로 넘기도록 수정했다. WebSocket은 별도 미지원 경로다. |
| [brave_proxying_url_loader_factory.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/brave_proxying_url_loader_factory.cc) | 요청 시작, redirect/header 변경, 응답·progress·completion, disconnect | Yee의 loader/client pipe 소유권과 전달 함수를 대조했다. 서버 redirect 및 client override 재평가를 유지한다. Brave 자체 URL rewrite·mock response는 지원하지 않는다. |
| [factory unittest](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/brave_proxying_url_loader_factory_unittest.cc) | unsafe redirect, initiator taint, 실제 navigation simulator, 요청 귀속 | Yee는 URL을 자체 생성해 redirect하지 않으므로 Chromium의 redirect 안전성 검사를 유지한다. Yee 테스트에는 실제 navigation factory와 같은 opaque metadata 및 trusted request 정보를 추가했다. Simulator 기반 통합 증명은 아직 아니다. |
| [TP network delegate](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/brave_ad_block_tp_network_delegate_helper.cc) | scheme/type/method/initiator, YouTube aggressive, exception/important, CNAME, rewritten URL, redirect resource, DevTools | Yee는 HTTP(S), type/method, exception/important를 원본 엔진으로 처리한다. main document는 자신의 URL, iframe은 request initiator를 출처로 쓴다. WebSocket/CNAME/rewrite/resource redirect/규칙별 DevTools 정보는 미지원이다. |
| [CSP network delegate](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/brave_ad_block_csp_network_delegate_helper.cc) | document/subdocument의 CSP 조회 및 응답 헤더 반영 | Yee에 `$csp` 조회와 응답 적용을 추가했다. 기존 서버 CSP와 parsed security metadata를 유지하고 추가 enforce 정책을 append한다. |
| [brave_proxying_web_socket.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/brave_proxying_web_socket.cc) | URLLoader와 별도의 handshake/auth/header/error 경로 | 현재 Yee URLLoader proxy가 이 경로를 차단한다고 주장하지 않는다. 별도 hook과 worker/context·handshake 회귀 검증이 필요하다. |
| [url_context.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/url_context.cc) | frame/tab 출처, shields·ads 설정, 요청 정보 갱신, 원 initiator 보존 | Yee는 Profile/RFH를 매칭 thread에 보관하지 않는다. owning factory context와 browser-authored request metadata를 이용한다. 사이트 예외는 publisher top context로 판정한다. 프로필별 UI 설정은 아직 없다. |
| [ad_block_engine.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/ad_block_engine.cc) | Rust request 변환, CSP, cosmetic, 엔진 로드/DAT, sequence·discard | Yee의 CXX API에 CSP를 추가했다. 원본 single-thread 엔진을 전용 sequence에서 재사용한다. DAT·regex discard 설정은 미지원이다. |
| [ad_block_engine_wrapper.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/ad_block_engine_wrapper.cc) | default/additional 엔진, first-party 정책, exception/important 병합, cosmetic 결과 | Yee는 고정된 하나의 목록 집합을 한 엔진에 넣는다. Brave의 standard/aggressive 및 여러 엔진 정책과 동등하지 않다. 예외 우선순위 자체는 upstream 엔진에 맡긴다. |
| [ad_block_service.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/ad_block_service.cc) | provider observer, resource/list 로드, DAT cache, custom/subscription provider, sequence | Yee는 고정 번들을 browser/renderer에 함께 포함한다. updater·사용자 구독·generation 교체는 별도 미구현 기능이다. |
| [domain_block_navigation_throttle.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/domain_block_navigation_throttle.cc) | primary main frame, start/redirect, target-site 설정, 자신을 first party로 검사, weak callback, interstitial | Yee의 main-frame site/source를 각 이동 URL 자체로 수정했다. Yee는 네트워크 시작 전 `ERR_BLOCKED_BY_CLIENT`와 Chromium 오류 페이지를 사용한다. Brave 전용 interstitial·진행 버튼은 구현하지 않았다. |
| [domain_block_tab_storage.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/domain_block_tab_storage.cc) | interstitial proceed 상태·ephemeral storage 수명 | Yee에는 이 상태를 모방한 임시 허용 플래그가 없다. 전용 차단 페이지를 추가할 때 navigation 단위 허용 범위를 설계해야 한다. |
| [default_resource_provider.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/core/browser/ad_block_default_resource_provider.cc) | component `resources.json`, 준비 전 empty storage, 비동기 파일 읽기 | Yee production resource는 빈 목록이다. Brave/uBO scriptlet 묶음이 자동으로 포함되는 것으로 취급하지 않는다. |
| [cosmetic_filters_js_handler.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/renderer/cosmetic_filters_js_handler.cc) | URL 설정·first-party filtering, scriptlet 주입, user-origin CSS, generic exception, CSS 배치 | Yee에 문서별 selector dedup 및 교체 가능한 CSS chunk를 추가했다. main-world scriptlet과 isolated generic collector는 용도가 다르다. Brave의 procedural/action·element picker는 미지원이다. |
| [cosmetic render frame observer](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/renderer/cosmetic_filters_js_render_frame_observer.cc) | isolated-world origin/empty CSP, inherited about:blank, navigation weak pointer 무효화, document-start readiness | Yee에 isolated-world origin과 non-null empty CSP를 설정했다. `about:blank/srcdoc`은 해당 frame의 상속 origin으로 판정한다. 문서 교체 때 weak pointer와 stylesheet 상태를 초기화한다. |
| [content_cosmetic.ts](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/resources/data/content_cosmetic.ts) | class/id 수집, observer·polling 전환, throttle, exceptions, procedural 처리 | Yee는 제한된 idle batch와 overflow 재순회를 유지한다. collector의 seen set이 재사용되어도 native CSS가 중복 증가하지 않도록 수정했다. Shadow DOM·procedural 동등성은 없다. |
| [procedural_filters.ts](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/resources/data/procedural_filters.ts) | has-text, matches-attr/css/media/path/property, upward, xpath 등 | 일반 CSS selector 엔진과 다른 실행기다. Yee의 `InsertStyleSheet`에 문자열을 넘기는 것만으로 지원되지 않는다. 별도 독립 실행기가 필요한 미지원 기능으로 기록했다. |
| [cosmetic_filters_resources.cc](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/browser/cosmetic_filters_resources.cc) | class/id 입력, URL resources, procedural action 직렬화·style 변환 | Yee CXX API는 selectors/exceptions/script/generichide만 전달한다. procedural action 결과를 전달하지 않는다는 경계를 확인했다. |

## 수정한 항목과 회귀 검증

| 발견 사항 | 구현 결과 | 검증 |
| --- | --- | --- |
| 실제 navigation factory가 opaque라 proxy가 생기지 않음 | `FactoryPurpose`를 전달하고 navigation은 proxy를 설치한다. main target 및 trusted iframe top site를 요청마다 판정한다. | opaque navigation, main/iframe, publisher/target 예외, server redirect, disabled feature, initiator/type native 테스트 |
| main navigation을 이전 페이지의 third party로 잘못 분류할 수 있음 | 최상위 문서는 이동 URL 자체를 first-party source로 사용한다. iframe의 출처와 분리한다. | document+third-party 규칙이 정상 main navigation을 막지 않는 native 테스트 |
| 엔진의 CSP 규칙이 응답에 적용되지 않음 | Rust→CXX 조회, document/subdocument 응답 처리, raw header 및 parsed enforce 정책 추가 | 엔진 resource scope/exception, 기존 CSP 유지, publisher 예외, CSP filter 예외, parsed header 없는 응답 native 테스트 |
| page CSP를 상속한 generic isolated world·상속 문서 누락 | 전용 security origin/empty CSP 및 `about:blank/srcdoc` frame origin fallback | 실제 탭 fixture에 strict CSP·blank·srcdoc 검증 추가. 실행은 보류 |
| YouTube의 넓은 sibling이 player 탐색을 고갈시킴 | known player container를 우선 처리하고 arbitrary enumeration을 뒤로 미룬다. | 5,000개 sibling 객체, root의 15,000개 sibling, nested player 테스트 |
| 같은 player 객체·nested player 교체 후 광고 필드가 복귀함 | 광고 필드 접근자와 기존 writable player container setter를 보호한다. 객체 identity는 유지한다. | retained reference 변경, nested replacement, cycle, frozen object 테스트 |
| inherited key가 작업량 제한 밖에 있음 | inherited/own check를 포함해 방문 budget을 차감한다. 임의 getter는 실행하지 않는다. | 20,000개 inherited key의 Proxy descriptor count, getter 오류 테스트 |
| text JSON 재직렬화가 숫자·escape를 변경함 | native parse는 유효성 확인에만 사용하고 원문 member span을 제거한다. | 71개 제거 조합, `9007199254740993`, 지수 표기, Unicode escape, 중복 key, comma, nested overlap, malformed/depth fallback 테스트 |
| generic selector 재전송으로 stylesheet가 누적됨 | 문서별 dedup, chunk당 64 KiB/전체 4 MiB/65,536 selector 제한. 변경된 tail sheet를 명시적으로 제거 후 재삽입한다. | 반복 발견, chunk/tail, document reset, oversized rule, total CSS budget native 테스트 |
| warm overlay에 삭제된 소스가 남음 | catalog의 owned mirror에서만 stale 파일을 사전 검사 후 삭제한다. original/generated 경로는 범위 밖이다. Python/PowerShell 모두 반영한다. | owned stale prune, original/generated 보존, source/destination symlink 거부 tooling 테스트 |
| 재vendoring이 이전 소스·GN 입력을 남김 | archive와 원본을 검증한 새 staging tree 전체를 교체한다. 검증 실패 시 이전 tree를 보존한다. | stale Rust/GN 입력 제거, 검증 실패 보존, unverified source 거부, Cargo marker 허용 tooling 테스트 |
| fixture가 CDP 준비 전 실패하면 소유 브라우저가 남음·이름 변경 guard 누락 | stable bundle ID+실제 executable inventory. CDP close 실패는 해당 child PID의 AppKit 정상 종료로 fallback한다. | startup/CDP failure/success/timeout·rename guard fake runtime, renamed bundle/process tooling 테스트, Swift typecheck |

CSS의 논리적인 collector token 재전송은 여전히 가능하다. 이를 다시 discovery하지 않는
것처럼 테스트하지 않는다. native stylesheet 저장소에서 같은 selector를 한 번만 적용하는
것이 해결책이다. Blink의 같은 key 재삽입은 교체가 아니므로 `RemoveInsertedStyleSheet`
후 새 key를 기록한다.

CSP는 기존 정책을 지워 합치지 않는다. 서버가 Network Service에서 이미 파싱한 정책과
다른 보안 필드는 그대로 두고, hash 고정 filter bundle에서 얻은 추가 정책만 파싱한다.
`parsed_headers`가 없는 특수 응답은 원래 응답을 보존하며 필터 CSP를 추가하지 않는다.

## YouTube 리소스 대조

| 기준 소스 | 확인 사항 | Yee 판단 |
| --- | --- | --- |
| [README](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/README.md), [index.js](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/index.js), [build.js](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/build.js), [metadata](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/metadata.json) | metadata의 실제 파일을 base64 resource로 조립하고 dist를 생성한다. 목록 catalog와 resource는 별도다. | `adblock-rust`를 연결했다고 이 리소스가 따라오는 것은 아니다. Yee의 production empty resource와 구별한다. |
| [목록 catalog](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/filter_lists/list_catalog.json) | 구독 목록 출처·목록 종류가 엔진 외부 데이터다. | Yee는 현재 EasyList/EasyPrivacy 고정 snapshot과 자체 규칙이다. Brave 목록 전체가 아니다. |
| [navigation fix](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/resources/brave-youtube-navigation-fix.js) | `performance.now` 단조성 보정 | 광고 데이터 제거 모듈과 다른 호환성 보정이다. Yee에 전역 timing API 변경을 추가하지 않았다. |
| [playback speed fix](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/resources/brave-youtube-playback-speed-fix.js) | player rate 변경 이벤트를 sessionStorage에 기록한다. | 실제 Yee에서 발생한 속도 저장 문제의 증거 없이 가져오지 않았다. |
| [theater fix](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/resources/brave-youtube-theater-fix.js) | wide cookie를 localStorage로 보완하고 특정 조건에 reload한다. | cookie/주기 timer/reload 정책은 현재 Yee 모듈에 없다. |
| [brave-unbreak](https://github.com/brave/adblock-lists/blob/0154f33b9d4e1a2630739fd02ba0f2e4d5dc7a3b/brave-unbreak.txt), [brave-firstparty](https://github.com/brave/adblock-lists/blob/0154f33b9d4e1a2630739fd02ba0f2e4d5dc7a3b/brave-lists/brave-firstparty.txt) | YouTube용 활성 규칙, 예외, 호환성 보정과 주석 처리된 실험 규칙이 공존한다. | 주석을 현재 활성 광고 차단으로 취급하지 않는다. 특정 API hook 몇 개가 Brave 전체 YouTube 처리와 같다는 결론을 내리지 않는다. |

현재 Yee YouTube 모듈은 초기 player, fetch `Response.json/text`, XHR의 player/next
데이터에만 적용한다. 본 영상·자막·playability·Web API body lifecycle을 유지한다.
작업량을 넘거나 frozen/nonconfigurable object를 만나면 원래 데이터를 남길 수 있다.
임의 page Proxy trap 자체의 실행 시간을 JS 방문 budget으로 강제 제한할 수는 없다.
광고 차단 감지, 다른 초기 데이터 경로, 서버 영상 결합, 로그인·Music·모바일·A/B 변형은
실제 재생 검증 대상이다. 리서치나 fixture 통과를 광고 없는 실제 재생 성공으로 표현하지 않는다.

## 남은 기능과 검증 경계

1. **WebSocket/CNAME**: URLLoader 이외 handshake와 DNS canonical-host 처리가 없다.
   HTTP(S) 광고·트래커 차단과 구분해야 한다.
2. **Procedural/action/Shadow DOM**: Yee의 CSS 삽입·generic collector는 별도 실행기를
   대신하지 않는다. 현재 snapshot의 EasyList에는 `#?#` 줄 289개가 있다. 파서가
   거부하거나 결과를 Yee가 전달하지 않는 줄을 지원 완료로 세지 않는다. 일반 CSS `:has`
   자체와 `:has-text` 등의 비표준 실행기는 다른 기능이다.
3. **Resource redirect/URL rewrite**: 현재 고정 EasyList/EasyPrivacy에는
   `redirect=`, `redirect-rule=`, `removeparam` 문자열은 없지만, 원본 엔진이 redirect
   별칭으로 처리하는 `$rewrite=abp-resource:*`가 EasyList 7줄·EasyPrivacy 1줄 있다.
   CSP 줄은 각 2개다. bool-only network API와 빈 production resource 때문에 대체
   응답을 처리하지 못하는 영향은 현재 입력에도 있다. 후속 반복 검수의 production
   googlevideo 규칙 진단으로 확인했다.
4. **Updater/프로필 설정/전용 interstitial**: runtime generation 교체와 rollback,
   사용자 구독, 프로필별 UI 설정, rule별 차단 원인 및 proceed UI는 아직 없다.
5. **실제 Chromium 통합**: native proxy 테스트는 Mojo 수준 증거다. navigation simulator,
   shared/service worker 복수 client, BFCache/prerender/fenced frame, 다운로드와
   partition별 정책 전달, 실제 strict CSP 및 inherited frame 동작은 추가 통합 증거가 필요하다.
6. **플랫폼/배포**: macOS Swift는 typecheck하고 fake lifecycle을 검증한다. AppKit 종료
   동작과 Windows PowerShell·앱 빌드는 실행 검증하지 않았다. 소스·고지 archive는
   빌드 산출물로 확인하되, 사용자에게 보이는 접근 안내는 배포 전 별도 확인 대상이다.

## 실행 기록

- tooling: 60개 테스트 통과.
- YouTube adapter: 위 object/API 경계와 lossless JSON 71개 case 통과.
- generic collector 및 fake runtime lifecycle 테스트 통과.
- macOS graceful-quit helper: Swift typecheck 통과. helper는 실행하지 않았다.
- 최종 native gate: core/설정/stylesheet 13개와 Mojo factory 30개, 총 43개 통과.
- 전체 앱 빌드: 기본 35 GiB guard, `YEE_BUILD_JOBS=3`, 최종 빌드 통과
  (2분 35초, 7 steps). 실앱을 실행하지 않았다.
- packaging: 실제 앱 bundle의 소스 archive 2,277 entries, 61개 crate의 원본 snapshot
  2,206개 파일 hash 확인 (registry archive 원본 2,145개 + Cargo marker 61개).
  MPL 고지와 root LICENSE 포함, 독립 Yee source/test 규칙 제외 확인.
- 빌드 입력으로 생성된 YouTube header가 현재 JS 원문과 일치함을 확인했다.
- 기존 간접 의존성에 기대던 factory test의 traffic-annotation/Blink mojom GN 의존성을
  명시했다. Browser/renderer/core의 GN header gate는 모두 통과했다.
- 최종 소스 동기화 preview 0개, 실제 Chromium/root whitespace와 shell patch
  reverse apply 검사 통과.
- 실제 브라우저 fixture: nav/iframe/server redirect, type-only rule, blank/srcdoc,
  strict CSP CSS 및 filter CSP image 차단 검증을 추가했으나 아직 실행하지 않았다.

Fixture CDP 연결은 open/error뿐 아니라 early close와 15초 connection/request timeout을
처리한다. Discovery HTTP 요청에도 15초 timeout을 적용했다. 연결 종료·잘못된 protocol
입력은 pending promise를 즉시 거부한다. 이 경로도 fake socket으로 검증했다.

이번 작업에서 기존 Yee를 종료하거나 앱을 실행하지 않았다. 다른 MCP 세션의 실험을
유지한 상태로 소스 수정, 빌드, headless 단위 테스트와 fake lifecycle만 진행한다.
