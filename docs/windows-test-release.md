# Windows 테스트판

이 패키지는 Yee의 사용자별 설치·실행·업데이트 수리·제거를 확인하기 위한 테스트판이다.
공개 정식 배포판이나 서명된 설치 파일로 취급하지 않는다. 브라우저 앱의 자동 업데이트는
아직 제공하지 않는다. 광고 필터의 자동 갱신은 별도로 동작한다.

## 설치와 데이터

함께 제공된 `Yee-버전-windows-아키텍처-test-unsigned.exe`를 실행한다.
관리자 권한이 없는 사용자별 설치를 대상으로 하며 시스템 전체 설치는 지원하지 않는다.

- 앱: `%LOCALAPPDATA%\Yee\Application`
- 설정·북마크·방문 기록: `%LOCALAPPDATA%\Yee\User Data`
- 시작 메뉴와 Windows 앱 목록: Yee

기존 Chrome·Chromium의 설치·프로필을 자동으로 가져오거나 변경하지 않는다.
Windows의 실행 파일 검색 등록도 `Yee.exe`라는 전용 키를 사용한다. 내부 실행 파일 이름은
`chrome.exe`이며, 다른 브라우저의 공유 `chrome.exe` 등록을 덮거나 삭제하지 않는다.
개발 중 사용한 Chromium 경로의 프로필도 이전하지 않는다. 수동으로 폴더를 덮어쓰지 않는다.
같은 버전 설치 파일을 다시 실행하면 설치를 수리하고 사용자 데이터를 보존한다.
새 테스트판은 Yee를 정상 종료한 뒤 해당 설치 파일을 실행해 적용한다.

Windows 앱 목록에서 Yee를 선택해 제거한다. 기본 제거는 사용자 데이터를 보존한다.
제거 화면에서 데이터 삭제를 선택하면 Yee의 사용자 데이터도 삭제된다.

## 서명과 고지

현재 설치 파일은 서명하지 않았다. Windows가 확인되지 않은 게시자 경고를 표시하거나
실행을 제한할 수 있다. 조직의 보안 정책을 해제하지 말고 관리자에게 배포 방식을 확인한다.
정식 배포 전에는 게시자 서명·브라우저 보안 업데이트·깨끗한 별도 PC 검증이 필요하다.

패키지의 `release.json`에는 버전·원본 커밋·작업 트리 변경 여부·설치 파일의 SHA-256과
포함한 광고 차단 자료의 hash가 기록된다. 개발 중 변경된 소스가 포함됐으면
`working_tree_dirty`가 `true`다. 재현 가능한 공식 릴리즈를 주장하지 않는다.

설치 폴더의 버전 하위 폴더에는 `YeeContentBlockingNotices.txt`,
`YeeContentBlockingSources.tar.xz`, `YeeCommunityFilterNotices.txt`,
`YeeCommunityFilterSources.tar.xz`가 들어 있다. 외부 코드·목록·리소스의 고지와
해당 원본 소스를 이 파일에서 확인할 수 있다. Chromium 고지는 앱의 `chrome://credits`에서도
확인한다. 외부 배포 전의 전체 라이선스·개인정보 안내 검토는 별도 작업이다.

## 개발 도구와 확장 경계

`tools/dev/package-windows.ps1`이 기존 Chromium `mini_installer`를 빌드하고
`.local-build/releases/`에 설치 파일·메타데이터·이 안내를 만든다. 개발용 component 빌드도
Chromium의 runtime dependency 목록에 따라 DLL을 함께 묶는다. 정적 빌드도 같은 진입점을 쓴다.

`node tools/dev/test-windows-installer.mjs PACKAGE_DIRECTORY`는 기존 Yee 설치나 데이터가
없을 때만 시작한다. 실제 설치·기본 프로필·수리·제거 후 데이터 보존·재설치·시험 데이터까지
제거를 검사하고 진단 자료를 `.local-build/windows-install-test-*`에 남긴다.
검증 중 생성한 프로필만 삭제 대상으로 삼는다. 모든 Yee를 정상 종료한 뒤 실행한다.
설치·수리 직후와 마지막 제거 후에 기존 Chrome·Chromium 등록·기본 브라우저·공유
실행 파일 키·과거 확장자 및 RLZ 데이터의 hash도 비교한다. native registry override
테스트는 타사 등록과 다른 실행 경로를 가리키는 Yee 이름의 키도 보존하는지 확인한다.
설치된 실행 파일에서도 Windows HTTP rendering fixture로 요청·프레임·CSS·scriptlet의
차단·해제·사이트 예외·정상 콘텐츠 복구를 검사한다. 이 fixture의 프로필은 일반 설치
프로필과 별도로 만든다. 외부 Chromium checkout을 쓰면 기존 빌드 도구와 동일하게
`YEE_CHROMIUM_ROOT`를 지정한다.

제품의 영속 식별자는 `branding/identity.json`, 표시 이름·로고는 `branding/brand.json`이
소유한다. `tools/overlay/product_identity.py`가 검증된 식별자를 native 빌드 입력으로 생성하고
기존 브랜딩 overlay가 적용한다. 제품 이름을 바꿔도 프로필·등록 식별자는 바꾸지 않는다.
Windows 패키징은 설치·제거를 Chromium에 맡기고 배포 파일·공통 릴리즈 메타데이터만 소유한다.

macOS·Linux 지원 시 같은 제품 식별자에 각 플랫폼의 검증된 bundle/desktop·데이터·서명 설정과
패키징 진입점을 추가한다. Windows 레지스트리나 설치 명령을 공용 제품 설정에 섞지 않는다.
현재 체크포인트는 macOS·Linux 설치·프로필을 변경하거나 검증하지 않는다.
Windows 시스템 전체 설치·서비스·앱 자동 업데이트도 이 테스트판의 범위 밖이다.

## Windows 검증 기록

2026-10-25 Windows x64의 실제 unsigned component 설치 파일로 다음을 확인했다.
전체 앱·설치 파일·native 설치 테스트가 빌드됐고, 설치된 버전 폴더의 필터·실행 리소스·
고지·원본 묶음 9개가 패키지 메타데이터의 SHA-256과 모두 일치했다.

- 별도 `--user-data-dir` 없이 실행해 native 버전 화면이
  `%LOCALAPPDATA%\Yee\User Data\Default`를 보고했다. 정상 페이지의 본문과 캡처를 확인했다.
- 같은 버전 수리와 제거 후 재설치 뒤에도 동일한 웹 저장 데이터가 유지됐다.
  수리의 native 상태 1과 성공 종료코드 0을 함께 확인했다.
- 일반 제거는 프로필을 보존했고, 명시적 데이터 삭제 제거는 시험 프로필을 삭제했다.
  마지막에는 설치 실행 파일·제거 항목과 시험 루트가 남지 않았다.
- 설치·수리·최종 제거 뒤에 기존 Chrome/Chromium·공유 실행 파일 등록·확장자·RLZ와
  기본 브라우저 설정의 hash가 시험 시작 전과 일치했다.
- 설치본의 Windows rendering fixture는 켬·끔·사이트 예외 세 모드를 통과했다.
  광고 요청 미도달·iframe/이미지 접기·상속 CSS/scriptlet·정상 콘텐츠 복구를 확인했다.

`install_static_unittests` 56개와 `setup_unittests` 297개 검사에서 실패가 없었다.
후자의 시스템 설치 미지원·관리자 서비스 권한이 필요한 시험은 지원 조건에 따라 skip했다.
tooling은 118개 중 macOS 전용 등 40개를 skip하고 나머지를 통과했다.
native 패치 역적용·범위 내 공백 검사와 JavaScript·PowerShell 구문 검사도 통과했다.

원시 결과는 로컬 `.local-build/windows-install-test-n8J1ZT/`와 설치본 rendering 결과에 있다.
초기 진단 회차의 게시자 문자열 전체 오인·문서 전환 중 context 소멸·버전 화면의 빈 초기
필드는 시험 도구에서 수정했다. 최종 완료 결과는 위 전체 흐름을 통과한 회차만 사용한다.
이 검증은 현재 개발 PC 범위다. 다른 깨끗한 PC, 시스템 전체 설치, 서명·정식 배포,
macOS·Linux 실행을 검증한 것으로 확대하지 않는다.
