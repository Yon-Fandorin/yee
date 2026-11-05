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

`test_legacy_ui_waits.py`는 `browser/ui` 아래 Yee 테스트와 GN testonly target의
`sources`·`public` 지원 코드(헤더 포함)의 `RunUntil`,
`RunUntilIdle`, polling helper, sleep, delayed task, timer 시작 및 JS
`setTimeout`·`setInterval`을 검사한다.
`legacy_ui_wait_allowlist.json`은 mock-clock 자극, 정상 완료 callback,
비UI task 전달과 일회 실패 deadline만 정확한 호출 위치·지문·사유로 분류한다.
새 호출, 호출 내용 변경, 빈 사유와 삭제된 호출의 예외는 모두 실패한다.
UI/model 상태의 완료를 기다리는 새 코드는 입력 전에 applied ticket을 등록한다.

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

`test_background_test.py`는 고정 headless 화면·창 옵션, layout 사례의 background 유지와
표시형 launch policy 사례의 foreground 분리를 검사한다. Swift fixture는 가짜
LaunchServices 응답으로 ASN·PID 해석과 전면 앱 전환을 확인한다. 조회 실패·읽기 공백·
전면 앱 표본 부족은 관측 미완료 또는 실패로 판정한다. 실제 LaunchServices나 GUI를
호출하지 않는다.

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

`test_windows_build_selection.py`는 Windows PowerShell이 있을 때 임시 루트와 가짜 도구로
명시값·기본값 보존, 후보 안내의 인용, 준비·빌드·실행·패키징의 부작용 전 중단을 검사한다.
사전 컴파일 실행기의 목록 모드, 핀·graph 차단, 복수 object 전달, UTF-8 기록·범위 요약과
실패 코드·실패 파일 보고도 확인한다. 공통 분류기의 Ninja 확장자와 Windows depfile의
드라이브·공백 처리 검사는 GUI와 실제 컴파일 없이 실행한다.
Windows 단일 object의 caret 제거 방지와 8191자를 넘는 목록 전달도 확인하며,
PowerShell 5.1·7의 native stderr가 성공을 실패로 바꾸거나 실제 실패 코드를 가리지 않는지 검사한다.

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

## Yee 테스트 gate 누락 검사

`test_gate_coverage.py`는 `browser/`, `renderer/`, `components/`의 C++ GTest 선언을
항상 검사한다. upstream 부분은 `YEE_LOCAL_BUILD_ROOT/chromium/src`, 환경 변수 미지정 시
`.local-build/*/chromium/src`와 `.local-build/chromium/src`에서 checkout을 찾는다.
checkout이 없으면 upstream 사례·GN 검사만 skip하고 이유를 출력한다. overlay 검사는
계속 실행하며, checkout 없이 확인할 수 없는 기존 native Header·Sidebar GN 연결에는
현재 소유한 overlay 소스 8개의 명시적 대응을 쓴다. checkout이 있으면 실제 GN 파일로
그 연결도 확인한다.

`0001`의 기본 3줄 문맥을 유지한 채 hunk의 변경 줄 범위와 checkout의 `TEST` 선언부터
닫는 괄호까지의 범위를 대조한다. 주석·문자열의 괄호는 가리고 포맷·주석만 바뀐 블록은
제외한다. C++ 문법을 해석하거나 본문 토큰을 비교하지 않는다. helper만 바꾼 경우
기존 suite 전체를 Yee 시나리오로 취급하지 않는다. patch 줄과 checkout이 다르면
적용 상태를 확인하도록 실패한다. `test_overlay_tools.py`는 긴 테스트 함수의 한 줄을
수정해 재생성 결과가 `git diff --unified=3`과 같은지 확인하여 함수 문맥 확장을 막는다.

복사한 `.sh` gate를 가짜 공통 함수·실행 파일로 평가해 Darwin·Linux·Windows 셸의
모드별 빌드 타깃과 실제 실행 인수를 수집한다. 원본 스크립트의 필터 변수·배열·OS 분기를
그대로 사용하며 앱·컴파일러는 실행하지 않는다. 새 native gate는 `GATE_MODES`에
모드를 등록해야 한다. GN의 `sources`, `deps`, `public_deps`를 따라 실행 바이너리에
소스가 포함되고 GTest 양수·음수 필터가 선언을 선택하는지 함께 확인한다. GN 조건은
플랫폼의 합집합으로 읽는다. 기존 heavy layout browser gate의 원본 fixture 소스는 명시적 연결 표로 보완한다.

매개변수 테스트는 overlay·checkout의 `INSTANTIATE_*TEST_SUITE_P`에서 실제 prefix를
읽는다. 기본 `Bool`·`Values`·`Combine`의 숫자 suffix와 단순 switch의 반환 이름도
대조한다. 그 밖의 C++ 이름 함수는 평가하지 않으며 suffix 전체를 고르는 필터만
인정한다. 필터의 prefix·suffix를 가상 이름에 복사하지 않는다.

이 검사는 선언 단위다. 매개변수의 모든 인스턴스, 기능 플래그에 따른 `GTEST_SKIP`,
실제 OS의 컴파일 여부는 native `--gtest_list_tests`·실행으로 검증한다. zsh가 없으면
셸 연결 검사는 건너뛰므로 Windows 검증은 별도로 기록한다.
`gate_coverage_allowlist.json`은 정확한 `소스::Suite.Case`별로 소유 gate와 이유를 둔다.
현재 예외는 opt-in YouTube 1개와 Windows 세션 소유 18개(Aura 포함)다. 삭제된 사례의
stale 항목이나 빈 이유는 실패한다. 일반 누락을 예외에 추가하는 대신 gate에 연결한다.

## 사례별 tier와 gate tier

[`test_tiers.json`](test_tiers.json)은 정확한 `소스::Suite.Case`별로 OS 범위,
A/B/C, 소유 gate, 실행 바이너리와 짧은 근거를 둔다. `binaries`는 GN 소스 연결과
대조하며 같은 이름 패턴을 쓰는 다른 바이너리의 사례가 섞이지 않게 한다.
매개변수 선언은 `parameters`에 실제
`INSTANTIATE_*`의 prefix·suffix를 기록하며 coverage 파서와 대조한다.
wildcard tier 기본값은 없다. allowlist 항목에도 tier가 필요하며 manifest와 같아야 한다.
`needs_display`는 B를 기존 foreground 실행기에 남길 OS별 사유를 담는다.
예를 들어 `{"Darwin": "기존 Widget runner를 유지하는 근거"}`이며, B 외의 tier나
선언의 OS 범위를 벗어난 키·빈 근거는 허용하지 않는다.

`test_gate_coverage.py`는 Yee 선언 외에도 gate가 선택한 upstream 선언을 수집한다.
분류 누락·삭제되거나 선택에서 빠진 manifest 항목·background의 C 선택·C의
OS별 소유 gate foreground/all 연결 누락을 실패로 처리한다. Header·Sidebar·layout·Projects의
기존 모드와 `all`, layout의 `horizontal`, 세 실행 tier를 Darwin·Linux·Windows
가짜 환경에서 평가한다. tier별 필터의 교집합이 비고 합집합이 전체 필터의 선택과
같은지 확인한다. background 전체의 display/종료 helper 호출과 `needs_display` 선택을 막고,
예외가 OS별 소유 gate의 foreground/all에 남아 있는지도 검사한다.

필터 출력은 실제 개발 환경이나 native 실행 파일 없이 확인할 수 있다.

```sh
./tools/dev/test-header.sh interactive --tier=background --print-filters
./tools/dev/test-header.sh interactive --tier=foreground --print-filters
./tools/dev/test-header.sh interactive --tier=all --print-filters
```

이 검사는 실제 창·포커스·입력 격리나 native 테스트 통과를 증명하지 않는다.
`test_background_test.py`는 실제 GUI 대신 가짜 자식·JSON으로 격리 프로필·PID 재사용,
macOS 임시 경로 환경 변수, 프로필 인수가 없는 자식 정리, 관찰 옵션, 목록 조회와 실행의
관찰 범위, 불완전한 관찰의 실패 판정을 검사한다. macOS에서는 Swift 판정 코드를 별도로
컴파일해 고아 자식의 세션 소유권과 최소화·다른 Space 창의 생존 비교를 확인한다.
일시적인 무관 앱의 identity 누락과 종료 중인 자체 helper는 실패로 세지 않는지,
시작 앱의 실제 종료는 캡처한 identity로 손실을 보고하는지 검사한다. 전체 프로세스·그룹
조회의 실패와 빈 목록을 구분하고, 이후 조회가 성공해도 실패 사유와 공백 횟수가 남는지 확인한다.
이 fixture는 AppKit 관찰이나 실제 창·입력을 시작하지 않는다.
Foreground 실행 조건은 [개발 도구 안내](../../tools/dev/README.md#background와-foreground-tier)를 따른다.
