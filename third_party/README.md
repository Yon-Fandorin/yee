# 외부 라이브러리

외부 프로젝트에서 가져온 소스와 라이선스를 보관한다.
`yee_adblock/`에는 버전을 고정한 `adblock-rust`와 관련 Rust 패키지가 있다.

## 관리 파일

| 파일 | 역할 |
| --- | --- |
| `yee_adblock/manifest.json` | 패키지 버전·출처·라이선스와 원본 파일의 해시 |
| `yee_adblock/sources.gni` | 배포 소스 묶음에 포함할 빌드 입력 목록 |
| 각 패키지의 `BUILD.gn` | Chromium에서 사용할 빌드 대상 정의 |

해시는 파일이 원본과 같은지 확인하는 검증값이다.
원본 소스·README·라이선스는 이 값으로 검사하므로 그대로 보존한다.
한글 안내와 제품 규칙은 직접 관리하는 문서와 코드에 작성한다.

## 갱신과 적용

갱신 도구는 [`vendor-yee-adblock.py`](../tools/overlay/vendor-yee-adblock.py)다.
Cargo.lock, 배포된 패키지 파일과 추출한 소스를 함께 확인한다.
원본이 변경되면 라이선스 안내와 소스 묶음을 만드는 과정에서 거부한다.
직접 작성한 엔진 연결은 [`components/`](../components/README.md)에 있다.

[`build/overlay.json`](../build/overlay.json)은 `yee_adblock/`을 Chromium의
`third_party/rust/yee_adblock/`에 복사하도록 지정한다.
이 README는 복사하지 않으며 패키지 안의 원본 README는 함께 복사한다.
자세한 내용은 [콘텐츠 차단 checkpoint](../docs/content-blocking-checkpoint.md)를 참고한다.
