# 광고 차단·YouTube 포팅 추가 최종 검수 — 2026-09-13

후속 수정·검증은 [구현 기록](content-blocking-implementation-20260913.md)을 따른다.
아래는 제품 수정 전의 감사 결과다.

결과: **새 동작 결함 1개(F4 · P2)와 예방 개선 1개(H1 · P3)**를 확인했다.
이전 검토에서 보류한 초기 빈 iframe 후보를 Chromium 생성 순서와 실제 Chrome의
서로 다른 JavaScript realm으로 다시 확인했다. 제품 코드는 수정하지 않았다.

[이전 반복 검수](content-blocking-brave-port-audit-20260913.md)의 F1–F3, Y1–Y6 및
기존 후속 객체 변경 결함은 미수정이다. 이전 두 회차의 “새 범주 0”을 기능 완성의
판정으로 사용할 수 없다. 이번 결과를 포함하면 추가 동작 결함은 F1–F4다.

실제 Yee 종료·재실행과 YouTube 광고 재생 검증은 다른 세션의 MCP 실험 종료 후라는
사용자 지시를 유지했다. 브라우저 fixture는 **별도 임시 프로필의 headless Chrome
152.0.7977.83**에서 실행했다. 실제 Yee 통합 실행과 구분한다.

## F4 · P2: 초기 빈 iframe에서 document-start hook을 놓친다

위치: [`DocumentFilterAgent` 생성자](../renderer/content_blocking/document_filter_agent.cc#L101),
[`ApplyAtDocumentStart()`](../renderer/content_blocking/document_filter_agent.cc#L114),
Chromium `ChromeContentRendererClient::RenderFrameCreated()`와
`RunScriptsAtDocumentStart()`의 Yee glue.

Yee는 inherited `about:blank`/`about:srcdoc`의 origin을 처리한다. 그러나 이 판정에
도달하려면 `Apply()`가 호출돼야 한다. **초기 빈 문서가 observer 등록보다 먼저
생성되는 경우에는 이 entry point 자체가 실행되지 않는다.**

현재 적용 Chromium(`25189dfe0a49b3f8324586374b8251b8d12ad1c2`)의 호출 순서:

1. `RenderFrameImpl::CreateChildFrame()`은 `finish_creation()`을 먼저 호출한다.
2. 이 callback은 `WebLocalFrameImpl::InitializeCoreFrameInternal()` →
   `LocalFrame::Init()` → `FrameLoader::Init()`으로 초기 빈 문서를 만든다.
3. `FrameLoader::Init()`은 `CommitReason::kInitialization`으로 문서를 commit하고
   parsing을 종료한다. `DocumentLoader`는 이 commit에서 observer 통지를 생략한다.
4. 초기 HTML root의 document-start callback이 발생해도
   `RenderFrameImpl::RunScriptsAtDocumentElementAvailable()`은
   `initialized_ == false`이면 반환한다.
5. `finish_creation()` 뒤의 `child_render_frame->Initialize()`에서야
   `RenderFrameCreated()`가 Yee agent를 만든다. Yee 생성자는 observer 등록만 하며
   이미 존재하는 문서에 `Apply()`를 재실행하지 않는다. `Initialize()`에도 replay가 없다.

확인한 적용 소스:

| 파일 | 확인 지점 |
| --- | --- |
| `content/renderer/render_frame_impl.cc` | 2131: Initialize/observer 등록, 3860–3889: child 생성 순서, 4263: 초기 callback skip |
| `third_party/blink/renderer/core/frame/web_local_frame_impl.cc` | 2440: frame Init, 2488–2529: finish_creation callback |
| `third_party/blink/renderer/core/frame/local_frame.cc` | 423–444: LocalFrame Init가 FrameLoader Init를 동기 호출 |
| `third_party/blink/renderer/core/loader/frame_loader.cc` | 286–300: initial commit/parsing 종료 |
| `third_party/blink/renderer/core/loader/document_loader.cc` | 1544: parser Finish, 2180–2236: 초기 응답/empty 처리, 3379: 초기 commit observer 통지 생략 |
| `third_party/blink/renderer/core/html/html_html_element.cc` | 53–62: parser가 root를 삽입할 때 document-start callback |
| `third_party/blink/renderer/core/frame/web_local_frame_client_test.cc` | 86–99: 초기 문서 생성 중 callback을 기대하는 upstream 테스트. 테스트 소스를 읽었으며 이 binary는 실행하지 않음 |

### 브라우저 재현과 대조군

실제 `youtube.js`를 부모 realm에 설치하고 초기 빈 iframe을 생성했다. 응답은 네트워크
광고 요청 대신 동일한 JSON을 담은 **Chrome native Response**로 만들고 player URL을
fixture metadata로 지정했다. 원본 영상의 `videoId`는 모든 경우에 보존됐다.

| 경로 | 광고 필드 |
| --- | --- |
| 부모의 `ytInitialPlayerResponse` / `Response.json()` | 제거 |
| 새 빈 iframe의 global / iframe native `Response.json()` | 남음 |
| iframe의 native `Response.prototype.json`을 빌려 부모 Response를 읽음 | 남음 |
| 같은 iframe에 Yee 모듈을 직접 설치한 뒤 global / Response를 읽음 | 제거 |
| pinned Brave fetch pruning + DOM-bypass 보정을 설치한 부모에서 `appendChild()`로 만든 iframe의 fetch 응답 | 제거 |

Brave 대조는 원본 함수와 실제 필터 인자를 사용했다. 원본
[trusted-prevent-dom-bypass](https://github.com/brave/uBlock/blob/06b48b9dfc183f7dee1e6a9abe7e07a213d406f1/src/js/resources/scriptlets.js#L1970)는
부모의 보호된 fetch/Request/JSON.parse를 해당 blank iframe에 전달한다.
[quick-fixes 83–85줄](https://github.com/uBlockOrigin/uAssets/blob/019d5d477da8fc60f3aaf6a3b41bd51a5e961b39/filters/quick-fixes.txt#L83)의
실제 인자와 57줄의 fetch pruning을 함께 실행해 전달과 광고 제거를 확인했다.
기준 pin과 Brave 전처리 포함 여부는 이전 검수와 동일하다.

**증거의 경계:** Chrome fixture에서 부모·자식 module 설치는 검토 코드가 수행했다.
Yee의 자동 hook 등록을 Chrome에 포팅해 실행한 테스트는 아니다. Yee의 누락 판정은
적용 Chromium 생성 순서와 Yee entry point를 대조한 결과이며, fixture는 독립 realm의
native API로 광고 데이터가 남는 경로와 hook 설치 시 제거되는 대조군을 검증한다.
실제 YouTube가 이 우회를 현재 사용하거나 광고를 재생한 것까지 관측하지 않았다.

수정 기준:

- 초기 inherited 문서가 부모 script에 노출되기 전에 보호를 적용하는 lifecycle 지점을
  정한다. 생성자에서 무조건 script를 실행하는 방식은 reentrancy/frame destruction을
  함께 검토해야 한다.
- 이미 존재하는 초기 문서와 다음 navigation을 구분해 중복 설치·style key·weak pointer
  상태를 관리한다. scriptlet가 설치 중 frame을 없애는 경우도 고려한다.
- 별도 realm의 fetch/Response/XHR/global 경로를 확인하고, parent에서 API를 빌리는
  경로도 검증한다. 도메인·사이트 예외·sandbox/CSP 및 실제 Yee document-start를
  함께 확인한다.

## H1 · P3: 깨진 CSS 선택자가 같은 chunk의 정상 규칙을 삼킨다

위치: [`SelectorStyles::Add()`](../components/content_blocking/selector_styles.cc#L11),
[`InsertSelectors()`](../renderer/content_blocking/document_filter_agent.cc#L81),
[`adblock GN features`](../third_party/yee_adblock/adblock_0_13_3/BUILD.gn#L84).

현재 GN에는 `css-validation`이 없다. 원본 Rust의 해당 feature 비활성 경로는 CSS
선택자를 그대로 통과시킨다. 실제 GN rlib에 `page.test##.broken[`와 정상 `.safe-ad`
규칙을 넣으면 두 선택자 모두 document rules에 남는다.

현재 Yee C++ helper를 직접 컴파일해 `.broken[`를 추가한 뒤 `.safe-ad`를 같은 tail
chunk에 추가했다. 생성한 CSS와 Chrome에서 검사한 CSS가 byte 단위로 일치한다.
unclosed bracket 때문에 두 번째 규칙까지 parse되지 않아 `.safe-ad`가 표시된다.
Brave의 [원본 규칙별 insertRule/try-catch](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/renderer/cosmetic_filters_js_handler.cc#L97)를
같은 Chrome에서 실행하면 깨진 규칙을 건너뛰고 `.safe-ad`를 숨긴다.

현재 production 세 목록에서 원본 Rust가 단일 CSS operator로 받아들인 후보
**24,198개**를 Chrome parser로 검사했다. 뒤의 marker 규칙까지 삼키는 선택자는
**0개**였다. 독립 CSS tokenizer에서도 0개였다. 따라서 현재 번들에서 발생하는
광고 누락으로 집계하지 않고 입력 오류 격리의 예방 개선으로 분류한다.

Chrome에서 개별적으로 유효하지 않은 후보 297개는 procedural/nested `:has()` 등
이미 알려진 consumer 한계를 포함한다. 이를 297개의 새 결함으로 세지 않는다.
이 검사는 모든 사이트에 모든 후보가 실제 적용됨을 증명하는 검사가 아니다.

수정 기준: filter 입력의 문법 검증 또는 renderer의 규칙별 오류 격리 중 책임을
정한다. 크기 제한·dedup·chunk 업데이트를 유지하면서 한 선택자가 정상 규칙까지
무효화하지 않도록 한다. 단순 bracket 개수 검사만으로 CSS 문법을 검증하지 않는다.

## 추가 회차의 대조 방향

| 회차 | 추가 확인 | 새 원인 |
| --- | --- | --- |
| 6 | 보류한 blank-frame 후보를 실제 Chromium 생성·parser·observer 순서까지 추적. Native Chrome realm과 Brave 실제 DOM 보정을 대조. GN filter 출력/C++ CSS 생성/Chrome parsing까지 연결 | F4, 예방 개선 H1 |
| 7 | 부모·빈 child·명시적 child 설치·빌린 native API의 대조군과 정상 videoId 보존을 독립 재실행. 같은 tail chunk에 나중에 추가한 정상 CSS가 받는 영향을 재확인. 문서 replacement/navigation·사이트/도메인 gate를 재검토 | 추가 원인 0. 기존 endpoint/host/body/mutation 누락은 중복 집계하지 않음 |

## 실행·무결성 기록과 남은 판정

ignored `.local-build/brave-content-blocking-port-review/final-round6/`에 저장했다.

| 증거 | 결과 |
| --- | --- |
| `cosmetic-probe.json` | 현재 GN Rust의 두 fixture 선택자 전달과 production CSS 후보 추출 |
| `native_styles_probe`, `native-styles-output.css` | 현재 Yee C++ helper를 직접 컴파일·실행. 브라우저에서 검사한 CSS와 byte 일치 |
| `full-production-browser-probe.json` | Chrome native CSS·iframe/API, Brave 원본 함수의 재현과 대조군. actualYeeExecuted=false |
| `browser-control-probe.json` | 현재 C++ helper의 CSS를 사용해 동일 iframe·CSS·Brave 대조군을 독립 재실행. 같은 결과 |
| `css-tokenizer-probe.json` | production 구조 오류를 독립 tokenizer로 재확인 |
| `lifecycle-source-integrity.json` | 추가로 읽은 Yee/적용 Chromium lifecycle 입력 14개의 hash |
| 기존 `round-6-verification.json` | 원본 참고 106개 hash, Yee 입력 25개/적용 소스 일치와 기존 진단 확인. 이전 실패의 해결을 의미하지 않음 |
| `final-round-verification.json` | 완성된 Chrome 실행 2개와 추가 소스 14개의 hash, 대조군·CSS 후보·native 출력 일치 확인 |

브라우저 전체 검사는 결과 JSON을 완성한 뒤 headless 실행의 시간 제한에 따라 직접
생성한 Chrome process group을 종료하고 DOM 결과를 수집했다. 시작/종료 또는 capture
조건에 따라 timeout·미완성 capture도 발생했으며, 결과 JSON과 모든 대조군이 완성된
실행만 위 증거로 채택했다. Yee 프로세스는 종료하거나 검증에 사용하지 않았다.

재실행:

```sh
python3 .local-build/brave-content-blocking-port-review/final-round6/build_cosmetic_probe.py
/usr/bin/clang++ -std=c++20 -I.local-build/chromium/src components/content_blocking/selector_styles.cc .local-build/brave-content-blocking-port-review/final-round6/native_styles_probe.cc -o .local-build/brave-content-blocking-port-review/final-round6/native_styles_probe
.local-build/brave-content-blocking-port-review/final-round6/native_styles_probe > .local-build/brave-content-blocking-port-review/final-round6/native-styles-output.css
node .local-build/brave-content-blocking-port-review/final-round6/scriptlet_payload.mjs
python3 .local-build/brave-content-blocking-port-review/final-round6/run_browser_probe.py --full-production
python3 .local-build/brave-content-blocking-port-review/final-round6/run_browser_probe.py
python3 .local-build/brave-content-blocking-port-review/final-round6/verify_final_round.py
```

새로 확인한 경계를 기준으로 document 생성/replacement/navigation과 원래 YouTube
global/Response/XHR 소비 지점을 다시 추적했다. CSS 정상 규칙의 손실을 기존 procedural
미지원과 분리했으며 추가로 확정한 원인은 F4/H1이다. 검토 종료는 모든 결함의 수정이나
Brave 동등성 판정을 뜻하지 않는다. 기존 F1–F3/Y1–Y6, 후속 객체 변경 및 F4를 먼저
수정하고 MCP 실험 종료 후 실제 Yee·YouTube 재생을 검증해야 한다.
