# 광고 차단·YouTube Yee 포팅 반복 검수 — 2026-09-13

후속 사용자 요청으로 제품을 수정했다. 현재 상태와 동등성의 검증 경계는
[구현 기록](content-blocking-implementation-20260913.md)을 따른다. 아래는 수정 전 감사 기록이다.

사용자의 범위 정정을 반영한 검토다. 대상은 광고·트래커 요청 차단과 YouTube 광고
데이터 처리의 Yee 포팅이다. overlay 동기화, 배포 도구, MCP 성능 및 UI layout 검토는
포함하지 않는다. 제품 코드와 Chromium glue는 수정하지 않았다.

결과: **추가 동작 결함 3개와 YouTube 포팅 누락 6개 범주**를 확인했다. 기존 YouTube
후속 객체 변경 결함은 여전히 재현되며 중복 집계하지 않는다. 마지막 두 검토 회차에서는
이 문서의 범위 안에서 새로운 결함·누락 범주가 없었다. 현재 상태를 Brave와 동등한
광고 차단 또는 실제 YouTube 광고 없는 재생으로 판정할 수는 없다.

**후속 갱신:** [추가 최종 회차](content-blocking-brave-final-round-20260913.md)에서 초기
빈 iframe의 lifecycle 누락 F4를 확인하고 CSS 오류 격리 H1을 예방 개선으로 기록했다.
이 문서의 회차 4/5와 iframe 후보 보류는 당시 기록이다. 후속 결과를 반영한 추가 동작
결함은 F1–F4이며 Y1–Y6와 함께 미수정이다.

실제 Yee 종료·재실행과 실제 YouTube 재생 검증은 사용자 요청대로 다른 세션의 MCP
실험 종료 후 진행한다. 아래 실행은 Node fixture와 headless 네이티브 테스트다.

## 기준 소스와 판정 방법

기존 [파일별 비교](content-blocking-brave-comparison-20260913.md)의 18개 brave-core
파일에 요청 context, 네트워크 테스트·stub 응답, Rust 연결 파일을 추가 대조했다.
추가로 실제 필터·리소스 생성기의 진입점을 따라 uBlock 원본 함수를 실행했다.

| 기준 | 고정 버전과 읽은 동작 |
| --- | --- |
| [brave-core](https://github.com/brave/brave-core/tree/11a6bbbc91a941dccca9ae997fb9b5d85991698a) | `11a6bbbc91a941dccca9ae997fb9b5d85991698a`: 요청 귀속·type·예외·redirect·CSP·Mojo 수명·문서 시작·CSS·Rust consumer |
| [CRX 생성기](https://github.com/brave/brave-core-crx-packager/blob/9a588ced646a27c971c74421b318901cda8d5215/lib/adBlockRustUtils.js) | `9a588ced646a27c971c74421b318901cda8d5215`: 실제 `generateResources()`, `preprocess()` 및 목록 생성 호출 |
| [생성기의 uBlock submodule](https://github.com/brave/uBlock/tree/06b48b9dfc183f7dee1e6a9abe7e07a213d406f1) | `06b48b9dfc183f7dee1e6a9abe7e07a213d406f1`: fetch/XHR 응답, JSON.parse, constant, 요청 body, DOM bypass 함수와 재귀 의존성 |
| [목록 catalog](https://github.com/brave/adblock-resources/blob/6da9b247c3368baa8b4f24cd02046e18702a6527/filter_lists/list_catalog.json) | `6da9b247c3368baa8b4f24cd02046e18702a6527`: Default Adblock, Privacy 및 First Party의 입력 구성 |
| [uAssets](https://github.com/uBlockOrigin/uAssets/tree/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters) | `019d5d477da8fc60f3aaf6a3b41bd51a5e961b39`: 기본 필터 15개, 특히 filters/quick-fixes/privacy |
| [Brave 자체 목록](https://github.com/brave/adblock-lists/tree/0154f33b9d4e1a2630739fd02ba0f2e4d5dc7a3b) | `0154f33b9d4e1a2630739fd02ba0f2e4d5dc7a3b`: unbreak, specific, social, sugarcoat, Android, First Party와 regional |
| [패키징 미러](https://github.com/brave/adblock-lists-mirror/tree/f87683d120015d077f971ad3963515ec84d71eb2/lists) | `f87683d120015d077f971ad3963515ec84d71eb2`: filters와 quick-fixes의 미러 입력을 확인. 위 uAssets 파일과 byte 일치 |
| Yee | 현재 소스와 적용된 Chromium 소스의 기능 입력 25개 byte 일치. GN이 컴파일한 `adblock 0.13.3` rlib 및 실제 factory object를 독립 진단에 연결 |

생성기는 uBO `builtinScriptlets`를 원본 함수·alias·dependency와 함께 등록하고 Brave
resources를 추가한다. Yee production `resources.json`은 빈 목록이다. 엔진을 사용한다는
사실만으로 Brave의 scriptlet 처리나 YouTube 보정이 따라오지 않는다.
[실제 조립 진입점](https://github.com/brave/brave-core-crx-packager/blob/9a588ced646a27c971c74421b318901cda8d5215/lib/adBlockRustUtils.js#L65)을 기준으로 판단했다.

Brave 원본 전처리 함수를 수정하지 않고 fixture에서 실행했다. 검토 입력 24개는
uAssets 15개, Brave 목록 8개, Yee의 고정 EasyPrivacy다. 키워드로 찾은 비주석 규칙
166줄 중 149줄이 전처리에 남고 17줄이 제외된다. 다른 사이트·부정 도메인·호환성
규칙도 포함된 검색 결과이며, 149줄 모두가 YouTube 광고 규칙이라는 뜻은 아니다.
전처리 통과는 Rust 파서 수용이나 서명된 실제 배포 component 적용을 증명하지 않는다.
기존 EasyList snapshot은 조건부 지시문이 없음을 확인했다. URLHaus 악성 URL 입력과
iOS 전용 목록은 이번 광고·트래커 포팅 검토의 동작 대조 대상에서 제외했다.

원본·manifest·진단 script는 ignored
`.local-build/brave-content-blocking-port-review/`에 저장했다. 고정 소스 hash 106개를
검증했다. 위 저장소 전체를 모든 실행 조합으로 전수 검증했다는 의미는 아니다.

## 추가 동작 결함

### F1. [P2] 조건부 필터의 ABP 전용 예외가 추적 요청을 허용한다

위치: [`embed_rules.py`](../components/content_blocking/embed_rules.py#L13),
[`new_engine()`](../components/content_blocking/rust/src/lib.rs#L44).

Yee는 원본 목록을 그대로 합친다. Rust 파서는 `!#if/endif`를 주석으로 무시하므로
조건 안의 규칙을 모두 넣는다. 현재 EasyPrivacy 15137–15164줄의 `ext_abp` 블록에는
`abema.tv`에서 New Relic을 허용하는 두 예외가 있다. Brave 생성기는 `ext_abp=false`로
이 블록을 제거한다.
[Brave 조건 및 전처리](https://github.com/brave/brave-core-crx-packager/blob/9a588ced646a27c971c74421b318901cda8d5215/lib/adBlockRustUtils.js#L218).

현재 GN 엔진에 정확한 Yee production 목록을 넣은 결과:

| 요청·출처 | 현재 Yee 목록 | 동일 목록에 Brave 전처리를 적용 |
| --- | --- | --- |
| `bam.nr-data.net/events`, `abema.tv`, XHR | 허용 | 차단 |
| 같은 요청, `other.test`, XHR | 차단 | 차단 |
| `js-agent.newrelic.com/nr-loader.js`, `abema.tv`, script | 허용 | 차단 |

증거: `engine-probe.json`, `easyprivacy-preprocessed.txt`, `preprocessing-probe.json`.
수정 기준: 원본과 출처·hash는 보존하고 컴파일 입력에 환경별 전처리를 적용한다.
중첩·else·잘못된 stack과 미지 조건의 처리 계약도 정해야 한다. upstream 엔진을
수정할 필요는 없다.

### F2. [P2] prefetch를 XHR로 분류해 `$other` 규칙을 놓친다

위치: [`RequestType()`](../browser/content_blocking/filtering_url_loader_factory.cc#L37),
특히 빈 destination 처리.

현재 Chromium의 `LinkPrefetchResource`는 `ResourceType::kPrefetch`와 destination
`kEmpty`를 만든다. Yee가 resource type으로 우선 처리하는 네 종류에 prefetch가 없어
XHR로 분류한다. Brave는 prefetch를 빈 type 문자열로 넘기며 원본 Rust는 이를
`Other`로 해석한다.
[Brave type 변환](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/ad_block_engine.cc#L29).

실제 factory object를 연결한 Mojo fixture에서
`cdn.fixture.test/client_204?event=fixture` prefetch가 downstream에 도달하고 성공한다.
현재 EasyPrivacy의 `/client_204?$image,other,ping,script`가 적용되지 않는다. 동일 GN
엔진에서 XHR로 평가하면 허용, `other` 또는 빈 문자열로 평가하면 차단이다.

증거: `native-extra-run.log`의 `FreshAuditPrefetchMissesProductionOtherRule`,
`engine-probe.json`의 `request_type_cases`.
수정 기준: 빈 destination을 전부 XHR로 간주하지 말고 Chromium이 부여한 resource
type을 포함한 변환 계약을 완성한다. 정상 fetch/XHR·ping과 prefetch를 함께 확인한다.

### F3. [P2] trusted prefetch의 요청별 사이트 예외를 factory 정보가 덮어쓴다

위치: [`PolicyContext()`](../browser/content_blocking/filtering_url_loader_factory.cc#L176),
[`MaybeAppendFilteringFactory()`](../browser/content_blocking/filtering_url_loader_factory.cc#L291),
Chromium `WillCreateURLLoaderFactory` glue.

요청별 trusted top origin은 navigation purpose에만 사용한다. cross-origin prefetch
factory는 빈 IsolationInfo로 만들고 각 요청에 목적지 origin을 trusted top origin으로
넣는데, Yee glue는 이를 일반 subresource로 취급한다. 따라서 factory 생성 시의
initiator/top-site를 사용하고, 그 사이트가 예외이면 proxy 자체를 생략한다. Brave의
context 생성은 요청의 trusted IsolationInfo를 우선 사용한다.
[Brave 요청 context](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/net/url_context.cc#L488).

현재 Chromium의 `CreateForPrefetch()`, RFH cross-origin factory 생성 및
`PrefetchURLLoaderServiceContext`의 trusted request 작성 경로를 따라 대조했다.
동일 metadata를 실제 Yee factory에 전달한 Mojo 결과:

| 사이트 설정과 요청 | 관측 |
| --- | --- |
| 목적지 `yee-block.test` 차단 off, 출발지 `page.test` on | 목적지 요청을 차단 |
| 출발지 `page.test` off, trusted 목적지 `yee-block.test` on | 목적지 요청을 downstream에 전달 |

증거: `FreshAuditTrustedTargetExceptionIsIgnored`,
`FreshAuditDisabledPublisherSkipsProtectedPrefetch` 두 진단.
수정 기준: 요청마다 정책 문맥이 달라지는 trusted factory를 구분해 proxy 설치와
요청 평가를 함께 고친다. renderer가 주장하는 metadata를 무조건 신뢰하는 변경은
해결책으로 삼지 않는다. 일반 subresource·main/iframe·worker의 기존 정책도 유지한다.
실제 링크를 통한 Chromium prefetch 통합 실행은 아직 하지 않았다.

## YouTube 포팅 누락

아래는 현재 구현의 커버리지와 Brave 기준 동작의 차이다. fixture에서 광고 데이터가
남거나 해당 보정이 없는 것을 확인했으며, 실제 계정의 광고 재생 실패를 관측한 결과는
아니다. 같은 endpoint 문제의 여러 API 변형을 별도 결함으로 부풀리지 않았다.

| ID·우선순위 | Yee의 누락과 재현 | Brave 기준 |
| --- | --- | --- |
| Y1 · P2: 응답 주소·형식 | player/next만 처리한다. get_watch의 adSlots, playlist/watch 응답은 hook 밖이다. 처리 대상 player도 XSSI prefix 등 순수 JSON이 아닌 text에는 광고 필드를 남긴다. | [기본 filters](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/filters.txt#L18)의 watch/playlist XHR 보정, [quick-fixes](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/quick-fixes.txt#L57)의 playlist 및 get_watch fetch 보정. trusted replacement는 JSON 파싱 없이 text를 변경한다. |
| Y2 · P2: fetch 본문 소비 | Response.json/text만 감싼다. player 응답을 arrayBuffer 또는 body reader로 읽으면 광고 데이터가 남는다. | [json-prune-fetch-response](https://github.com/brave/uBlock/blob/06b48b9dfc183f7dee1e6a9abe7e07a213d406f1/src/js/resources/json-prune.js#L78)와 [trusted replacement](https://github.com/brave/uBlock/blob/06b48b9dfc183f7dee1e6a9abe7e07a213d406f1/src/js/resources/scriptlets.js#L202)는 fetch 결과의 Response 본문을 교체한다. 실제 규칙 인자로 arrayBuffer/reader에서도 제거됨을 확인했다. |
| Y3 · P2: 초기 데이터·파싱 | 전역 playerResponse를 설치 전/후 할당해도 광고가 남는다. Music의 JSON.parse로 만든 player 객체도 그대로다. ytInitialPlayerResponse는 제거되는 대조군이다. | [filters 35–40줄](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/filters.txt#L35): playerResponse constant와 Music/Kids/nocookie의 JSON.parse pruning. 원본 함수에 실제 규칙 인자를 넣어 확인했다. |
| Y4 · P2: Shorts 광고 항목 | reel_watch_sequence endpoint를 제외한다. hook이 실행되는 next에서도 entries의 adClientParams.isAd 광고 항목을 제거하지 않는다. 네 가지 필드 삭제만으로는 항목 삭제가 되지 않는다. | [Shorts fetch/JSON.parse 규칙](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/filters.txt#L45). 원본 object-prune의 `[-]`는 조건 경로가 있는 배열 항목을 제거한다. 광고 항목만 제거하고 정상 항목은 남기는 대조군을 확인했다. |
| Y5 · P2: 임베드·Kids 도메인 | renderer는 DomainIs(youtube.com)만 설치하고 JS도 그 host만 처리한다. youtube-nocookie.com과 youtubekids.com은 설치·응답 판정 양쪽에서 제외된다. 강제로 script를 설치한 fixture에서도 광고 데이터가 남는다. | [기본 데이터 hook 규칙](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/filters.txt#L35)에 두 도메인이 명시돼 있다. Music은 Yee 현재 hook의 정상 대조군이다. |
| Y6 · P2, 호환성 설계 필요: 요청·재생 보정 | Yee에는 요청 body/client experiment flag 변경과 buffering/UNPLAYABLE 재생 복구가 없다. `channel` UA 조건의 player XHR body는 WATCH 그대로이며, Brave의 실제 26줄 함수는 CHANNEL로 변경한다. | [quick-fixes 25–35줄](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/quick-fixes.txt#L25). script 수정·조건부 요청 수정·네트워크 experiment 설정·재생 복구가 결합돼 있다. 해당 전략 전체의 성공은 fixture 하나로 증명하지 않는다. |

Yee 위치: [`youtube.js`](../renderer/content_blocking/youtube.js#L81)의 endpoint/text,
같은 파일 158줄 이후의 global/Response/XHR hook,
[`document_filter_agent.cc`](../renderer/content_blocking/document_filter_agent.cc#L145)의
도메인 gate. JSON.parse 및 fetch 함수 자체를 감싸는 처리는 없다.

증거: `youtube-port-probe.json` 24개 진단과 `brave-scriptlet-probe.json` 8개 원본 함수
대조. 본 영상 videoId/streamingData, 정상 Shorts 항목 및 응답 header 보존도 확인했다.
원본 함수 인자는 실제 필터 줄을 uBO ArglistParser로 읽어 전달했다.

Y6의 원본 보정에는 seek/timer/UA 변경 전략도 포함돼 있다. 현재 Yee 모듈의
“광고를 mute/seek하며 지나가지 않는다”는 구현 방향과 함께 검토해야 한다. 단순히
Brave 전체 코드를 복사하거나 UA만 바꾸는 것을 수정 완료로 삼지 않는다. 로그인·Premium·
Music·임베드·Shorts와 서버/A/B 변형에서 실제 광고 요청과 정상 재생을 함께 확인한다.

## 기존 한계의 추가 증거와 잘못된 판정 정정

- **기본 목록 구성 차이:** 이미 알려진 “EasyList/EasyPrivacy와 Brave 전체 목록은
  다르다”는 한계의 실제 사례를 추가했다. 현재 GN 엔진은 TVHTML5/oad initplayback,
  외부 `_ad_` media, youtube.com의 log_event fixture를 허용한다. 동일 엔진에
  전처리된 Brave 기본 filters/quick-fixes/privacy 입력을 추가하면 세 요청 모두
  차단한다. `engine-probe.json`의 youtube_network_cases다. 실제 배포 Brave를
  실행한 결과는 아니며 이 세 변형을 새 결함 세 개로 집계하지 않는다.
- **대체 응답은 현재 입력에도 필요하다:** 현재 목록에는 `redirect=` 문자열은 없지만
  `$rewrite=abp-resource:*`가 EasyList 7줄·EasyPrivacy 1줄 있다. 원본 Rust는 rewrite를
  redirect 별칭으로 지원한다. googlevideo videoplayback의 production 규칙은 현재
  차단=true, replacement 없음이다. Brave의 [redirect resource metadata](https://github.com/brave/uBlock/blob/06b48b9dfc183f7dee1e6a9abe7e07a213d406f1/src/js/redirect-resources.js#L145)는 blank-mp4 alias를 noop-1s.mp4에 연결하고 [stub response](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/browser/adblock_stub_response.cc)는 대체 응답을 반환한다. 기존 문서의 “목록을 바꿀 때 재검토”라는 설명은 정정한다. 이전에 기록한 resource redirect 미지원의 현재 영향이며 새 범주로 중복 집계하지 않는다.
- **후속 객체 변경:** [이전 P2 재현](content-blocking-fresh-review-20260913.md)이 다시 확인됐다. 보관한 child에 새 광고 key/container 추가 및 delete/redefine으로 보호를 우회하는 문제는 미수정이다.
- **리소스 검증:** valid JSON/invalid base64 resource의 등록 실패를 GN 엔진에서 확인했다.
  이전 P3 판정을 강화하는 증거다. Brave Rust consumer도 convenience constructor와
  무시되는 등록 오류를 사용하므로 Brave만 더 강한 검증을 제공한다고 표현하지 않는다.

기본 목록을 무조건 전부 추가하는 것도 해결책이 아니다. rules/resource/consumer 지원,
프로필 모드 및 호환성 예외를 함께 맞춰야 한다. 파싱 성공과 실제 실행을 별도로 확인한다.

## 반복 검토 회차와 재현하지 못한 후보

각 회차는 요청·정책·응답·문서·데이터 경로를 확인하고, 발견한 조건은 뒤 회차에서
동일 원인과 새 원인을 구분했다. 기존 UI/tooling 문제는 이번 집계에서 제외했다.

| 회차 | 대조 방향과 추가 확인 | 새 범주 |
| --- | --- | --- |
| 1 | Yee 요청/type/예외·문서 시작·YouTube hook을 고정 Brave 파일의 관련 함수와 대조. 현재 production 조건부 규칙을 GN 엔진으로 비교 | F1 및 Y1–Y5 |
| 2 | 실제 CRX 조립 진입점 → pinned uBlock 함수 → 전처리된 규칙 → Yee consumer. 요청·복구 보정과 fetch 본문 교체를 원본 함수로 대조 | Y6. Y1/Y2/Y3/Y4의 원본 대조 증거 보강 |
| 3 | Chromium의 실제 prefetch metadata 작성과 factory 호출 지점까지 역추적. 현재 factory object로 세 Mojo 진단 실행. Privacy/First Party 입력도 확인 | F2/F3. 기존 목록·resource redirect 한계의 현재 증거와 문서 오류 확인 |
| 4 | Brave에서 Yee 방향으로 재검토: 요청 type/source/사이트, redirect/CSP/stub, 문서 수명/CSS/script, 전처리 분기·각 YouTube 데이터 경로·요청 보정 | **0**. 조건부로 제외되는 규칙·다른 사이트 규칙·기존 미지원은 추가 결함으로 세지 않음 |
| 5 | Yee에서 Brave 방향으로 재검토: Rust export의 C++ 소비, Chromium glue, 모든 factory/client callback, renderer gate/global/Response/XHR, 필터·리소스 입력과 진단의 대조군 | **0**. source hash·적용 소스 일치·재현 결과 재확인 |

고려했으나 새 확정 결함으로 채택하지 않은 후보:

| 후보 | 판단 |
| --- | --- |
| www YouTube의 모든 player JSON.parse도 Brave가 무조건 정리한다 | quick-fixes의 해당 일반 player pruning은 Firefox 조건 안이다. Brave 전처리가 제외한다. 실제 확인한 Music pruning과 Shorts parsing, playerResponse global로 범위를 한정 |
| HTML `$replace`·주석 처리된 JSON.stringify 실험을 Brave 활성 처리로 간주 | cap_html_filtering 분기의 17줄 및 주석을 제외했다. 실제 남는 scriptlet 기준으로 비교 |
| 새 iframe에서 fetch/JSON.parse를 가져오면 Yee를 반드시 우회한다 | 당시 보류. [후속 회차의 F4](content-blocking-brave-final-round-20260913.md)에서 초기 빈 문서가 Yee observer 등록보다 먼저 생성되며 replay가 없음을 확인했다. Chrome native realm으로 Response/global 우회와 독립 설치·Brave DOM-bypass 대조군을 검증했다. 실제 Yee/YouTube 통합 재생은 보류 |
| 끝 점 host·private PSL 판정이 반드시 Brave와 다르다 | Chromium과 Rust의 관련 코드 및 데이터 역할을 확인했으나 새로운 입력 차이를 재현하지 못함. private registry snapshot 차이는 추가 통합 확인 대상 |
| CSP 정책을 별도 추가하면 서버 정책을 덮어쓴다 | raw header와 parsed enforce 정책을 추가하며 기존 metadata를 보존한다. 서로 다른 CSP policy는 함께 적용된다. 기존 focused native case 통과 |
| main document가 factory initiator 때문에 third-party로 잘못 판정된다 | 현재 navigation purpose는 매 URL/redirect target을 자신의 site/source로 사용한다. 기존 native 대조군 통과 |

마지막 두 회차의 hash·진단 확인은 `round-4-verification.json`,
`round-5-verification.json`에 기록했다. 여기서 “새 발견 0”은 고정 소스와 위 범위의
반복 검수 결과다. 미래 YouTube 변경·모든 Chromium lifecycle·실제 광고 계정의
미검증 동작까지 결함 없음을 증명하는 결과는 아니다.

## 검증과 후속 수정 기준

| 실행 | 결과와 의미 |
| --- | --- |
| 현재 core/settings/stylesheet binary | 13개 통과 |
| 현재 Mojo factory binary | 30개 통과 |
| 독립 native-extra binary | 실제 factory object를 연결, 새 세 조건에서 현재 잘못된 동작이 재현됨. 관측값 확인 assertion 3개 통과는 수정 완료를 뜻하지 않음 |
| GN Rust engine probe | ABP 조건부 예외, type 세 입력, 보충 목록의 요청 세 입력, production rewrite alias 및 잘못된 resource 등록 확인 |
| 기존 YouTube/generic suites | 통과. 기존 YouTube lossless JSON 71개 조합 및 object/API lifecycle 포함 |
| 추가 Node 진단 | Yee 24개 조건, pinned Brave 원본 함수 8개 대조. 모두 actualBrowserExecuted=false |
| 기존 mutation 재현 | 미수정 광고 필드 재등장 재확인 |
| 원본/적용 소스 | 참고 소스 hash 106개 확인, Yee 기능 입력 25개 변동 없음·Chromium 적용 소스와 일치 |
| 전체 앱/실제 탭 | 이번에는 제품 변경·전체 앱 재빌드·Yee 종료/실행 없음. 실제 광고 차단과 YouTube 재생 판정 보류 |

재실행 진단:

```sh
node .local-build/brave-content-blocking-port-review/preprocess_probe.mjs
python3 .local-build/brave-content-blocking-port-review/build_engine_probe.py
python3 .local-build/brave-content-blocking-port-review/build_native_probe.py
node .local-build/brave-content-blocking-port-review/youtube_port_probe.mjs
node .local-build/brave-content-blocking-port-review/brave_scriptlet_probe.mjs
python3 .local-build/brave-content-blocking-port-review/verify_audit.py 5
```

native probe는 이번 checkout의 Ninja compile/link 명령을 참고한다. build 디렉터리를
바꾸면 명령 reference도 다시 생성해야 한다. 원본 함수는 검토 fixture에서만 읽어
실행했고 Yee 제품에 복사하지 않았다.

수정 순서 권고:

1. F1의 build 입력 전처리, F2/F3의 type·trusted 요청별 정책 문맥을 먼저 정리한다.
2. YouTube 응답의 주소/host/형식 판정과 초기 player·Shorts 처리 계약을 분리하고,
   fetch 본문을 모든 소비 방식에서 동일하게 처리한다. 현재 lossless literal·영상 정보·
   body lifecycle·header/clone/error 의미를 유지하는 대조군을 포함한다.
3. 기존 retained-object 문제를 같은 데이터 처리 checkpoint에서 해결한다.
4. 요청 body·anti-adblock·재생 복구는 필요 조건과 정상 재생 증거를 갖춰 독립 보정한다.
   기본 목록 추가 시 resource replacement/procedural/removeparam의 현재 미지원도 확인한다.
5. MCP 실험이 끝나면 새 앱으로 실제 request 차단, document-start, 임베드·Shorts·
   로그인/Music·정상 영상 및 광고 재생을 검증한다.

WebSocket/WebTransport, CNAME, procedural/action/Shadow DOM, removeparam/대체 응답,
동적 목록 갱신·프로필 Shields UI·전용 차단 해제 화면은 기존 미지원으로 유지한다.
worker/cache 응답, BFCache/prerender/fenced frame/partition, strict CSP와 inherited frame,
Windows 및 실제 YouTube 재생의 통합 확인은 이 검수로 완료 판정하지 않는다.
