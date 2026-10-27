# macOS 테스트판

`Yee-버전-macos-arm64-test-adhoc.dmg`를 열고 Yee 아이콘을 Applications 폴더로
드래그한다. 복사가 끝나면 디스크 이미지를 추출하고 응용 프로그램의 Yee를 연다.
아직 `/Applications/Yee.app`이 없으면 디스크 이미지 안의 Yee를 더블클릭해
기본 설치 안내를 사용할 수도 있다. 설치를 선택하면 Applications로 복사한 앱을
실행하고 Yee를 Dock에 추가하며, 설치 디스크를 추출한 뒤 원본 DMG를 휴지통으로 옮긴다.
이미 같은 위치에 앱이 있으면 이 보조 설치 안내는 생략된다. 새 버전으로 교체할
때는 아래의 Finder 대치 절차를 사용한다.
드래그 설치 후 Dock에 유지하려면 실행 중인 Yee 아이콘의 옵션에서 Dock에 유지를 선택한다.
앱을 디스크 이미지 안에서 계속 실행하지 않는다.

## 교체와 사용자 데이터

새 버전은 Yee를 정상 종료한 뒤 같은 방식으로 복사하고 Finder에서 대치를 선택한다.
같은 버전의 재설치도 앱 번들만 교체한다. Finder는 구버전으로의 교체를 자동으로
차단하지 않으므로 현재 설치와 같거나 더 최신인 버전을 사용한다.

- 앱: `/Applications/Yee.app` 또는 사용자의 `~/Applications/Yee.app`
- 설정·북마크·방문 기록·탭: `~/Library/Application Support/Yee`
- 캐시: `~/Library/Caches/Yee`
- 앱 식별자: `io.github.yon-fandorin.yee`
- 키체인: 서비스 `Yee Safe Storage`, 계정 `Yee`

앱을 휴지통으로 옮겨도 사용자 데이터는 남는다. 다시 설치하면 같은 프로필을 사용한다.
브라우저 앱의 자동 업데이트는 아직 제공하지 않는다. 새 DMG로 직접 교체한다.
광고 필터의 자동 갱신은 별도로 동작한다.

기존 개발 번들과 Chrome·Chromium 프로필을 자동으로 이전하지 않는다.
개발용 실행 경로와 키체인은 유지하며, 설치판은 독립 프로필로 시작한다.
설치 도구는 다른 브라우저의 앱·기본 브라우저 선택을 수정하지 않는다.
기본 설치 안내는 Yee의 Dock 항목을 추가하거나 DMG 실행 경로에서 설치 경로로 바꾸며,
다른 앱의 Dock 항목은 유지한다.

## 서명과 배포 범위

현재 패키지는 Apple Silicon 실행에 필요한 ad-hoc 코드 서명과 번들 무결성 검사를
적용한 테스트판이다. Developer ID 서명과 Apple 공증은 포함하지 않는다.
인터넷에서 다운로드한 파일은 Gatekeeper가 실행을 제한할 수 있다.
일반 사용자에게 경고 없이 배포하려면 Developer ID Application 인증서와 Apple
공증을 추가해야 한다. 조직의 보안 정책이나 Gatekeeper를 해제하지 않는다.

`release.json`에는 제품·엔진 버전, 소스 커밋, 작업 트리 변경 여부, DMG의 SHA-256,
아키텍처, 서명·공증 상태와 콘텐츠 차단 자료의 해시가 들어 있다.
개발 중 변경이 포함된 패키지는 `working_tree_dirty`가 `true`다.

콘텐츠 차단의 라이선스 고지와 해당 소스 묶음은 Yee Framework의 Resources에 포함한다.
앱의 `chrome://credits`에서도 Chromium 고지를 확인할 수 있다.

## 개발 명령

저장소 루트에서 실행한다.

```sh
./tools/dev/package-macos.sh
```

기존 전체 앱을 검증해 패키징하려면 `--skip-build`를 사용한다. 이 모드도 제품 버전,
Framework, 독립 번들 경계와 고지 자료를 확인한다. component 빌드는 지원하지 않는다.
완성한 DMG를 읽기 전용으로 다시 연결해 내부 앱의 코드 서명도 검증한다.
결과는 `.local-build/releases/macos-버전-시각/`에 생성한다.
`YEE_LOCAL_BUILD_ROOT`를 지정하면 같은 빌드 루트의 `releases/`를 사용한다.

```sh
./tools/dev/package-macos.sh --skip-build
node tools/dev/test-macos-installer.mjs PACKAGE_DIRECTORY
```

설치 검사는 DMG를 읽기 전용으로 연결하고 격리된 Applications 폴더에 실제 앱을
복사한다. 앱 실행·종료, 같은 버전 재설치 후 탭 그룹·북마크·웹 저장 데이터 복원과
앱 제거 후 프로필 보존을 확인한다. 실제 `/Applications`와 기존 사용자 프로필은
수정하지 않는다. 검사 전 실행 중인 Yee를 정상 종료한다.
실행 검사는 mock 키체인을 사용해 개인 키체인에 검사 데이터를 남기지 않는다.
설치판과 개발 번들의 키체인 이름 선택은 별도의 native 단위 검사로 확인한다.
검사 기록과 전용 프로필은 `.local-build/macos-install-test-*`에 남는다.

DMG 감지 회귀 검사는 Chromium의 `chrome/browser/mac:install_sh_test`를 빌드한 뒤
`DiskImageInstallTest.*`만 실행한다. 임시 HFS+ 디스크 이미지를 직접 연결해 읽기 전용
설치 원본과 쓰기 가능한 이미지·일반 앱 폴더를 구분한다. 검사에서 만든 이미지의
추출·휴지통 이동도 확인하며, 실제 앱을 설치하지 않는다.

`InstallShTest.UpdaterBuildFlagControlsElevatedSteps`는 실제 설치 템플릿을
업데이터 포함·미포함 값으로 각각 렌더하고 관리자 권한 분기를 검사한다.
EUID 판정과 시스템 명령을 임시 fixture로 바꾸므로 실제 권한 승격이나
`/Applications`·`/Library`·LaunchServices 변경은 수행하지 않는다.
`enable_updater=false`인 빌드에서는 앱을 복사한 뒤 업데이터·Keystone 등록을
생략한다. 실제 Authorization Services 인증 창 검증은 이 검사에 포함되지 않는다.

영속 식별자는 `branding/identity.json`, 배포 버전은 `branding/VERSION`이 소유한다.
DMG 제작은 `tools/dev/lib/macos_release.py`가 담당하고 Finder가 설치·교체를 수행한다.
빌드 원본은 보존하고 복사한 배포 번들에만 설치판 식별자와 기본 프로필 경로를 적용한다.
