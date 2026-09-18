# 로고 원본

제품 아이콘을 만드는 로고 이미지를 둔다.
Windows용 ICO·PNG와 macOS용 ICNS·PNG를 생성할 때 사용한다.

## 로고 선택

[`branding/brand.json`](../../branding/brand.json)의 `logo_source`로 원본 경로,
`logo_crop_size`로 자르기 크기를 지정한다. 로고를 바꿀 때는 이 설정을 수정한다.
아이콘 생성 도구도 같은 설정을 읽는다.

아이콘 적용 방법은 [오버레이 도구](../../tools/overlay/README.md)를 참고한다.
해상도별 이미지, macOS 자산 묶음과 벡터 이미지 등 남은 작업은
[브랜딩 적용 범위](../../docs/branding-coverage.md)에 있다.

다른 PNG는 Git에서 제외된 로컬 디자인 실험일 수 있다.
실제로 사용하는 로고는 설정과 사용하는 코드에서 확인한다.
