# 빌드 설정

빌드와 Chromium 코드 적용에 사용하는 설정 파일을 둔다.

- [`args.gn`](args.gn): 디버그 정보를 줄인 Chromium 빌드 설정
- [`overlay.json`](overlay.json): 패치 순서, 소스 복사 경로, 생성 자산과 새 연결 파일 목록

Chromium 소스, 캐시와 빌드 결과는 Git에서 제외된 `.local-build/`에 생성한다.
표시 이름과 로고는 [`branding/brand.json`](../branding/brand.json)에서 설정한다.

빌드 명령은 [개발 도구](../tools/dev/README.md), 소스·패치 적용은
[오버레이 도구](../tools/overlay/README.md)를 참고한다.
새 소스 디렉터리를 추가할 때는 [프로젝트 구조](../docs/project-structure.md)의 등록 절차를 따른다.
