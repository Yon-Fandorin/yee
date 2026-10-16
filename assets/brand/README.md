# 브랜드 로고 자산

제품의 기준 원본은 [`yee-logo.svg`](yee-logo.svg)이다.
선택한 02 이음매듭의 경로를 그대로 사용하며, `data-brand-colour`로 두 가닥
묶음의 `primary`·`secondary` 색 역할을 지정한다.
실제 색과 표시 이름은 [`branding/brand.json`](../../branding/brand.json)에서 읽는다.

[`generated/manifest.json`](generated/manifest.json)은 SVG·배색·제품명·생성기의
해시와 Chromium에 적용할 파일 목록을 기록한다. ICNS, macOS의 `.icon`·
asset catalog·`Assets.car`, Windows ICO·문서 아이콘·tile, 배율별 PNG,
WebUI SVG, 네이티브 벡터, 워드마크와 macOS 앱 폴더 표시를 함께 생성한다.
새 탭·버전 페이지의 별도 파비콘, 주소창·알림의 최신/이전 벡터,
첫 실행 안내 삽화, 맞춤설정 미리보기의 내장 표식, WebUI 도구 모음의 inline SVG와
Windows 설치·PWA 실행 아이콘도 같은 원본을 사용한다.
새 탭 파비콘은 네이티브 색 보정이 매듭의 투명도를 사용하므로 배경판 없이
생성한다. 앱·OS 아이콘의 배경판은 유지한다.
이미 같은 파일이면 다시 복사하지 않는다.

`developed/`에는 발전안 01 후보와 채택한 02의 비교 원본이 있다.
`final/`은 발전 전 선택 시안이다. 이 비교 파일들은 제품에 직접 설치하지 않는다.
배색 05인 주홍·청록도 `developed/manifest.json`에 후보로 보관한다.

## 교체와 생성

1. `brand.json`의 이름, `logo_source`, `logo_palette`를 변경한다.
2. 다른 SVG를 쓸 때는 `viewBox="0 0 128 128"`과 절대 M/L/C/Z로 된 채운 경로,
   경로에 합쳐진 transform, `primary`·`secondary` 색 역할을 사용한다.
3. macOS에서 생성한다. 필요한 렌더러는
   [`requirements-brand-assets.txt`](../../tools/overlay/requirements-brand-assets.txt)에 있다.

```sh
python3 tools/overlay/generate-brand-assets.py "$PWD/.local-build/chromium/src"
./tools/overlay/install-brand-assets.sh "$PWD/.local-build/chromium/src" --check
python3 tools/overlay/audit_logo_assets.py "$PWD/.local-build/chromium/src"
```

macOS의 적용·빌드 스크립트도 오래된 입력을 감지해 다시 생성한다.
Windows의 로고 자산 설치에는 이미지 렌더러가 필요 없으며 동일한 생성 묶음을 복사한다.
Windows에서 설정을 바꿨다면 macOS에서 묶음을 다시 생성한 뒤 적용한다.
소스나 해시가 맞지 않는 자산은 빌드에 넣지 않는다.
독립 감사는 Chromium의 GRIT 연결과 중복 벡터 파일을 다시 읽어,
묶음에서 빠지거나 적용되지 않은 제품 표식을 실패로 보고한다.

자세한 적용 방법은 [오버레이 도구](../../tools/overlay/README.md)를 참고한다.
