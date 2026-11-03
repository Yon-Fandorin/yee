# 개발 도구 테스트

브랜딩 설정과 적용, 소스 복사 경로, 개발 명령이 예상대로 동작하는지 확인한다.

## 실행

저장소 루트에서 실행한다.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

임시 파일, 임시 Git 저장소와 가짜 빌드 도구를 사용한다.
실제 Chromium 컴파일이나 브라우저 실행은 수행하지 않는다.
일부 브랜딩 테스트는 `.local-build/chromium/src`의 도구를 읽는다.
해당 소스가 없으면 테스트를 건너뛰므로 통과와 건너뜀을 구분해서 보고한다.

`test_chromium_version.py`는 공통 원본 핀, OS별 업데이트 조회와 Windows 개발 명령의
사전 검사를 다룬다. `test_overlay_tools.py`는 별도 후보 원본에서 패치를 다시 만들 때의
소유권 경계도 확인한다. macOS 개발 명령 검사는 zsh가 없으면 건너뛴다.
브랜딩 적용 테스트는 Windows에서 PowerShell, macOS에서 zsh 진입점을 사용한다.
새 원본에 적용·이름 변경·반복 적용과 생성 경로의 패치 재진입 거부를 검사한다.

`test_patch_ratchet.py`는 파일 분류·지표 측정·멤버 선언 근사 판정, 최초 기준선 기록,
감소분만 저장하는 `--update`, 증가·예외 허용량·잘못된 설정을 임시 패치로 검사한다.
`test_overlay_tools.py`의 `test_regenerate_saves_patch_then_fails_when_ratchet_grows`는
래칫 증가 시에도 `0001`을 먼저 저장한 뒤 실패 코드를 반환하는지 확인한다.

`test_native_test_discovery.py`는 일반·타입·값 매개변수 목록, 양수·음수 필터,
비활성 테스트, 패턴 일부 누락과 순수 Python CLI를 가짜 목록으로 확인한다.
zsh가 있으면 가짜 바이너리로 목록 조회·실제 실행의 인수 일치와 실패 코드 전파도 확인한다.
기록의 소스 지문·바이너리 해시 캐시·선택 명시 여부와 XML 누락·실패·오류·0개·손상·
이전 결과 재사용의 불일치, 호출자 출력 경로 보존도 검사한다.
미추적 파일의 같은 경로·크기 내용 변경과 빈 XML 경로의 사용법 오류도 확인한다.
SIGINT 계약 검사는 가짜 실행기의 500ms 자식 정리와 후속 handler 완료를 기다리는지,
실제 종료 코드·인터럽트 기록과 코드가 없는 경우의 130을 검사한다.
실제 네이티브 바이너리나 GUI는 실행하지 않으며 zsh가 없으면 셸 검사는 건너뛴다.

`test_macos_installer_processes.py`는 Node.js에서 순수 필터 함수만 호출한다.
가짜 ps 목록의 설치판·개발판·검사 앱·Helper·검사 프로필을 섞고 정확한 경로 경계와
제품명의 공백·정규식 문자를 검사한다. 실제 ps 조회·브라우저 실행·종료는 수행하지 않는다.

`test_observation_processes.py`는 가짜 개발 번들 목록으로 선택한 앱·정확한 검수 프로필과
다른 out·worktree의 경계를 확인한다. Helper만 남은 종료 대기와 두 관찰 스크립트의 실제
종료 검사 연결부도 실행하며, 무관한 프로세스의 개수·경로만 기록하는지 검사한다.
`test_browser_bundle_executables.py`는 임시 plist·가짜 ps 목록으로 설치판 제외와
`--running`·출력 경로의 기존 CLI, `--details`의 Helper·커널 argv 해석을 확인한다.
공백이 붙은 다른 프로필과 환경 변수는 검수 프로필로 오인하지 않는다.
실제 앱·ps·컴파일러는 실행하지 않는다.

`test_compile_preflight.py`는 임시 overlay·가짜 Ninja와 PATH의 가짜 autoninja로
패치의 삭제 파일 제외, owned 목적지·제외 규칙, graph의 미컴파일 분류, 한 번의
keep-going 호출과 실패 코드·파일 보고를 검사한다. 미빌드 object의 기본 제외,
`--include-unbuilt`·목록 옵션 결합, 부분 생략 요약과 빈 출력의 종료 코드 16도 확인한다.
플랫폼 미컴파일만 남은 0개 대상도 16으로 끝나는지, depfile 입력의 mtime·누락·동기화 후
변경이 컴파일을 19로 막고 목록에서는 경고하는지 검사한다. 경로 이스케이프·stamp 기준,
실패 줄의 따옴표 손상, 완료 요약의 포함·생략 수와 디스크 부족 시 목록 조회·알 수 없는
옵션의 종료 코드 2도 가짜 도구로 확인한다.
오래된 graph로 19 종료 시 기록 폴더를 만들지 않는지, 환경 변수 없이 실행해도
overlay import가 새 바이트코드를 만들지 않는지도 확인한다.
실제 Chromium 컴파일은 실행하지 않는다.

`test_development_tools.py`의 통합 fixture는 다른 OS 전용 사례를 목록에서 빼 두어
레이아웃의 `horizontal`·`interactive`·`all`과 Sidebar의 플랫폼 필터 혼합을 확인한다.
`test_build_selection.py`는 개발 환경 변수를 격리한 임시 루트에서 루트·출력 선택 가드와
기존 기본값을 검사한다. 도구는 모두 가짜이며 YouTube 관찰기의 경로 검사도 앱 실행 전에 중단한다.
`--no-build` 테스트·앱/프로토타입 실행·패키징(`--skip-build` 포함)·Python fixture에서
두 선택 가드가 부작용보다 먼저 실행되는지, 진단 명령은 힌트 뒤 계속되는지도 확인한다.
공백이 든 제품 이름과 선택한 루트·출력을 경로 헬퍼와 관찰기에 전달한다.
관찰기의 경로 선택과 종료 callback을 직접 평가해, CDP 종료 실패 시 선택한 루트의
가짜 종료 helper에 선택한 실행 파일·PID가 전달되는지 실제 앱 없이 검사한다.
종료 helper는 가짜 swiftc로 생성·재사용·실패를 검사하고, Metal·표시 helper와
Grok cohort의 경로·최신성 검사도 선택 가드 뒤에 실행되는지 확인한다.

[`tools/dev/`](../../tools/dev/README.md)의 기존 브랜딩·앱 번들 테스트 명령도
이곳의 테스트를 실행한다. C++ 테스트는 구현 옆에 두며 실제 UI 테스트 절차는
[`AGENTS.md`](../../AGENTS.md)를 따른다.
