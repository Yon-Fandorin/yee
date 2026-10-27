# 제품 브랜딩

제품에 표시할 이름과 로고를 설정하고, 적용할 곳과 검토 상태를 관리한다.
브랜드를 바꿀 때는 [`brand.json`](brand.json)을 수정한다.

## 파일의 역할

| 파일 | 역할 |
| --- | --- |
| `brand.json` | 표시 이름, 내부 URL 접두어, 이름 확정 여부, SVG 원본·배색·생성 자산 경로 |
| `identity.json` | 제품 ID·프로필 이름·Windows 등록·Mac 설치판 Bundle ID와 키체인 식별자 |
| `VERSION` | 설치·업데이트 순서에 사용하는 제품의 네 자리 배포 버전 |
| `strings.json` | 제품 문구·번역 생성 범위와 보존 규칙 |
| `surfaces.json` | 브랜딩 대상의 소스 위치, 처리 상태, 보존 이유와 테스트 항목 |

`provisional`은 이름이 아직 확정되지 않았는지 표시한다.
`identity.json`은 표시 이름을 바꿀 때 수정하지 않는다. 식별자 변경은 기존 설치·사용자
데이터 접근에 영향을 주므로 별도의 이전 작업이 필요하다. Windows와 Mac 설치판은
독립 Yee 프로필을 사용하고 Chromium 데이터를 자동 이전하지 않는다.
Mac 개발 번들의 기존 키체인은 유지하며, 패키징한 설치판에서만 Yee 식별자를 사용한다.
`VERSION`은 `product_version.py`가 공통 Chromium 빌드 입력 `chrome/VERSION`에 적용한다.
Yee 변경을 배포할 때도 이전 배포보다 큰 버전으로 올린다. 앞 세 자리는 checkout 원본의
Chromium major/minor/build와 같아야 하고 마지막 자리는 엔진 patch 이상이어야 한다.
엔진을 갱신하면 이 파일도 함께 검토한다. 설치 파일·실행 파일·버전 폴더가 같은 버전을
사용하며 `release.json`은 엔진 원본 버전도 `chromium_version`으로 별도 기록한다.
`short_name`을 생략하면 `name`을 사용한다.
내부 URL 접두어는 기본적으로 `short_name`의 소문자 값이다. 이름을 바꾸고
빌드하면 접두어도 바뀐다. URL scheme으로 쓸 수 없는 이름은 선택 필드
`internal_url_scheme`에 소문자 영문 접두어를 지정한다.
로고 원본은 [`assets/brand/`](../assets/brand/README.md)에 둔다.
현재 제품 로고는 02 이음매듭이며 먹색·청록 배색을 사용한다.
`logo_palette`의 기본·어두운 배경용 두 색과 아이콘 배경을 함께 관리한다.
macOS 빌드 진입점은 이름·SVG·배색·생성기 변경을 감지해 자산을 다시 만든다.
Windows에서는 저장소의 같은 로고 묶음을 검증하고 적용한다.
문구·번역 생성은 빌드에 사용하는 Python으로 수행한다. `strings_policy`로 정책을 연결한다.

## 점검 목록의 상태

| 상태 | 의미 |
| --- | --- |
| `managed` | 코드가 브랜딩 설정을 읽도록 연결됨 |
| `pending` | 설정과 연결할 작업이 남아 있음 |
| `preserved` | 호환성 등 기록된 이유로 기존 값을 유지 |
| `not_verified` | 해당 테스트를 아직 수행하지 않음 |

`managed`여도 생성 파일에 반영됐는지, 실제 앱에 표시되는지는 따로 확인해야 한다.
이 목록은 알려진 대상을 관리하므로 모든 브랜딩 지점이 빠짐없이 수집됐다는 뜻은 아니다.

## 변경 전 확인

저장소 루트에서 실행한다. 아래 명령은 파일을 수정하거나 브라우저를 빌드하지 않는다.

```sh
./tools/overlay/install-branding.sh "$PWD/.local-build/chromium/src" --check
python3 tools/overlay/audit-branding.py "$PWD/.local-build/chromium/src"
```

[제품 브랜딩](../docs/product-branding.md)에 변경 절차,
[브랜딩 적용 범위](../docs/branding-coverage.md)에 남은 작업이 있다.
소스 디렉터리 이름, 코드 식별자, 프로토콜과 OS 식별자는 표시 이름과 별도로 관리한다.
