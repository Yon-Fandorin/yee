# Browser Surface Header regression coverage

이 문서는 Header와 Omnibox의 사용자 가시 계약을 자동 테스트와 실제 화면 검수로
나눈다. 테스트 이름은 구현 함수가 아니라 보장하는 결과를 기준으로 둔다.

## 1. 색상과 geometry

`test-header.sh unit`이 확인한다.

| 계약 | 자동 검증 |
| --- | --- |
| 새 Tab은 다른 Tab의 page 색을 이어받지 않고 현재 시스템 테마 색으로 전환하며, 이미 확인한 Tab은 자신의 색을 복원 | `BrowserSurfaceColorControllerTest.UncachedTabTransitionsToThemeFallbackAndCachedTabRestoresItsColor` |
| 테마 변경은 아직 page 색이 없는 Tab의 fallback만 바꾸고 확정된 page 색을 덮지 않음 | `BrowserSurfaceColorControllerTest.ThemeChangesOnlyRetargetUnresolvedTabs` |
| 첫 유효 paint와 load 완료 뒤 bounded settling, 진행 중 전환의 연속 retarget | `BrowserSurfaceColorControllerTest.FirstVisuallyNonEmptyPaintStartsBoundedSettlingSamples`, `LoadCompletionAlsoStartsBoundedSettlingSamples`, `RetargetingStartsFromTheCurrentlyPresentedColor` |
| 두 Pane Header가 각자 `WebContents` 색을 독립적으로 유지 | `BrowserSurfaceColorControllerTest.SplitPaneControllersKeepIndependentSurfaceColors` |
| Header 텍스트·버튼·focus stroke·popup theme identity가 밝고 어두운 surface에서 같은 palette 계약을 사용 | `YeeSurfaceColorTest.HeaderRolesRemainReadableOnLightAndDarkPages`, `FocusStrokeKeepsNonTextContrast`, `OmniboxPopupThemeIdentityIsStablePerOpaqueColor` |
| 단일·분할 Header가 42 DIP, 주소 표면 34 DIP, 공통 중심축과 edge inset을 공유 | `YeeSurfaceGeometryTest.SplitAndSingleHeadersShareMetricsContract` |

## 2. 실제 native View 구조

`test-header.sh interactive`가 실제 Yee 창과 Chromium Omnibox를 함께 사용한다.

| 계약 | 자동 검증 |
| --- | --- |
| native LocationBar 하나가 단일 Toolbar와 active Pane Header 사이를 이동해도 높이·중심축을 유지하고 분할 해제 시 원래 parent로 복귀 | `YeeSingleAndSplitHeadersShareLocationBarGeometry` |
| inactive Pane Header 클릭이 pane 활성화와 native 주소 편집을 한 번에 수행 | `YeeInactivePaneHeaderActivatesAddressEditing` |
| Omnibox popup이 단일·양쪽 active pane·분할 해제 후 모두 주소 표면의 좌우 폭과 상단에 연결되고 pane 전체 폭으로 확장되지 않음 | `YeeOmniboxPopupFollowsSingleAndSplitHeader` |

## 3. 실제 화면에서만 확인할 것

아래는 geometry 한 프레임만으로 체감 품질을 판단할 수 없어 실제 Yee 앱 검수를
유지한다.

- 첫 로드·Tab 전환·스크롤 중 Header 색이 한 frame 튀거나 단계적으로 끊기지 않는지
- page-aware 배경과 텍스트·버튼·열린 popup이 함께 자연스럽게 전환되는지
- focus ring, popup 그림자와 연결형 상단 모서리가 밝고 어두운 페이지 모두에서
  겹치거나 거터 밖으로 뚫리지 않는지
- 분할 진입·pane 전환·해제 순간에 이전 focus outline 조각, 높이 변화, renderer
  clip 밖의 scrollbar가 보이지 않는지
