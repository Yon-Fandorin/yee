# 설치된 Chrome으로 탭 동작 확인

로컬에 설치된 Google Chrome을 별도 프로필로 실행한다.
실제 탭과 웹페이지를 사용해 Chromium의 세로 탭 동작을 확인하는 환경이다.
직접 빌드한 제품 앱과는 별개다.

## 실행

저장소 루트에서 실행한다.

macOS:

```sh
./native-pilot/launch.sh
```

Windows PowerShell:

```powershell
.\native-pilot\launch.ps1
```

| 설정 | 기본값 | 변경할 환경 변수 |
| --- | --- | --- |
| macOS 앱 | `/Applications/Google Chrome.app` | `YEE_CHROME_APP` |
| Windows 실행 파일 | `%ProgramFiles%\Google\Chrome\Application\chrome.exe` | `YEE_CHROME_BINARY` |
| 별도 프로필 | `native-pilot/runtime/profile` | `YEE_PILOT_PROFILE` |

처음 실행할 때 `profile-template/Preferences`를 복사한다.
사용자의 일반 Chrome 프로필은 수정하지 않는다.

기본으로 웹페이지 세 개를 연다. 다른 페이지를 열려면 URL을 전달한다.

```sh
./native-pilot/launch.sh https://chromium.org/ https://lit.dev/
```

## 화면 설정과 확인 범위

실행 도구는 세로 탭과 민트색 테마를 지정한다.
macOS에서는 지원되는 시스템에 `GlassFrame` 기능 인자도 전달한다.
실제 화면은 설치된 Chrome의 기능 지원에 따라 달라질 수 있다.

탭 선택·닫기·로딩·탐색 등 Chrome의 실제 동작을 확인한다.
제품의 자체 UI나 Agent 연결을 이 바이너리에 추가하지는 않는다.
그 기능은 [제품 코드](../browser/README.md)와 [개발 도구](../tools/dev/README.md)를 따른다.
소스 빌드의 [`run.sh`](../tools/dev/run.sh)는 이 도구와 달리 테마를 지정하지 않는다.
