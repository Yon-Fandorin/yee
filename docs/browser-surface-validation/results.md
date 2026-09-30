# Browser Surface의 실앱 검증 기록

이 디렉터리는 당시 다시 빌드한 macOS 개발 브라우저의 캡처와 검증 기록을 보존한다.
아래 결과는 여러 체크포인트에서 수행한 범위를 각각 나타내며, 현재 소스의 새
검증 결과나 하나의 합산 테스트 수로 해석하지 않는다. 현재 변경은 해당 검증을
다시 수행해야 한다.

각 실행은 격리 프로필을 사용했고, 이전 개발 브라우저 프로세스를 정상 종료한 뒤
`tools/dev/run.sh`의 전경 실행 경로로 시작했다. 초기 PDF 문구 검증은
`chrome://infobar-internals`에서 PDF 행을 키보드로 실행해 WebContents 클릭 전달
결함이 결과를 가리지 않도록 했다. 2026-09-03 재검증은 보이는 실제 앱을
Computer Use로 조작했다. 새 임시 프로필에서 `chrome://chrome-urls`로 내부
디버깅 페이지를 활성화하고 네이티브 접근성 동작으로 PDF 행을 실행했다.

Sidebar 전환은 macOS에서 창만 녹화하고 원래의 가변 프레임 타임스탬프로
해석했다. 동작 후 Computer Use의 캡처 지연을 전환 시점의 근거로 사용하지 않았다.
F13/F14 Side Panel 검증은 새 임시 프로필과 실제 새 탭 페이지를 사용했다.
WindowServer에는 화면의 CGWindow가 있었지만 Computer Use가 로컬 Chromium 번들을
찾지 못해, 로컬 DevTools로 실제 맞춤설정 동작을 실행하고 WindowServer ID로
네이티브 창을 직접 캡처했다.

## 실앱 결과

| 상태 | 근거 | 당시 결과 |
| --- | --- | --- |
| 1171×768, 단일 화면, Sidebar 펼침·접힘 | `expanded-new-tab.jpeg`, `collapsed-new-tab.jpeg` | 통과: Browser Surface가 콘텐츠 열과 외곽 경계 안에 유지됨 |
| 1171×768, 실제 탭 두 개의 분할 화면, Sidebar 펼침·접힘 | `split-expanded-two-tabs.jpeg`, `split-collapsed-two-tabs.jpeg` | 통과: 두 pane 카드와 구분자가 Browser Surface 안에 유지됨 |
| 브라우저 수준의 기본 브라우저·세션 복원 InfoBar, 펼침·접힘 | `default-browser-infobar-expanded.jpeg`, `default-browser-infobar-collapsed.jpeg`, `session-restore-infobar-expanded.jpeg`, `session-restore-infobar-collapsed.jpeg` | 통과: 네이티브 InfoBar 하나가 활성 pane 안에서 Pane Header 아래에 유지됨 |
| 768×874, 정확한 PDF 문구의 InfoBar, Sidebar 펼침 | `pdf-infobar-exact-expanded-sidebar.jpeg` | 통과: `Chromium을 기본 PDF 뷰어로 설정` 문구·동작·닫기 제어가 보인다. 지원 최소 폭 800 px보다 좁은 상태에서도 Sidebar 뒤에서 시작하고 끝이 창 밖으로 나가지 않음 |
| PDF 문구 재검증: 768×875 펼침, 1413×768 접힘·왕복, Header를 숨긴 1365×768 네이티브 전체 화면 | `macos-real-app-pdf-infobar-expanded-20260903.png`, `macos-real-app-pdf-infobar-collapsed-20260903.png`, `macos-real-app-pdf-infobar-fullscreen-20260903.png`, `macos-real-app-pdf-infobar-fullscreen-roundtrip-20260903.png` | 통과: 안정된 각 상태에서 정확한 한글 문구·동작·닫기 제어·상단 모서리·수평 그림자가 Surface 안에 유지되었다. Header를 숨긴 전체 화면에서도 이전 Header 예약 공간을 제거하고 외곽을 넘지 않음 |
| 단일 pane의 Sidebar 접힘·펼침 전환, 수정 전 1284×880·수정 후 1200×800 | 수정 전: `sidebar-collapse-animation.mov`, `sidebar-expand-animation.mov`, `sidebar-expand-toolbar-overlap-midframe.png`; 수정 후: `sidebar-expand-animation-fixed.mov`, `sidebar-expand-animation-fixed-native-transition-contact-sheet.png`, `sidebar-expand-animation-fixed-mid-2333ms.png`, `sidebar-expand-animation-fixed-mid-2367ms.png`, `sidebar-expand-animation-fixed-mid-2400ms.png` | **F23 수정 후 통과**: 수정 전에는 Toolbar 제어가 Sidebar에 남았으나, 수정 후 녹화의 2.215–2.588초 네이티브 타임스탬프 19프레임 모두에서 Sidebar Toggle·뒤로·앞으로·새로고침·Omnibox가 이동 중인 Header 경계 안에 유지됨 |
| 1171×768 분할 Find Bar, 펼침·접힘 | `split-expanded-findbar.jpeg`, `split-collapsed-findbar.jpeg` | 해당 폭에서 통과: 활성 pane 안에 유지됨 |
| 새 실앱의 1165×768 분할 Find Bar | `macos-real-app-split-fixture-version-20260903.png`, `macos-real-app-split-findbar-20260903.png` | 통과: 페이지 호스트 이전 후 실제 탭 두 개·두 Pane Header·구분자·네이티브 Find Bar가 각 소유 Surface 안에 유지됨 |
| 네이티브 전체 화면, 분할, Sidebar 접힘·펼침 | `split-collapsed-fullscreen-final.jpeg`, `split-expanded-fullscreen-repro.jpeg`, `split-expanded-fullscreen-roundtrip.jpeg` | 최종·왕복 프레임 통과: 이전 Sidebar 예약 공간이나 외곽 이탈 없음 |
| 800×600 분할, Sidebar 펼침·접힘 | `minwidth-800x600-split-expanded.jpeg`, `minwidth-800x600-split-collapsed.jpeg` | 브라우저 UI 구조 통과: 좁은 웹 콘텐츠는 자신의 pane 안에서 잘림 |
| 800×600 분할, Sidebar 접힘, Find Bar | `minwidth-800x600-split-collapsed-findbar.jpeg` | 통과: 활성 pane 안에 유지됨 |
| 800×600 분할, Sidebar 펼침, LTR 밝은 테마, Find Bar | `minwidth-800x600-ltr-split-expanded-findbar.jpeg` | **수정 전 실패 근거(F10)**: 활성 pane은 약 x=250…517인데 Find Bar는 x=123…507에 그려져 Sidebar 약 127 px를 덮음 |
| 800×600 분할, Sidebar 펼침, RTL 어두운 테마, 강제 배율 1.25, Find Bar | `minwidth-800x600-dark-rtl-scale125-split-expanded-findbar.jpeg` | **수정 전 실패 근거(F10)**: 반전된 Find Bar가 pane 경계를 넘어 오른쪽 Sidebar를 덮음 |
| 수정 후 800×600 분할 Find Bar, LTR 밝은 테마·RTL 어두운 테마, 배율 1.0/1.25 | `macos-real-app-minwidth-800x600-ltr-split-expanded-findbar-postfix-20260903.jpeg`, `macos-real-app-minwidth-800x600-dark-rtl-split-expanded-findbar-postfix-20260903.jpeg`, `macos-real-app-minwidth-800x600-dark-rtl-scale125-split-expanded-findbar-postfix-20260903.png` | 통과: 각 네이티브 Find Bar가 활성 pane 안에 유지되었다. RTL/DSF 1.25에서 WindowServer의 자식 Widget은 264×84 DIP·`alpha=1`이다. 소수 배율 왕복 손실로 발생한 수정 전 265×84·`alpha=0` 상태를 대체함 |
| 1171×768 분할, 어두운 테마·RTL·강제 배율 1.25, Sidebar 펼침 | `dark-rtl-scale125-split-expanded.jpeg` | 캡처 폭의 최종 구조 경계 통과했다. 이 OS 캡처는 정확한 장치 픽셀 clip의 증거가 아님 |
| 800×600 분할, 어두운 테마·RTL·강제 배율 1.25, Sidebar 펼침·접힘 | `minwidth-800x600-dark-rtl-scale125-split-expanded.jpeg`, `minwidth-800x600-dark-rtl-scale125-split-collapsed.jpeg` | 최종 구조 경계 통과했다. 정확한 장치 픽셀 clip 일치를 증명하지 않음 |
| 네이티브 compositor의 hard clip 읽기, 단일·애니메이션 대상·분할 | `BrowserViewTabbedLayoutImplContentLayoutUiTest.YeeHardClipOwnersMatchAtDeviceScale` | 재시도 없이 강제 DSF 1.0/1.25/1.5/2.0 통과했다. Views·NativeViewHost의 clip·radius 입력이 정확히 같고, Sidebar·여백·구분자·비활성 pane 등 소유 body 밖의 sentinel 픽셀이 정확히 0 |
| 1000×700 네이티브 Chrome 맞춤설정 Side Panel, 숨김·열림, LTR/RTL | `f13-f14-side-panel-hidden-ltr.png`, `f13-f14-side-panel-open-ltr.png`, `f13-f14-side-panel-hidden-rtl.png`, `f13-f14-side-panel-open-rtl.png` | 안정된 실앱 픽셀 통과: Surface·외곽 안에서 공통 Header 행 바로 아래에 시작하며, 물리적 오른쪽에서 왼쪽으로 반전해도 제품 Sidebar에 들어가지 않음 |
| 1131×768 실제 분할과 네이티브 Chrome 맞춤설정 Side Panel | `macos-real-app-split-native-side-panel-settled-20260903.png` | 안정된 상태 통과: 최종 패널이 두 pane의 물리적 오른쪽에 배치되고 Sidebar·구분자·페이지를 덮지 않음 |
| 네이티브 Side Panel 전환 여백 읽기: 닫힘, 열림 시작·중간·끝, 정렬 전환, 닫기·역전 | `BrowserViewTabbedLayoutImplContentLayoutUiTest.NativePlannerMatchesAppliedAnimationFramesAndReversal` | 재시도 없이 통과: 시작·끝·하단의 6 DIP 여백에서 실제 compositor 표본 21개가 Chromium의 흰색 균열 방지 배경 대신 제품 셸 재질을 유지했다. Surface 상단·하단도 모든 전환 프레임에서 닫힌 기준과 정확히 같아 네이티브 그림자 여백이 이중 적용되지 않음 |
| 실제 분할에서 렌더링된 before-unload 탭 모달 | `macos-real-app-split-beforeunload-navigation-modal-20260903.png` | 부분 통과: 실제 경고와 키보드·접근성 Cancel 경로는 확인했다. 캡처에 모달만 있어 아래 브라우저 pane 기준의 배치를 시각적으로 증명하지 못함 |
| 1152×768, 호스트를 이전한 Contents, 실제 탭 두 개의 분할 | `f18-host-migration-split-real-app.jpeg` | 통과: 두 중첩 Contents가 Pane Header 아래에서 Sidebar·구분자·둥근 외곽 경계 안에 유지됨 |
| 1152×768, 호스트 이전 Contents와 오른쪽 pane의 오른쪽 도킹 DevTools | `f18-host-migration-split-devtools-real-app.jpeg` | 안정된 열림·닫힘 픽셀 통과: 중첩 Contents와 직접 DevTools 분기가 소유 pane만 분할했다. DevTools를 닫으면 충돌·경계 이탈 없이 Contents가 pane 전체로 복원됨 |
| 768×875, 두 개발 브라우저 창의 WebUI Omnibox popup과 반복 활성화 전환 | `macos-real-app-omnibox-multi-window-visibility-20260903.jpeg` | F26 수정 후 통과: popup 열기·비활성화 시 닫기·창 복귀 후 입력 재개 확인했다. 네 차례 창 전환에서 응답을 유지하고 로그에 fatal·`ValidatePopupState`가 없으며 새 macOS 충돌 보고서가 발생하지 않음 |

전체 화면 결과는 위의 최종 재현 프레임과 종료·재진입 왕복 캡처를 기준으로 판단했다.

## 보존한 진단 자료

- Sidebar 수정 전 녹화와 Toolbar 겹침 중간 프레임은 F23 실패 근거다.
  수정 후 녹화, 네이티브 타임스탬프 contact sheet와 세 중간 프레임은 수정 결과의 근거다.
- 최소 폭 Find Bar의 수정 전 LTR·RTL 캡처와 수정 후 캡처는 F10 회귀 비교 근거다.
- 전체 화면의 최종 재현·왕복 캡처는 안정된 상태의 근거다.
  중복 contact sheet, 초기 타이밍 캡처와 InfoBar 선택기 캡처는 정리했다.

## 당시 자동 회귀 검증

- `YeeSurfaceGeometryTest.*`: 10/10 통과
- 보완한 빠른 검증은 `yee_layout_unittests`의
  `BrowserViewTabbedLayoutNativeGeometryTest.*`, `YeeSurfaceGeometryTest.*`와
  `multi_contents_geometry_unittests`의 Surface 전환·다중 콘텐츠·viewport·이전
  테스트를 실행해 40/40 통과했다. 행 임계값, 두 패널 유형, 수평·수직·탭 없음,
  제외 조건, 폭 배분, 구분자, 확정된 Header의 상단 자식 계산, reveal·전환 반올림,
  네 가장자리의 패널 clip, 분할 inset과 underlap, 제품 기하 계약을 확인했다.
- F13/F14 적용 레이아웃 필터: 기본 LTR 8/8, 강제 RTL·DSF 1.25 8/8 통과,
  재시도는 0회였다. planner 결과를 top-container·panel·animation-content·배경·그림자·
  MCV·분할 inset·clip의 실제 배치와 비교했다. 서로 다른 BrowserView 소유 분할
  inset을 주입해 대상 애니메이션의 콘텐츠 경계 보존을 확인했고,
  새 계획을 사용하는 애니메이션 전환도 3회 연속 통과했다.
- `SidePanelCoordinatorTest.ShowFromAnimationReparentsContentView`: 별도 브라우저
  수준 검증에서 1/1 통과
- `SelectionOverlayBrowserTest.SelectionUsedFromController`,
  `SelectionStaysScopedThroughSplitLifecycle`: 조상 관계 가정을 최저 공통 조상의
  z-order 비교로 바꾼 뒤 재시도 없이 2/2 통과했다. 두 번째 테스트는 Toolbar 버튼 없이
  실제 controller를 열고 분할 포커스 변경·인접 pane 닫기·overlay 닫기를 통해
  pane 경계와 Contents 입력 복원을 확인했다. 전체 Side Panel/Lens/Glic 검증은 5/5 통과
- `VerticalTabsSinglePaneCollapse`, `VerticalTabsSinglePaneExpand`,
  `VerticalTabsSplitViewCollapse`, `VerticalTabsSplitViewExpand`:
  상호작용 UI 테스트 4/4 통과
- `YeeHeaderSnapshotInvalidatesToolbarChildLayout`와 선택한 단일 pane 접힘·펼침:
  F23 생명주기 수정 후 3/3 통과
- `YeeHardClipOwnersMatchAtDeviceScale`: 강제 DSF 1.0/1.25/1.5/2.0에서 각각 첫 시도
  통과했다. 1.25 실행은 1500×1708 compositor 표면을 읽어 단일·비어 있지 않은
  애니메이션 대상·활성 분할·비활성 분할의 clip 소유를 시각적 허용 오차 없이 확인했다.
- `NativePlannerMatchesAppliedAnimationFramesAndReversal`: 재시도 없는 전체
  상호작용 검증 23/23 안에서 통과했다. 정확한 계획·적용 경계뿐 아니라 Side Panel의
  제어된 7상태에서 3개의 물리적 Surface 여백을 읽었다. 21표본 모두 Chromium
  Toolbar보다 제품 셸 재질에 가까웠다. Surface·Header의 상단과 Surface 하단도
  닫힌 기준과 같아야 하므로 흰 띠와 이중 외곽 여백 회귀를 직접 확인했다.
- `YeeFindBarStaysInsideActiveSplitPaneAtMinimumWidth`: 재시도 없이 800×600에서
  통과했다. LTR 활성 pane과 Find Bar가 모두 265 DIP이며 보이는 자식이 Widget 안에
  유지되었다. `YeeFindBarUsesPixelStableBoundsAtMinimumWidth`는 RTL·DSF 1.25에서
  remote Cocoa와 같은 픽셀 반올림·DIP 내림 변환 후 실제 Widget 크기를 확인해
  반복 5/5 통과했다. 해당 전체 상호작용 검증은 재시도 없이 22/22 통과했다.
- `YeeStatusBubblesStayInsideOwningSplitPane`: 기본 LTR과 강제 RTL·DSF 1.25에서
  재시도 없이 통과했다. 800×600의 활성·비활성 pane 모두 일반 표시, 반대쪽 마우스
  회피, 마우스 이탈 복원과 전체 pane 폭 popup이 소유 `ContentsWebView`의
  화면 사각형 안에 유지되었다.
- `YeeAiOverlayStaysInsideOwningSplitPane`: 기본 LTR과 강제 RTL·DSF 1.25에서
  재시도 없이 통과했다. 오른쪽 도킹 DevTools가 있는 각 pane에 800×600 선호 크기의
  overlay를 넣고 좌우·상하 분할 및 RTL 원점에서 정확한 물리적 포함과 크기 제한을 확인했다.
- `YeeTabModalDialogHostStaysInsideOwningSplitPane`: 기본 LTR과 강제 RTL·DSF 1.25에서
  재시도 없이 통과했다. 단일 pane에서 Chromium 원래 위치·최대 크기 공식을 확인한 뒤,
  두 분할 방향의 양쪽 호스트가 작은 dialog를 페이지 body 경계에 중앙 정렬하고
  최대 dialog 사각형도 소유 pane 카드 안에 유지하는지 확인했다.
- `YeeShellWithoutVerticalTabsUsesNativeToolbar`: 기본 LTR과 강제 RTL·DSF 1.25에서
  재시도 없이 통과했다. 두 세로 탭 기능을 끈 상태에서 실제 Location Bar, 네이티브
  분할 inset과 구분자 투명도를 유지하고 제품 mini-toolbar 제어를 표시하지 않았다.
- `YeePopupWindowUiTest.PaneHeadersRetainNativeControls`: 재시도 없이 통과
  POPUP에는 세로 탭 controller가 없고 두 pane의 mini-toolbar에 제품 탐색·Sidebar
  제어가 나타나지 않았다.
- F24 접힘·펼침 네 흐름은 단일·분할 전환 전후마다 브라우저 context의 보이는
  `kVerticalTabStripCollapseButtonElementId`가 정확히 하나인지 직접 셌다.
- F3/F5/F20 구조 장식: 새 resolver 유닛 1/1, 순수 Surface 기하 14/14,
  적용 상호작용 17/17, Side Panel/Lens/Glic 브라우저 검증 5/5 통과했다. 재시도를
  모두 끄고 outline 경계·표시, 단일 구분자 소유, 첫 InfoBar의 상단 모서리만 둥글게
  처리, 수평 네이티브 그림자 포함과 Side Panel의 동적 z-order 복원을 확인했다.
- F6 소유 경계는 역방향 계층 불변 조건도 확인했다. 직접 container, PageTargetHost,
  ViewportOverlayHost의 실제 자식 집합이 NTP footer의 직접 구분자까지 포함한
  migration ledger와 정확히 같았다. 강화한 적용 테스트 1/1, 분할 Contents·도킹
  DevTools 캡처와 Lens/Glic 생명주기 5/5가 당시 F6의 기록된 차단 항목을 해소했다.
  F18의 남은 macOS 자동 검증은 아래에 기록하고, 미지원 플랫폼·실제 기능 상태는 남겼다.
- F11 전체 화면·immersive: 순수 전환·기하 18개와 적용 상호작용 18/18 통과,
  재시도는 없었다. `YeeFullscreenPreservesSurfaceWithoutStaleChromeRows`는 양방향 전환,
  보류 epoch의 확정 source 기하, 대상 outset 즉시 취소, 의도적으로 재도입한 늦은
  대상 거부, 숨긴 Header의 6 DIP·모든 모서리 기하, 실제 `NativeViewHost`·layer clip과
  Toolbar 항상 표시 모드의 안정된 복원을 확인했다.
- F18 macOS 자동 검증: 적용 상호작용 19/19, 확장 브라우저 생명주기 10/10 통과,
  재시도는 없었다. `YeeAiOverlayStaysInsideOwningSplitPane`는 같은 ID의 AI View 두 개를
  찾아 활성 pane에 따른 controller 선택을 확인했다.
  `YeePageHostsPreserveSplitFocusAndContainerReuse`는 두 Contents·Pane Header·구분자의
  정방향·역방향 포커스 순서, 구조 호스트 세 개의 포커스 제외와 분할 재생성 시
  캐시 container 재사용을 확인했다. 브라우저 검증은 기존 Side Panel/Lens/Glic에
  더해 렌더러 종료 후 Sad Tab 이동, 응답 없는 렌더러 후 Read Anything 재생성,
  비활성 pane 활성화, 분할 탭 닫기와 동기적 창 종료를 다뤘다.
  NTP Omnibox 전환 입력을 결정적인 `about:blank` 탭으로 바꾼 뒤 새 포커스·재사용
  항목도 5/5 연속 통과했다.
- F26 Omnibox 상태 동기화: `HiddenWidgetClearsClassicPopupState` 1/1,
  `MultiWindowActivationRestartsAutocompleteWithoutStaleState` 반복 5/5,
  `YeeOmniboxPopupFollowsSingleAndSplitHeader` 2/2 통과, 재시도 없음
  실제 Widget 직접 숨기기, 두 Browser 창의 활성화 이전, 복귀 후 자동완성 재개와
  단일·분할 Pane Header 사이의 popup 이동을 확인했다. 해당 전체 Surface 상호작용
  검증은 22/22 통과했다.
- F8/F9/F10 통합 필터: 기본 LTR 4/4, 강제 RTL·DSF 1.25 4/4 통과, 재시도 없음
- 당시 소스 반영 후 실제 Ninja로 개발 브라우저·Framework, `interactive_ui_tests`와
  좁은 `yee_header_unittests` 타깃을 빌드했다. Siso는 실제 산출물 없이 가상 빌드
  로그를 갱신할 수 있어 Ninja dry run을 빌드 근거로 사용하지 않았다.
- `tools/dev/test-run-preflight.sh`: 통과

## 복합 전환 회귀 체크포인트 — 2026-09-05

- Side Panel·Sidebar 결합 테스트가 F11 보호 조건의 누락을 재현했다.
  보류된 전체 화면 epoch가 대상 경계를 한 번 지워도 이후 패널·Sidebar 배치에서
  다시 생성할 수 있었다. 두 대상 생성 경로 모두 안정된 epoch를 요구하도록 바꿨다.
  Chromium의 기능 비활성화 동작은 유지했다.
- `YeeCombinedPanelSidebarTransitionsKeepViewportContained`: 단일·분할 pane,
  독립 애니메이션 시계, 역전, 패널 정렬, 창 크기 변경, 한 애니메이션의 먼저 종료와
  보류·반전된 전체 화면 epoch를 확인했다. 모든 표본 프레임에서 viewport 포함과
  네이티브 clip·PageTargetHost·ViewportOverlayHost의 관계를 검사했다.
- `YeeSplitPanelDevToolsNoticeKeepOwningPane`: Side Panel, 두 분할 방향,
  세 DevTools 크기 변경 전략과 InfoBar를 결합했다. 분할 역전과 활성 탭 변경에서
  clip과 알림 소유를 확인했다. 이는 적용 기하 검증이며 실제 DevTools frontend
  검증은 아니다.
- 일반 `interactive` 단계에 위 두 항목과 이전에 빠진 Header snapshot·hard clip·
  Sidebar 전환 여섯 항목을 포함했다. 이 여덟 항목을 RTL·DSF 1.25에서도 재시도 없이 실행했다.
- 전체 화면 항목은 실제 레이아웃 소비 코드에 전환 epoch를 주입한다.
  네이티브 OS callback 타이밍이나 전환 중 픽셀까지 증명하지 않는다.
- 당시 검증: 순수 기하 40/40, 재빌드한 상호작용 31/31, 이어서 RTL·DSF 1.25
  8/8 통과, 모두 재시도는 없었다. `build.sh`는 성공했다. 수정 전 첫 실행에서는 기존
  `NativePlannerMatchesAppliedAnimationFramesAndReversal`의 Header 전경 픽셀이
  간헐적으로 실패했다. 해당 assertion 변경 없이 단독 재실행과 최종 전체 단계는
  통과했지만 간헐적 실패의 원인은 아직 밝혀지지 않았다.
- 정상 종료·새 실행으로 재빌드한 개발 브라우저에서 `about:blank`와 로컬 테마 입력을
  열었다. Computer Use가 앱을 발견했지만 `cgWindowNotFound`로 창에 접근하지 못했다.
  따라서 이 체크포인트의 새 수동 시각 검증 통과는 주장하지 않는다.

## 당시 남은 검증 항목

- F5/F20 최종 픽셀: 2026-09-03 PDF 검증은 실제 앱에서 Header를 숨겼을 때
  InfoBar의 상단 모서리와 수평 ContentShadow를 확인했다. Side Panel 전환 중간
  여백은 제어된 compositor 읽기로 확인했으며 View 경계나 로딩 화면에서 추정하지 않았다.
- 녹화한 Sidebar 외의 전환 픽셀: 수정 빌드의 네이티브 타임스탬프 근거가 F23
  Sidebar 접힘·펼침 픽셀 항목을 해소했다. 안정된 macOS 전체 화면, 종료·재진입
  기하와 Header를 숨긴 정확한 PDF InfoBar도 Computer Use로 확인했다.
  제어된 전체 화면 전환 프레임은 미검증이며, 동작 후 안정된 캡처를 전환 중
  근거로 사용하지 않는다. Side Panel 프레임은 위의 제어된 프로세스 내 읽기로 확인했다.
- Side Panel 전환·분할 픽셀: 안정된 숨김·열림의 LTR/RTL과 실제 분할·패널 상태는
  OS 캡처가 있다. 제어된 닫힘·열림·중간·정렬·닫기·역전 프레임은 노출된 세
  Surface 여백의 실제 compositor 픽셀로 확인했다. 잠긴 디스플레이 세션에서
  검게 캡처되어 수정 후 새 OS 캡처는 얻지 못했다. 제어된 픽셀 assertion과
  캡처 도구의 한계를 구분한다.
- 플랫폼별 소수 배율 픽셀: F22는 macOS DSF 1.0/1.25/1.5/2.0의 정확한
  compositor 읽기를 확보했다. 정규화된 이전 OS 이미지는 그 증거로 사용하지 않는다.
  Windows 125/150/200%와 Linux 200% 실앱 실행은 이 macOS 환경에서 수행하지 못했다.
- Status Bubble: 양쪽 pane의 LTR/RTL·DSF 1.25 popup 기하는 자동 검증했다.
  당시 데스크톱 도구에는 hover·마우스 이동 동작이 없어 실제 hover 픽셀과
  포인터 전환은 남았다. 기하 테스트로 실제 픽셀 결과를 추정하지 않는다.
- AI overlay·탭 모달: 두 분할 방향과 방향·배율 조합의 pane 호스트 기하는 자동
  검증했다. 네이티브 before-unload의 표시·Cancel 경로는 실제 확인했지만 모달만
  담긴 캡처로 아래 pane 기준의 배치를 증명할 수 없다. 실제 AI WebUI 제어와
  pane 기준 탭 모달 픽셀은 남았다.
- 예약 Sidebar 슬롯: Pins·Bookmarks·Chat·Agent 활성화는 `AGENTS.md`의 제품 범위
  결정이므로 검증 과정에서 임의로 켜지 않았다.
- 특수 페이지 overlay·플랫폼 조합: Glic·Lens·DevTools·Read Anything·Sad Tab·분할
  닫기·창 종료는 가능한 macOS controller·생명주기 항목을 통과했다.
  실제 Data Protection·Indigo·Actor 상태와 Windows/Linux 네이티브 호스트·
  compositor 검증에는 해당 기능과 플랫폼 환경이 필요하다.
- 당시 독립 기능·커밋 준비 검토는 최종 소스·테스트·패치·문서·증거 검사를 통과했다.
  실앱 픽셀 캡처 조합을 독립적으로 재현한 것은 아니다. 캡처는 구현자가 확보한
  근거로 유지하며 발견 항목은 `built` 또는 `open` 상태다.
  자체 판단만으로 `verified`로 승격하지 않는다.
