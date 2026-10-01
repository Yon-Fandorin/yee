# 비공개 Yee 본체와 공개 필터·scriptlet 패키지

Yee 본체는 비공개로 유지한다. 공개 자료에는 원본 필터·JavaScript·라이선스와
독립 데이터 생성 도구·실행 템플릿을 넣는다. Yee 본체, Rust adapter, renderer,
자체 YouTube 구현은 이 소스 아카이브에 넣지 않는다.

## 라이선스와 프로그램 경계

adblock-rust 0.13.3은 MPL 원본을 수정하지 않고 기존 원본 소스 제공을 유지한다.
MPL 코드가 없는 새 파일에 자동으로 같은 공개 의무가 생기지는 않는다는 Mozilla
설명을 근거로 한다. [Mozilla MPL FAQ Q8·Q11](https://www.mozilla.org/en-US/MPL/2.0/FAQ/).

uAssets 필터는 GPL-3.0, uBlock 원본 JavaScript는 GPL-3.0-or-later와 개별 파일
고지에 따른다. Brave 고유 리소스는 MPL-2.0 및 개별 파일 고지를 유지한다.
원문, 정확한 버전·출처·checksum, 수정 선택 결과와 재현 도구를 함께 제공한다.
원본 JavaScript를 비공개 코드로 재작성하거나 원본의 라이선스를 변경하지 않는다.

GPL JavaScript는 외부 JSON 리소스로 배포하여 시작 전에 읽고, 표준 JavaScript·DOM
API를 사용하는 별도 프로그램으로 해석한다. GPL 원본을 C++ 생성 header에 넣거나
GPL 네이티브 라이브러리를 링크하지 않는다. 원본에서 Yee 전용 JavaScript API를
호출하지 않으며, Yee YouTube JavaScript와 원본을 하나의 소스 파일로 합성하지 않는다.
각 프로그램은 별도의 ExecuteScript 호출로 실행한다. 생성된 scriptlet 프로그램의
작은 실행 스코프 템플릿도 BSD로 공개 자료에 포함한다.

GNU는 다른 라이선스의 interpreter에서 GPL 프로그램을 해석하는 경우와 라이브러리
binding을 통한 결합을 구분한다. 이는 본체 비공개를 목표로 한 설계 판단이며,
**파일 분리만으로 법적 독립성이 확정되는 것은 아니다.** 배포 시 실제 결합 관계와
조건을 확인해야 한다. GPL 자료의 수정·재배포 권리를 Yee 배포 조건으로 제한하지 않는다.
[GNU FAQ InterpreterIncompat](https://www.gnu.org/licenses/gpl-faq.en.html#InterpreterIncompat),
[GNU FAQ MereAggregation](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation).

## 원본과 생성 자료

- uAssets: `019d5d477da8fc60f3aaf6a3b41bd51a5e961b39`, default catalog 관련 14개와
  privacy 1개, 총 15개 원문. `data/community/sources.json`으로 고정한다.
- Brave uBlock: `06b48b9dfc183f7dee1e6a9abe7e07a213d406f1`, 등록 함수·보조 함수 152개와
  redirect 리소스 45개. 필요한 원본 모듈·매핑·라이선스도 제공한다.
- Brave adblock-resources: `6da9b247c3368baa8b4f24cd02046e18702a6527`, metadata의
  고유 리소스 17개와 원본 라이선스를 제공한다.
- JavaScript·redirect 원본 101개 파일의 출처와 hash는
  `data/community/scriptlet-sources.json`으로 고정한다. 원문은 변경하지 않았다.

`build_filter_pack.py`와 `build_scriptlet_resources.mjs`는 다음 다섯 파일을 생성한다.

| 파일 | 역할 |
| --- | --- |
| `YeeCommunityFilters.txt` | Chromium 분기를 선택한 GPL 규칙·원본 scriptlet 호출 |
| `YeeCommunityResources.json` | 원본 함수 소스, 의존성, 별칭, 권한과 redirect bytes |
| `YeeCommunityFilterManifest.json` | schema 2, 규칙·리소스·고지·source archive hash와 선택 내역 |
| `YeeCommunityFilterNotices.txt` | 출처·버전·라이선스 전문·재현 명령 |
| `YeeCommunityFilterSources.tar.xz` | 명시한 원본·라이선스·공개 도구·실행 템플릿·선택 보고서 |

브라우저 빌드는 `YeeCompiledFilters.dat`와 `YeeCompiledFilterManifest.json`도
추가한다. adblock-rust 0.13.3으로 같은 기본·trusted 규칙을 미리 컴파일한 선택적
캐시다. JavaScript·redirect 리소스는 계속 외부 JSON에서 읽는다. 바이너리는
별도 데이터 파일이며 C++ header에 넣지 않는다. 기본 bundle과 커뮤니티 pack의
generation·SHA-256이 일치해야 사용하고, upstream의 binary format 검증도 거친다.
없거나 오래됐거나 손상됐으면 텍스트 파싱으로 돌아가므로 수정 pack에 필수는 아니다.

Node는 원본 모듈의 등록 목록을 import하여 `fn.toString()`을 직렬화한다. 원본 함수
내용이나 의존성 연결을 Yee 함수로 대체하지 않는다. redirect 매핑도 원본 export를
사용한다. upstream adblock-rust/Brave와 같이 parameterized redirect를 제외하고,
Brave가 명시적으로 제외한 `google-ima-dai.js`도 제외한다.

아카이브는 명시적 whitelist로 만든다. 폴더 전체를 탐색해 추가하지 않는다.
독립 작성한 `build_filter_pack.py`, `preprocess_filters.py`,
`build_scriptlet_resources.mjs`, `scriptlet_runtime.js`는 의도적으로 BSD로 제공한다.
Python 3과 Node.js 22 이상으로 압축을 푼 자료만 사용해 다섯 배포 파일을 동일하게
재생성할 수 있다. npm 설치·네트워크·Yee 본체 소스는 필요 없다.
선택적 캐시를 재현할 공개 BSD `compile_filters.rs`·`compile_filter_snapshot.py`와
ABP 입력·불투명한 bundle generation도 포함한다. Rust 컴파일러에는 고정한
adblock-rust 의존성이 필요하며 원본 engine/dependency 소스는 별도 MPL 소스
아카이브에 있다. 비공개 Rust 연결과 renderer 코드는 포함하지 않는다.

## 로딩·권한·실행

ChromeMainDelegate::PreSandboxStartup에서 browser·renderer·Linux zygote의
불변 snapshot을 읽는다. sandbox 이후 파일 IO가 없고, zygote 자식은 snapshot을
상속한다. GPU·utility에는 로드하지 않는다. 기본 경로는 macOS framework Resources,
그 외 desktop은 실행 파일 assets 디렉터리다.

manifest는 최대 128 KiB, 규칙·리소스는 각각 16 MiB로 제한한다. 고정 파일명,
UTF-8·SHA-256·JSON 구조를 확인한다. Rust의 실제 resource storage로 base64,
MIME, 중복 이름·별칭, canonical 의존성·cycle, 기존 Yee fallback 이름과의 충돌을
확인한다. 리소스 수는 4,096개, 의존성 깊이는 128로 제한하여 upstream의 재귀 주입
전 과도한 깊이를 거부한다. 실패하면 규칙과 리소스 모두 거부하고 기존 bundle을 쓴다.

원본 redirect의 별칭이 Yee fallback 별칭보다 우선하며, canonical 충돌은 오류다.
엔진별 test 리소스 등과의 추가 충돌도 진단 후 해당 엔진의 기존 bundle로 돌아간다.
규칙·리소스 hash 모두 bundle generation에 반영한다.

기존 Yee/EasyList/test 입력은 permission 0, 별도 community 입력만 uBO trusted bit 1을
받는다. 원본 `requiresTrust` 리소스 33개도 bit 1을 요구한다. Brave 고유 리소스는 bit 2로
분리하고, community 목록에 그 권한을 부여하지 않는다. 이는 Brave 전용 제품 기능을
필터 데이터만으로 활성화하지 않기 위한 경계다. 원본 리소스의 포함과 호출 활성화는
별개이며, 모든 Brave 제품 기능을 Yee에 포팅한 것은 아니다. 고정 Brave 목록에서
`brave-yt-sabr-fix` 기본 호출은 주석 처리되어 있으며 Yee에서도 자동 활성화하지 않는다.

scriptlet 출력은 공개 실행 템플릿의 독립 함수 스코프에서 실행한다.
`scriptletGlobals`는 Map의 `get/set/has`와 속성 접근을 같은 상태에 연결한다. 메서드를
추출해 호출해도 receiver가 유지되고 strict mode의 속성 쓰기도 허용한다. DeAMP는
제품 설정이 없어 `deAmpEnabled = false`, debug 공유 상태는 비활성으로 제공한다.
원본 helper와 클래스가 페이지의 전역 함수 이름을 오염하지 않는다.
document-start/initial-empty-realm의 기존 생명주기와
site disable·재진입·frame 소멸 guard를 유지한다. Brave의 Rust resource packager처럼
원본의 extension `world` 구분 대신 현재 main-world 주입 경로를 사용한다. uBO 확장의
isolated-world 실행 구조 자체를 포팅한 것은 아니다.

YouTube에서는 Yee의 lossless ingress 처리를 먼저 설치하고 원본을 별도로 실행한다.
retained player에는 기존 영구 광고 필드 guard를 유지한다. JSON/Response/XHR의 parsed
값에는 필드를 제거하되 존재하지 않는 필드의 영구 accessor를 새로 만들지 않는다.
그 accessor가 원본 pruner에 이미 제거된 광고를 다시 발견한 것처럼 보여 불필요하게
JSON을 재직렬화하던 충돌을 해결했다. 원본의 trusted JSONPath 요청 수정·serverContract
DOM rewrite는 함께 실행된다.

사용자는 원문을 수정하고 `sources.json` 또는 `scriptlet-sources.json`의 hash를 갱신한
뒤 공개 도구로 rebuild할 수 있다. `--yee-community-filter-dir=ABSOLUTE_DIRECTORY`는
child에 전달되며 재시작으로 적용한다. 이 경로는 **신뢰하는 실행 가능한 JavaScript**를
선택하는 startup 설정이다. 웹 페이지가 지정할 API는 없으며 checksum은 일관성 검사이지
서명·공급자 인증이 아니다. hot swap·인터넷 자동 updater는 추가하지 않았다.

## 지원 수치와 한계

선택된 텍스트 규칙은 30,571개, 2,970,098 bytes이며 원본 scriptlet 호출 10,179개를
포함한다. 리소스는 uBO 152 + redirect 45 + Brave 17 = 214개,
1,029,481 bytes다. 현재 compiler의 없는 scriptlet 이름 제외는 0개다.
extended/procedural/action cosmetic 1,429개, 미지원 redirect 호출 1개,
response/URL 변환 35개는 계속 제외한다.

선택된 텍스트 규칙 수는 engine이 모두 해석·적용했다는 통계가 아니다. 미지원 option,
일부 regex removeparam·새 인자 문법 등은 upstream engine 단계에서 거부할 수 있다.
모든 원본 scriptlet의 모든 사이트 계약이나 Brave 전체 동등성을 증명한 상태도 아니다.
원본 JS의 JSONPath 편집은 자체 parse/stringify 계약을 유지하므로 임의의 tagged 요청
편집 전후 JSON 숫자 표기까지 lossless라고 주장하지 않는다. 실제 Yee 자동 주입 fixture는
통과했고, 라이브 YouTube에서 프리롤 영상 차단과 30분 50초 본편 전체 재생을 관찰했다.
관측 범위의 시작·중간 광고 대조와 재생 회귀는 완료했다. 실제 수치, 테스트 환경의
40초대 오류 원인과 재생 gate는
[현재 checkpoint](content-blocking-checkpoint.md#40초대-재생-오류)에 있다.

## 검증

Brave 테스트에서 유지할 설계 원칙과 보강 범위는
[현재 checkpoint](content-blocking-checkpoint.md#반복-검토에서-유지한-원칙)에 정리했다.
최종 실행 결과:

- tooling **13개 통과**. 기본 공개 아카이브 131개 파일로 다섯 배포 파일의 byte 단위
  재현과 vendored manifest의 Git 포함을 검사했다. 실제 앱에 배포된 ABP cache 입력
  포함 133개 파일 아카이브도 추출한 자료만으로 다섯 파일을 동일하게 재생성했다.
  비공개 sentinel·C++·Rust adapter·자체 YouTube 코드는 제외한다. 공개 Rust 파일은
  명시적으로 포함한 독립 데이터 compiler `compile_filters.rs` 하나다.
- native core/settings/style/data/공통 worker **51개**와 Mojo factory/profile service **50개**, 총 **101개 통과**.
  원본 generated script, trusted 권한·예외, redirect 별칭 우선순위, 잘못된 리소스의
  전체 거부와 canonical 충돌·과도한 의존성 깊이 거부를 확인했다. 복합 permission mask,
  dependency 권한, 이름 대소문자, 전체 scriptlet 예외, 목록 간 예외·CSP와 원본 redirect
  45개·모든 별칭의 바이트, UTF-8·바이너리·빈 본문의 실제 Mojo 응답도 확인했다.
  실제 컴파일 캐시와 텍스트 엔진의 출력 일치, generation 변경·checksum 오류의
  cache 거부와 binary format 오류의 텍스트 복구도 확인했다. renderer worker의 엔진
  생성·재사용, generic 예외·응답 sequence와 삭제된 수신자의 응답 취소도 통과했다.
  다른 객체를 사용하는 공통 worker의 FIFO 실행·작업 스레드 해제와 이동 가능한
  입력·결과도 검증했다.
- 실제 Rust 엔진이 생성한 프로그램을 임시 Chrome의 독립 frame에서 실행하는
  **1,239개 Chromium fixture assertion 통과**. 원본 set-constant·JSON prune·trusted
  JSONPath 요청 편집, 원본 serverContract의 기존 DOM 노드 변환과 변환 결과의 실행,
  로딩 중 parser가 추가한 serverContract 노드의 observer 변환과 종료, 실제 엔진이 생성한
  공유 상태 계약의 실행, WWW/Mobile/Music/TV/Kids/nocookie 원본 + Yee 조합을 확인했다.
  6개 host × 6개 endpoint × 6개 Body reader의 광고 제거·정상 내용·메타데이터·큰 정수와
  이스케이프 보존·bodyUsed·두 번째 읽기 거부·unmatched 응답을 확인했다.
  기존 실제 Web API/CSS/MP4 fixture **25개도 통과**했다. 이는 실제 Yee document-start callback이나
  실제 YouTube 서버 계약·영상 재생 증명이 아니다.
- 기존 YouTube lossless JSON **71 cases**, 확장 protocol/playback **248 assertions 통과**.
- `tools/dev/build.sh` 전체 chrome target **빌드 성공**. macOS 실제 앱 bundle에서
  다섯 원본 자료와 두 cache 자료의 byte·hash, GPL 원문·공개 도구 아카이브와 기존
  MPL 자료를 확인했다. 첫 엔진 로딩의 비용과 Brave 대조는
  [현재 checkpoint](content-blocking-checkpoint.md#첫-문서의-필터-엔진-로딩과-brave-대조)에 있다.
- owned 차단 입력이 적용 Chromium과 byte 단위로 일치하고, community 원본 **116개**의 hash가 일치한다.
  filter_data/core GN header dependency check, Chromium whitespace, `0001` reverse apply,
  patch를 제외한 repository whitespace check를 통과했다.

실제 앱을 확인할 때는 모든 Yee를 정상 종료하고 새 빌드를 실행한 뒤 별도 임시
프로필을 사용한다. 로컬 `.local-build` 로그는 현재 판정의 영구 근거로 간주하지 않는다.
