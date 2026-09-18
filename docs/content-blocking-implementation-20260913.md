# 광고 차단·YouTube 포팅 개선 — 2026-09-13

후속 라이선스 결정과 GPL 목록·원본 scriptlet의 별도 패키지 구현은
[비공개 본체·공개 필터·원본 scriptlet](content-blocking-private-core-filter-data.md)를 따른다.
아래 GPL 원본을 넣지 않았다는 기록은 그 후속 결정 이전 checkpoint 상태다.

사용자의 “동작은 동등한 수준으로 하고 개선 진행” 요청에 따른 구현 기록이다.
[반복 검수](content-blocking-brave-port-audit-20260913.md)와
[추가 최종 회차](content-blocking-brave-final-round-20260913.md)의 광고 차단 관련
누락을 수정했다. 기존 감사 문서의 미수정 표시는 당시 상태이며 현재 판정은 이 문서를 따른다.

**실제 Yee 자동 주입과 실제 YouTube 재생 판정은 보류한다.** 사용자가 다른 세션의
MCP 실험 종료 후 검증하도록 지시했으므로 Yee를 종료하거나 실행하지 않았다.
임시 Chrome fixture는 실제 Chromium Web API 증거이며 실제 Yee 실행 증거가 아니다.
Brave 전체 기본 목록·scriptlet 및 모든 계정의 광고 없는 재생과 동등하다는 판정도 아니다.

## 구현과 검증 범위

| 항목 | 변경 | 확인한 증거 |
| --- | --- | --- |
| F1 조건부 목록 | 원본·hash를 유지하고 Chromium 조건을 선택한 뒤 Rust 입력 생성. 중첩 false 부모, else, 미지 조건과 잘못된 지시문을 처리 | Python 5개와 실제 production 엔진의 Abema/New Relic 차단 대조군 |
| F2 prefetch type | `kPrefetch`를 `$other`로 분류 | 실제 production `/client_204` 규칙의 native Mojo 차단 |
| F3 prefetch context | trusted per-request top origin과 원 initiator로 판단. publisher의 사이트 예외로 prefetch proxy 전체를 생략하지 않음 | 목적지 예외와 보호 목적지의 native 대조군 |
| F4 초기 빈 문서 | frame 초기화와 loader factory 준비 후, 생성 함수가 반환하기 전에 별도 renderer hook 실행 | content/public·content/renderer·Chrome renderer 컴파일. 자동 초기 주입의 실제 Yee gate는 미실행 |
| Y1 endpoint·XSSI | player/next/get_watch/reel_watch_sequence/watch/playlist와 XSSI envelope 처리 | 정상 endpoint·외부 final URL·browse 대조군, prefix와 JSON 원문 보존 |
| Y2 모든 body reader | fetch의 clone stream을 한 번 정리하고 실제 Response body 교체 | json/text/arrayBuffer/blob/body reader/clone 및 단일 소비. clone의 URL/type/redirected 보존 개선 |
| Y3 초기·parse 경로 | 두 player global을 보호. Mobile/Music/Kids/nocookie의 JSON.parse 및 Shorts parse 처리 | host별 대조군. 일반 WWW player JSON.parse는 보존. 대상 parse의 root `important`만 제거하고 nested 정상 필드는 보존 |
| Y4 Shorts | `entries`의 `command.reelWatchEndpoint.adClientParams.isAd` 항목 제거 | 광고 entry만 제거하고 정상 entry의 큰 숫자·문자열 escape 원문 유지 |
| Y5 host gate | renderer와 JS 모두 youtube.com/nocookie/Kids 문맥으로 확장. 상속 문서는 native effective URL 전달 | host/blank 문맥 fixture. 실제 blank/srcdoc 자동 주입은 실제 Yee gate에 추가 |
| Y6 요청·복구 | 명시적 복구 UA tag에만 clientScreen/params/adType/lactMilliseconds/referer 보정. 두 network-machine flag 처리. 광고·정확한 UNPLAYABLE 오류·명시적 재시도 이후 zero-buffer stall에 제한된 복구 | tag별 fetch/XHR/Request, caller init·abort·credentials·숫자 보존. 정상/Live/Premium/Captcha/LOGIN_REQUIRED 대조군, cooldown·4회 상한·timer 종료·SPA 이탈·playlist 보존 |
| H1 CSS 격리 | isolated world의 CSSOM insertRule로 각 selector를 검증한 뒤 native user-origin chunk 생성. 반환값 변환도 해당 V8 context에서 수행 | 실제 Chrome에서 malformed selector 뒤 두 정상 규칙 적용. source 선행 주석의 return/ASI도 방지. C++ context 진입은 renderer 컴파일로 확인 |
| 기존 후속 객체 변경 | 방문한 객체의 광고 필드와 알려진 player container를 accessor로 보호 | retained child 새 광고 key, 새 playerResponse, delete/redefine, Response object 및 identity/frozen 대조군 |
| 리소스 검증 | JSON뿐 아니라 등록 결과·base64·중복 name/alias·canonical dependency DAG 확인 | 잘못된 base64, 자체 alias 충돌, 없는 dependency, cycle의 native construction 거부 |
| redirect/rewrite/removeparam | Rust 결과를 C++까지 전달하고 data URL을 실제 Mojo 응답으로 공급. query 변경은 307과 FollowRedirect를 통해 적용 | 대체 JS/MP4, legacy abp-resource alias, redirect-rule 단독 허용, server redirect, HEAD/CORS/same-origin/tainted origin, header·method·body·factory 수명·취소 대조군 |

네트워크 정책과 리소스는 `components/content_blocking/`, 요청 pipe 처리는
`browser/content_blocking/`, 문서·YouTube 처리는 `renderer/content_blocking/`에 둔다.
Chromium glue는 기존 `0001`에서 재생성하며 tab model·sidebar·MCP 코드에 정책을 넣지 않는다.

## YouTube 복구 계약

광고 ingress의 데이터 제거와 실제 player.getPlayerResponse 소비 경계를 함께 보호한다.
`ad-showing` 또는 `SSAP, AD`가 확인되면 광고 video를 pause하고 원래 영상 ID로
다시 요청한다. 광고의 current time을 본 영상의 start position으로 사용하지 않는다.
원래 playbackStartConfig가 있으면 유지하고, 재시도 전에 저장한 playlist를 정상
재생 복구 후 같은 list에서 한 번만 복원한다. 광고를 mute하거나 seek하여 끝까지
시청하는 방식은 사용하지 않는다.

네 가지 요청 mode를 최대 4번, 최소 10초 간격으로 사용한다. 정상 영상·Live·Premium은
복구 대상이 아니다. Captcha·로그인 오류·다른 영상 ID는 자동 reload하지 않는다.
zero-buffer는 명시적 복구 시도 이후 8초 이상 정체한 경우만 재확인한다. SPA 이탈,
정상 재생 또는 retry 상한에서 timer와 임시 UA를 복원한다.
문서 교체 시 이전 observer·event listener·timer를 해제한다. ytcfg 교체와 새 configuration
getter 오류가 있어도 이전에 tag를 넣은 client 객체를 직접 복원하며 정상 새 UA를 덮어쓰지 않는다.
prerender 중에는 player 복구를 실행하지 않고 활성화 이벤트에서 재확인한다. pagehide에서
복구를 중단하고 임시 UA를 복원하며, 이전 문서의 지연 콜백이 복구를 다시 시작하지 못하도록
막는다. pageshow에서 현재 문맥을 다시 확인한다. 설치 시 캡처한 native clock으로 cooldown을
계산하므로 페이지가 Date.now를 바꿔도 재시도 간격을 유지한다.

Brave가 배포하는 GPL scriptlet 함수나 긴 serverContract 패치를 복사하지 않았다.
Map/Array/Promise prototype의 광범위한 contract 보정 대신 위의 별도 제한된 복구를
독립 작성했다. 따라서 **Y6 전체 serverContract와의 완전한 행동 동등성은 아직
입증하지 않았다**. 초기 버퍼 정체의 모든 원인, 서버/A/B·계정·연령/로그인 변형에서
광고 차단과 정상 재생을 실제 Yee로 비교해야 한다. 요청 body tag 보정만으로 성공을 판정하지 않는다.

## 입력과 호환성 경계

기존 소스 제공 의무를 피하는 의존성 선호를 유지한다. uAssets의 GPL-3.0 원본 데이터와
uBO scriptlet 원본을 신규 번들에 넣지 않았다.
[고정 uAssets LICENSE](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/LICENSE).
기존에 사용하기로 한 adblock-rust 0.13.3 MPL 원본과 소스·고지 패키징은 유지한다.
Yee의 연결·YouTube 처리와 빈 JS/무음 black MP4 리소스는 독립 작성·생성했다.
`abp-resource:blank-js`와 `abp-resource:blank-mp4` 별칭도 연결했다.

production에는 기존 EasyList/EasyPrivacy와 Yee 규칙을 사용한다. 검토에서 허용됐던
TVHTML5/oad initplayback, 외부 `_ad_` media와 YouTube log_event 요청은 독립 규칙으로
차단한다. parameter 경계·순서 변형, 정상 initplayback/media/player와 다른 publisher는
대조군으로 확인한다. 이 사례를 맞춘 것이 Brave 기본 catalog 전체를 대체했다는 뜻은 아니다.

객체 identity와 정상 데이터는 유지한다. 광고 및 알려진 player container의 descriptor는
후속 재도입을 막기 위해 non-configurable로 만든다. 해당 보호 필드를 strict delete하거나
재정의하면 native TypeError가 발생할 수 있다. 일반 필드의 descriptor는 유지한다.
이미 frozen/non-configurable인 광고 객체는 강제로 바꾸지 않는다. 일반 객체에 원래 없던
`player`·`response` alias는 추가하지 않는다. 임의 새 raw container, 무한 Proxy trap,
작업량 제한을 넘는 객체·본문을 모두 차단한다고 주장하지 않는다.
fetch body 8 MiB/request body 1 MiB와 object/JSON traversal budget 초과 시 원본을 보존한다.

Brave standard/aggressive의 복수 엔진·first-party catalog 정책, 전체 procedural/action CSS,
일반 uBO scriptlet catalog, WebSocket/WebTransport/CNAME, updater와 프로필별 제어는
여전히 차이가 있다. 광고·YouTube 범위 밖의 overlay/MCP/tooling 감사 결함은 이 구현의 수정 대상이 아니다.

## 실행 기록

- `tools/dev/test-content-blocking-native.sh`: core/settings/style 17개 + 실제 Mojo factory 44개, **61개 통과**.
- `tools/dev/test-youtube-content-blocking.mjs`: 기존 lossless JSON 71 case·객체/API 경계와 추가 **242개 protocol/playback assertion 통과**.
- `python3 -m unittest tests/tooling/test_content_blocking_bundle.py`: **5개 통과**.
- generic collector 및 fake content-blocking runtime suite 통과.
- `tools/dev/test-content-blocking-browser-fixture.py`: 임시 Chrome의 실제 Web API·CSS·대체 MP4 **25개 assertion 통과**, `actualYee=false`. ffprobe에서도 video stream만 존재하고 오디오가 없는 1초 MP4를 확인했다.
- `tools/dev/build.sh`: 최종 source를 동기화한 전체 chrome target 빌드 **성공**. 로그는 `.local-build/content-blocking-implementation-final5-build.log`이다.
- GN core/browser/renderer의 header dependency check, 실제 Chromium diff whitespace와 `0001` reverse apply check 통과.
- 고정 Brave 참고 소스 hash 106개 재확인. owned 차단 입력 38개가 적용된 Chromium과 일치하고 생성된 YouTube/selector-validation header도 최종 원문과 일치한다.
- 앱 bundle의 원본 source archive 2,277개 entry, 61개 crate의 원본 hash와 MPL 고지를 확인했다.
- 실제 Yee fixture runner는 초기 빈 iframe의 즉시 설치, 대체 응답과 query 정리·server log 검증을 추가하고 syntax를 확인했다. **실행하지 않았다**.

빌드 로그와 Web API 결과는 ignored `.local-build/content-blocking-implementation-*.log`,
`.local-build/content-blocking-browser-fixture.json`에 있다. 고정 Brave 참고 commit은 기존
감사 문서를 따른다. 과거 “미수정 동작을 재현하는” ignored probe의 assertion을 수정 완료
gate로 재사용하지 않았으며, 제품 native/Node 테스트에 별도 성공·정상 데이터 대조군을 추가했다.
