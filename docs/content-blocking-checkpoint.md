# Yee 네이티브 차단 통합 checkpoint

작성일: 2026-09-12. 최종 검토 갱신: 2026-09-18.

이 문서는 콘텐츠 차단의 현재 구조, 기능 범위와 남은 실제 앱 gate를 설명하는
단일 진입점이다. 현재 패키지·라이선스 판정과 최종 수치는
[비공개 본체·공개 필터·원본 scriptlet](content-blocking-private-core-filter-data.md),
회귀 보강 내역은 [Brave 테스트 보강](content-blocking-brave-test-hardening-20260913.md)을
따른다. 구현 경위는 [광고 차단·YouTube 개선 기록](content-blocking-implementation-20260913.md)에
있다. 그보다 앞선 날짜별 연구·검토 문서의 실패, 미수정 표기와 시험 수는 당시
상태를 보존한 연혁이며 현재 판정으로 사용하지 않는다.
[초기 조사](content-blocking-research-20260912.md)와
[첫 통합 재검토](content-blocking-review-20260913.md)는 설계 경위가 필요할 때만 참고한다.

상태: 엔진·요청·페이지 연결, 전체 앱 빌드와 최신 오프라인 회귀 검증은 통과했다.
실제 Yee의 document-start 자동 주입과 실제 YouTube 서버·영상 재생은 아직 검증하지
않았다. Chrome fixture 결과를 이 실앱 gate의 통과로 간주하지 않는다.

후속 요청을 반영해 `adblock-rust` 0.13.3 원본을 유지하고, Yee의 Rust/C++ API,
요청 proxy와 문서 처리를 독립 작성했다. 엔진 핵심 파일을 번역하거나 이름을 바꾸어
다른 라이선스로 취급하지 않는다. 원본 엔진은 MPL-2.0이며 배포물에 고지와
해당 소스를 포함하도록 연결했다.

## 실제 연결

```mermaid
flowchart LR
  R[고정된 필터·리소스 번들] --> E[adblock-rust 원본]
  E --> C[Yee CXX API]
  C --> N[브라우저 전용 매칭 thread]
  N --> F[FilteringURLLoaderFactory]
  F --> S[Chromium Network Service]
  C --> D[renderer 문서 처리]
  D --> CSS[Blink user-origin CSS]
  D --> JS[페이지 scriptlet·YouTube 데이터 hook]
  D --> G[전용 isolated world의 동적 CSS 수집]
```

제품 원본은 `components/content_blocking/`, `browser/content_blocking/`,
`renderer/content_blocking/`, `third_party/yee_adblock/`에 있다. `build/overlay.json`을 통해
Chromium의 `components/yee_content_blocking/`, `chrome/browser/yee_content_blocking/`,
`chrome/renderer/yee_content_blocking/`, `third_party/rust/yee_adblock/`로 동기화한다.
installer는 owned mirror에서 삭제된 파일도 제거하며 original/generated 경로는 보존한다.

Chromium originals에는 content client 호출, GN dependency와 Yee isolated world ID만
추가했다. 기존 `0001`에 전체 변경을 재생성했다. `TabStripModel`, Omnibox와 Sidebar
구조, Agent/MCP 실행 코드에는 차단 정책을 넣지 않았다. Yee의 페이지 필터링 world와
MCP의 `CHROME_INTERNAL` world는 분리했다.

## 요청 처리

- factory chain에서 확장·인증 등의 기존 연결 뒤에 Yee proxy를 추가한다.
- 브라우저의 전용 thread에서 시작 URL, 종류, method와 매칭 출처를 평가한다.
  Subresource는 factory context, navigation은 browser-authored request metadata를 쓴다.
  Main document의 site/source는 이동 URL 자체, iframe의 site는 trusted top origin이다.
  엔진은 해당 thread에서 한 번 만들고 재사용한다. 요청별 UI thread 왕복은 추가하지 않는다.
- 차단 요청은 downstream을 시작하지 않고 `ERR_BLOCKED_BY_CLIENT`로 완료한다.
- server redirect와 client의 redirect URL override도 다시 평가한다.
- 응답 head·body pipe·metadata·upload progress·transfer size를 그대로 전달한다.
- factory clone은 같은 정책과 연결을 공유한다. 요청은 factory/프로필/프레임 raw pointer를
  보관하지 않고 자신의 loader/client pipe를 소유한다.
- 완료와 disconnect는 한 번만 처리한다. upstream client의 completion/disconnect 순서를
  사용해 정상 완료와 loader pipe 종료 사이의 경쟁을 피한다.

요청 종류는 `ResourceType::kMainFrame/kSubFrame`을 먼저 document/subdocument로
분류하고, ping/CSP report도 별도로 구분한 뒤 `RequestDestination`에서 변환한다. fetch/XHR와 sendBeacon을 같은
종류로 처리하지 않는다. 테스트는 ping 전용 규칙으로 beacon을 차단하면서 동일
host의 fetch를 허용하는 것을 확인한다.

사이트 예외는 알려진 HTTP(S) top origin을 기준으로 한다. worker 등의 top metadata가
opaque이고 initiator는 HTTP(S)이면 initiator로 판단한다. 둘 다 opaque이거나 internal
문맥이면 적용하지 않는다. 알려진 web top origin에서 시작한 internal/opaque initiator는
해당 top origin을 매칭 출처로 사용한다. 복수 client의 shared/service worker 및
partition별 예외의 정확한 전달은 실제 통합 검증이 남아 있다.

## 문서 처리

`RenderFrameCreated`에서 agent를 만들고 `RunScriptsAtDocumentStart`에서 적용한다.
CSS는 Blink의 user-origin stylesheet를 사용한다. 엔진에서 반환한 scriptlet은 페이지
실행 환경에 적용한다. script 실행으로 프레임이 사라지면 weak pointer로 확인해
후속 페이지 처리 및 Chromium extension 호출을 중단한다.

generic CSS는 Yee 전용 isolated world에서 class/id 추가·변경을 모아 native 엔진에
전달한다. 처리량과 보관량을 제한하고 idle callback을 사용한다. attribute 변경 때문에
해당 요소의 전체 subtree를 매번 다시 순회하지 않는다. native callback은 보관된 frame
pointer 대신 실행 중인 V8 context에서 살아 있는 frame을 확인한다.
입력 제한을 넘거나 예외 목록을 온전히 읽을 수 없으면 해당 batch를 적용하지 않는다.
예외의 일부를 삭제한 상태로 더 넓게 숨기지 않는다.
대기열이 가득 차면 하나의 증분 재순회로 합치고, 한 요소에 class가 많이 있어도
다음 batch에서 진행을 이어 간다. 너무 긴 UTF-8 class/id 하나 때문에 정상 항목까지
버리지 않도록 수집 단계에서도 입력 크기를 확인한다.

문서 생성 시 weak pointer와 selector/sheet 상태를 초기화하고 같은 문서에는 중복 설치하지 않는다.
전용 isolated world에 security origin과 non-null empty CSP를 설정한다.
`about:blank/srcdoc`은 해당 frame의 상속 HTTP(S) origin으로 판정한다.
CSS는 문서별 dedup과 64 KiB chunk/전체 4 MiB 제한을 적용하며 변경된 tail sheet를 교체한다.
엔진의 `$csp` 결과는 document/subdocument 응답의 기존 서버 정책과 parsed security metadata를
유지하면서 추가 enforce 정책으로 연결한다.
기본 번들을 두 프로세스에 동일하게 포함하므로 document-start의 async Mojo 조회나
download 대기가 없다. 초기화·dynamic CSS·iframe·CSP·BFCache의 최종 실제 동작은
실앱 gate에서 확인한다. callback 자체는 다른 문서의 준비 결과를 기다리지 않는다.

## 데이터와 배포 고지

`adblock-rust`와 60개 의존성을 버전 고정해 `third_party/rust/yee_adblock/`에 포함했다.
기존 Chromium Rust dependency graph를 업데이트하지 않고 private GN 타깃으로
구성했다. registry checksum과 원본 파일 SHA-256을 `manifest.json`에 기록하고,
vendoring 단계에서 Cargo.lock checksum과 61개 registry archive를 검증하고,
archive의 원본 파일 2,145개가 추출된 source와 같은지 확인했다.
고지·소스 묶음을 만들 때 원본 파일이 바뀌지 않았는지 검증한다.
`sources.gni`로 모든 원본 파일과 생성 GN 파일을 packaging action 입력에 연결해,
컴파일된 원본이 바뀌었는데 소스 묶음만 이전 상태로 남는 것을 방지한다.
빌드 중 다운로드는 하지 않는다.

기본 목록은 EasyList·EasyPrivacy와 별도로 식별한 Yee 규칙이다. 목록은 공식
Adblock Plus 서버의 원본을 보존하고, dual license의 CC BY-SA 3.0-or-later 조건을
선택했다. 원저작자, 출처·조회일·hash와 라이선스 URI를 보존한다.
필터와 라이선스 데이터가 기록된 SHA-256과 다르면 생성/packaging을 중단한다.
FlatBuffers와 SeaHash의 published crate에 빠진 전문은 공식 저장소에서 보완하고
별도 출처·hash를 기록했다. 원본 crate 파일은 수정하지 않았다.

범용 uBO/Brave scriptlet 묶음을 복사하지 않았다. production resources는 빈 목록이다.
예약 `.test`의 검증 규칙·scriptlet은 `--yee-content-blocking-test-rules`를 켠 경우에만
적용한다. 기본 실행에서 fixture host 차단·global fixture selector·scriptlet이 없는
것을 검증했다. YouTube 코드는 Yee 자체 모듈이다.

빌드는 `YeeContentBlockingNotices.txt`와 `YeeContentBlockingSources.tar.xz`를 생성한다.
소스 묶음에는 모든 원본 crate와 외부 filter 데이터·고지를 포함한다. 독립 작성한
Yee Rust/C++/JavaScript·test 규칙은 포함하지 않는다. macOS에서는 bundle
data로, 다른 플랫폼에서는 output directory의 파일로 연결했다.
macOS 전체 빌드 후 실제 `Yee.app/Contents/Frameworks/Yee Framework.framework/Resources/`에
두 파일이 포함된 것을 확인했다. source archive에는 적용되는 root `LICENSE`도 포함한다.
Windows 빌드는 아직 검증하지 않았다. 현재 개발 빌드의 `chrome://credits`는 Chromium
sample이므로 외부 배포 전 사용자에게 보이는 고지/source 접근 안내를 추가 확인해야 한다.

현재 목록 갱신은 원본·hash를 다시 고정해 앱을 빌드하는 방식이다. 런타임 updater,
사용자 구독과 이전 generation 복구는 아직 추가하지 않았다. 미지원 조건을 임의로
삭제해 더 넓은 차단 규칙을 만드는 별도 변환기는 없다.

## YouTube 모듈

초기 `ytInitialPlayerResponse` 설정, player/next endpoint의 `Response.json/text`,
XHR 응답에서 확인된 광고 데이터 key를 처리한다. 본 영상 streaming data, captions,
playability 정보는 보존한다. 외부 host의 같은 경로나 다른 endpoint는 처리하지 않는다.
SPA 재설치는 중복 wrapper를 만들지 않는다. 광고를 재생한 뒤 mute/seek하는 방식은 넣지 않았다.
YouTube text JSON은 원문 member span 제거로 숫자 정밀도·escape를 유지한다.
Known player container는 arbitrary enumeration보다 먼저 처리하고, 기존 player 필드의 변경을
보호하면서 객체 identity를 유지한다. XHR text는 동일 URL/내용에 한해 결과를 재사용한다.
작업량 제한은 inherited/own check와 object/property 방문 수도 포함한다. 큰 배열 하나에서
큐를 무제한 늘리지 않고, page-defined getter/Proxy 오류로 player 설정이 깨지지 않도록 처리한다.

이 연결이 실제 YouTube 광고 차단 성공을 의미하지 않는다. 서버가 광고를 영상 스트림에
결합하는 변형, 다른 데이터 경로, 광고 차단 감지와 실계정 A/B 변형은 실제 재생 검증이
필요하다. 광고 영상·소리가 재생되지 않으면서 본 영상이 정상 재생되어야 성공이다.

## 제어와 현재 한계

기본 활성 feature 이름은 `YeeContentBlocking`이다.
`--disable-features=YeeContentBlocking`으로 전체 기능을 비활성화할 수 있다.
`--yee-content-blocking-disabled-sites=example.com,www.example.org`는 정확한 top-level
host 예외이며 대소문자는 구분하지 않고 renderer child에도 전달한다. ASCII/punycode
host 입력을 사용하는 임시 CLI 제어다. UI·영구 프로필 설정은 아직 만들지 않았다.

이번 consumer는 차단 결과, 일반 CSS와 generic class/id, 준비된 scriptlet을 연결한다.
엔진이 파싱할 수 있는 모든 동작을 실행하는 것은 아니다. 대체 응답, removeparam,
procedural/action CSS 전체 실행기는 후속 범위다. CSP response directive는 위의
document/subdocument 응답 경로에 연결했다.
WebSocket/WebTransport 연결에는 Yee interceptor가 없다. `about:blank/srcdoc` 문서의
cosmetic 처리는 frame의 상속 HTTP(S) origin으로 연결했으며 실제 탭 검증은 남아 있다.
HTTP(S) factory를 거치지 않는 service worker/cache
응답, inherited/opaque 문맥과 prefetch/prerender의 상세 범위는 별도 통합 검증이 남았다.
일반 CSS와 document MutationObserver는 shadow root 내부를 관찰하거나 관통하지 않는다.
쿠키 격리·fingerprinting·CNAME 방어까지 포함한 Shields 전체 구현은 아니다.

## 검증 기록

- native core/settings/style/data 41개와 Mojo factory 47개, 총 **88개 통과**.
- tooling **12개 통과**. 공개 source archive 129개 파일과 배포 파일을 byte 단위로 확인했다.
- 실제 엔진 출력의 Chrome fixture **1,239개 assertion**, 기존 Web API/CSS/MP4 fixture
  **25개 assertion** 통과. 이는 실제 Yee 자동 주입이나 YouTube 재생 증거가 아니다.
- YouTube lossless JSON **71개 case**, protocol/playback **242개 assertion** 통과.
- `tools/dev/build.sh` 전체 chrome target과 macOS 앱 bundle 검증 통과.
- owned 입력 167개와 원본 116개 hash, Chromium whitespace와 `0001` reverse apply 검증 통과.

세부 대조군, 배포 파일 수와 로그 위치는
[현재 패키지 설계](content-blocking-private-core-filter-data.md)와
[회귀 테스트 보강 기록](content-blocking-brave-test-hardening-20260913.md)에 둔다.

실앱 fixture는 `tools/dev/test-content-blocking.mjs`다. 별도 프로필의 실제 Yee 탭에서
첫 inline script 이전 적용, fetch·redirect·worker·beacon, 허용 응답 보존, 동적 CSS와
사이트 예외, main/iframe navigation, type-only 규칙, blank/srcdoc 및 strict/filter CSP를 비교한다. 테스트 HTTP 서버가 차단 요청을 받지 않았는지도 확인한다.
실행 전 모든 Yee의 graceful shutdown이 필요하다. runner는 각 launch 전에 실행 중인
stable bundle ID와 실제 executable inventory로 제품 browser process가 없는지 검사한다.
CDP 준비 전/연결 실패 시 자신이 launch한 child PID에만 AppKit 정상 종료를 요청한다.
현재 이 gate와 실제 YouTube 재생은 아직 수행하지 않았다.
