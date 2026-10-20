# Product branding configuration

제품의 작업 이름은 [`branding/brand.json`](../branding/brand.json)
한곳에서 관리한다. 현재 `Yee`는 임시 기본값이며 `provisional: true`로
브랜드명 미확정 상태를 표시한다. 이름 확정 전후 모두 같은 설정을 사용한다.

Brave의 제품명·번역·아이콘·설치 식별자·내부 URL 연결 분석과 적용 순서는
[`brave-branding-analysis.md`](brave-branding-analysis.md)에 정리했다.

```json
{
  "name": "New Brand",
  "short_name": "NewBrand",
  "provisional": true,
  "logo_source": "assets/brand/yee-logo.svg",
  "logo_palette": {
    "primary": "#242824",
    "secondary": "#438578",
    "dark_primary": "#F5F6F2",
    "dark_secondary": "#8BC4B3",
    "background": "#F7F9F5",
    "dark_background": "#202724"
  },
  "logo_assets_manifest": "assets/brand/generated/manifest.json",
  "strings_policy": "branding/strings.json"
}
```

`name`을 바꾸면 앱·Framework·Helper 이름, 설치 프로그램 이름, GRIT의
`IDS_PRODUCT_NAME`·`IDS_SHORT_PRODUCT_NAME`·macOS `IDS_APP_MENU_PRODUCT_NAME`,
Yee-owned Agent dialog·활동 포인터·탭 접근성 안내와 Sidebar Footer,
CLI 안내의 제품명이 같은 설정을 따른다.
네이티브 UI는 `yee::branding::ProductName()`을 사용하고, 이름 없는 공통 안내는
브라우저를 가리키는 중립적인 문구를 쓴다.
짧은 이름이 따로 필요하면 선택 필드 `short_name`을 추가한다. 생략하면 `name`이다.
내부 URL 접두어는 기본적으로 `short_name`의 소문자 값이다. 예를 들어 `Yee`는
`yee://settings`, `NewBrand`는 `newbrand://settings`를 사용한다. 이름 변경 뒤
빌드하면 URL 접두어도 같은 설정을 따른다. 공백이나 한글이 포함된 이름처럼
URL scheme으로 사용할 수 없는 이름은 `internal_url_scheme`에 영문 접두어를
명시한다. 이 값도 같은 브랜드 설정에서 관리하며 코드에 고정하지 않는다.
접두어는 소문자 ASCII 문자로 시작하고 영문·숫자·`+`·`.`·`-`만 포함할 수 있다.
`http`, `chrome`, `file` 등 기존 브라우저 프로토콜과 충돌하는 값은 거부한다.
`logo_source`는 저장소 내부의 기준 SVG 경로다. `logo_palette`는 두 색의
기본·어두운 배경 값과 앱 아이콘 배경을 지정한다. 경로의 `data-brand-colour`는
`primary` 또는 `secondary`를 사용한다. 단색 원본은 기본 `primary`를 따른다.
`logo_assets_manifest`는 같은 원본·배색·제품명에서 생성한 desktop 자산 묶음이다.
이름 변경은 워드마크도 갱신한다. SVG·배색·생성기 변경도 재생성 입력으로 추적한다.
내장 표식이 사용하는 Chromium 원본의 `HEAD` blob도 추적하며, 원본이 바뀌면 macOS에서 재생성하고 Windows와 `--check`는 오래된 묶음을 거부한다.
네이티브 벡터 생성기는 내용이 같은 선언·정의·검사 파일의 수정 시간을 유지한다.
로고 경로만 바꿨을 때 같은 선언 헤더 때문에 전체 native UI를 다시 컴파일하지 않는다.
`provisional`은 이름 확정 여부만 나타낸다. OS 등록이나 저장소 이전을
실행하지 않는다. 이름에는 공백·한글·작은따옴표를 사용할 수 있다. 경로에
쓸 수 없는 문자, `$`, `@PRODUCT_FULLNAME@` 같은 생성기 치환 구문은 거부한다.

## 적용과 빌드의 경계

설정을 읽는 명령과 적용을 미리 보는 명령은 Chromium을 빌드하지 않는다.

```sh
python3 tools/overlay/brand_config.py show
./tools/overlay/install-branding.sh "$PWD/.local-build/chromium/src" --check
```

빌드 없이 제품명 입력만 적용하려면 `--check`를 빼고 실행한다.
로고는 `generate-brand-assets.py`가 macOS에서 생성하며,
`install-brand-assets.sh`와 `install-brand-assets.ps1`이 같은 묶음을 검증하고 적용한다.
macOS 적용은 오래된 입력을 먼저 재생성한다. `--check`는 재생성하지 않는다.
생성에는 CairoSVG·Pillow·Xcode의 asset compiler와 작은 ICNS packer를 사용한다.
Windows 적용에는 이미지 생성 도구나 Python이 필요 없다.
이 스크립트들은 앱을 빌드하지 않는다.

Windows에서는 아래 명령이 같은 설정을 사용한다. 설정 읽기와 제품명 적용에
별도 Python 설치가 필요하지 않다.

```powershell
.\tools\overlay\install-branding.ps1 -ChromiumSrc F:\chromium\src -CheckOnly
```

`apply.*`·`configure.*`와 `build.*`·`build-ui.*`는 제품명 입력과 로고 묶음을
함께 적용하므로 이름 변경 후 기존 output을 재사용할 수 있다.
실행 스크립트도 새 이름의 앱 경로를 찾는다. 정상 종료 전에는 같은 build output의
동일 Bundle ID를 가진 기존 `.app`도 찾아 정확한 실행 경로로 종료를 요청한다.
삭제된 번들이나 다른 output에 있는 프로세스까지 검색하는 기능은 아니다.
이미 빌드된 옛 이름의 앱을
새 이름의 앱으로 간주하지 않는다. 실제 앱에 반영하려면 이후 전체 앱 링크가
필요하다. 이름 변경만으로 새 output directory를 만들 필요는 없다.

`0002-brand-yee-application.patch`는 `OVERLAY_BRANDING_MANAGED=1` 표식과 GRIT
part 연결, 작은따옴표가 포함된 제품명의 macOS 서명 Python template 연결을
둔다. Windows 설치 프로그램의 별도 RC 생성기도 선언된 제품명 part를 읽고,
그 part를 GN action 입력으로 추적한다. 일반 Windows 버전 RC는 UTF-16으로,
공통 버전 메타데이터는 UTF-8로 생성하며 crash client의 제품명 변환도 UTF-8을
사용한다. Linux KV 파서는 값에 들어간 `=`를 보존한다. 이름 자체는 넣지 않는다.
`BRANDING`은 Chromium의 실제 `version.py`가
모든 줄을 `KEY=VALUE`로 읽으므로 주석을 표식으로 쓰지 않는다.
설치 스크립트는 `BRANDING`의
네 제품명 `PRODUCT_*` 필드, `PRODUCT_INTERNAL_URL_SCHEME`과 생성된 두 `.grdp`를
관리하며, 같은 설정을 재적용하면
파일의 수정 시간을 유지한다. 과거 고정 `Yee` 패치가 적용된 체크아웃에서도
새 `0002` 연결을 적용할 수 있다. 이전 overlay의 주석 표식도 이전한다.
`regenerate-shell-patch.py`는 `0002`의 모든 소유 경로를 `0001`에서 제외해
새 체크아웃의 순차 적용을 유지한다.

## 표시 이름과 고정 식별자

표시 이름 변경은 `yee::` namespace, `yee-ui` source directory, `YEE_*` 개발
플래그, MCP/agent protocol schema, copyright를 바꾸지 않는다. 기존 macOS
Bundle ID·Keychain 이름·기본 profile 경로와 Windows install-mode identity도
이 설정의 범위에 넣지 않았다. 독립 설치 식별자로 전환할 때 데이터 이전을
함께 설계한다.

제품 문구와 번역은 `strings_policy`가 지정하는 정책을 따른다.
`brand_strings.py`는 Chromium `HEAD`의 GRD·GRDP·XTB를 읽어 여섯 문자열 계열의
제품 참조를 같은 이름으로 생성한다. 원래 GRD 파일 이름과 리소스 ID 순서는 유지하고,
GRDP·XTB 사본은 `chrome/app/yee_branding_strings/`에 둔다. 바뀐 원문에는 GRIT의
메시지 ID를 다시 계산해 기존 언어·gender 번역을 연결한다. placeholder, 원래 번역 파일,
외부 제품명·URL·저작권은 보존하며 라이선스의 앱 이름과 프로젝트 링크를 구분한다.
입력의 로컬 수정은 적용 전에 거부한다. 재적용은 내용이 같은 파일의 시간을 유지한다.

`yee_branding_strings.gni`는 번역 생성 여부를 GN에 전달한다. Windows 설치 문자열도
활성 정책에서는 생성된 XTB를 읽고, 정책이 없는 구성에서는 원래 경로를 사용한다.
이름을 바꾸면 항상 동일한 upstream에서 다시 생성하므로 이전 브랜드명이 남지 않는다.
PDF 뷰어의 표시명은 제품 메타데이터를 JSON으로 escape해 읽는다. macOS 문서 설명과
앱 폴더 표시명도 같은 설정을 따르며 기존 lookup key·저장 경로는 유지한다.

기본 브라우저 InfoBar는 Yee 소유 영어·한국어 GRIT 템플릿에 현재 제품명을
매개변수로 넣고 다른 언어는 영어로 fallback한다. 아이콘은 같은 SVG 설정에서 만든
투명한 두 색 로고를 `ProductLogoIcon()`으로 공유한다. PDF·Dock/작업표시줄 고정·
세션 복원·Windows 시작 시 실행·macOS 업데이터 권한 승격 안내도 같은 경로를 사용한다.
PDF와 고정 안내는 기존 delegate와 중앙 `InfoBarSpec` 경로를 모두 연결한다.
실제 배너 배경에 따라 `IDR_PRODUCT_LOGO`·`IDR_PRODUCT_LOGO_WHITE`를 선택한다.
플랫폼·기능 플래그별 실제 실행 검수는 적용 범위 문서에서 별도로 구분한다.
쿠키·권한·화면 공유·확장 프로그램·보안 안내의 기능 아이콘은 의미를 유지한다.
핵심 제품명 세 ID는 계속 `translateable="false"`다.

[전체 적용 범위와 후속 기준](branding-coverage.md)에 OS 안내, 번역,
보조 앱, 추가 아이콘, URL 저장·복사 및 영속 identity의 미연결 경로를 남겼다.
자동 감사는 [branding-surfaces.json](../branding/surfaces.json)에
등록한 소비 경로 외에도 GRD의 재귀 part와 네이티브 UI 고정 이름 후보를 찾는다.
이 검사는 후보 목록이며 모든 문구를 전역 치환하라는 지시가 아니다.

## 브랜드 내부 URL

`chrome://`를 사용자에게 브랜드 주소로 보여주는 방향을 권한다. Edge는
[`edge://settings/help`](https://learn.microsoft.com/en-us/troubleshoot/microsoft-edge/security/troubleshoot-sign-in-issues)를,
Vivaldi는 [`vivaldi://settings`](https://help.vivaldi.com/de/desktop-de/werkzeuge/einstellungen/)를
공식 안내에 사용한다. Yee는 현재 브랜드 설정에서 생성한 URL 접두어를 사용한다.

`components/branding/internal_urls.*`가 입력 주소와 표시 주소를 변환한다.
Chromium에는 scheme 등록·Omnibox 입력 분류·navigation 정규화·주소 포매팅·복사·
드래그·북마크와 세션 저장 연결을 둔다. 실제 WebUI 주소와 origin은 `chrome://`
형식을 사용하며, 사용자에게 보여주고 복사하는 주소는 현재 브랜드를 따른다.
기존 Chromium 설정은 `chrome://settings`와 하위 경로를 그대로 표시·복사한다.
브라우저에서 시작한 이동은 navigation 이전에 정규화하고, renderer에서 시작한
이동에는 기존 WebUI 접근 검사를 적용한다. 북마크와 세션의 저장 주소도 정규화해
다음 빌드에서 브랜드가 바뀌어도 같은 내부 페이지를 복원한다.
브라우저 실행 경로로 전달된 외부 주소는 먼저 기존 Chromium 주소로
정규화해 같은 launch 검사를 적용한다. 주소창에서 열 수 있는 내부 페이지라도
외부 실행 인자로 열 수 있는지는 Chromium의 별도 허용 범위에 따른다.

Brave의 현재 소스는 브랜드 scheme을 Chrome scheme으로 정규화하고,
LocationBarModelDelegate의 주소 포매팅과 Omnibox 복사 처리를 별도로 연결한다.
구체적인 소스 근거는
[Brave 내부 URL 분석](brave-branding-analysis.md#5-brave는-입력정규화표시복사를-연결한다)을 참고한다.

추가 내부 페이지와 표시 경로를 연결할 때 아래 기준을 유지한다.

- 입력·자동완성·메뉴·내부 링크에서 브랜드 scheme으로 이동
- 브랜드 scheme 등록과 legacy `chrome://` 입력의 정규화
- 주소 표시·편집·복사·북마크·뒤로/앞으로·reload·session restore의 일관성
- WebUI controller·origin·CSP·renderer 권한 검사가 기존 보안 경계를 유지하는지
- 기존 `chrome://` 주소와 저장된 bookmark/session의 호환성
- Yee split pane의 읽기 전용 주소 표시와 native Omnibox가 같은 주소를 사용하는지

기본 접두어는 브랜드 이름을 따르고, `internal_url_scheme`은 이름이 URL 문법에
맞지 않거나 별도 접두어가 필요할 때 사용한다. `chrome-untrusted://`,
`chrome-search://`와 리소스 import 주소는 기존 권한과 origin을 유지한다.
WebUI 본문의 고정 링크 문구와 북마크 편집 화면 등 모든 표시 문자열의 브랜드
전환은 별도 소비 경로를 확인한 뒤 연결한다.

`브랜드://settings`는 Yee 설정으로 연결한다. 기존 Chromium 설정은
`chrome://settings`로 접근하며 하위 경로와 주소창 표시·복사를 유지한다.
Yee 설정의 고급 설정·세부 설정 링크도 이 주소를 사용한다. 화면 설계는
[Yee 설정](settings.md)에 둔다.

## 빌드 없는 검증

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-brand-config.py
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-brand-assets.py
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-brand-strings.py
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-branding-overlay.py
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-branding-audit.py
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-browser-bundle-executables.py
./tools/dev/test-run-preflight.sh
python3 tools/overlay/audit-branding.py "$PWD/.local-build/chromium/src"
python3 tools/overlay/audit-branding.py "$PWD/.local-build/chromium/src" --require-complete
```

설정·패치 검증은 임시 체크아웃에서 이름 변경·XML escaping·반복 적용·preview·
고정 식별자 보존·패치 분리·이전 패치 이전 및 실제 Chromium 버전/Python
생성 입력을 검사한다. 번들 검증은 임시 plist에서 이전 이름 탐색 범위를 검사한다.
Windows RC는 실제 Chromium 생성기로 임시 EN_US·KO 출력과 rename을 검사한다.
이것은 Windows 설치 프로그램을 빌드하거나 Windows에서 실행한 검증은 아니다.
어느 명령도 브라우저를 빌드하거나 실행하지 않는다. `--require-complete`는
미연결·미검토 후보, 미적용 입력, 실제 앱 미검증이나 임시 브랜드명이 남으면
실패한다. 현재 상태에서는 실패하는 것이 정상이다.
