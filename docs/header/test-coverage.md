# Browser Surface Header regression coverage

이 문서는 Header와 Omnibox의 사용자 가시 계약을 자동 테스트와 실제 화면 검수로
나눈다. 테스트 이름은 구현 함수가 아니라 보장하는 결과를 기준으로 둔다.

## 1. 색상과 geometry

`test-header.sh unit`은 Header 전용 `yee_header_unittests` 실행 파일만 빌드해
확인한다. Chromium 전체 `unit_tests` 빌드는 광범위 최종 게이트에서만 사용한다.

| 계약 | 자동 검증 |
| --- | --- |
| 각 `WebContents`가 하나의 source identity와 transition timeline만 소유하고 Tab 재활성화는 현재 화면색에서 시작 | `BrowserSurfaceColorControllerTest.WebContentsOwnsOneStablePresentationSource`, `TabActivationTransitionsFromTheCurrentlyPresentedColor` |
| 각 persistent content container가 현재 `WebContents` source 하나만 전달하고 detach 시 presentation을 즉시 비움 | `BrowserSurfaceColorControllerTest.ContainerBindingPublishesOnlyItsCurrentWebContentsSource`, `ContainerBindingClearsPresentationWhenContentsDetach` |
| 테마 변경은 아직 page 색이 없는 Tab의 fallback만 바꾸고 확정된 page 색을 덮지 않음 | `BrowserSurfaceColorControllerTest.ThemeChangesOnlyRetargetUnresolvedTabs` |
| 첫 유효 paint와 load 완료 뒤 bounded settling, 진행 중 전환의 연속 retarget | `BrowserSurfaceColorControllerTest.FirstPaintAndLoadCompletionStartBoundedSettlingSamples`, `RetargetingStartsFromTheCurrentlyPresentedColor` |
| 연속 scroll 입력은 진행 중 epoch·capture·안정 후보를 보존하면서 관찰 window만 연장하고, 실제 timeout 후 page sampling 모드로 돌아가 반복·timeout timer를 모두 정리 | `BrowserSurfaceColorControllerTest.RepeatedScrollSignalsPreserveTheActiveSamplingEpoch`, `ScrollSamplingTimeoutReturnsToPageSamplingMode` |
| 두 Pane Header가 각자 `WebContents` 색을 독립적으로 유지 | `BrowserSurfaceColorControllerTest.SplitPaneSourcesKeepIndependentSurfaceColors` |
| navigation 이전의 늦은 surface capture가 같은 Tab의 새 sampling 상태를 오염시키지 않고, 현재 epoch의 안정된 실제 capture 두 회는 page presentation을 publish | `BrowserSurfaceColorControllerTest.IgnoresLateCaptureFromPreviousSamplingEpochOnSameWebContents`, `StableCurrentCapturePublishesPagePresentation` |
| popup provider는 animation frame마다 바뀌지 않고 최신 transition 완료에서만 한 번 교체 | `BrowserSurfaceColorControllerTest.CommitStartingTransitionDoesNotRefreshPopupUntilCompletion`, `RetargetedTransitionRefreshesPopupOnceForLatestTarget`, `ImmediateCommitRefreshesPopupExactlyOnce` |
| Header 텍스트·버튼·focus stroke가 전체 gray 및 대표 chromatic surface에서 같은 대비 계약을 사용하고 실제 Label이 물리색을 유지 | `BrowserSurfacePresentationResolverTest.ExhaustiveGrayAndChromaticContrastContract`, `YeeRestingTextViewTest.WidgetAttachedLabelsUseExactSurfaceAndPhysicalForeground` |
| popup은 exact surface를 쓰되 high contrast/forced colors에서는 native mixer를 보존하고 global provider cache를 늘리지 않음 | `YeeSurfaceColorTest.PopupProvidersAreExactAndDoNotGrowGlobalCache` |
| 단일·분할 Header가 42 DIP, 주소 표면 34 DIP, 공통 중심축과 edge inset을 공유 | `YeeSurfaceGeometryTest.SplitAndSingleHeadersShareMetricsContract` |

## 2. 실제 native View 구조

`test-header.sh interactive`가 실제 Yee 창과 Chromium Omnibox를 함께 사용한다.

| 계약 | 자동 검증 |
| --- | --- |
| native LocationBar 하나가 단일 Toolbar와 active Pane Header 사이를 이동해도 높이·중심축을 유지하고 분할 해제 시 원래 parent로 복귀 | `YeeSingleAndSplitHeadersShareLocationBarGeometry` |
| 단일 A → 분할 active A/inactive B → active B → popup B → 분할 해제 B 전환에서 주소 배경, 일반·disabled 버튼, IME inline·rich autocomplete·AI hint label, Pane Header 텍스트, location/security icon이 같은 source snapshot을 사용하고, 열린 popup은 animation의 최종 `popup_revision`에서만 한 번 갱신되며 native 모드에서 모든 물리색 override를 제거 | `YeePresentationFollowsSingleAndSplitSources` |
| active pane 전환과 분할 해제에서 native LocationBar가 최종 parent로 이동하고 실제 host 변경 generation마다 layout/bounds 동기화를 정확히 한 번 수행 | `YeeLocationBarRehostSynchronizesOncePerGeneration` |
| 분할 생성·제거 중 Chromium의 focus 정책이 popup을 닫는 동작은 우회하지 않고, popup 색·anchor는 단일과 안정화된 각 분할 상태에서 별도로 검증 | `YeeOmniboxPopupFollowsSingleAndSplitHeader` |
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
