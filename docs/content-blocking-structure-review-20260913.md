# 콘텐츠 차단 구조 변경 후 코드 검토

2026-09-13. 현재 루트 구조를 기준으로 수행한 추가 코드 검토다.
아래는 수정 전 검토 기록이다. [수정 후속 및 Brave 비교](content-blocking-brave-comparison-20260913.md)를 참고한다.
이번 작업은 검토와 재현이며 제품 코드는 수정하지 않았다. 실제 Yee를 종료하거나
실행하지 않았고, 실앱 검증은 사용자가 지정한 MCP 실험 종료 이후로 유지한다.

디렉토리 이동 자체의 연결은 정상이다. 그러나 페이지 탐색 차단 누락 1건과
YouTube 처리·CSS 누적·소스 동기화의 추가 문제를 확인했다. 이전 checkpoint의
테스트 통과를 전체 보호 경로의 완료로 해석해서는 안 된다.

## 범위와 증거

- `build/overlay.json`의 소유 디렉토리 5개와 Chromium 적용 경로를 비교했다.
  2,370개 파일에서 누락·추가 파일·내용 불일치가 없었다.
- 소스 동기화 preview는 0개, 현재 checkout에서 patch 0001·0002·0003 적용 검사가 통과했다.
- 도구 테스트 52개, 기존 native 테스트 7+14개, generic/YouTube JS 회귀 테스트가 통과했다.
  native 테스트는 이전 빌드의 집중 테스트 실행 파일을 사용했다. 이번에 전체 앱을
  다시 빌드하거나 실제 브라우저 fixture를 실행한 것은 아니다.
- 아래 navigation 문제는 실제 Chromium 호출부와 Yee 구현을 연결해 확인했다.
  YouTube/CSS 사례는 현재 JS를 직접 실행한 VM fixture로 재현했다.
  삭제 동기화 사례는 임시 디렉토리에서 실제 `source_plan`/`sync_sources`로 재현했다.
- JS 재현 입력은 로컬 `.local-build/yee-content-blocking-structure-review-repro.mjs`에 있다.
  실행: `node .local-build/yee-content-blocking-structure-review-repro.mjs`.
  VM은 Blink·실제 YouTube 동작의 증거가 아니며, 현재 함수의 경계 조건을 검증한다.

## 1. P1 — 실제 페이지·iframe 탐색 factory가 차단 없이 통과

위치: [`MaybeAppendFilteringFactory`](../browser/content_blocking/filtering_url_loader_factory.cc),
[`ChromeContentBrowserClient` 연결](../patches/0001-integrate-yee-shell.patch).

Chromium의 `content/browser/loader/navigation_url_loader_impl.cc`에서
`CreateNetworkLoaderFactory`는 `WillCreateURLLoaderFactory`를 호출할 때
`type=kNavigation`, `request_initiator=url::Origin()`, `net::IsolationInfo()`를 전달한다.
`content/public/browser/content_browser_client.h`의 API 설명도 navigation/download에서
opaque origin과 빈 isolation info를 전달한다고 명시한다.

Yee glue는 factory type을 전달하지 않고, 빈 top origin을 같은 빈 initiator로 대체한다.
결국 `EnabledForSite`가 false여서 Yee proxy 자체를 추가하지 않는다. 실제 HTTP(S)
페이지 및 iframe 탐색과 그 redirect는 `FilteringRequest::Blocked`에 도달하지 않는다.
도착한 문서의 CSS를 숨길 수 있어도 광고 iframe의 첫 요청을 차단한 것은 아니다.

기존 `WebNavigationFromInternalInitiatorIsProtected` 테스트는 web top origin을 직접
주고, 요청도 기본 XHR로 생성한다. 실제 navigation 메타데이터를 재현하지 않는다.
`EntirelyOpaqueFactoryIsOutsideProtection`은 동일 메타데이터의 HTTP 요청이 terminal에
도착하는 것을 확인하며, 요청 목적을 구분하지 못하는 현재 동작을 고정하고 있다.

수정 방향: factory 목적과 요청별 문맥을 구분한다. navigation은 요청 URL,
request initiator 및 browser가 설정한 trusted isolation info에서 문맥을 얻고,
main-frame 목적지와 iframe의 상위 사이트를 구분해야 한다. main-frame redirect에서
사이트 예외도 재판정해야 한다. browser-owned opaque 요청을 일괄 보호 대상으로
돌리는 수정은 피한다. frame/Profile raw pointer를 매칭 thread에 보관하지 않는다.

필요 테스트: 실제 navigation 호출 형태, main-frame/iframe 목적, 초기 요청과 redirect,
주소창 입력·internal initiator, 상위 사이트 예외. 실제 iframe 서버 도착 여부도 추가한다.

## 2. P2 — 작은 YouTube 응답에서도 중첩 광고 키가 남음

위치: [`youtube.js`의 `clean`](../renderer/content_blocking/youtube.js).

`pending.pop()`으로 마지막 자식부터 처리하고 모든 일반 property/object가 하나의
budget을 공유한다. 앞서 넣은 중요한 player subtree가 뒤에 넣은 넓은 content
subtree 때문에 한 번도 방문되지 않을 수 있다.

재현: `{playerResponse:{adPlacements:[...],videoDetails:...},wide:Array.from({length:5000},()=>({}))}`
형태의 약 15 KB 데이터에서 `playerResponse.adPlacements`가 그대로 남았다.
8 MB text 제한에 걸리는 거대한 입력만의 문제가 아니다. 광고가 root에 있는 기존
wide-object 테스트는 이를 잡지 못한다. 실제 YouTube가 이 배열·객체 형태를
사용하는지는 별도의 live 검증 대상이다.

수정 방향: 확인된 player/ad-bearing 경로를 우선 처리하고, 일반 content 순회가
광고 처리를 굶기지 않도록 budget과 traversal 순서를 설계한다. 무제한 재귀로
돌리지 않는다. nested player + wide sibling의 조합을 회귀 테스트로 추가한다.

## 3. P2 — 초기 player 객체를 나중에 직접 수정하면 재검사되지 않음

위치: [`youtube.js`의 `ytInitialPlayerResponse` accessor](../renderer/content_blocking/youtube.js).

getter는 원래 객체를 그대로 반환하고 setter만 `clean`을 호출한다.
`window.ytInitialPlayerResponse.adPlacements = [...]` 또는 기존 참조에 대한
`Object.assign`에는 setter가 실행되지 않는다. 실제 재현에서 광고 키가 남았다.
초기 할당 검사와 SPA 설치 중복 방지는 이후 객체 변경을 보호하지 않는다.

수정 방향: 실제 player 갱신 경로를 확인하고 그 소비/갱신 지점까지 처리한다.
전체 객체를 무조건 Proxy로 바꾸면 객체 identity·native API 호환성을 바꿀 수 있으므로
그 접근을 기본 해법으로 정하지 않는다. 이 경로의 실제 사용 여부는 아직 확인하지 않았다.

## 4. P2 — collector의 메모리 제한과 CSS 보관량 제한이 연결되지 않음

위치: [`generic_cosmetic.js`](../renderer/content_blocking/generic_cosmetic.js),
[`ApplyGeneric`/`InsertSelectors`](../renderer/content_blocking/document_filter_agent.cc).

seen class/id가 10,000개를 넘으면 전체 cache를 비운다. 이후 같은 광고 class를
다시 제출할 수 있지만 native 측에는 이미 적용한 selector의 중복 제거가 없다.
각 결과를 새 user-origin stylesheet로 삽입한다.

재현: 10,020개 일반 class를 처리하는 주기를 4번 반복하면 같은 광고 class가 4번
native callback에 제출된다. 같은 generic selector가 반환될 때 stylesheet도 계속
추가되는 구조다. 입력 Set과 queue가 제한되어도 장시간 SPA의 stylesheet 개수와
CSS 총량은 제한되지 않는다. 실제 RSS/style recalculation 비용은 측정하지 않았다.

수정 방향: 문서 단위 applied-selector 중복 제거와 stylesheet 관리 방식을 둔다.
document 교체 때 초기화하고, 동적 필터·예외 갱신이 추가될 때 제거도 가능하도록 한다.
전체 collector history를 무제한 보관하는 방식으로 해결하지 않는다.

## 5. P2 — 소스 동기화가 삭제·이동된 파일을 checkout에서 제거하지 않음

위치: [`source_plan`/`sync_sources`](../tools/overlay/lib/overlay_tools.py),
Windows의 [`Get-YeeOverlaySourcePlan`/`Sync-YeeOverlaySourcePlan`](../tools/overlay/lib/overlay-tools.ps1).

소스에 현재 존재하는 파일만 비교·복사한다. checkout의 소유 디렉토리에만 남은 파일을
검사하거나 제거하지 않는다. 임시 fixture에서 `old_handler.cc`를 동기화한 후 원본을
삭제하고 다시 동기화하면 `Synced 0`이면서 checkout 파일은 남았다.

현재 실 checkout에는 추가 파일이 없었다. 따라서 이번 디렉토리 이동에서 실제
잔재가 발견됐다는 뜻은 아니다. 앞으로 파일 삭제·이동 시 stale header/source가
잘못된 include/GN 연결을 만족해 기존 빌드만 성공하고 clean checkout은 실패할 수 있다.

수정 방향: catalog 소유 경계 안에서 삭제 후보도 preview/검증하고 반영한다.
Chromium 원본·generated root를 소유 파일로 간주하지 않는다. 모든 경로 검증을
mutation 전에 완료하고 Python/PowerShell 동작을 동일하게 유지한다.
vendoring도 `copytree(..., dirs_exist_ok=True)`와 destination 전체 scan을 쓰므로,
갱신 시 이전 crate 내부 파일이 끼지 않도록 staged replacement가 필요하다.

## 6. P2 — YouTube 작업량 제한이 상속 property 열거를 세지 않음

위치: [`youtube.js`의 `for...in`](../renderer/content_blocking/youtube.js).

`if (!own(object,key)) continue`가 budget 차감보다 먼저 실행된다. 상속된 enumerable
property가 많으면 10,000 budget과 관계없이 전부 열거하고 own-property 검사를 한다.
재현에서는 상속 property 20,000개로 Proxy의 enumeration/descriptor trap이 40,005번
실행됐다. 임의 Proxy trap 한 번의 실행 시간까지 보장할 수는 없지만, 코드 자체가
선택한 상속 항목을 예산 밖에서 계속 처리하는 부분은 별도로 제한할 수 있다.

이는 JSON parser가 만든 plain payload보다 page-defined 초기 객체에서 중요한 문제다.
수정 방향: 실제로 처리하는 열거 작업 모두를 예산에 포함하고, 데이터 종류·prototype에
맞춰 순회 정책을 정한다. Proxy 전체 안전을 보장한다고 주장하지 않는다.

## 7. P2 — JSON text 재직렬화가 광고 외 숫자 값도 바꿈

위치: [`youtube.js`의 `cleanText`](../renderer/content_blocking/youtube.js).

광고 키가 보이면 text 전체를 native `JSON.parse`/`JSON.stringify`로 재작성한다.
재현에서 `9007199254740993`이 `9007199254740992`로 변경됐다. Response.json 호출자는
이미 native JSON 숫자 변환을 선택했지만, Response.text/XHR responseText 호출자는
raw text 또는 별도의 정밀한 parser를 사용할 수 있다.

실제 YouTube 본 영상 데이터가 이 값에 의존하는지는 확인하지 않았다. 다만 현재
text 경로의 '광고 외 내용 보존'은 모든 JSON numeric literal에 성립하지 않는다.
수정 방향: 정밀도를 보존하는 text 처리 또는 해당 경로의 호환성 조건을 정하고,
본문 numeric literal 보존 테스트를 추가한다. 단순 stringify 결과 비교만으로 검증하지 않는다.

## 8. P2/P3 — 아직 실행하지 않은 실앱 runner의 실패 정리·브랜딩 누락

위치: [`test-content-blocking.mjs`](../tools/dev/test-content-blocking.mjs).

- P2: startup/CDP 연결 실패 시 `browserCDP`가 없으면 graceful 종료 요청 자체를
  하지 않는다. 10초 기다린 뒤 assertion만 실행하므로 시작된 test-owned browser가
  남을 수 있다. 성공 경로의 `Browser.close`만으로 실패 정리를 검증할 수 없다.
  live runner를 실행한 재현은 하지 않았다. 명시적 test-owned executable/profile에
  한정한 graceful 종료 경로와 startup 실패 fixture가 필요하다.
- P3: 사전 종료 검사가 `/Contents/MacOS/Yee`를 하드코딩한다. 새 구조의
  `branding/brand.json` 기반 표시 이름 변경을 반영하지 않는다. 현재 이름 Yee에서는
  동작하지만 향후 이름 변경 후 실행 중인 제품을 놓칠 수 있다.
  이미 있는 `browser_bundle_executables.py`의 bundle identity 기반 경계를 사용한다.

## 후속 검증 순서

1. navigation factory 문맥을 수정하고 native factory/실제 Chromium 연결 검증을 추가한다.
2. YouTube player 우선 처리·객체 갱신·text 호환성을 작은 fixture로 고정한다.
3. 문서별 CSS 중복 제거와 collector 장기 실행 검증을 추가한다.
4. source 삭제 동기화·vendoring 갱신·runner 실패 정리를 임시 fixture로 검증한다.
5. 관련 수정의 집중 native 테스트와 전체 앱 빌드를 마친다.
6. MCP 실험 종료 뒤 모든 Yee의 graceful shutdown을 확인하고 새 앱으로 navigation,
   iframe, worker, redirect, 사이트 예외 및 실제 YouTube 본 영상/광고를 비교한다.

WebSocket/WebTransport, service worker 자체 cache, 일부 필터 action, updater,
사용자 설정 UI와 배포 credits는 이전 기록에 남긴 후속 범위다. 이번에 발견한
navigation 누락과 별개이며, 정상 HTTP 탐색을 이 범위 목록으로 제외해서는 안 된다.
## 수정 후속 기록

이 문서는 수정 전 검토 시점의 증거를 보존한다. 사용자의 후속 요청으로 진행한
구현 변경, Brave 경로별 대조, 회귀 검증과 남은 기능은
[Brave 비교 및 개선 기록](content-blocking-brave-comparison-20260913.md)을 따른다.
