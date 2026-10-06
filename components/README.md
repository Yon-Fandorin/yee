# 공용 기능

브라우저와 렌더러가 함께 사용하는 코드를 둔다.
현재 `content_blocking/`에 차단 엔진, 설정, 필터 데이터와 라이선스 안내 생성 코드가 있다.
`branding/internal_urls.*`는 브랜드 설정에서 생성한 내부 URL 접두어와
Chromium WebUI의 실제 주소 사이를 변환한다.
`tasks/worker_owned.h`는 `base::SequenceBound`의 객체를 factory로 작업 sequence에서
생성·소유·실행하는 공통 어댑터다. 전용 스레드 선택과 작업 priority·shutdown 정책은
consumer가 정하며, DOM이나 제품별 동기 대기 규칙은 공통 어댑터에 포함하지 않는다.

| 파일·디렉터리 | 역할 |
| --- | --- |
| `engine.*`, `settings.*` | 엔진 호출과 사이트별 예외 설정 |
| `rust/` | 직접 작성한 Rust 연결 코드와 버전을 고정한 `Cargo.lock` |
| `data/` | 필터·리소스와 출처·해시·라이선스 기록 |
| `embed_rules.py` | 필터 데이터를 빌드에 포함 |
| `package_notices.py` | 라이선스 안내와 외부 원본 소스 묶음 생성 |
| `*_unittest.cc` | 엔진·설정 테스트 |

외부 Rust 원본은 [`third_party/`](../third_party/README.md)에 보관한다.
네트워크 요청과 페이지 처리의 연결은
[`browser/`](../browser/README.md), [`renderer/`](../renderer/README.md)가 담당한다.

`content_blocking/`은 Chromium의 `components/yee_content_blocking/`,
`tasks/`는 `components/yee_tasks/`, `branding/`은 `components/yee_branding/`에 복사된다.
경로는 [`build/overlay.json`](../build/overlay.json)에서 관리하며 이 README는 복사하지 않는다.
지원 기능과 남은 테스트는 [콘텐츠 차단 checkpoint](../docs/content-blocking-checkpoint.md)에 있다.
