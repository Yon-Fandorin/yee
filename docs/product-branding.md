# Product branding configuration

제품의 작업 이름은 [`branding/brand.json`](../branding/brand.json)
한곳에서 관리한다. 현재 `Yee`는 임시 기본값이며 `provisional: true`로
브랜드명 미확정 상태를 표시한다. 이름 확정 전후 모두 같은 설정을 사용한다.

Brave의 제품명·번역·아이콘·설치 식별자·내부 URL 연결 분석과 적용 순서는
[`brave-branding-analysis.md`](brave-branding-analysis.md)에 정리했다.

```json
{
  "name": "New Brand",
  "provisional": true,
  "logo_source": "assets/brand/yee-logo-v8c-dino-nubs.png",
  "logo_crop_size": 820
}
```

`name`을 바꾸면 앱·Framework·Helper 이름, 설치 프로그램 이름, GRIT의
`IDS_PRODUCT_NAME`·`IDS_SHORT_PRODUCT_NAME`·macOS `IDS_APP_MENU_PRODUCT_NAME`,
Yee-owned Agent dialog·활동 포인터·탭 접근성 안내와 Sidebar Footer,
CLI 안내의 제품명이 같은 설정을 따른다.
네이티브 UI는 `yee::branding::ProductName()`을 사용하고, 이름 없는 공통 안내는
브라우저를 가리키는 중립적인 문구를 쓴다.
짧은 이름이 따로 필요하면 선택 필드 `short_name`을 추가한다. 생략하면 `name`이다.
`logo_source`는 저장소 내부의 상대 경로이며, `logo_crop_size`는 기존 로고의
가운데 crop 크기다. 새 로고를 넣으면 그 이미지에 맞게 지정한다.
`provisional`은 이름 확정 여부만 나타낸다. OS 등록이나 저장소 이전을
실행하지 않는다. 이름에는 공백·한글·작은따옴표를 사용할 수 있다. 경로에
쓸 수 없는 문자, `$`, `@PRODUCT_FULLNAME@` 같은 생성기 치환 구문은 거부한다.

## 적용과 빌드의 경계

설정을 읽는 명령과 적용을 미리 보는 명령은 Chromium을 빌드하지 않는다.

```sh
python3 tools/overlay/brand_config.py show
./tools/overlay/install-branding.sh "$PWD/.local-build/chromium/src" --check
```

빌드 없이 제품명 입력만 적용하려면 `--check`를 빼고 실행한다. 로고 입력은
`install-brand-assets.sh` 또는 Windows의 `install-brand-assets.ps1`로 별도 생성한다.
이 스크립트들은 앱을 빌드하지 않는다. macOS 로고 생성은 기존 방식대로 작은
ICNS packer를 컴파일한다.

Windows에서는 아래 명령이 같은 설정을 사용한다. 설정 읽기와 제품명 적용에
별도 Python 설치가 필요하지 않다.

```powershell
.\tools\overlay\install-branding.ps1 -ChromiumSrc F:\chromium\src -CheckOnly
```

`apply.*`·`configure.*`는 제품명 입력과 로고를 적용한다. `build.*`·`build-ui.*`는
제품명 입력을 먼저 갱신하므로 이름 변경 후 기존 output을 재사용할 수 있다.
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
네 `PRODUCT_*` 필드와 생성된 두 `.grdp`만 관리하며, 같은 설정을 재적용하면
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

이번 연결은 핵심 제품명 ID와 Yee-owned UI 이름을 대상으로 한다. Chromium
문자열에 제품명이 문장 일부로 직접 들어간 설정·안내·오류 문구와 `.xtb` 번역은
별도 전환 작업이다. 영어 원문을 바꾸면 GRIT translation ID도 달라질 수 있다.
변경된 원문과 번역을 함께 이전해야 한다. 생성된 `.grdp`의 제품명 ID는
원래와 동일하게 `translateable="false"`이므로 번역 파일 변경이 필요하지 않다.

[전체 적용 범위와 후속 기준](branding-coverage.md)에 OS 안내, 번역,
보조 앱, 추가 아이콘, URL 저장·복사 및 영속 identity의 미연결 경로를 남겼다.
자동 감사는 [branding-surfaces.json](../branding/surfaces.json)에
등록한 소비 경로 외에도 GRD의 재귀 part와 네이티브 UI 고정 이름 후보를 찾는다.
이 검사는 후보 목록이며 모든 문구를 전역 치환하라는 지시가 아니다.

## 브랜드 내부 URL 제안

`chrome://`를 사용자에게 브랜드 주소로 보여주는 방향을 권한다. Edge는
[`edge://settings/help`](https://learn.microsoft.com/en-us/troubleshoot/microsoft-edge/security/troubleshoot-sign-in-issues)를,
Vivaldi는 [`vivaldi://settings`](https://help.vivaldi.com/de/desktop-de/werkzeuge/einstellungen/)를
공식 안내에 사용한다. 이번 설정 작업은 URL 동작을 변경하지 않는다.

현재 Chromium 코드 구조에서는 브랜드 scheme의 입력을 기존 WebUI URL로
정규화하고, navigation entry의 virtual URL에 브랜드 주소를 보존하는 접근을
먼저 검토할 수 있다.

Brave의 현재 소스는 브랜드 scheme을 Chrome scheme으로 정규화하고,
LocationBarModelDelegate의 주소 포매팅과 Omnibox 복사 처리를 별도로 연결한다.
virtual URL 보존과 이 방식을 함께 비교한다. 구체적인 소스 근거는
[Brave 내부 URL 분석](brave-branding-analysis.md#5-brave는-입력정규화표시복사를-연결한다)을 참고한다.

실제 구현 전 아래 경로를 한 checkpoint로 다룬다.

- 입력·자동완성·메뉴·내부 링크에서 브랜드 scheme으로 이동
- 브랜드 scheme 등록과 legacy `chrome://` 입력의 정규화
- 주소 표시·편집·복사·북마크·뒤로/앞으로·reload·session restore의 일관성
- WebUI controller·origin·CSP·renderer 권한 검사가 기존 보안 경계를 유지하는지
- 기존 `chrome://` 주소와 저장된 bookmark/session의 호환성
- Yee split pane의 읽기 전용 주소 표시와 native Omnibox가 같은 주소를 사용하는지

scheme은 표시 이름에서 자동으로 만들지 않고 별도의 안정된 설정으로 둔다.
이름에 공백이나 비 ASCII 문자가 있을 수 있고, 저장된 내부 주소의 이전은
일반 제품명 변경과 다른 문제다. scheme 설정은 실제 URL 연결을 구현할 때 추가한다.

## 빌드 없는 검증

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/dev/test-brand-config.py
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
