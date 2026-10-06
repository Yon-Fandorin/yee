# 브랜딩 적용 범위와 검증 경계

최초 소스 감사일: 2026-09-12. 런타임 후속 확인일: 2026-09-28.
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
출시할 수 있다는 완료 판정은 아니다. 아직 연결하지 않은 문자열·URL·자산·설치
식별자가 있으며 Windows/Linux와 설치·업데이트 경로는 실행 검증하지 않았다.

## 설정의 독립 역할

현재 구현된 필드는 `name`, 선택적인 `short_name`, `provisional`,
`logo_source`, `logo_crop_size`다. 아래 `identity`·scheme·service·추가 asset
필드는 후속 연결의 설계 역할이며, 이름만 추가하면 적용되는 기능이 아니다.

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
- **별도 원본 필요:** 현재 PNG의 크기 조절만으로 완성되지 않는 벡터·워드마크·레이어·애니메이션 입력이다.
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
| 문장 속 제품명·번역 | `chrome/app/chromium_strings.grd`; `components/components_chromium_strings.grd`; 각 locale `.xtb` | 소비 확인 / 미연결. GRD 원문과 XTB 번역 ID를 같은 원본에서 변환한다. 이전 생성물에 재치환하지 않는다. 외부 제품명·Attribution 예외를 명시한다. |
| macOS Helper·권한 안내 | `chrome/tools/build/mac/infoplist_strings_util.cc:147`, `:165`, `:169`, `:184` | 소비 확인 / 미연결. Helper 이름, 카메라·마이크·Bluetooth·위치·로컬 네트워크·credential 권한 문장은 제품명 세 ID만 변경해도 모두 바뀌지 않는다. |
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

## OS 통합과 영속 identity

| 항목 | 소비 경로 | 상태와 처리 |
| --- | --- | --- |
| macOS 앱·Helper·Framework ID | `chrome/BUILD.gn:535`, `:772`, `:1249`; `chrome/app/framework-Info.plist:10`; `chrome/app/helper-alerts-Info.plist:6` | 소비 확인 / 미연결. `MAC_BUNDLE_ID`에서 suffix를 생성한다. 현재 `org.chromium.Chromium`을 유지한다. 표시 이름을 바꿀 때 ID를 유도하지 않는다. |
| macOS 기본 profile/cache | `chrome/common/chrome_paths_mac.mm:26`, `:87`, `:106` | 소비 확인 / 미연결. 기본값은 `Chromium`; `CrProductDirName` plist override를 사용할 수 있다. profile 변경은 cache 경로에도 영향을 준다. |
| macOS Keychain | `components/os_crypt/common/keychain_password_mac.mm:34–43` | 소비 확인 / 미연결. `Chromium Safe Storage`/`Chromium`은 사용자에게 보이는 암호화 키 조회 identity다. 변경하면 기존 암호화 데이터 접근 이전이 필요하다. |
| macOS 파일 종류 설명 | `chrome/app/app-Info.plist:236`, `:264`; `chrome/tools/build/mac/infoplist_strings_util.cc:198` | 소비 확인 / 미연결. `Chromium Extension`·`Chromium Shortcut` 설명과 고정 번역 key를 별도로 연결한다. 공유 UTType·MIME·확장자는 호환성 유지 대상으로 분류한다. |
| macOS PWA 폴더 | `chrome/browser/web_applications/os_integration/mac/apps_folder_support.mm:40`, `:118`; `chrome/browser/shell_integration.cc:199` | 소비 확인 / 미연결. `Chromium Apps.localized`라는 저장 경로와 GRIT 표시명을 분리한다. 이미 생성된 폴더·PWA 바로가기의 갱신을 검증한다. |
| 외부 직접 실행 scheme | `build/apple/tweak_info_plist.py:287`; `chrome/browser/shell_integration_mac.mm:301`; `chrome/install_static/chromium_install_modes.h:53` | 소비 확인 / 미연결. macOS는 새 Bundle ID만 넣으면 plist 직접 실행 항목이 제거될 수 있다. 생성·런타임·Windows 등록의 scheme을 함께 연결한다. |
| Windows 설치·profile·AppID·ProgID | `chrome/install_static/chromium_install_modes.h:20–103`; `chrome/common/chrome_paths_win.cc:42`; `chrome/install_static/install_util.cc:503` | 소비 확인 / 미연결. 표시명, 설치/profile 경로, AppID·ProgID prefix·설명·COM GUID·sandbox identity를 구분한다. 이름 변경 시 안정된 ID는 유지한다. |
| Windows Elevation/Tracing 서비스 | `chrome/install_static/install_util.cc:357–385`; `chrome/installer/setup/install_worker.cc:125`, `:406` | 소비 확인 / 미연결. 현 Chromium은 서비스 영속 이름을 표시명에서 공백 제거로 생성한다. 표시명과 안정된 서비스 이름의 소비 경로를 먼저 분리해야 한다. |
| Windows 정책 domain | `components/policy/tools/generate_policy_source.py:29–31`, `:1208–1217`; `chrome/install_static/install_util.cc:517` | 소비 확인 / 미연결. install-mode와 독립된 `SOFTWARE\Policies\Chromium` 생성도 있다. 두 정책 경로에 같은 identity를 연결해야 한다. |
| GCM product category | `chrome/browser/gcm/gcm_product_util.cc:39` | 소비 확인 / 미연결. short name의 ASCII 영숫자를 추출해 category를 만들고 profile prefs에 저장한다. 한글 이름이나 새 profile·rename에서 값이 달라질 수 있다. 표시명과 안정된 서비스 identity를 분리하고 기존 prefs 이전·서버 소비를 검토한다. |
| macOS 서명·entitlement | `chrome/installer/mac/signing/build_props_config.py.in:28`, `:40`; `signing/parts.py:39–123`; `chrome/BUILD.gn:740` | 소비 확인 / 배포 미검증. Bundle suffix·실제 서명·privileged helper와 일치해야 한다. Google 전용 entitlement는 브랜드 치환 범위가 아니다. |
| updater·enterprise companion | `chrome/updater/branding.gni:10–45`; `chrome/updater/mac/BUILD.gn:126`; `chrome/enterprise_companion/branding.gni` | 소비 확인 / 미연결. 일반 `BRANDING`과 독립된 표시명·Bundle/AppID·서비스·mutex·팀 ID·서버를 설정한다. 기능 활성화 여부와 실제 서비스 준비를 확인한 뒤 연결한다. |

## 이미지·아이콘 적용 manifest

현재 PNG 원본에서 만들 수 있는 출력과 별도 디자인 원본이 필요한 출력을
구분한다. 각 플랫폼 생성기는 같은 manifest를 따르고, preview는 필수 원본의
존재·출력 대상·크기·활성 분기를 검사해야 한다. macOS와 Windows의 crop 규칙도
동일하게 정의한다. 현재 하나는 지정 crop 크기, 다른 하나는 원본 크기 이하로
clamp하므로 작은 원본에서 결과가 달라질 수 있다.

| 출력/사용처 | 소비 경로 | 상태와 원본 |
| --- | --- | --- |
| 앱 ICNS·Windows 주 아이콘·일부 PNG | `tools/overlay/install-brand-assets.sh`, `install-brand-assets.ps1`; `chrome/BUILD.gn:655`; `chrome/app/chrome_exe.rc:59` | 연결 확인. 현재 PNG 원본에서 생성한다. |
| macOS asset catalog | `chrome/BUILD.gn:660`; `chrome/app/app-Info.plist:152`; `chrome/app/theme/chromium/mac/Assets.xcassets`; `tools/mac/icons/compile_car.py:175` | 소비 확인 / 미연결. fallback PNG와 `Assets.car`를 갱신해야 한다. `.icon` 레이어·appearance 원본은 별도 필요하다. |
| 배율별 제품 로고·About 화면 | `chrome/app/theme/theme_resources.grd:178`, `:207`; `chrome/browser/resources/settings/about_page/about_page.html.ts:16`; `chrome/browser/ui/webui/current_channel_logo.cc:36` | 소비 확인 / 미연결. `default_100_percent`/`default_200_percent`의 16/32 등 실제 GRIT 출력 경로를 포함한다. PNG 생성 가능. |
| HTML/PDF·앱 목록·Incognito ICO | `chrome/app/chrome_exe.rc:75–79` | 소비 확인 / 미연결. 문서 badge 등 조합 규칙이 필요하다. icon resource 순서와 인덱스는 유지한다. Incognito 모양의 변경은 제품 결정과 구분한다. |
| Windows Start tile | `chrome/installer/setup/generate_visual_elements_manifest_work_item.cc:26–35`; `chrome/app/theme/chromium/win/tiles` | 소비 확인 / 미연결. `Logo.png`, `SmallLogo.png`와 배경색. PNG에서 생성 가능. |
| WebUI 제품 SVG·애니메이션 | `chrome/app/theme/chrome_unscaled_resources.grd:87–88`; `chrome/browser/ui/webui/default_browser/default_browser_modal_ui.cc:86`; `chrome/browser/ui/webui/intro/intro_ui.cc:223` | 소비 확인 / 별도 원본 필요. SVG mark·animation은 크기별 PNG와 별개다. 기본 브라우저 안내·Intro·Profile Picker·검색엔진 선택에도 사용된다. |
| 공통 WebUI dark SVG | `ui/webui/resources/images/BUILD.gn:63`; `chrome/browser/resources/extensions/manager.html.ts:37`; `chrome/browser/resources/contextual_tasks/top_toolbar_logo.html.ts:22` | 소비 확인 / 별도 원본 필요. `chrome_logo_dark.svg`는 제품 theme 밖의 직접 소비 경로다. |
| 네이티브 제품 벡터 | `components/vector_icons/BUILD.gn:17–18`; `chrome/browser/ui/startup/default_browser_prompt/default_browser_bubble_dialog.cc:31`; `chrome/browser/ui/pdf/infobar/pdf_infobar_controller.cc:169` | 소비 확인 / 별도 원본 필요. `product.icon`, `product_refresh.icon`은 SVG와 다른 Chromium vector icon 입력이다. |
| 결제 light/dark 워드마크 | `chrome/app/theme/theme_resources.grd:219–220`; `chrome/browser/ui/views/payments/payment_request_views_util.cc:98` | 소비 확인 / 별도 원본 필요. `product_logo_name_22.png`, `_white.png`와 배율별 출력. mark만 있는 앱 PNG로 텍스트 워드마크가 생성되지 않는다. |
| macOS PWA 폴더 overlay | `chrome/app/theme/chrome_unscaled_resources.grd:140–143`; `chrome/browser/web_applications/os_integration/mac/icon_utils.mm:353` | 소비 확인 / 미연결. 폴더 바탕과 제품 overlay 합성. 일반 앱 아이콘과 별도 출력이다. |

버전 페이지의 `components/resources/version_ui_scaled_resources.grdp`에도 별도
제품 이미지가 있다. 현재 `chrome/browser/ui/webui/version/version_ui.cc:120`의
해당 resource 연결은 Android 분기다. desktop 활성 사용처와 혼동하지 않고
지원 플랫폼별 manifest에서 관리한다.

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
