# 빌드 설정

빌드와 Chromium 코드 적용에 사용하는 설정 파일을 둔다.

- [`args.gn`](args.gn): 디버그 정보를 줄인 Chromium 빌드 설정
- [`chromium-version.json`](chromium-version.json): Windows·macOS가 함께 쓰는 Stable 원본 버전·커밋
- [`overlay.json`](overlay.json): 패치 순서, 소스 복사 경로, 생성 자산과 새 연결 파일 목록

Chromium 소스, 캐시와 빌드 결과는 Git에서 제외된 `.local-build/`에 생성한다.
표시 이름과 로고는 [`branding/brand.json`](../branding/brand.json)에서 설정한다.

빌드 명령은 [개발 도구](../tools/dev/README.md), 소스·패치 적용은
[오버레이 도구](../tools/overlay/README.md)를 참고한다.
새 소스 디렉터리를 추가할 때는 [프로젝트 구조](../docs/project-structure.md)의 등록 절차를 따른다.

진행 중인 157 이식의 체크포인트와 Mac 빌드 시작·Windows 재개 명령은
[Stable 작업 인수인계](../docs/chromium-stable-handoff.md)에 정리했다.

## Stable 이식 후 전수 재검사

큰 버전 이식은 다음 항목을 별도로 확인한다. `layout all`은 레이아웃 전체 검사이며
제품 전체의 통과를 뜻하지 않는다. 각 실행 전 새 바이너리의 테스트 목록에서
필수 필터별 실제 사례가 존재하는지 확인하고 통과·실패·건너뜀 수를 기록한다.

| 범위 | 확인할 내용 |
| --- | --- |
| 원본·오버레이 | 공통 핀, 제품 버전, 세 패치의 역할·역방향 적용, 원본 공백 검사, 소유 소스·브랜드 자산 일치 |
| 빌드·연결 | 새 출력의 GN·전체 앱 빌드, 헤더 의존 검사, 필요한 native 검사 바이너리 |
| 화면 | Layout all, Header·Sidebar의 단위/상호작용 검사, RTL 125%, unified 수평 탭 |
| 프로젝트 | ProjectNavigationBrowserTest 전체, project_service_unittests, unified 조건부 사례 별도 실행 |
| 제품 기능 | Site Controls, 내부 페이지, 콘텐츠 차단, native Agent 계약 |
| CLI·MCP | macOS에서 tests/tooling 외 직접 실행하는 test-yee-browser 계열, 실제 새 앱 연결·관찰·입력·탭 전환·취소 |
| 미디어 | 새 실행 파일로 직접 MP4·MSE의 AAC/H.264 디코딩, 요청 영상의 다시보기와 현재 라이브 각각 확인 |
| 실제 앱 | 격리 프로필·새 프로세스의 탭/그룹/분할/맞춤설정/전체화면/복원, 밝고 어두운 테마, 적용 가능한 배율·접근성·모션 |

실행 결과에는 공통 원본 핀, 제품 버전, 출력 경로·빌드 설정, 실행 파일과 DLL 해시를
함께 남긴다. 이전 버전의 실행 증거를 새 버전 결과로 사용하지 않는다. 라이브 결과는
선택한 영상 ID, 실제 라이브 확인 시각, 재생 진행·프레임·오디오·플레이어 오류를 기록하며
종료된 방송의 다시보기와 구분한다.

Windows 검사는 `--test-launcher-preserve-desktop-windows`로 다른 창을 보존한다.
macOS 결과는 Mac에서 실제 실행할 때까지 미검증 상태로 둔다. 설치판·실사용 프로필을
자동 교체하지 않으며, 새 프로필 검사가 데이터 이전·복구 검사까지 대신하지 않는다.
CLI·MCP mailbox는 현재 [macOS 개발용 POSIX 구현](../docs/agent-browser-mcp-usage.md)이며
Windows transport는 비활성 상태다. Windows 결과에서 이 항목을 통과 처리하거나
이식을 위해 transport 지원 범위를 조용히 넓히지 않는다.

## 이식에서 유지할 연결 경계

이번 153→157 충돌에서는 Chromium의 UI 대상이 frame·layout·contents 단위로
나뉜 점과, 팝업 그림자의 그리기 책임이 native와 WebUI로 나뉜 점이 반복 수정의
원인이었다. 파일 이동만 따라가거나 과거의 큰 UI 의존을 그대로 붙이지 않는다.

- Chromium의 공개 헤더는 Yee의 `*_headers`나 작은 타입 대상만 참조한다.
  실제 `yee_ui`·`project_navigation` 구현은 `chrome/browser:browser`에서 조립한다.
  헤더 접근을 위해 무거운 구현이나 제한된 `Browser` visibility를 열지 않는다.
- 프로젝트와 Site Controls는 `BrowserWindowInterface`로 연결한다. 공유 제품명
  접근자는 `brand` leaf 대상에서 제공한다.
- 일반 탭·그룹은 Chromium의 공유 애니메이션 시계를 유지한다. Favorites의 문서화된
  420ms 재배치는 dock View가 소유한 전용 시계에 한정하고 제품 설정은 `favorites`가 소유한다.
- 패널의 현재 프레임 입력은 원본의 실제 전환 채널을 따른다. 제거된 네 채널을 복제하지
  않고 내용 진행률 하나와 native `RectValueBetween`으로 같은 edge 반올림을 사용한다.
- 생성된 브랜드 자산·문구와 소유 소스는 패치에서 제외한다. 패치 선언과 실제 파일
  헤더의 경로가 일치하는지 변경 전에 확인하고, 같은 자산에 생성기와 패치가 함께 쓰지 않는다.
- 팝업의 native 배경과 그림자는 renderer가 그리는 경우의 투명도·문서 여백 계약을
  존중한다. OS 전용 회귀 필터는 각 플랫폼에서 컴파일된 사례만 요구한다.

후속 구조 개선 후보는 공통 native manager에 남은 Favorites 영역 간 도착 모션의
제품 수명 관리를 `favorites`로 옮기는 것이다. 일반 manager에는 외부 layer 모션을
덮어쓰지 않는 계약만 남기는 방향이며, 이번 버전 이식의 시계·수명 검증과 분리해 검토한다.
팝업에서도 기존 `browser/ui/omnibox_popup_style`을 확장해 표시 방식과 해석된 기하를
공유할 수 있다. 이때도 창 수명·포커스·지연 표시는 Chromium에 남긴다. 두 후보 모두
별도 회귀 검사를 갖춘 후속 변경으로 다루며 이번 이식에서 범용 구조를 추가하지 않는다.
