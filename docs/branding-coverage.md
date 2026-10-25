# 브랜딩 적용 범위와 검증 경계

최초 소스 감사일: 2026-09-12. 런타임 후속 확인일: 2026-09-28.
기본 브라우저 InfoBar 브랜딩 연결·macOS 실제 앱 확인일: 2026-10-08.
추가 네이티브 제품 배너 소스 교차 감사일: 2026-10-14.
로컬 Chromium `153.0.8005.0`, upstream 커밋
`25189dfe0a49b3f8324586374b8251b8d12ad1c2`의 소비 경로와 현재 overlay를
읽기 전용으로 감사했다. 아래 Chromium 소스 경로의 기준 디렉터리는
`.local-build/chromium/src/`다. Brave의 연결 방식은
[Brave 분석](brave-branding-analysis.md), 현재 설정 사용법은
[제품 브랜딩 설정](product-branding.md)을 참고한다.

브랜드명은 아직 확정되지 않았다. [brand.json](../branding/brand.json)의
`name: "Yee"`는 임시 기본값이며 `provisional: true`가 이 상태를 나타낸다.
새 표시 이름을 넣을 때 namespace·파일 이름·프로필·Keychain·OS 등록 ID까지
자동으로 바꾸지 않는다. 확정 이름을 코드와 패치에 직접 넣는 대신 설정에서
제품 메타데이터와 표시 리소스를 생성한다.

현재 판정은 **macOS core build verified / branding coverage incomplete**다.
설정된 제품명과 핵심 리소스는 macOS 앱 번들에 적용되어 빌드·실행됐고, Yee 소유
native UI도 같은 빌드에서 사용됐다. 모든 사용자 표시·OS 등록이 전환됐거나
출시할 수 있다는 완료 판정은 아니다. 일반 제품 문구·번역과 알려진 desktop 로고 소비 경로는 공통 생성기에 연결했다.
독립 OS 식별자·서비스 endpoint의 전환과 Windows/Linux·설치·업데이트 실행 검증은 별도다.

## 설정의 독립 역할

현재 구현된 필드는 `name`, 선택적인 `short_name`, `provisional`,
`internal_url_scheme`, `logo_source`, `logo_palette`, `logo_assets_manifest`, `strings_policy`다.
OS identity·외부 실행 scheme·service endpoint는 표시 이름과 별개의 역할이다.

| 설정 역할 | 값과 소비 대상 | 이름 변경 시 원칙 |
| --- | --- | --- |
| `name`, `short_name` | 제품·앱·설치 프로그램 표시명, Helper/Framework 표시 경로, UI 문장·접근성 안내 | 임시 이름을 다른 이름으로 바꾸면 같은 입력에서 다시 생성한다. 짧은 이름은 별도 지정할 수 있다. |
| `provisional` | 브랜드명 미확정 상태 | 기본값 `true`. 이름 확정 상태를 나타내며 OS identity 전환을 자동 실행하는 스위치가 아니다. |
| 내부 URL scheme | 사용자가 입력·편집·복사하는 브랜드 WebUI 주소 | 표시 이름에서 자동 생성하지 않는다. 실제 Chromium WebUI origin과 표시용 주소의 정책을 별도로 정의한다. |
| OS·저장소 `identity` | macOS Bundle ID, profile/cache/PWA 경로, Keychain 서비스·계정, Windows AppID·ProgID·COM GUID·정책 경로·서비스 이름 | 표시 이름을 바꿔도 유지한다. 최초 독립 ID 적용이나 기존 ID 변경에는 데이터·등록 이전을 설계한다. |
| 외부 실행 scheme | OS에서 브라우저를 실행하는 protocol handler | 내부 WebUI scheme과 분리한다. plist·Windows 등록·런타임 조회에 같은 안정된 값을 사용한다. |
| updater identity | Browser/Updater AppID·Bundle ID, privileged helper, 서비스·mutex·채널 ID | 표시 이름과 분리한다. 업데이터 기능과 실제 설치·삭제 경로에 연결한 뒤 적용한다. |
| services endpoints | 도움말·지원·업데이트·오류 보고·로그·로고 서버 URL | 실제 운영 서비스에 맞게 정한다. `Chromium` 문자열 치환으로 새 서버를 만들지 않는다. |
| assets | PNG mark, ICNS/ICO, 배율별 이미지, SVG/벡터, light/dark 워드마크, macOS `.icon`·asset catalog | 원본과 출력 manifest를 관리한다. 이름 변경과 이미지 변경은 독립 입력이다. |
| 서명 설정 | macOS Team ID·인증서·provisioning, Windows 서명 인증서 | 실제 배포 identity와 일치해야 한다. Chromium/Google 전용 entitlement를 브랜드 이름만 바꿔 재사용하지 않는다. |

## 판정 읽는 법

- **연결 확인:** 현재 설정에서 해당 입력까지 연결한 소스·생성 경로다. 빌드와 런타임 검증 범위는 각 행에 별도로 적는다.
- **소비 확인 / 미연결:** Chromium에서 실제 사용 지점을 찾았지만 우리 브랜드 설정을 아직 연결하지 않았다.
- **별도 원본 필요:** 하나의 PNG 크기 조절만으로 완성되지 않는 벡터·워드마크·레이어·애니메이션 입력이다.
- **호환성 유지:** 공유 형식·기술 식별자·저작권·외부 제품명으로, 전역 치환에서 제외한다.

표의 상태는 활성 desktop 경로를 우선한다. Android·ChromeOS·Linux 및 채널별
분기는 추가 플랫폼을 지원할 때 별도 manifest에 포함한다. 파일이 존재한다는
사실만으로 해당 플랫폼·채널에서 활성화된다고 판정하지 않는다.

## 제품 메타데이터·문자열·개발 진입점

| 항목 | 소비 경로 | 상태와 처리 |
| --- | --- | --- |
| 제품 메타데이터 | `chrome/app/theme/chromium/BRANDING`; `build/util/branding.gni:17`; `chrome/common/chrome_constants.cc:39` | 연결 확인. `PRODUCT_*` 네 값에서 앱·Framework·Helper 경로와 설치 프로그램 이름을 생성한다. 회사·저작권·identity는 보존한다. |
| 핵심 제품명 GRIT ID | `chrome/app/chromium_strings.grd`; 생성 `yee_product_names.grdp`, `yee_app_menu_name.grdp` | 연결 확인. 세 ID는 원래와 같은 `translateable="false"` 제품명 리소스다. |
| Windows installer 별도 문자열 생성기 | `base/win/embedded_i18n/create_string_rc.py`; `generate_embedded_i18n.gni`; `chrome/installer/util/BUILD.gn:226` | 연결 확인. 기존 SAX 생성기는 GRDP part를 무시해 조건부 `Chrome for Testing` 원문이 남을 수 있었다. 선언된 `--source-part`만 제자리에서 읽도록 연결하고 GN action 입력에 등록했다. 실제 생성기 임시 EN_US·KO RC의 이름 변경 검증 통과. 설치·OS 서비스 표시명 전체는 별도 미연결이다. |
| 비ASCII 메타데이터·Windows 버전 RC | `build/util/version.py`; `chrome/process_version_rc_template.gni`; `build/toolchain/win/rc/rc.py:139` | 연결 확인. 공통 읽기·출력은 UTF-8, Windows RC는 UTF-16 BOM으로 생성하도록 연결했다. 실제 RC driver의 읽기 함수가 임시 한글 RC를 Unicode로 인식하는 것을 검증했다. RC compiler와 Windows 실행은 미검증이다. |
| Windows crash client 제품명 변환 | `chrome/installer/setup/installer_crash_reporter_client.cc:41`; `chrome/notification_helper/notification_helper_crash_reporter_client.cc:63`; `chrome/windows_services/service_program/crash_reporting.cc:119` | 연결 확인. `ASCIIToWide`를 `UTF8ToWide`로 바꿔 설정된 short name의 UTF-8을 읽도록 했다. 컴파일·오류 수집 서버의 product identity는 미검증이다. |
| Linux installer KV | `chrome/installer/linux/common/installer.py:394` | 파서 보완 확인. 첫 등호만 분리해 `Orbit = Browser` 같은 값을 보존하고 파일을 UTF-8로 읽는다. 실제 helper 임시 입력 테스트 통과. Linux 패키징 전체는 범위 밖이다. |
| 우리 소유 UI | `browser/ui/brand.h`, `agent_bridge.cc`, `agent_bridge_prompt.cc`, `sidebar_footer.cc` | 연결 확인. 공통 helper로 제품명을 읽는다. 탭 glue 접근성 문구와 Agent 질문 제목도 연결했고, Agent 포인터 폭은 이름의 텍스트 폭에서 계산한다. macOS `yee_ui` 빌드와 native prompt test를 통과했다. 다른 제품명으로 바꾼 실제 앱 geometry는 별도 검증 대상이다. |
| 실행·빌드 진입점 | `tools/dev/common.zsh`, `common.ps1`, `build.*`, `build-ui.*`, `apply.*`, `configure.*` | 연결 확인. macOS 전체/UI 빌드 경로는 실제 output 동기화와 앱 생성을 확인했다. Windows 전체 빌드도 기존 output에 UI 소스를 복사하고 이름 입력을 갱신하도록 보완했으나 Windows 실행 환경은 미검증이다. |
| 이름 변경 전 실행 프로세스 | `tools/dev/common.zsh`; `browser_bundle_executables.py`; `gracefully-quit-yee.swift` | 같은 output에 남은 이전 이름 번들 탐색 연결 확인. 동일 Bundle ID의 plist 실행 경로만 선택하고 native helper가 정확한 앱·실행 경로를 확인한다. 임시 번들 테스트와 macOS 실제 앱 검증 전 정상 종료 경로를 통과했다. 삭제된 번들·다른 output의 프로세스는 범위 밖이다. |
| 기본 브라우저·PDF InfoBar | `browser/ui/default_browser_prompt.cc`; `browser/ui/product_logo.cc`; Chromium `default_browser_infobar_delegate.cc`, `pdf_infobar_delegate.cc`, `pdf_infobar_controller.cc` | 연결 확인. 기본 브라우저 안내의 영어·한국어 템플릿에 현재 제품명을 전달한다. 두 배너는 투명한 두 색 로고를 공유하고 실제 배경에 따라 `IDR_PRODUCT_LOGO`·`IDR_PRODUCT_LOGO_WHITE`를 선택한다. 다른 언어는 영어 fallback이다. 2026-10-08 기본 브라우저 안내의 문구·기존 로고·버튼을 확인했다. 2026-10-14 macOS의 새 프로세스에서 중앙 InfoBar 경로의 PDF 배너와 밝음·어둠 전환에 따른 배색 갱신을 확인했고, 공통 로고 자산 테스트도 통과했다. 기본값 설정 동작과 닫기·재표시 정책은 Chromium이 유지한다. 별도 modal·bubble·OS 알림은 이 배너 수정을 공유하지 않는다. |
| 추가 제품 InfoBar | `browser/ui/product_logo.cc`; Chromium `pin_infobar_delegate.cc`, `browser_infobar_registry.cc`, `session_restore_infobar_delegate.cc`, `startup_launch_infobar_delegate.cc`, `keystone_infobar_delegate.cc` | 연결 확인. Dock/작업표시줄 고정은 기존 delegate와 중앙 spec을 모두 연결했다. 세션 복원·Windows 시작 시 실행·macOS 업데이터 권한 승격도 같은 두 색 로고를 사용한다. 안내 생성·닫기·설정 변경 동작은 유지한다. 플랫폼·기능 플래그별 실행 검수는 아래 표와 구분한다. |
| 문장 속 제품명·번역 | `branding/strings.json`; `brand_strings.py`; 여섯 GRIT 계열 | 연결 확인. 고정 `HEAD`에서 GRD·GRDP·XTB를 생성하고 메시지 ID와 기존 번역을 함께 연결한다. 원래 번역·placeholder·URL·외부 제품명·저작권을 보존한다. 재생성 비교로 실제 적용을 검사한다. |
| macOS Helper·권한 안내 | `chrome/tools/build/mac/infoplist_strings_util.cc`; 생성된 branded locale pak | 연결 확인. Helper와 권한 문장이 같은 브랜드 문구·번역을 읽는다. 실제 OS 권한 요청 dialog는 별도 검수한다. |
| WebUI 링크의 문구 | `chrome/browser/resources/net_internals/index.html:24`; `components/components_chromium_strings.grd:318` | 소비 확인 / 미연결. 주소 포매터 밖의 `chrome://` 안내·링크 본문도 포함한다. 실제 링크 destination과 표시 문자열을 구분한다. |
| 문자열 예외 | 저작권/credits/license, UA·Client Hints, ChromeOS·Chromecast·외부 Google 제품명, protocol/schema 식별자 | 호환성 유지. 남은 `Chrome`/`Chromium`이 모두 브랜딩 누락은 아니다. ID별 분류와 예외 사유가 필요하다. |

`BRANDING` 관리 marker도 실제 Chromium 파서의 입력 계약을 따라야 한다.
`build/util/version.py:18–31`은 모든 줄을 `KEY=VALUE`로 읽으므로 `=` 없는
주석 marker는 메타데이터 생성 자체를 실패시킨다. 이번 보완에서는
`OVERLAY_BRANDING_MANAGED=1` 같은 KV marker를 사용하고 실제 `version.py`의
임시 입력 생성을 빌드 없이 검사한다.

이름은 XML·GN·Python·C++·plist 생성 언어마다 escaping/입력 검사를 거쳐야 한다.
기존 허용값 `O'Reilly`를 `chrome/installer/mac/signing/build_props_config.py.in:28`에
raw 치환하면 Python 문자열 파싱이 실패하는 것을 메모리에서 재현했다.
`build/util/branding.gni:17`의 `$` interpolation과 `version.py:74`의 `@KEY@`
치환도 고려한다. shell 경로 검사 통과만으로 생성 입력이 유효하다고 판정하지 않는다.

`0001` 재생성은 `0002`가 소유한 GRD 변경을 중복 포함하면 안 된다. 이를
포함한 임시 새 체크아웃에서 다음 `0002` 적용이 실패하는 경로를 확인했으므로
패치 소유 분리와 순차 적용·역적용 검사를 독립 수용 기준으로 둔다.

### 네이티브 배너의 소스 연결과 실행 검수

2026-10-14 교차 감사는 제품 표식의 배색과 공통 배너 레이아웃 경로를 구분했다.
`InfoBarView`의 오른쪽 링크·닫기 영역 정렬은 공통 수정이므로 API 키 안내 하나에만
적용되지 않는다. 일반·인라인 링크 `ConfirmInfoBar`는 같은 바탕을 사용한다.
Alternate Nav와 화면 공유는 같은 바탕 안에 별도 콘텐츠 배치가 있어 별도 검수가 필요하다.
쿠키·권한·화면 공유·확장 프로그램·보안 경고는 제품 표식으로 바꾸지 않고 기능 아이콘을 유지한다.

| 대상 | 연결·플랫폼 | 실제 실행 검수 경계 |
| --- | --- | --- |
| 기본 브라우저 | 기존 delegate, desktop | 2026-10-08 macOS 문구·로고·버튼 확인. 2026-10-14 최신 macOS 앱의 어두운 배너에서 두 색 로고와 버튼 정렬을 확인했다. |
| PDF | 기존 delegate·중앙 spec, macOS/Windows | 2026-10-14 macOS 기존 delegate의 밝은 배너와 중앙 spec의 밝음·어둠·실시간 배색 전환을 확인했다. |
| Dock/작업표시줄 고정 | 기존 delegate·중앙 spec, macOS/Windows | 두 색 소스 연결. `infobar-internals`에 고정 안내 트리거가 없어 실행 검수는 별도다. |
| 세션 복원 | 기존 delegate, macOS/Windows/Linux | 2026-10-14 macOS 내부 도구로 생성해 어두운 배경의 두 색 로고와 설정 링크 정렬을 확인했다. 해당 검수 프로필의 정상 탭 전체에 나타난다. |
| 시작 시 실행 | 기존 delegate, Windows | 두 색 소스 연결. macOS 빌드·실행으로 검증하지 않는다. |
| 업데이터 권한 승격 | Keystone delegate, macOS·updater 활성 구성 | 두 색 소스 연결. 현재 macOS 구성의 `ENABLE_UPDATER=0`에서는 컴파일·실행되지 않는다. |
| API 키·지원 종료·보안 안내 | 공통 ConfirmInfoBar, 기존·중앙 경로 | 공통 링크 정렬의 소스 연결. 안내 조건·버튼 유무·좁은 폭은 각각 구분해 검수한다. |
| 쿠키·권한·개발자 도구·확장·테마·플러그인 | 공통 배너·각 기능 아이콘 | 2026-10-14 macOS 쿠키 안내와 허용·거부 두 버튼의 DevTools 배너를 생성해 의미 아이콘·정렬을 확인했다. 확장·테마·플러그인별 실행 검수는 별도다. 권한 허용·설정 변경 동작은 실행하지 않았다. |
| Alternate Nav·화면 공유 | 공통 바탕·별도 내부 배치 | 공통 수정의 대상이지만 별도 기능 조건·실행 검수가 필요하다. |

Windows의 `installer_downloader_infobar_delegate.cc`와 중앙 controller spec은
기존 단색 제품 벡터를 사용한다. Google Chrome branded 구성 전용이어서 현재 Yee
빌드에는 포함되지 않는다. 별도의 기본 브라우저 bubble·OS 알림도 단색 벡터를 사용하며
이 배너 배색 수정과 분리한다. modal WebUI의 생성된 제품 SVG 연결은 이와 별개다.
소스 연결만으로 Windows/Linux 또는 비활성 updater의 실행 검수를 완료했다고 판정하지 않는다.

## OS 통합과 영속 identity

| 항목 | 소비 경로 | 상태와 처리 |
| --- | --- | --- |
| macOS 앱·Helper·Framework ID | `chrome/BUILD.gn:535`, `:772`, `:1249`; `chrome/app/framework-Info.plist:10`; `chrome/app/helper-alerts-Info.plist:6` | 소비 확인 / 미연결. `MAC_BUNDLE_ID`에서 suffix를 생성한다. 현재 `org.chromium.Chromium`을 유지한다. 표시 이름을 바꿀 때 ID를 유도하지 않는다. |
| macOS 기본 profile/cache | `chrome/common/chrome_paths_mac.mm:26`, `:87`, `:106` | 소비 확인 / 미연결. 기본값은 `Chromium`; `CrProductDirName` plist override를 사용할 수 있다. profile 변경은 cache 경로에도 영향을 준다. |
| macOS Keychain | `components/os_crypt/common/keychain_password_mac.mm:34–43` | 소비 확인 / 미연결. `Chromium Safe Storage`/`Chromium`은 사용자에게 보이는 암호화 키 조회 identity다. 변경하면 기존 암호화 데이터 접근 이전이 필요하다. |
| macOS 파일 종류 설명 | `chrome/app/app-Info.plist:236`, `:264`; `chrome/tools/build/mac/infoplist_strings_util.cc:198` | 연결 확인. 문서 설명은 설정된 제품명·번역을 사용하며 `Chromium Extension`·`Chromium Shortcut` lookup key는 보존한다. 공유 UTType·MIME·확장자는 호환성 유지 대상으로 분류한다. |
| macOS PWA 폴더 | `chrome/browser/web_applications/os_integration/mac/apps_folder_support.mm:40`, `:118`; `chrome/browser/shell_integration.cc:199` | 연결 확인. `Chromium Apps.localized` 저장 경로를 보존하고 GRIT 표시명과 폴더 로고를 교체한다. 이미 생성된 폴더·PWA 바로가기의 갱신을 검증한다. |
| 외부 직접 실행 scheme | `build/apple/tweak_info_plist.py:287`; `chrome/browser/shell_integration_mac.mm:301`; `chrome/install_static/chromium_install_modes.h:53` | 소비 확인 / 미연결. macOS는 새 Bundle ID만 넣으면 plist 직접 실행 항목이 제거될 수 있다. 생성·런타임·Windows 등록의 scheme을 함께 연결한다. |
| Windows 설치·profile·AppID·ProgID | `chrome/install_static/chromium_install_modes.h`; `chrome/common/chrome_paths_win.cc`; `chrome/install_static/install_util.cc`; `branding/identity.json` | 사용자별 테스트판 연결. Yee 경로·AppID·ProgID·등록 GUID·sandbox identity를 표시 설정과 분리한다. 실행 파일 검색에는 전용 `Yee.exe` 등록 이름을 사용하고 제거 시 경로 소유를 확인한다. 기존 Chromium 프로필은 자동 이전하지 않는다. 검증 범위는 [Windows 테스트판](windows-test-release.md)에 둔다. |
| Windows Elevation/Tracing 서비스 | `chrome/install_static/install_util.cc:357–385`; `chrome/installer/setup/install_worker.cc:125`, `:406` | 소비 확인 / 미연결. 현 Chromium은 서비스 영속 이름을 표시명에서 공백 제거로 생성한다. 표시명과 안정된 서비스 이름의 소비 경로를 먼저 분리해야 한다. |
| Windows 정책 domain | `components/policy/tools/generate_policy_source.py`; `components/policy/BUILD.gn`; `chrome/install_static/install_util.cc` | Yee 경로 연결. 제품 생성 입력과 generic generator의 선택 인자를 사용해 `SOFTWARE\Policies\Yee`로 일치시킨다. |
| GCM product category | `chrome/browser/gcm/gcm_product_util.cc:39` | 소비 확인 / 미연결. short name의 ASCII 영숫자를 추출해 category를 만들고 profile prefs에 저장한다. 한글 이름이나 새 profile·rename에서 값이 달라질 수 있다. 표시명과 안정된 서비스 identity를 분리하고 기존 prefs 이전·서버 소비를 검토한다. |
| macOS 서명·entitlement | `chrome/installer/mac/signing/build_props_config.py.in:28`, `:40`; `signing/parts.py:39–123`; `chrome/BUILD.gn:740` | 소비 확인 / 배포 미검증. Bundle suffix·실제 서명·privileged helper와 일치해야 한다. Google 전용 entitlement는 브랜드 치환 범위가 아니다. |
| updater·enterprise companion | `chrome/updater/branding.gni:10–45`; `chrome/updater/mac/BUILD.gn:126`; `chrome/enterprise_companion/branding.gni` | 소비 확인 / 미연결. 일반 `BRANDING`과 독립된 표시명·Bundle/AppID·서비스·mutex·팀 ID·서버를 설정한다. 기능 활성화 여부와 실제 서비스 준비를 확인한 뒤 연결한다. |

## 이미지·아이콘 적용 manifest

현재 제품 로고는 02 이음매듭 SVG와 설정의 먹색·청록 배색을 사용한다.
`generate-brand-assets.py`는 표시 이름·원본·배색·생성기에서 하나의 desktop
자산 묶음을 만들고, macOS·Windows 적용기는 같은 manifest와 파일 해시를
검증한다. 이름을 바꾸면 워드마크도 다시 생성한다. 적용 전에 모든 출력의
존재·해시·소유 경로를 검사하며 같으면 수정 시간을 유지한다.

| 출력/사용처 | 소비 경로 | 상태와 원본 |
| --- | --- | --- |
| 앱 ICNS·Windows 주 아이콘·일부 PNG | `tools/overlay/install-brand-assets.sh`, `install-brand-assets.ps1`; `chrome/BUILD.gn:655`; `chrome/app/chrome_exe.rc:59` | 연결 확인. 같은 SVG·배색에서 생성한 ICNS·ICO·PNG를 해시 검증 후 적용한다. |
| macOS asset catalog | `chrome/BUILD.gn:660`; `chrome/app/app-Info.plist:152`; `chrome/app/theme/chromium/mac/Assets.xcassets`; `tools/mac/icons/compile_car.py:175` | 연결 확인. fallback PNG, 세 appearance의 매듭 레이어, 문서 badge와 `Assets.car`를 같은 패키지에서 생성한다. |
| 배율별 제품 로고·About 화면 | `chrome/app/theme/theme_resources.grd:178`, `:207`; `chrome/browser/resources/settings/about_page/about_page.html.ts:16`; `chrome/browser/ui/webui/current_channel_logo.cc:36` | 연결 확인. 실제 GRIT 입력인 `default_100_percent`/`default_200_percent`에 같은 크기의 1x/2x PNG를 적용한다. 작은 raster 로고는 밝은 배경판을 포함해 두 테마에서 읽힌다. |
| HTML/PDF·앱 목록·Incognito ICO | `chrome/app/chrome_exe.rc:75–79` | 연결 확인. HTML/PDF는 문서 윤곽과 매듭 badge를 결합하고 PDF 표기를 유지한다. 앱 목록도 교체하며 resource 순서와 인덱스는 유지한다. Incognito 의미 아이콘은 보존한다. Windows profile 아이콘 cache 버전을 올려 재생성을 요청한다. |
| Windows Start tile | `chrome/installer/setup/generate_visual_elements_manifest_work_item.cc:26–35`; `chrome/app/theme/chromium/win/tiles` | 연결 확인. 두 tile PNG도 설정의 같은 로고에서 생성한다. Windows 실행은 미검증이다. |
| 새 탭·버전 파비콘 | `chrome/browser/ui/webui/new_tab_page/new_tab_page_ui.cc:1467`; `chrome/browser/ui/webui/version/version_ui.cc:237`; `chrome/browser/ui/webui/whats_new/whats_new_ui.cc:94` | 연결 확인. `IDR_NTP_FAVICON`과 `IDR_PRODUCT_FAVICON`의 별도 1x/2x 파일도 매듭에서 생성한다. NTP는 native alpha mask가 배경판까지 칠하지 않도록 배경판 없는 투명 매듭을 사용한다. 이를 공유하는 업데이트 안내·분할 새 탭에도 적용된다. 버전 파비콘과 앱/OS 아이콘은 기존 배경판 역할을 유지한다. |
| WebUI 제품 SVG·애니메이션 | `chrome/app/theme/chrome_unscaled_resources.grd:87–88`; `chrome/browser/ui/webui/default_browser/default_browser_modal_ui.cc:86`; `chrome/browser/ui/webui/intro/intro_ui.cc:223` | 연결 확인. 두 resource 모두 같은 매듭 SVG를 사용한다. 기존 애니메이션 소비에는 정적 매듭을 제공하며 색은 light/dark media를 따른다. |
| 공통 WebUI dark SVG | `ui/webui/resources/images/BUILD.gn:63`; `chrome/browser/resources/extensions/manager.html.ts:37`; `chrome/browser/resources/contextual_tasks/top_toolbar_logo.html.ts:22` | 연결 확인. 공통 dark 리소스도 매듭의 밝은 단색 형태로 생성한다. |
| 네이티브 제품 벡터 | `components/vector_icons/BUILD.gn`; `components/omnibox/browser/BUILD.gn`; `chrome/app/vector_icons/BUILD.gn`; `ui/message_center/BUILD.gn` | 연결 확인. 공통·주소창·브라우저·알림의 최신/이전 제품 표식 아홉 파일을 변환한다. 각 원본의 기본 canvas와 다중 크기 표현을 유지하고 호출자의 테마 색을 따른다. |
| WebUI 검색 제안 표식 | `chrome/browser/ui/webui/cr_components/searchbox/searchbox_handler.cc:203`; `ui/webui/resources/cr_components/searchbox/icons/BUILD.gn` | 연결 확인. 네이티브 벡터와 별개인 `chrome_product_cr23.svg`도 같은 매듭의 단색 형태를 사용한다. |
| 공유·설정·WebUI Browser inline 제품 표식 | `ui/webui/resources/cr_elements/icons.html.ts`; `chrome/browser/resources/settings/icons.html.ts`; `chrome/browser/resources/webui_browser/icons.html.ts` | 생성 연결. Chromium Settings의 About 메뉴 `cr:chrome-product`, Safety Hub의 `settings20:chrome-product`, WebUI Browser 제품 표식을 같은 매듭 geometry로 생성한다. 세 파일의 rounded·legacy 6개 표식은 `currentColor`를 따르며 기능·외부 서비스 아이콘과 TS 분기는 보존한다. 감사는 underscore·hyphen 제품 ID와 공유 WebUI root를 모두 탐색한다. 2026-10-14 macOS 최종 앱의 밝음·어둠 Settings 메뉴에서 매듭 표시를 확인했다. Safety Hub·WebUI Browser는 소스·자산·빌드 확인 범위다. |
| 콘텐츠 안의 안내·첫 실행 배너 | `chrome/browser/ui/webui/intro/intro_ui.cc:219–225`; `chrome/browser/resources/intro/default_browser/app_refresh.css:84`; `browser/ui/default_browser_prompt.cc` | 연결 확인. 이미지에 삽입되어 있던 Chromium 표식은 매듭을 담은 벡터 기반 브라우저 삽화로 교체한다. 환영·기본 브라우저·프로필 화면의 공유 SVG와 네이티브 안내 아이콘도 생성 묶음을 사용한다. |
| Windows 설치·PWA 실행 파일 | `chrome/installer/setup/setup.rc:57`; `chrome/installer/mini_installer/mini_installer.rc:32`; `chrome/browser/web_applications/chrome_pwa_launcher/chrome_pwa_launcher_exe.rc:6` | 연결 확인. 주 앱 ICO와 별개인 세 실행 파일의 ICO도 같은 원본에서 생성한다. Windows 실행은 미검증이다. |
| 결제 light/dark 워드마크 | `chrome/app/theme/theme_resources.grd:219–220`; `chrome/browser/ui/views/payments/payment_request_views_util.cc:98` | 연결 확인. 매듭과 현재 `short_name`의 워드마크를 1x/2x로 생성한다. 외부 결제 제공자 로고는 보존한다. |
| macOS PWA 폴더 overlay | `chrome/app/theme/chrome_unscaled_resources.grd:140–143`; `chrome/browser/web_applications/os_integration/mac/icon_utils.mm:353` | 연결 확인. 네 개의 폴더 PNG에 매듭을 사용한다. 기존 폴더 이름·저장 경로의 이전은 별도 작업이다. |
| 제품 로고 접근성 이름 | `chrome/browser/ui/webui/version/version_ui.cc`; `intro/intro_ui.cc`; `settings/settings_localized_strings_provider.cc`; `search_engine_choice/search_engine_choice_ui.cc` | 연결 확인. 로고 이미지의 접근성 이름은 설정에서 생성한 `IDS_SHORT_PRODUCT_NAME`을 읽는다. 별도의 Chromium 로고 번역이 새 표식에 남지 않도록 한다. |

버전 페이지의 `components/resources/version_ui_scaled_resources.grdp`에 있는
제품 light/dark 이미지와 파비콘도 연결한다. Android의 직접 resource path와
desktop의 `chrome://theme/IDR_PRODUCT_LOGO` 참조가 같은 ID를 사용하므로,
Android 전용 이미지로 간주하지 않는다.

`audit_logo_assets.py`는 생성 목록에서 독립적으로 GRIT 제품 이미지 연결,
아홉 네이티브 원본과 공통 WebUI 표식, 검토한 삽화·OS 입력을 조사한다.
누락·미적용 파일을 실패로 보고하며, source 검사 통과가 모든 실행 상태의
검수를 뜻하지는 않는다. 일반 웹 페이지의 기본 파비콘, 시크릿·비밀번호 등
기능 아이콘과 외부 서비스 로고는 해당 의미를 유지한다.

## 내부 URL의 추가 경계

브랜드 설정에서 생성한 접두어를 입력·주소 표시·전체 복사·드래그에 연결했다.
기본값은 `short_name`의 소문자 값이며 필요하면 `internal_url_scheme`으로
지정한다. 실제 navigation과 WebUI origin, 북마크·세션 저장은 Chromium 주소를
사용한다. `chrome-untrusted://`, `chrome-search://` 같은 origin은 유지한다.

| 경계 | 확인한 경로 | 필요한 수용 기준 |
| --- | --- | --- |
| 실제/virtual URL 분기 | `content/browser/renderer_host/navigation_controller_impl.cc:453`; `chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.cc:72` | 입력이 실제·virtual URL에 들어간 뒤 실제 URL만 rewrite될 수 있다. 각 entry가 어떤 주소를 보존하는지 검증한다. |
| 전체 복사 | `components/omnibox/browser/omnibox_text_util.cc:167` | navigation entry에서 text와 URL clipboard 형식을 다시 만든다. 주소 표시 문자열만 바꿔도 복사 결과가 같다고 가정하지 않는다. 부분 복사·편집·공유도 포함한다. |
| 주소 드래그 | `chrome/browser/ui/views/location_bar/location_bar_view.cc:1945` | `GetVisibleURL()`을 사용한다. text/URL drag payload와 외부 앱 결과를 확인한다. |
| 북마크 | `chrome/browser/ui/bookmarks/bookmark_utils.cc:167` | `GetVisibleURL()` 저장 결과와 기존 `chrome://` bookmark·수정·재실행을 확인한다. |
| 세션 저장·복원 | `components/sessions/content/content_serialized_navigation_builder.cc:38`, `:159` | virtual URL을 저장하고 복원한다. 이름 변경·scheme alias 변경·back/forward·reload·tab 복제를 함께 검증한다. |
| navigation 이전 명령 | `chrome/browser/ui/navigator/browser_navigator.cc:920` | quit/restart 같은 non-navigation URL은 LoadURL 전에 처리한다. about rewrite보다 먼저 실행되는 경로에 alias를 연결할지 명시한다. |
| PageInfo 전제 | `chrome/browser/ui/views/page_info/page_info_bubble_view.cc:143` | InternalPageInfoBubbleView가 `chrome` scheme을 `CHECK`한다. branded virtual URL을 전달하는 조합에서 실제/표시 URL 선택을 확인해야 한다. |
| WebUI 권한·origin | scheme 등록, WebUI controller·bindings, origin 포매터·CSP·renderer 접근 검사 | trusted/untrusted 경계를 유지한다. 외부 페이지 iframe·window.open·location 이동과 guest/private 제한을 확인한다. |
| 읽기 전용 pane 주소 | Yee split-pane 표시와 native Omnibox | 같은 표시 helper와 URL 정책을 사용한다. menu·자동완성·내부 link·bookmark가 동일한 alias로 이동해야 한다. |

위 경로에 공통 helper를 연결했으며, macOS 앱 검증은 실제 주소와 표시·복사
주소를 구분해 확인한다. WebUI 본문의 링크 문구, 북마크 편집 문자열,
권한 origin의 표시 문자열과 Windows 동작은 추가 소비 경로·검증 대상이다.
source-level 검사만으로 모든 runtime 경계가 검증됐다고 판정하지 않는다.

## 처리 순서와 완료 조건

1. 실제 Chromium 파서에 맞는 KV marker, 생성 언어 입력, `0001`/`0002` 소유
   분리, 모든 빌드 진입점의 동기화를 먼저 확인한다. 임시 upstream·이전 패치
   적용 트리에서 preview·순차 적용·반복 적용·rename·역적용을 검사한다.
2. 임시 이름 표시를 설정에 모으고 Agent 접근성·긴 이름 geometry까지 연결한다.
   이름 변경이 영속 identity에 영향을 주지 않는지 확인한다.
3. 문자열 ID·locale·플랫폼·번역 및 예외 inventory를 만들고 GRD/XTB를 같은
   upstream 원본에서 생성한다. OS Helper·권한·파일 종류 설명도 포함한다.
4. 자산 manifest를 완성하고 PNG 생성 출력과 별도 원본을 연결한다. ICNS/ICO
   파일 존재 검사만으로 `Assets.car`·벡터·워드마크까지 적용됐다고 판정하지 않는다.
5. 브랜드 설정에서 scheme을 생성하고 입력·표시·복사·드래그·저장·명령·
   PageInfo를 공통 변환 helper에 연결한다. 실제 WebUI origin을 유지하고,
   추가 표시 경로와 플랫폼별 권한 경계를 integration checkpoint로 검증한다.
6. 독립 OS/updater identity는 표시 이름과 별도로 적용한다. profile·Keychain·
   설치 등록·PWA·정책·서비스·업데이트의 최초 전환과 복구를 함께 설계한다.
7. 지원 플랫폼별 실제 앱·패키지·등록 결과를 검사한다. macOS 핵심 앱은 빌드와
   실행을 확인했으며, 설치·서명·업데이트와 Windows/Linux 결과는 아직 남아 있다.
   실행 전에는 기존 브라우저를 정상 종료하고 새로 빌드된 앱을 실행한다.

자동화 감사 진입점은 다음 형식으로 관리한다. 빌드나 앱 실행은 하지 않는다.

```sh
python3 tools/overlay/audit-branding.py /absolute/path/to/chromium/src
python3 tools/overlay/audit-branding.py /absolute/path/to/chromium/src --json
python3 tools/overlay/audit-branding.py /absolute/path/to/chromium/src --require-complete
```

일반 모드는 확인한 적용 범위와 남은 항목을 보고한다. `--require-complete`는
브랜드 전환 대상 residual이 남으면 실패해야 한다. 저작권·공유 파일 형식·
외부 제품명은 사유를 가진 예외 목록으로 관리하고, 무조건 문자열을 지워
strict 검사를 통과시키지 않는다. 감사가 통과해도 적용된 빌드와 실제 OS 동작을
검증했다는 의미는 아니다. 번역·asset 원본·URL 및 identity 연결이 미완료라면
이를 숨기지 않고 coverage의 미연결 상태로 보고한다.

## 검증 기록

### 2026-09-12 최초 소스 감사

2026-09-12 기준 선언된 소비 범주와 재귀 GRD part를 검사했다. 문자열 후보
697개 중 632개는 개별 판정 전이다. 조건부 플랫폼·제품 분기와 외부 제품명도
포함한 후보 수이며, 전부 브랜드 치환 대상이라는 뜻은 아니다. 우리 네이티브
UI와 `0001`의 추가 C++ 문자열에서 고정 `Yee` 표시 후보는 0개였다. namespace,
개발 플래그, 프로토콜 식별자와 저작권은 이 표시 후보에 포함하지 않는다.

빌드 없는 설정·패치·감사·번들 fixture 테스트 34개와 실행 preflight가 통과했다.
실제 Chromium 버전/Python/RC 생성 입력, 이전 고정 이름·주석 marker 이전,
rename·preview·재적용 및 패치 소유 분리를 검사했다. 실제 소스의 `0001`
역적용 검사와 Chromium 소스 경로의 whitespace 검사도 통과했다.
공통 버전 생성기 변경은 임시 체크아웃에서 upstream Python 회귀 검사도 실행했다.
새 GN 연결은 parser로 문법을 확인했으며 GN generation이나 빌드는 실행하지 않았다.

최초 검토에서는 실제 `.local-build`에 새 제품명 GRDP 연결을 적용하지 않았으며,
적용 preview가 기존 입력 내용과 수정 시간을 유지하는 것을 확인했다.
이후 사용자의 `Yee` 설정 요청에 따라 `0002` 연결과 제품명 입력을 실제 소스에
적용했다. 제품명·short name은 `Yee`, `provisional`은 `true`이며, 제품명 입력
감사의 적용 상태는 `applied`다. 패치 역적용·소스 whitespace 검사도 통과했다.
미연결·미검토·임시 이름과 runtime 미검증이 남아 `--require-complete`는 exit 1이다.
Windows PowerShell 런타임은 이 환경에 없으므로 실행 검증하지 않았다.
브라우저 빌드·앱 실행·설치 등록 검증도 이 최초 감사에서는 수행하지 않았다.

### 2026-09-28 후속 확인

제품명 입력은 현재 checkout에 적용되어 있고 생성된 macOS `Yee.app`의
`CFBundleDisplayName`과 `CFBundleName`은 모두 `Yee`다. 전체 Chromium/Yee 빌드와
새 앱 실행을 완료했으며, 이후 UI 전용 변경도 `build-ui.sh`와 native prompt
test로 확인한다. 실제 앱에서 Sidebar·Agent UI와 일반 탭 동작을 확인한 기록은
각 주제별 검증 문서에 남아 있다.

자동 감사는 694개 문자열 후보 중 632개를 아직 개별 판정 전으로 보고한다.
소유 C++의 이름 휴리스틱에 남는 항목은 내부 URL·명령줄 switch와 개발 로그이며,
사용자 질문 제목은 공통 제품명 accessor를 사용한다. 감사 도구 자체는 빌드나
실행 증거를 읽지 않으므로 `runtime_verification: not_verified`를 계속 보고한다.
미연결 문자열·번역·asset·URL·identity와 임시 제품명이 남아
`--require-complete`도 계속 실패해야 한다. 이 결과는 위 macOS 핵심 빌드 확인과
모순되지 않으며, 브랜딩 전체 완료를 뜻하지 않는다.

### 2026-10-13 문구·로고·맞춤설정 보강

여섯 GRIT 계열의 1,190개 제품 참조를 고정 upstream에서 생성했다. 592개 생성 입력의
현재 적용 결과가 재생성 결과와 일치한다. 실제 GRIT로 원본·변환 문구의 fingerprint
2,440개를 비교해 불일치가 없었으며, 한국어·placeholder·라이선스 링크·이름 재변경·
로컬 수정 보호를 실제 GRIT fixture로 검사했다. Windows 설치 문자열도 생성된 XTB를
읽도록 연결했다. 이는 Windows 설치 프로그램의 실행 검증과 구분한다.

로고 묶음은 83개 자산이며 독립 감사의 desktop 소비 파일 57곳에 누락·미적용이 없다.
이는 원본·생성물 연결 판정이며, 모든 안내의 배색 일관성·상태별 실행 검수를 뜻하지 않는다.
미리보기와 WebUI 도구 모음의 내장 SVG, PDF 뷰어명, macOS 문서 표시명도 연결했다.
전체 앱 빌드와 네이티브 색상·페이지 색 controller 31개 검사가 통과했다.
실제 앱 상태 검수와 플랫폼·출시 범위는 각 실행 기록으로 별도 확인한다.
