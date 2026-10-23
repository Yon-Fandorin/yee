# Sidebar regression coverage

이 문서는 펼쳐진 Tab Sidebar의 제품 계약 중 무엇을 자동화하고 무엇을 실제
화면에서 확인하는지 구분한다. 테스트 이름은 구현 세부가 아니라 사용자에게
보이는 결과를 기준으로 둔다.

## 1. 정책과 순수 geometry

`test-sidebar.sh`의 기본값은 `unit`이며 `--no-build`만 줘도 단위 검사만 실행한다.
기존 공통 테스트 source_set을 묶은 작은 `yee_layout_unittests`를 사용하며 전체
Chromium `unit_tests`를 빌드하지 않는다. 기존 Sidebar 필터의 검사는 모두 유지한다.
계산·색상·직접 callback 검사는 창 활성화 없이 검사한다. native Footer의 popup·focus
계약 검사는 실제 Widget을 표시하고 활성화할 수 있으므로 완전한 무창 검사는 아니다.
Group Header 색상·mock clock 애니메이션은 `ShowInactive()`를 사용하고 검사 중에도
Widget이 비활성인지 확인한다. Footer도 비활성으로 시작하며 popup·keyboard focus
검사가 필요한 시점에만 활성화한다. 기존 실제 hover·키보드·popup 검사는 유지한다.

| 계약 | 자동 검증 |
| --- | --- |
| Favorite 최대 12개, 여러 탭 추가 시 합산, unpin은 항상 허용 | `FavoritesPolicyTest.*` |
| 단일 탭도 같은 창 Sidebar 안에서는 정리 drag를 우선 | `FavoritesPolicyTest.LoneTabGetsSidebarFirstRefusalOnly` |
| 4열 독, 상단 inset 없음, 한 Favorite의 좌·우 삽입 슬롯 | `FavoritesDockLayoutTest.*` |
| 독/목록 hit magnet, 첫 Tab 경계, RTL, grab offset | `FavoritesDragGeometryTest.*` |
| 76 DIP 빈 드롭존과 reduced-motion의 원자적 전환 | `FavoritesDragGeometryTest.*DropZone*` |
| Light scheme은 frame 색을 보존하고 Dark scheme은 같은 hue의 차분한 shell로 정규화 | `YeeShellColorTest.*` |
| 일반 행·Favorite·drag preview의 라이트/다크 및 활성/비활성 상태 팔레트 | `YeeSidebarItemColorTest.*` |
| Resting→Hover→Active→Dragging의 fill·stroke 강도와 실제 합성 표면의 전경 대비 | `YeeSidebarItemColorTest.EveryStateSharesOneThemeAwarePalette` |
| 세로 Group Header의 resting·hover 표면과 전경이 함께 바뀌고 창 활성 상태를 다시 반영 | `TabGroupHeaderViewTest.YeeVerticalHoverUpdatesSurfaceAndForegroundTogether` |
| Group mark가 브라우저 신호만 사용하고 Agent 상태를 섞지 않음 | `GroupMarkSignalsTest.*` |

## 2. 펼친 Sidebar의 실제 View 구조

`test-sidebar.sh interactive`가 실제 Yee 창과 `TabStripModel`을 함께 사용한다.
주소줄·Header의 실제 색상·popup·renderer 연결 검사는
[`test-header.sh interactive`](../header/test-coverage.md)가 소유하며 여기서 중복
실행하지 않는다. 두 영역을 함께 바꾸면 두 gate를 선택한다. 아래의 Sidebar flyout
두 검사는 `test-browser-surface-layout.sh interactive`가 소유하며 applied layout
변경 시 함께 실행한다. 실제 입력·focus·animation 계약의 검사는 유지한다.

| 계약 | 자동 검증 |
| --- | --- |
| Favorite 0개 유휴 상태의 8 DIP 목록 inset, 독이 있을 때 32+8 배치, 라벨·구분선 없음 | `YeeSidebarExpandedSectionsFollowMetrics` |
| Tab ScrollView가 마지막 행에서 접히지 않고 Sidebar 하단까지 채움 | `YeeSidebarExpandedSectionsFollowMetrics`, `DragToScroll` |
| 0개일 때 drag 중에만 76 DIP 드롭존과 preview가 나타나고 취소 시 복원 | `YeeSidebarEmptyFavoritesDropZoneLifecycle` |
| 마지막 Favorite을 집으면 포인터가 목록으로 넘어가도 드롭존 공간을 유지하고 취소 시 독 복원 | `YeeSidebarLastFavoriteDropZoneSurvivesUntilCancel` |
| Favorite이 둘 이상이면 하나를 집어도 0개용 드롭존 대신 기존 독 유지 | `YeeSidebarDraggingOneOfTwoFavoritesKeepsDock` |
| Favorite 하나를 집어 기존 독의 다른 위치로 재정렬하면 pin 상태와 선택한 순서 유지 | `YeeSidebarSingleFavoriteDragReordersWithinDock` |
| Group 헤더 drag는 Favorite 드롭존·preview·pin을 만들지 않음 | `YeeSidebarGroupHeaderDragHasNoFavoriteAffordance` |
| Group 헤더와 행은 32 DIP, Group 행은 10 DIP 들여쓰기, 접기 시 자식 숨김 | `YeeSidebarExpandedGroupUsesSharedRowMetrics` |
| Header의 New Tab·Agent와 탭 목록 아래 Footer는 한 번만 남고 split/collapse에서도 같은 View의 소유권을 유지 | `YeeSidebarControlsPersistAcrossSplitAndCollapse` |
| 저장된 좁은 폭에서도 Header가 온전히 표시되고, 창 버튼 아래54 DIP 상단·가로/하단12 DIP 외곽을 유지하며, 창 버튼 행은 native hit target에서 제외. 실제 hover·Footer 팝업은 페이지 폭을 바꾸지 않고 닫기 후 접힘으로 복귀 | `YeeSidebarFlyoutKeepsExpandedControlsAndPageBounds` |
| hover에서 고정·접힘으로 전환할 때 폭·불투명도·위치가 중간 프레임에서 튀지 않고 Header 버튼이 겹치거나 부분적으로 잘리지 않음 | `YeeSidebarFlyoutPinAndReverseKeepContinuousPresentation` |

위 두 flyout 검사는 고정 상태에서 펼침 잠금을 얻은 뒤 접는 순서도 확인한다.
Footer·Create에 키보드 focus가 있어도 종료 통지 뒤 hover로 인계하고,
focus와 잠금을 해제하면 페이지 폭을 바꾸지 않고 숨겨야 한다.
실제 애니메이션과 애니메이션 생략 경로를 기존 두 검사 안에서 각각 검증한다.

`SidebarFooterPolicyTest.*`는 local context의 실제 계정·빈 계정 fallback을 확인한다.
`SidebarFooterViewTest.*`는 실제 메뉴의 목적지, 키보드 이동과 Back, 선별된 Browser
action 실행, 접근 가능한 이름, Footer와 독립적인 메뉴 폭, Memory callback을 확인한다.
`WorkspaceMarkAppearsOnlyInFooter`는 profile mark 중복을 확인한다.
`InteractionStatesUseOnlyTheYeeSurface`는 rest → hover →
rest와 direct focus → keyboard traversal focus 순서에서 배경·외곽선 painter와
고정 inset 계약을 확인한다. `PointerDismissDoesNotRestoreFocusOrOutlinedSurface`는
별도 focus target으로 직접 focus가 이동한 뒤 lost-focus 종료를 수행해 새 focus를
보존하고 배경과 active 외곽선을 제거하는지 확인한다.
`EscapeDismissRestoresKeyboardTraversalFocus`는 그 과정에서도 Escape의 keyboard
focus 복원 계약이 유지되는지 확인한다.
또한 `TriggerReclickClosesTheSameBubble`과
`ThemeChangeAfterNativeTeardownHasNoDanglingBubble`은 정상·네이티브 종료 직후
theme 변경과 즉시 다시 열기에서 이전 anchor·잠금이 남지 않는지 검증한다.

## 3. 드래그 출발점과 도착점

한 출발점의 성공만으로 다른 출발점도 된다고 가정하지 않는다.

| 출발점/도착점 | 자동 검증 |
| --- | --- |
| 일반 Tab → 목록 처음·중간·끝, 취소 | `DragWithinUnpinnedContainer`, `CancelDragWithinUnpinnedContainer` |
| 일반 Tab → 펼친 Group 중간, Group 행과 같은 preview 정렬 | `YeeSidebarTopLevelTabCanEnterExpandedGroupMiddle` |
| Group Tab → Group 내부 위치, Group 밖 목록 | `DragWithinGroup`, `DragOutOfGroup` |
| Group Tab → 기존 Favorite 오른쪽 슬롯 | `YeeSidebarGroupedTabCanBecomeFavorite` |
| Favorite → 목록 처음·중간·끝 | `DragFavoriteIntoTabList`, `DragFavoriteBeforeFirstTabCommitsFirst`, `DragFavoriteBelowLastTabCommitsLast` |
| Favorite → 펼친 Group 처음·중간 retarget·끝, 실제 들여쓰기 정렬 | `DragFavoriteToExpandedGroupFirstSlot`, `DragFavoriteCanRetargetWithinExpandedGroup`, `DragFavoriteToExpandedGroupLastSlot` |
| Favorite → Group 헤더 경계와 접힌 Group 뒤의 최상위 목록 | `DragFavoriteOnGroupHeaderStaysTopLevel`, `DragFavoriteAfterCollapsedGroupStaysTopLevel` |
| Tab → 기존 Favorite 좌·우 슬롯, 가득 찬 독 거절 | `DragTabBeforeExistingFavoriteCommitsPin`, `DragTabAfterExistingFavoriteCommitsPin`, `FullFavoritesRejectsWithoutNativeReorder` |
| Favorite → Group 이동 취소 시 pin·Group·순서 복원 | `CancelFavoriteToGroupDragRestoresSource` |
| Group 헤더 전체 이동·접힌 상태 유지 | `DragGroupHeader`, `DragCollapsedGroupStaysCollapsed`, `DragCollapsedGroupOverExpandedGroup` |
| 여러 선택 일반 Tab과 unpinned split 묶음의 기존 Chromium drag 회귀 | runner의 `DragMultipleTabs*`, `DragSplitTabs`, `DragOverSplit*` 테스트 |
| browser/keyboard pin 명령과 세로 Tab context menu가 최대 용량을 지키고 unpin은 허용 | `YeeSidebarFavoriteCommandHonorsCapacityAndUnpin`, `YeeFavoriteContextMenuHonorsCapacityAndAllowsUnpin` |

## 4. 실제 화면에서만 확인할 것

다음은 상태나 geometry 한 프레임을 통과했다고 UX가 보장되지 않는다. 자동
테스트는 시작·종료 상태만 잡고, 최종 판단은 실제 Yee 앱에서 한다.

- 드롭존 높이와 표면 opacity가 이어지는 체감 리듬
- Group 경계를 빠르게 왕복할 때 가로 geometry가 튀거나 깜빡이지 않는지
- 라이트/다크 및 활성/비활성 창에서 색 대비가 시각적으로 과하거나 약하지 않은지
- 긴 제목 fade, focus ring, hover 전환이 native Tab과 자연스럽게 이어지는지
- 긴 Tenant·Workspace·Account 이름의 말줄임, Footer와 위로 열리는 메뉴의 연결감,
  라이트/다크에서 mark와 보조 텍스트 대비
- renderer overlay와 scrollbar가 둥근 Browser Content clip 밖으로 새지 않는지
- 단일 Tab이 같은 Sidebar를 벗어나 창 이동으로 전환되는 경로, 다른 Yee 창으로
  옮겨 Favorite에 놓는 경로와 취소 복원. macOS의 upstream detach 테스트가
  비활성화되어 있어 플랫폼 실제 창 검수로 유지한다.

`tabs.md`의 **분할을 구성하는 개별 Tab 하나만 떼는 drag**와 여러 Favorite을
한 번에 재정렬하는 drag는 native 제약과 미정 UX가 남은 계약이다. 기존
unpinned split 묶음·다중 선택 drag 테스트를 이 기능들의 지원 근거로 간주하지
않으며, 구현과 제품 결정 전에는 통과하는 테스트를 만들지 않는다.

## 5. 수평 탭과의 전환

`test-browser-surface-layout.sh horizontal`은 기존
`YeeHorizontalTabsPreserveRowsColorsAndTabIdentity`와 Windows 전용 이음새 검사를
기본·RTL 125%·통합 탭바 경로에서 실행한다. Header와 주소줄의 중복 여백,
그룹 접기/펴기 및 탭
추가/제거 후 favicon 요약 갱신, toolbar/content Side Panel 좌우 배치에서
Header의 전체 폭 유지와 패널이 그 아래에 배치되는지 확인한다.
색이 있는 그룹의 내부 탭을 실제로 선택한 뒤 색상·포커스를 확인하며, 그룹
소속 때문에 활성 탭의 색상 윤곽선이 되살아나지 않아야 한다. 그룹 공통 배경은
탭바 안에 머물고 Header·Side Panel과 겹치지 않아야 한다.
같은 검사에 `--yee-tabbar-capture-dir`를 주면 밝게·어둡게, 수평·세로,
그룹 펼침·접힘, 패널 닫힘·toolbar/content 좌우의 40개 실제 창 화면을 저장한다.
긴 한·영 혼합 이름 → 빈 이름 → 짧은 이름으로 같은 그룹을 바꾸며 일반 폭과
최소 폭의 수평·세로 배치를 검사한다. 제목과 favicon·개수·native 조작 아이콘의
겹침 및 잘림, 펼친 뒤 요약의 숨김, 한 줄 축약과 전체 접근성 이름을 확인한다.
선택적 캡처에는 최소 폭의 이름·접힘 조합 12개가 추가된다.
Windows 수평 모드에서는 검색·새 탭 버튼·드래그 여백과 창 버튼의 예약 폭까지
포함한 현재 최소 창 너비를 사용한다. 세로 모드의 최소 너비도 접힌 그룹의
favicon 요약과 native 펼치기 아이콘을 포함하며, 모든 자식의 실제 경계를 검사한다.
수평 창 버튼의 색상은 페이지 Header가 아닌 shell 탭 행을 기준으로 검사하고,
밝은·어두운·유색 페이지로 바뀐 뒤에도 최소 아이콘 대비를 유지해야 한다.
`YeeWindowsHorizontalActiveTabPaintJoinsHeader`는 Windows의 실제 선택 탭 아래쪽과
본문 Header 위쪽을 비교한다. 일반·최대화와 밝은·어두운 창에서 탭 행과
반대색인 Header 표면을 적용해 이음새의 합성 픽셀까지 검사한다. 탭 행 전체
높이만 맞고 실제 탭 밑에 shell 바탕이 끼는 경우도 실패해야 한다. 같은 검사를 기본·RTL 125%·통합 탭바에서
실행하며, 세로 Sidebar의 기존 창 버튼 조작 축은 유지한다.
`YeeWindowsSplitThemeChangesKeepCaptionReservations`는 Windows에서 수평·세로 탭과
좌우·상하 분할을 유지한 채 밝음·어두움 테마를 바꾼다. 캡처 옵션 없이도 프레임
재생성을 실행하고, 다시 배치된 각 Pane Header의 실제 자식이 창 버튼 예약 영역과
겹치지 않는지와 페이지가 카드 안에 남는지 확인한다. 새 프레임의 client 배치가
비어 있는 동안에는 마지막 유효 여백을 유지하며 다음 배치에서 새 예약을 적용한다.
Windows 분할 호스트는 공간이 부족할 때 앞으로 조작을 먼저 접어 native 주소창의
최소 폭과 보안·별표 조작을 보존한다. Pane 닫기 버튼은 창 버튼 클릭 영역과 겹치지
않아야 하며, 주소창 내부 조작도 자신의 표면 안에 남아야 한다. 캡처 전 각 renderer의
실제 viewport 물리 크기와 새 프레임을 기다려 이전 크기의 페이지를 판정하지 않는다.
크기뿐 아니라 WebContents의 화면 원점도 비교하고, 두 페이지의 왼쪽 위 고정 표식을
root 합성 픽셀에서 읽는다. 가로·세로 전환 뒤 비활성 native layer가 이전 좌표계에
남아 글자가 잘리는 경우는 단색 페이지 중앙 검사와 달리 반드시 실패해야 한다.
macOS에서는 분할 탭의 두 pane 활성화와 전체화면 진입·해제에서도 그룹 공통 배경의
높이와 탭바 내부의 하단 여백을 확인한다. 컨테이너가 탭의 선호 높이보다 작아져도
그룹 공통 배경이 경계에서 잘리지 않아야 한다. 선택적 캡처는 AppKit이 옮긴 전체화면
상단을 해당 Widget의 compositor에서 따로 저장한다.
통합 탭바의 고정·일반·그룹·분할 행은 공통 높이 계산을 사용해 부모의 제약을
전달하고, 제약 없는 선호 크기 측정에서는 native 기본 높이를 보존한다.
일반 창 캡처는 macOS material까지 포함하며, 시각적 합격 여부는 사람이 직접 확인한다.
Header의 실제 배경을 그려 좌우 끝의 색도 검사하므로, bounds와 모서리 경로가
맞더라도 배경이 꺼져 있는 경우는 통과하지 않는다.
Side Panel·분할·패널을 연 전체화면과 가로/세로 전환에서
탭 수·pin·group·활성 탭 보존을 확인한다. 테마 대비와 불투명 표면·단일 경계는
별도의 작은 비활성 Widget 검사와 순수 색상 검사가 소유한다.
수평 모드의 Header 바깥 상단과 main backing은 같은 곡률을 사용한다. 패널을 열면
공통 backing이 resize gutter까지 채우고, 내부 경계는 1 DIP 선으로 표시한다.
픽셀 검사는 선의 폭과 Header 위로 넘치지 않는지, 전환 뒤 사라지는지 확인한다.
실제 Header clip은 자식 요소를 같은 윤곽 안에 두며, 세로 모드에서는 현재 크기의
사각 clip으로 돌아간다. 세로 모드와 하단 곡률도 보존한다.

실제 사이트 favicon, 맞춤설정의 탭 배치 선택과 native 버튼의 양방향 동기화,
라이트/다크의 그룹 chip·선택 탭·주소줄 연결감은 새 프로세스의 실제 앱에서 확인한다.

Side Panel 핸들의 기본 배경은 그리지 않고 hover·focus에서만 표시한다.
기존 `SidePanelResizeGutterViewTest.KeyboardDelegatesAndMarkerSharesMetrics`는
기본 배경의 투명도와 mouse exit 복귀를 비활성 Widget에서 확인한다.
`YeeSidePanelGutterPhysicallyResizesBesideWebContents`는 수평·세로와 좌우 배치에서
실제 포인터의 hover·drag 및 keyboard focus 표시를 확인한다.

공통 Shell paint는 가로·세로에서 같은 glass/fallback 정책을 사용한다. 기존
`SurfaceDecorationSurvivesIndependentWidgetsAndColorUpdates`는 창을 활성화하지 않고
조상 정책을 공유하지 않는 두 Widget에 같은 장식을 적용한다. 모드 왕복, 색상 변경,
구분선·모서리와 실제 그림자 레이어 및 Split의 임시 억제·복구를 확인한다.
같은 수평 모드에서 단일→분할→단일로 바뀌면 Side Panel의 공용 Header 연결도
해제·복구돼야 한다. 독립 패널은 둥근 상단과 윤곽을 유지하며, 같은 수평 통합
사례가 실제 분할 상태의 좌우 Side Panel backing도 확인한다.
native mac `GlassViewGeometryMatchesShellPolicy`는
가로→세로→가로 전환에서 실제 material이 창 전체를 덮는지 확인한다.
기존 `YeeSurfaceDecorationAndZOrderUseResolvedFrame`는 View와 compositor layer 모두
outline < Side Panel gutter < Sidebar 순서이며 동적 panel 재부착 뒤에도 유지되는지
확인한다. gutter는 find UI와 창 전체 scrim 아래에 둔다.

연결된 Side Panel 핸들은 실제 backing이 gutter를 덮을 때만 같은 페이지 snapshot으로
대비를 보정한다. 이 소유권은 `ResolvedBrowserSurfaceDecoration`이 계산해서 전달하며,
핸들은 sibling View ID나 적용된 bounds를 다시 조회하지 않는다. 기존 geometry 사례는
좌우 gutter의 완전한 포함과 부분 이탈을, marker 사례는 light/dark × 흰/검은 backing의
합성 후 3:1 대비와 색 변경 중 표시 유지, 명시적인 native 색 복귀를 확인한다.
기존 physical resize 사례는 다크 palette와 흰 페이지에서 수평 핸들의 대비를 확인하고
세로로 돌아가면 native 색을 유지하는지 실제 hover 경로에서 확인한다.

접힌 Group의 favicon 요약은 `TabGroupPreview`가 가로·세로 Header에서 공유한다.
기존 `YeeHorizontalTabsPreserveRowsColorsAndTabIdentity` 사례는 두 방향에서
접기/펼치기, 멤버 추가·제거·이동, 이미 접힌 상태의 favicon 갱신, `+N`과
leading 첫 favicon이 앞에 그려지는 실제 overlap raster, 뒤 favicon의 경계가
부분 투명도에서 불투명한 본체로 이어지는 feather를 함께 확인한다.
RTL·125%와 unified strip에서도 같은 사례를 사용한다.
기본·unified 수평 Group의 접힌 chip paint는 같은 owned background를 사용하며,
펼친 그룹의 공통 배경도 같은 owner가 그린다. native Header의 입력·접근성과
allocation은 유지한다. Header clip도 native가 계산한 현재 paint allowance를
명시적으로 받고 이전 clip의 형태로 geometry를 추론하지 않는다.

앞의 Group 시각 검사는 모델 명령으로 만든 상태를 확인하므로 실제 드래그의
편입·해제 성공을 대신하지 않는다. `horizontal`과 `interactive` 게이트는 native
`DetachToBrowserTabDragControllerTest`의 양방향 편입, 그룹 밖 이동, 취소 복원,
그룹 헤더 이동과 접힌 그룹 경계를 함께 실행한다. legacy 수평 fixture는 프로필의
기본 탭 배치와 관계없이 수평 모드로 진입한다.
`YeeGroupMembershipDragSurvivesLayoutSwitch`는 세로→가로 전환 후 일반 창과
최대화 창에서 분할 드래그 설정을 켠 채 두 그룹 멤버 사이로 편입하고 다시
그룹 밖으로 뺀다. 실제 포인터 입력 후 탭 identity·순서·활성 탭·멤버 수와
원래 멤버의 그룹 보존을 검증한다. 첫 멤버의 이전 중앙은 native lean 판정상
그룹 앞의 재정렬 슬롯이 될 수 있으므로 내부 편입 지점과 혼동하지 않는다.
`--yee-group-marker-capture-dir`를 주면 편입·해제 후 실제 창도 저장한다.
`interactive`에는 세로 그룹의 편입·내부 정렬·해제·접기·헤더 드래그와
Favorites 경계 및 취소 사례도 포함한다.
RTL의 방향 이동 사례는 논리적인 앞·뒤를 화면 좌표로 반전하여 실행한다.
`PreviewScreenBoundsMatchRowsInLtrAndRtl`는 Windows에서 그룹 들여쓰기별 실제
행과 드래그 미리보기의 화면 좌표를 비교한다. 미리보기의 물리 좌표는 배치
직전에만 Views의 논리 bounds로 바꾸며, 드롭 판정과 전환 애니메이션의 좌표
계약 및 Mac·Linux 경로는 유지한다.
Windows 개발 PC에서는 각 입력 사례를 개별 프로세스의 `--single-process-tests`
모드와 `--test-launcher-preserve-desktop-windows` 옵션으로 실행한다. 첫 옵션은
주변 창을 정리하는 테스트 런처 단계를 건너뛰며, 두 번째는 포커스 전환 실패 때
다른 앱을 닫는 복구 대신 검사를 실패시킨다.
