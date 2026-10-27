# Browser Surface Header regression coverage

이 문서는 Header와 Omnibox의 사용자 가시 계약을 자동 테스트와 실제 화면 검수로
나눈다. 테스트 이름은 구현 함수가 아니라 보장하는 결과를 기준으로 둔다.

표면 구조는 `browser_surface_layout`의 `BrowserSurfaceMode`와
`ResolvedBrowserSurfaceDecoration`이 결정한다. 모드는 Chromium에 실제 적용된
탭 배치에서 읽으며 전체화면의 일시적인 표시 여부와 구분한다. 일반 Header,
본문 Outline, Side Panel과 별도 Widget의 전체화면 바탕은 같은 결과를 직접
적용한다. 렌더러가 조상·형제 View를 찾아 스타일을 다시 결정하지 않는다.
macOS 호스트는 좌표와 수명을 관리하고, 색상 callback은 마지막으로 적용된
구조를 유지한다. Split 진입 직후의 임시 그리기 억제는 다음 레이아웃의
resolved 결과로 확정한다.

수평 분할 탭은 한 쌍의 캡슐로 표시하고 독립된 Pane Header 위에 시각적인
간격을 남긴다. 기본 native hit target은 탭 행 아래까지 유지한다. 기존 수평
통합 사례가 좁은 폭·전체화면·포커스 pane 전환에서도 이 계약을 확인한다.
좁은 분할 탭은 파비콘·상태 아이콘을 우선하고 포커스되지 않은 닫기 버튼을
숨긴다. 실제 아이콘의 16 DIP 윤곽이 캡슐 안에 온전히 들어오는지도 확인한다.
`--yee-split-tab-capture-dir`는 관련 밝게/어둡게·활성/비활성·hover·키보드
focus 화면만 별도로 캡처하며 일반 실행에는 추가 입력을 발생시키지 않는다.

수평 그룹은 이름과 모든 멤버를 하나의 둥근 배경으로 묶는다. 평상시 멤버와
펼친 이름의 개별 채움은 생략하고, 선택된 일반·분할 탭은 공통 배경 안에
표시한다. 그룹 바깥 구분선에는 간격을 두며 안쪽 구분선은 기존 위치를 쓴다.
공통 배경은 입력 대상에서 제외하고 선택·hover·키보드 포커스와 접힌 그룹의
파비콘 요약은 native 경로를 유지한다.

기존 수평 통합 사례는 별도 기능 플래그 없이 기본·RTL125%·통합 탭바에서
이 표현이 적용됨을 확인한다. 실제 배율의 raster로 배경의 라운드, 모든
멤버·이름의 포함 범위와 제목 대비, 앞뒤 경계의 간격을 검사한다. 일반 창과
전체화면의 Pane Header 위 간격, 실제 그리기와 독립된 native hit target도
함께 확인한다. `--yee-group-marker-capture-dir`는 배치·레이어를 바꾸지 않고
현재 그룹 화면을 캡처한다.

이미지 테마와 접근성 팔레트에서는 그룹과 멤버가 함께 native 표시로 돌아간다.
`HorizontalGroupsAndMembersShareNativeThemeFallback`은 공통 테마 조건을 검사한다.
같은 수평 통합 사례는 대비 설정 왕복만으로 그룹의 위치·그리기 순서와 focus
색 override가 갱신됨을 확인한다. 포커스된 탭의 배치 전환에서는 종료 중인
탭의 늦은 blur가 정리된 노드에 접근하지 않는지도 확인한다.

## 1. 색상과 geometry

`test-header.sh unit`은 Header 전용 `yee_header_unittests` 실행 파일만 빌드해
확인한다. Chromium 전체 `unit_tests` 빌드는 광범위 최종 게이트에서만 사용한다.
모드를 생략하거나 `--no-build`만 주면 이 단위 검사만 실행한다. geometry·색상과
직접 callback 검사는 활성화 없이 Widget을 표시하거나 숨긴 채 확인한다.

| 계약 | 자동 검증 |
| --- | --- |
| origin·title 표면과 AA 배경이 Rest→Hover→Rest의 실제 주소줄 색과 일치 | `YeeRestingTextViewTest.RestingTextSharesHoveredControlSurface` |
| 미디어 구분선이 밝고 어두운 페이지색을 따르고 native 팔레트로 복귀 | `YeeRestingTextViewTest.ControlDividerTracksSurfaceAndNativeReset` |
| 각 `WebContents`가 하나의 source identity와 transition timeline만 소유하고 Tab 재활성화는 현재 화면색에서 시작 | `BrowserSurfaceColorControllerTest.WebContentsOwnsOneStablePresentationSource`, `TabActivationTransitionsFromTheCurrentlyPresentedColor` |
| 각 persistent content container가 현재 `WebContents` source 하나만 전달하고 detach 시 presentation을 즉시 비움 | `BrowserSurfaceColorControllerTest.ContainerBindingPublishesOnlyItsCurrentWebContentsSource`, `ContainerBindingClearsPresentationWhenContentsDetach` |
| 테마 변경은 미확정 fallback을 갱신하고 page 재검증을 시작하되 확정된 page 색은 검증 전까지 보존 | `BrowserSurfaceColorControllerTest.ThemeChangesRevalidatePagesWithoutDiscardingTheirColor` |
| 첫 유효 paint와 load 완료 뒤 bounded settling, 진행 중 전환의 연속 retarget | `BrowserSurfaceColorControllerTest.FirstPaintAndLoadCompletionStartBoundedSettlingSamples`, `RetargetingStartsFromTheCurrentlyPresentedColor` |
| 연속 scroll 입력은 진행 중 epoch·capture·안정 후보를 보존하면서 관찰 window만 연장하고, 실제 timeout 후 page sampling 모드로 돌아가 반복·timeout timer를 모두 정리 | `BrowserSurfaceColorControllerTest.RepeatedScrollSignalsPreserveTheActiveSamplingEpoch`, `ScrollSamplingTimeoutReturnsToPageSamplingMode` |
| 두 Pane Header가 각자 `WebContents` 색을 독립적으로 유지 | `BrowserSurfaceColorControllerTest.SplitPaneSourcesKeepIndependentSurfaceColors` |
| navigation 이전의 늦은 surface capture가 같은 Tab의 새 sampling 상태를 오염시키지 않고, 현재 epoch의 안정된 실제 capture 두 회는 page presentation을 publish | `BrowserSurfaceColorControllerTest.IgnoresLateCaptureFromPreviousSamplingEpochOnSameWebContents`, `StableCurrentCapturePublishesPagePresentation` |
| popup provider는 animation frame마다 바뀌지 않고 최신 transition 완료에서만 한 번 교체 | `BrowserSurfaceColorControllerTest.CommitStartingTransitionDoesNotRefreshPopupUntilCompletion`, `RetargetedTransitionRefreshesPopupOnceForLatestTarget`, `ImmediateCommitRefreshesPopupExactlyOnce` |
| Header 텍스트·버튼·focus stroke가 전체 gray 및 대표 chromatic surface에서 같은 대비 계약을 사용하고 실제 Label이 물리색을 유지 | `BrowserSurfacePresentationResolverTest.ExhaustiveGrayAndChromaticContrastContract`, `YeeRestingTextViewTest.WidgetAttachedLabelsUseExactSurfaceAndPhysicalForeground` |
| 일반적인 밝은·어두운 페이지에서 primary와 secondary의 강약을 유지하면서 두 역할 모두 읽을 수 있는 대비를 보장 | `YeeSurfaceColorTest.HeaderRolesRemainReadableOnLightAndDarkPages` |
| popup은 exact surface를 쓰되 high contrast/forced colors에서는 native mixer를 보존하고 global provider cache를 늘리지 않음 | `YeeSurfaceColorTest.PopupProvidersAreExactAndDoNotGrowGlobalCache` |
| 단일·분할 Header가 42 DIP, 주소 표면 34 DIP, 공통 중심축과 edge inset을 공유 | `YeeSurfaceGeometryTest.SplitAndSingleHeadersShareMetricsContract` |
| LTR·RTL에서 고정·hover Header의 새 탭·Agent 버튼은 창 드래그 영역에서 제외 | `YeeNativePresentationTest.SidebarHeaderButtonsStayOutsideWindowCaption` |
| 빈 주소 힌트·선택색·Side Panel 바탕·배너 링크 기준선·제품 로고와 favicon 테마가 실제 native View에서 유지 | `YeeNativePresentationTest.*`, `YeeTabIconPresentationTest.*` |

## 2. 실제 native View 구조

`SiteControlsBrowserTest.ProtectionButtonClosesOnSecondNativeClick`는 수평·수직
탭 배치에서 실제 마우스로 방패 버튼을 연속 클릭해 열기→닫기→열기→닫기를 확인한다.
버튼을 누르는 동안 팝업이 포커스를 잃고 닫혀도 손을 뗄 때 다시 열리지 않아야 한다.
2026-10-26 Windows 실제 입력 검증에서 두 배치의 총 8회 클릭 assertion을 완료했다.
초기 실행은 아래 공통 COM `ghost_count` 종료 오류로 실패했다. 후속 수정 결과는
아래 접근성 종료 검증 기록을 따른다.

`SiteControlsBrowserTest.PageInfoAndProtectionUseSeparatePopups`는 수평·수직 탭
배치에서 방패 버튼이 탭 없는 Protection 팝업을 열고, 사이트 정보 action은 별도의
Chromium Page Info bubble을 여는지 확인한다. `PageInfoOpensNativeSiteSettings`는
그 native 팝업에서 현재 사이트의 설정 화면으로 이동하는 경로를 확인한다.
`--yee-site-controls-capture-dir`를 지정하면 두 배치의 각 팝업 PNG를 저장한다.
Windows 캡처 실행에는 `--enable-pixel-output-in-tests`도 전달한다. 2026-10-26
Windows 검증에서 앱·검사 실행 파일 빌드와 실제 팝업 캡처를 완료했다. 관련 8개
사례의 기능 assertion은 완료됐지만, 모든 사례가 종료 시 공통 접근성 검사
`content_test_suite_base.cc`의 COM `ghost_count` 오류로 실패했다. 전체 통과로
기록하지 않았다. 후속 수정 결과는 아래 접근성 종료 검증 기록을 따른다.

같은 날 후속 진단에서 방패 팝업을 열지 않는
`InternalURLsBrowserTest.ProductAndNativeSettingsRoutes`도 같은 종료 오류를 재현했다.
접근성 노드의 소유자 연결은 모두 해제됐지만 Windows UI Automation의 외부 COM
참조가 남아 있었다.
창을 명시적으로 닫은 진단 실행에서는 `ghost_count`가 44에서, 메시지를 처리하며
5초 기다린 뒤 3으로 줄었다. `UiaDisconnectRootProviders`를 활성화한 별도 실행도
44→3으로 같았다. 일부는 비동기 해제 지연이며, 나머지 참조가 영구 누수인지와
안전한 수정 위치는 당시 확정하지 못했다. 플랫폼 소멸자에서 전체 연결을 해제하거나
개별 요소를 해제하는 초기 실험은 오류를 해결하지 못해 모두 되돌렸다.

2026-10-26 후속 진단에서는 두 기존 검사의 종료 참조 45개를 재현했고, Yee 화면을
끈 기본 Chromium 화면에서도 같은 문제가 발생했다. 창을 닫고 10초 동안 메시지를
처리하거나 `UiaDisconnectRootProviders`만 켜는 비교 실행에서도 43개가 남았다.
반면 UI 스레드에서 전체 UIA 연결을 해제하면 마지막 창이 먼저 닫힌 경로까지 0개가 됐다.

Windows의 실제 앱 종료를 소유하는 `HandleAppExitingForPlatform()`에서 Widget을
정리하기 전에 기존 `AXPlatform::DisableActiveUiaProvider()`를 호출한다. 새 UIA
요청을 먼저 차단한 뒤 연결을 해제해 재진입 시 같은 공급자를 다시 반환하지 않는다.
일반 창 닫기나 취소 가능한 종료 시도에는 이 처리를 적용하지 않는다.
[Microsoft의 종료 전 UIA 자원 정리 지침](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiadisconnectallproviders)을
따르며 접근성 기능·COM 누수 검사는 유지한다.

`UiaApplicationExitBrowserTest.*`는 별도 프로세스의 실제 UIA 클라이언트로 세 경로를
확인한다. 다른 창을 닫아도 남은 창 조회가 가능해야 하며, 앱 종료와 마지막 창이 먼저
닫힌 종료는 클라이언트가 참조를 보유한 상태에서도 모든 접근성 노드가 0개가 돼야 한다.
수정 호출을 제거한 대조 빌드는 종료 검사 두 개에서 각각 41개·2개 참조가 남아
실패했고, 수정본은 세 경로를 통과했다. 같은 수정본의 관련 실행 35회 중 34회가
통과했으며 모든 실행에서 COM 종료 오류가 사라졌다. 서비스워커 캐시 응답 사례
하나는 기대한 `allowed` 대신 `blocked`로 실패했다. 이 검사 실패는 종료 수정을
뺀 비교 빌드에서도 재현됐으므로 접근성 수정과 구분하고 해당 회차를 전체 suite
통과로 기록하지 않았다.

후속 캐시 검사 진단에서 이 실패는 서비스워커 캐시를 허용해야 한다는 옛 기대값이
현재의 응답 전 차단 정책과 충돌한 것으로 확인됐다. 검사를
`ServiceWorkerCacheCannotBypassReenabledProtection`으로 바꾸고 캐시 준비·보호 켬·
사이트 예외 복구·캐시 삭제 후 서버 도달을 검증한다. 저장된 본문과 서버 요청 횟수를
함께 확인하며 제품의 차단 동작을 완화하지 않는다. 상세 계약은
[서비스 워커 연결](../content-blocking-checkpoint.md#실시간-연결서비스-워커-후속-연결)을 따른다.
수정 후 같은 관련 실행 35회가 모두 종료 단계까지 통과했다. 서비스워커·HTTP 캐시,
사이트 제어·내부 페이지와 외부 UIA 클라이언트의 접근성 종료 검사를 포함한다.

`test-header.sh interactive`가 실제 Yee 창과 Chromium Omnibox를 함께 사용한다.
렌더러 경계색·presentation binding 검사는 RTL + DSF 1.25에서도 실행한다.
단위·GUI 실행 모두 launcher retry를 0으로 고정한다.
실제 View parent·renderer·popup·입력 전달 계약은 순수 geometry 검사로 대체하지
않는다. `interactive`와 명시적 `all`에서는 필요한 창 활성화를 유지한다.

| 계약 | 자동 검증 |
| --- | --- |
| native LocationBar 하나가 단일 Toolbar와 active Pane Header 사이를 이동해도 높이·중심축을 유지하고 분할 해제 시 원래 parent로 복귀 | `YeeSingleAndSplitHeadersShareLocationBarGeometry` |
| 미디어 버튼이 흰색·검정·빨강 Header의 일반/disabled 색과 크기를 따르고, 표시 여부에 따라 구분선이 함께 사라지며 실제 Omnibox hover에서 origin·title 배경이 일치 | `YeeMediaButtonTracksHeaderAndVisibility` |
| 단일 A → 분할 active A/inactive B → active B → popup B → 분할 해제 B 전환에서 주소 배경, 일반·disabled 버튼, IME inline·rich autocomplete·AI hint label, Pane Header 텍스트, location/security icon이 같은 source snapshot을 사용하고, 열린 popup은 animation의 최종 `popup_revision`에서만 한 번 갱신되며 native 모드에서 모든 물리색 override를 제거 | `YeePresentationFollowsSingleAndSplitSources` |
| active pane 전환과 분할 해제에서 native LocationBar가 최종 parent로 이동하고 실제 host 변경 generation마다 layout/bounds 동기화를 정확히 한 번 수행 | `YeePresentationFollowsSingleAndSplitSources` |
| 분할 생성·제거 중 Chromium의 focus 정책이 popup을 닫는 동작은 우회하지 않고, popup 색·anchor는 단일과 안정화된 각 분할 상태에서 별도로 검증 | `YeeOmniboxPopupFollowsSingleAndSplitHeader` |
| inactive Pane Header 클릭이 pane 활성화와 native 주소 편집을 한 번에 수행 | `YeeInactivePaneHeaderActivatesAddressEditing` |
| Omnibox popup이 단일·양쪽 active pane·분할 해제 후 모두 주소 표면의 좌우 폭과 상단에 연결되고 pane 전체 폭으로 확장되지 않음 | `YeeOmniboxPopupFollowsSingleAndSplitHeader` |
| 밝은·파란 실제 페이지의 Views·WebUI 제안 두 행에서 구분선 아래·목록 하단 8 DIP, 결과 강조 좌우 8 DIP와 네 모서리 8 DIP, pointer hover와 방향키 선택·Escape 닫기를 확인 | `YeeOmniboxSuggestionsHaveBalancedInsets` (`--yee-omnibox-popup-capture-dir`로 실제 창·팝업 PNG 저장 가능) |
| WebUI 제안은 강제 색 없는 대비 증가, 강제 색의 custom·less에서 선택 표시를 유지하고 일반 대비에서는 Yee 행 강조를 사용 | 같은 실제 팝업의 renderer에 media emulation을 적용하고 실제 media query와 선택 행의 계산된 표시를 검사. Windows 대비 설정 전파 검증과는 별도 |
| macOS 실제 전체화면에서 Toolbar·본문 경계와 Sidebar Header 축, 메뉴막대·창 버튼 노출 시 주소줄·바탕의 잘림과 복귀, Header·고정 Sidebar의 실제 합성색 일치, 고정·접힌 Sidebar의 native 창 가림 방지·본문 배분 유지, 항상 표시/숨김·고정/접힘, Flyout 고정 중 Header 행·목록 연속성, 설정을 보존한 분할 전환·비활성 renderer 크기·주소창 실제 키 입력, 해제 시 레이어 복귀 | `ImmersiveModeControllerMacInteractiveTest.ToggleFullscreenWithVerticalTabstrip` (`test-browser-surface-layout.sh interactive` 소유) |
| macOS 수평 전체화면에서 별도 Widget으로 이동한 Header가 명시적으로 적용된 구조를 유지하고, 색상 갱신에도 clip·곡률이 보존되며, 추가 상단 카드 윤곽선을 그리지 않음 | `BrowserViewTabbedLayoutImplUiTest.YeeHorizontalTabsPreserveRowsColorsAndTabIdentity`의 실제 전체화면·Side Panel 조합과 투명 raster 검사 |

`BrowserNativeWidgetMacGlassTest.FullscreenWindowIsOpaque`의 선택적
`--yee-fullscreen-capture-dir`는 수평 탭바와 주소줄을 함께 호스팅하는 실제
AppKit 창을 캡처한다. 이동 전 overlay Widget의 오프스크린 NSWindow 대신
이동된 NSView의 현재 창을 사용하므로 두 영역 사이의 접합도 검수할 수 있다.
일반 실행에서는 캡처나 추가 입력을 수행하지 않는다. 그룹 클릭 검사는 모델의
접기·펼치기 완료를, 전체화면 검사는 native Header의 재배치 완료를 기다린다.

## 3. 실제 화면에서만 확인할 것

`--yee-media-header-capture-dir`는 미디어·주소줄의 Rest/Hover를 흰색·검정·빨강
페이지에서 선택적으로 캡처한다. 이 색상 경로에는 OS 분기가 없으며 macOS의
native glass와 Windows의 불투명 frame 차이는 별도 계약이다. macOS에서도
실제 창 검증 전에는 재현 여부가 확인된 것으로 기록하지 않는다.
Windows 캡처는 `--enable-pixel-output-in-tests --disable-gpu`로 실제 화면 출력을
켜고, `--single-process-tests --test-launcher-preserve-desktop-windows`를 사용해
각 exact case를 개별 프로세스로 실행한다. 출력 옵션 없는 검정 frame은 검수
증거로 사용하지 않는다.

Footer 회귀 검사는 외부 UIA 조회와 닫힌 요소의 무효화를 확인하고 접근성
클라이언트를 UI 스레드 종료 전에 정리한다. 기존 COM 누수 검사는 유지한다.

WebUI balanced-insets 검사는 DOM 결과뿐 아니라 native WebView의 자동 크기 조절과
실제 frame 표시를 기다린 뒤 hover를 보낸다. DOM 결과 교체 중에는 행이 다시
준비될 때까지 기다리며, 캡처 옵션 없이도 두 split 방향의 검증을 수행한다.

2026-10-27 Windows 후속 검수에서는 동일한 최종 검사 빌드로 40개 exact case를
각각 새 프로세스와 별도 프로필에서 실행해 모두 종료 단계까지 통과했다.
미디어 표시·실제 재생 4개, WebUI·Views 제안 4개, AI·맞춤설정 팝업 2개,
창 버튼 8개, 분할·포커스·탭 상태 13개, 수평 탭·테마·경계 9개다.
기본·RTL 125%·통합 탭바를 포함한다. 별도로 통과한 단위 106개와 도구 검사
16개는 이 실제 창 검사 수에 합산하지 않는다.

제품 실행 파일과 `chrome.dll`은 `95f3e45` 검수본과 동일하며 검사 코드만 보완했다.
WebUI 대비 행렬은 Blink의 색상 캐시도 갱신하는 정식 media emulation을 사용하고,
실제 선택 행의 Down·Up 결과를 확인한다. 맞춤설정은 로컬 페이지의 탐색을 완료한
뒤 실제 WebView와 활성 select를 제한 시간 안에서 확인하며 기존 6회의 native
팝업 위치·Escape 검증을 유지한다. Rest 표면·구분선의 픽셀 검사 전에는 실제
커서를 본문으로 옮기고 필요한 hover 종료를 기다린다. 모니터 밖 창 버튼 여백은
논리 hit-test만 검증한다.

대비 행렬은 renderer의 CSS 반응 검증이며 Windows 자체 대비 설정 전파나
화면 읽기 프로그램 동작을 검증한 결과는 아니다. 이번 실행은 Windows의
software rendering 경로이며 macOS·Linux 실기 결과를 의미하지 않는다.
완료된 검수의 일회성 프로필·로그·캡처·보고서는 보관하지 않는다. 재검증에는
저장소의 검사 코드와 위 실행 조건을 사용한다.

테마 보강의 자동 검사와 빌드/실행 범위는
[`theming-structural-audit.md`](../theming-structural-audit.md)를 따른다.
`BrowserSurfaceColorSampleTest.*`는 양자화 경계·상단 경계 소유·투명도·불균일
샘플을 검사한다. 컨트롤러 검사는 입력/resize 재검사, 후속 검사의 종료 및
hidden 취소를 다루며, `YeeRenderedBoundaryTracksDelayedCssWithoutMetadata`는
실제 페이지의 지연 CSS 변경과 클릭을 두 split 방향에서 검증한다.
선택/hover 및 보안색 대비는 `BrowserSurfacePresentationResolverTest.*`에 포함한다.

아래는 geometry 한 프레임만으로 체감 품질을 판단할 수 없어 실제 Yee 앱 검수를
유지한다.

- 첫 로드·Tab 전환·스크롤 중 Header 색이 한 frame 튀거나 단계적으로 끊기지 않는지
- page-aware 배경과 텍스트·버튼·열린 popup이 함께 자연스럽게 전환되는지
- focus ring, popup 그림자와 연결형 상단 모서리가 밝고 어두운 페이지 모두에서
  겹치거나 거터 밖으로 뚫리지 않는지
- 분할 진입·pane 전환·해제 순간에 이전 focus outline 조각, 높이 변화, renderer
  clip 밖의 scrollbar가 보이지 않는지

Site Controls의 숫자 칩은 같은 Header snapshot을 받으며, 실제 칩 바탕과 글자색을
한 쌍으로 적용한다. `SiteControlsBrowserTest.RapidTabSwitchKeepsBadgeOwner`는 기존
탭별 건수 검증에 밝은·어두운 페이지 전환 후 실제 Label 대비와 아이콘 분리·포함
영역을 함께 확인한다. `--yee-header-capture-dir`는 같은 건수 10의 가로·세로 및
밝게·어둡게 native 창 캡처를 선택적으로 생성한다. 이 선택적 실행에서만 실제
숫자 영역의 hover·클릭 전달·키보드 focus와 눌림·보호 해제·99+ 표시를 추가로
확인한다. 일반 실행에서 캡처나 시스템 마우스 입력을 반복하지 않는다.

## 4. Browser Surface에 남은 검증 범위

구조별 구현·테스트 연결은 [레이아웃 검토](../browser-surface-layout-design-audit.md)를
따른다. 과거 macOS 검수의 완료된 캡처·녹화·실행 이력은 정리했으며, 아래 한계는
이번 Windows 40개 사례의 통과로 해소된 것으로 간주하지 않는다.

- 전체화면의 제어된 전환 중간 픽셀과 실제 OS callback 타이밍. 안정된 전체화면
  화면이나 결정적 epoch 검사가 실제 OS 전환의 모든 프레임을 증명하지 않는다.
- Side Panel 전환은 제어된 compositor 읽기로 검증했지만, 당시 잠긴 세션 때문에
  수정 후 새 OS 캡처를 얻지 못했다. 이후 해당 경로 변경 시 실제 창에서 재확인한다.
- 전체 hard-clip 행렬의 Windows 배율별 실행과 Linux 200% 실행. 과거 macOS의
  DSF 1/1.25/1.5/2 결과와 이번 Windows 기본·RTL 125% 사례는 범위가 다르다.
- Status Bubble의 실제 hover 픽셀·포인터 전환, 실제 AI WebUI 제어와 pane 기준
  탭 모달 픽셀. 기존 기하 검사나 before-unload 표시·Cancel만으로 대체하지 않는다.
- Data Protection·Indigo·Actor의 실제 상태와 남은 Windows/Linux native-host·
  compositor 조합. Glic·Lens·DevTools·Read Anything·Sad Tab 생명주기 검사와 구분한다.
- `YeeSplitPanelDevToolsNoticeKeepOwningPane`는 Side Panel·분할·세 DevTools dock
  전략·InfoBar의 적용 기하를 검사하며 실제 DevTools frontend 검증은 아니다.
- 과거 `NativePlannerMatchesAppliedAnimationFramesAndReversal`의 Header 전경 픽셀은
  간헐 실패 뒤 assertion 수정 없이 최종 재실행을 통과했다. 간헐 실패의 원인은 미확정이다.

예약 Sidebar 슬롯 Pins·Bookmarks·Chat·Agent의 활성화는 제품 결정이며 검수를 위해
임의로 켜지 않는다. 구조 검토의 `built`는 전체 플랫폼 독립 검증을 의미하지 않는다.
