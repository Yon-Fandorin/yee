# Sidebar Footer

Sidebar Footer는 탭 목록 아래에 고정된 **하나의 Context Switcher 행**이다.
Tenant와 Workspace를 각각의 카드나 버튼으로 나누지 않고, 현재 작업 맥락과
계정을 한 번에 확인하고 전환하는 진입점으로 사용한다.

상단 Yee Hub는 작업·에이전트 상태를 다루고 Footer는 현재 context, 빠른 진입
가치가 있는 Browser tools, Yee 전용 workspace 기능을 다룬다. Chromium의 전체
Settings/Profile 구조를 복제하지 않는다.

## 지속 표시 행

펼친 Sidebar의 맨 아래에 다음 정보를 한 행으로 표시한다.

- 주 정보: 현재 Workspace
- 보조 정보: `Tenant · Account`
- 선행 mark: 현재 Workspace의 짧은 표식
- 후행 정보: 펼침 표시

행 전체가 하나의 버튼이다. 내부 펼침 표시는 별도 hit target이 아니다.
Sidebar가 접히면 행과 메뉴를 함께 숨기고, hover flyout에서 펼친 표현을 사용하는
동안에는 다시 보인다.

## 메뉴 계층

메뉴를 열면 별도 카드나 중첩 popover를 추가하지 않고 같은 패널 안에서 화면을
교체한다. 첫 화면에는 다음 세 항목만 연속된 행으로 둔다.

1. **Current context**: Account와 Tenant/Workspace 전환
2. **Browser tools**: 바로 찾기 어려운 Chromium 기능의 선별된 단축 진입점
3. **Yee & memory**: 선택된 Workspace에만 속하는 Yee Memory 상태와 제어

Browser tools에는 Reopen closed tab, Downloads, History, Tabs from other devices,
Manage extensions만 둔다. Appearance, Search, Privacy, Profile 같은 범용 설정은
기존 Chromium Settings에 남기고, 예약된 Sidebar Bookmarks 슬롯도 이 메뉴에
복제하지 않는다. 아직 product provider나 실제 명령이 없는 context 관리·로그아웃
행도 placeholder로 노출하지 않는다.

```text
Footer row
└─ Root
   ├─ Current context
   │  └─ Tenant / Workspace choices
   ├─ Browser tools
   │  ├─ Reopen closed tab
   │  ├─ Downloads
   │  ├─ History
   │  ├─ Tabs from other devices
   │  └─ Manage extensions
   └─ Yee & memory
      └─ Workspace memory state
```

## 상호작용

- 열릴 때 Root의 첫 actionable row로 focus를 옮긴다.
- 하위 화면의 Back과 `Escape`는 바로 위 화면으로 돌아간다.
- Root에서 `Escape`는 메뉴를 닫고 trigger로 keyboard traversal focus를 복원한다.
  trigger 재클릭과 외부 클릭은 메뉴를 닫되 포인터가 선택한 새 focus를 유지한다.
- Context를 선택하면 Footer 표시를 먼저 갱신하고 메뉴를 닫는다.
- Browser tools는 임의 URL이나 새 WebContents를 만들지 않고 기존 Chromium
  page/command를 제한된 action callback으로 호출한다. 공개 action enum에는 위의
  다섯 shortcut만 존재하며 범용 Settings/Profile 목적지는 추가하지 않는다.
- 화면 전환은 bubble Widget을 새로 중첩하지 않고 한 Widget의 content만 바꾼다.
- 화면 전환은 reduced motion이 꺼져 있을 때만 짧은 opacity 전환을 사용한다.

## 데이터와 소유권

Yee는 Footer의 계층, 행 표현, focus와 화면 전환을 소유한다. Tenant, Workspace,
Account 목록과 선택 결과는 별도 product model이 제공해야 한다. Footer View가
조직이나 계정을 임의로 생성하거나 `TabStripModel`에 저장하지 않는다.

Footer 메뉴의 `BubbleDialogDelegate`와 Widget은 Yee의 단일 lifetime owner가 함께
관리한다. Widget은 `CLIENT_OWNS_WIDGET`으로 만들고 Widget을 delegate보다 먼저
제거한다. 정상 닫기와 네이티브 창 종료 모두에서 browser anchor 관찰을 먼저
해제해, 테마 변경이 닫힌 bubble에 전달되지 않게 한다.

Tenant provider가 아직 연결되지 않은 native 체크포인트는 Chromium Profile에서
얻은 Account와 `Local / Personal` 단일 context를 안전한 fallback으로 표시한다.
이는 샘플 조직이 아니며 전환 가능한 원격 Tenant가 있다는 뜻도 아니다. Footer
component는 처음부터 여러 context를 받는 model 경계를 유지한다. 이 fallback에서
실제 provider가 필요한 context 관리와 로그아웃 action은 숨기고, Workspace Memory는
연결되지 않은 상태로 비활성 표시한다. 임시 UI 상태를 실제 account나 memory 상태인
것처럼 저장하지 않는다. Memory를 변경할 수 있는 provider가 연결되면 View 내부
상태에 머물지 않고 별도 callback으로 변경 결과를 product model에 전달한다.
Chromium Profile에서 실제 표시 이름을 얻지 못해 `Local profile` placeholder를 쓰는
경우에도 푸터 보조 문구만 사용하고 `LP` 같은 가짜 이니셜이나 별도 profile mark를
만들지 않는다.

## 시각 계약

- 지속 행 높이는 50 DIP이고 Sidebar 좌우 inset을 따른다.
- 행과 메뉴 색은 고정 RGB가 아니라 현재 Chromium theme와 Yee shell 대비색에서
  파생한다.
- 지속 행의 기본 상태는 배경과 외곽선 없이 Sidebar 위에 놓인다. Hover에는 옅은
  배경만, open·keyboard focus에는 active 배경과 외곽선을 사용한다.
- 지속 행의 background와 hover·focus·press highlight는 같은 10 DIP 곡률을
  공유해 상태가 바뀌어도 사각 모서리가 드러나지 않는다.
- hover와 open 상태는 한 겹의 Yee surface로 표현하고 keyboard focus는 같은
  rounded path를 따르는 system focus ring으로 구분한다. trigger는 접근성 트리에
  menu popup과 expanded/collapsed 상태를 알린다. hover surface의 진입·퇴장
  진행값과 easing은 Sidebar 탭 행과 동일하게 유지하며 reduced motion에서는
  즉시 최종 상태로 전환한다. 포인터 hover·click으로 생긴 직접 focus에는 ring을
  그리지 않고, 키보드 focus traversal에서만 표시한다.
- Root의 세 행은 별도 section card로 분리하지 않는다.
- 현재 Workspace mark만 accent tonal surface를 사용한다. 일반 action은 배경 없는
  neutral glyph slot으로 낮춰 identity와 navigation의 위계를 분리한다. 선택·hover는
  기존 Sidebar 상태 팔레트보다 강해지지 않는다.
- 지속 행의 Workspace title은 primary hierarchy, `Tenant · Account`는 system
  secondary foreground를 사용한다. 후행 disclosure는 선행 Workspace mark보다
  작아야 한다.
- 현재 Context는 neutral selected surface와 check를 함께 쓰고 접근성 트리에도
  selected 상태로 노출한다.
- 이동과 선택은 문자 기호가 아니라 native vector icon으로 표시하고, `System`,
  Memory 상태 같은 값은 작은 tonal status pill로 구분한다.
- Root의 두 줄 행은 46 DIP, 하위 action 행은 38 DIP를 사용해 정보 밀도와
  가독성의 균형을 유지한다.
- 메뉴 폭은 Footer 행의 사용 가능한 폭과 맞추고 화면별 내용 높이만 변한다.
- 메뉴는 Footer와 4 DIP 간격을 두어 두 surface의 경계를 유지한다. 간격을 위해
  고정 화면 좌표를 쓰지 않고 Footer anchor를 이동시켜 창 이동·폭 변경을 따라간다.
- 메뉴는 elevated system surface, 12 DIP 곡률과 native shadow를 사용한다.
  진입과 화면 교체는 위치 이동 없이 짧은 opacity 전환만 사용하며 시스템의
  reduced motion과 focus ring 정책을 그대로 따른다.

## 회귀 계약

순수 테스트는 화면 계층, Back 목적지, Root 순서, Browser tools의 제한된 action
목록, context fallback을 검증한다.
실제 View 테스트는 Footer가 한 번만 호스팅되는지, 탭 목록 아래에 남는지,
split에서 복제되지 않는지, collapse와 expand에 맞춰 표시되는지를 검증한다.
메뉴를 정상 종료하거나 네이티브 종료한 직후 browser theme가 바뀌어도 dangling
anchor observer가 남지 않는지 함께 검증한다.
외부 클릭 종료는 배경과 외곽선이 없는 resting 상태로 돌아가고 포커스를 강제로
trigger에 되돌리지 않으며, Escape 종료만 keyboard traversal focus를 복원한다.
지속 행의 후행 요소는 disclosure 하나로 유지해 Account/Profile mark가 다시
중복되지 않게 한다.
테마 대비, bubble의 연결감, 긴 이름 말줄임은 실제 Yee 앱에서 확인한다.
