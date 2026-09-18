# 제품 브랜딩

제품에 표시할 이름과 로고를 설정하고, 적용할 곳과 검토 상태를 관리한다.
브랜드를 바꿀 때는 [`brand.json`](brand.json)을 수정한다.

## 파일의 역할

| 파일 | 역할 |
| --- | --- |
| `brand.json` | 표시 이름, 이름 확정 여부, 로고 경로와 자르기 크기 |
| `surfaces.json` | 브랜딩 대상의 소스 위치, 처리 상태, 보존 이유와 테스트 항목 |

`provisional`은 이름이 아직 확정되지 않았는지 표시한다.
`short_name`을 생략하면 `name`을 사용한다.
로고 원본은 [`assets/brand/`](../assets/brand/README.md)에 둔다.

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
