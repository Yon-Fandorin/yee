# Brave 테스트를 참고한 Yee 광고 차단 회귀 보강

2026-09-13. 광고·트래커·YouTube 포팅의 실행 경계와 빠져 있던 테스트를 점검했다.
Brave 테스트 코드를 제품 소스로 복사하지 않고, 확인한 계약을 Yee의 실제 엔진·Mojo
factory·Chromium Web API 경계에서 독립 작성한 테스트로 검증했다.

## 참고한 원본

Brave core revision: `11a6bbbc91a941dccca9ae997fb9b5d85991698a`.

- [AdBlockService browser tests](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/brave_shields/ad_block_service_browsertest.cc):
  `ScriptletInjectionPermissions`, `CanDebugSetToTrue`, `CheckForDeAmpPref`,
  window/iframe/about:blank scriptlet, eTLD+1 판정, 목록 간 예외, redirect, generichide와 CSP 사례.
- [공유 상태 초기화](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/common/scriptlet_constants.cc):
  Map 메서드와 속성을 같은 상태로 연결하는 scriptlet 계약.
- [renderer 주입](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/cosmetic_filters/renderer/cosmetic_filters_js_handler.cc):
  isolated world에서 초기화 후 DOM script로 main world 프로그램을 실행하는 경로.
  Yee는 별도 native ExecuteScript 호출로 main world에 직접 실행한다.
- [Stub response tests](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/test/adblock_stub_response_unittest.cc):
  데이터 MIME 우선순위와 UTF-8 본문.
- [Cosmetic merge tests](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/components/brave_shields/content/test/cosmetic_merge_unittest.cc):
  예외와 generichide 결합. Yee의 단일 FilterSet에서도 목록 간 예외를 확인했다.
- [Custom resource browser tests](https://github.com/brave/brave-core/blob/11a6bbbc91a941dccca9ae997fb9b5d85991698a/browser/brave_shields/ad_block_custom_resources_browsertest.cc):
  리소스 이름 대소문자와 충돌 사례.

고정 Brave uBlock 원본의 object-prune, safe-self, JSON/fetch/XHR scriptlet도 읽어
테스트 데이터와 예상을 확인했다. 원본 revision과 checksum은 기존 공개 manifest를 유지한다.

## 발견과 수정

| 발견 | 수정과 재현 |
| --- | --- |
| Yee의 일반 Map은 속성 쓰기와 `.get()`이 같은 값을 공유하지 않았다. 추출한 메서드는 receiver를 잃어 TypeError가 발생했다. | 공개 BSD runtime에서 속성과 `get/set/has`를 같은 내부 Map에 연결하고 bound method를 제공한다. 독립 VM 테스트로 실패를 재현한 뒤 통과했고, 실제 엔진 출력의 strict-mode 함수도 Chrome frame 두 개에서 실행했다. |
| Brave scriptlet이 읽는 `deAmpEnabled`가 정의되지 않았다. | Yee에 DeAMP 설정이 없으므로 false를 명시한다. debug 상태도 켜지 않는다. 설정 활성화나 Brave permission bit 2 부여를 추가하지 않았다. |
| parser-loading observer와 native Body reader를 원본 scriptlet 조합으로 확인하지 않았다. | 실제 srcdoc loading 중 observer 설치·노드 변환·원문 실행·interactive 이후 종료를 검사했다. 6 host × 6 endpoint × 6 reader 조합을 추가했다. |
| fixture HTML 안의 srcdoc `</script>` 문자열이 바깥 script를 닫아 테스트가 완료되지 않았다. 종료 과정의 상속 PIPE도 timeout을 일으킬 수 있었다. | HTML script 종료 문자열을 escape하고 진단을 임시 파일에 쓴다. 임시 Chrome만 TERM 후 최대 5초 기다리고 필요하면 해당 테스트 프로세스 그룹에 KILL을 보낸다. 기존 fixture에도 같은 수정을 적용했다. |

원본 GPL/MPL 리소스는 수정하지 않았다. 공개 runtime 변경도 별도 소스 아카이브에
포함하여 패키지만 추출한 재빌드를 유지한다. 비공개 연결 코드·YouTube 코드가
그 아카이브에 포함되지 않는 기존 검증을 유지했다.

## 추가한 네이티브 회귀 테스트

| 경계 | 보강한 계약 |
| --- | --- |
| network matcher | `co.uk` registrable domain, baseline/community 양방향 exception, important 우선순위 |
| page resources | 양방향 CSS exception, generichide가 generic selector만 제외하고 specific selector를 유지 |
| scriptlet permission | mask 0/1/2/3/5, list bit 1로 다른 bit까지 허용하지 않음, 동일 호출을 여러 목록에 넣어도 권한 상승 방지 |
| dependencies | 권한 없는 entry가 trusted helper를 통해 실행 권한을 얻지 않음 |
| resource identifier | 별칭과 대소문자 유지, 다른 대소문자 리소스 분리 |
| whole scriptlet exception | 빈 `#@#+js()`가 모든 원본 scriptlet 호출을 제외하되 CSS·network 차단을 유지 |
| CSP | 목록 결합과 third-party frame 문맥 |
| invalid resources | canonical 충돌 시 baseline만 유지하고 community 규칙까지 폐기 |
| production redirects | 원본 45개와 모든 별칭을 실제 엔진에서 평가해 전체 MIME/base64 bytes 비교, 기존 fallback 별칭보다 원본 우선 |
| Mojo replacement | Accept보다 데이터 MIME 우선, UTF-8 π, NUL·invalid UTF-8를 포함한 바이너리, 빈 본문 성공 |

## 실행 결과

- Native core/settings/style/data 41개 + factory 47개 = **88개 통과**.
- Tooling **12개 통과**. 공개 source archive 129개 파일만 추출해 다섯 배포 파일을
  byte 단위로 동일하게 재생성했다.
- 실제 엔진 출력과 native Chrome fixture **1,239 assertion 통과**.
  기존 자체 adapter/CSS/MP4 Chrome fixture **25 assertion 통과**.
- 기존 YouTube lossless JSON **71 case**, 확장 protocol/playback **242 assertion 통과**.
- core GN dependency, Chromium/owned source whitespace, 기존 Chromium glue patch의
  reverse-apply 검사를 통과했다. 이번 작업은 Chromium 원본 glue를 수정하지 않았다.

- `tools/dev/build.sh` 전체 chrome target **빌드 성공**. macOS 앱 bundle의 별도 자료
  7개를 생성 결과와 byte/hash로 비교했다. 공개 runtime이 생성 header와 source archive에
  정확히 들어 있고, 적용된 owned 입력 167개와 원본 hash 116개도 일치했다.
- 최종 실행 로그와 checksum 결과는 ignored
  `.local-build/brave-test-hardening-integrity.json`에 기록했다.

## 범위와 남은 gate

Chrome fixture는 독립 임시 프로필에서 표준 Web API로 실행하며 Yee 자동 주입을
대신하지 않는다. about:blank/srcdoc의 native callback, 사이트 disable, 새 navigation과
실제 YouTube 서버·영상 재생의 실앱 검증은 사용자가 요청한 **MCP 실험 종료 후** gate다.
이번 srcdoc 테스트의 서버 계약 노드는 inert script로 observer 변환을 확인한 뒤 직접
실행했다. 실제 parser의 페이지 script 실행을 선제 차단했다는 증거로 사용하지 않는다.
전체 `#@#+js()` 회귀 검사는 엔진이 선택하는 scriptlet에 대한 것이다. 별도 Yee
YouTube adapter는 사이트 차단 해제로 제어하며, 이 필터 예외로 adapter까지 끄는
동작은 현재 제공하지 않는다.

Brave의 제품 설정·Standard/Aggressive 구분, DeAMP/debug UI, 사용자 리소스 편집 UI,
cache DAT·자동 목록 업데이트, 전체 procedural/action cosmetic을 이번 테스트 보강으로
구현했다고 주장하지 않는다. 기존 compiler의 제외 내역과 Brave 고유 permission bit 2
경계는 [현재 패키지 설계](content-blocking-private-core-filter-data.md)를 따른다.
