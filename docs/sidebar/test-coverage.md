# Sidebar regression coverage

이 문서는 펼쳐진 Tab Sidebar의 제품 계약 중 무엇을 자동화하고 무엇을 실제
화면에서 확인하는지 구분한다. 테스트 이름은 구현 세부가 아니라 사용자에게
보이는 결과를 기준으로 둔다.

## 1. 정책과 순수 geometry

`test-sidebar.sh unit`이 창을 열지 않고 확인한다.

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
| New Tab·Agent control은 Sidebar Header에 한 번만 남고 split/collapse에서도 소유권 유지 | `YeeSidebarHeaderControlsPersistAcrossSplitAndCollapse` |
| Context Switcher Footer는 탭 목록 아래에 한 번만 남고 split에서 복제되지 않으며 collapse와 expand를 따름 | `YeeSidebarFooterPersistsAcrossSplitAndCollapse` |
| Footer trigger는 하나의 메뉴 Widget을 열고 Root를 Context → Browser tools → Yee Memory 순서로 표시하며, 선별되지 않은 Chromium 설정은 복제하지 않고 메뉴 폭·접근성 이름·재클릭 닫기를 유지 | `YeeSidebarFooterOpensCuratedRootMenu` |

Footer의 Root 순서, Browser tools의 제한된 action 목록, 하위 화면의 Back 목적지, 단일 local context fallback은
`SidebarFooterPolicyTest.*`가 창을 열지 않고 확인한다. `SidebarFooterViewTest.*`는
bubble이 Footer 폭 변경을 추적하는지, 접근 가능한 창 이름을 제공하는지, trigger
재클릭으로 같은 bubble을 닫는지, Memory 변경을 callback으로 전달하는지 확인한다.
`TriggerUsesOnlyWorkspaceMarkAndDisclosure`는 지속 행의 후행 profile mark가 다시
추가되지 않도록 한다. `InteractionStatesUseOnlyTheYeeSurface`는 rest → hover →
rest와 direct focus → keyboard traversal focus 순서에서 배경·외곽선 painter와
고정 inset 계약을 확인한다. `PointerDismissDoesNotRestoreFocusOrOutlinedSurface`는
별도 focus target으로 직접 focus가 이동한 뒤 lost-focus 종료를 수행해 새 focus를
보존하고 배경과 active 외곽선을 제거하는지 확인한다.
`EscapeDismissRestoresKeyboardTraversalFocus`는 그 과정에서도 Escape의 keyboard
focus 복원 계약이 유지되는지 확인한다.
또한 `ThemeChangeAfterCloseHasNoDanglingBubble`과
`ThemeChangeAfterNativeTeardownHasNoDanglingBubble`은 정상 종료와 네이티브 종료
직후 theme 변경에도 닫힌 bubble의 anchor observer가 남지 않는지 검증한다.

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
