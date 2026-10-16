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
- 보조 정보: `Tenant · Account`; 로컬 context는 중복되는 Tenant 없이 Account만 표시
- 선행 mark: 현재 Workspace의 짧은 표식
- 후행 정보: 펼침 표시

행 전체가 하나의 버튼이다. 내부 펼침 표시는 별도 hit target이 아니다.
Sidebar가 접히면 행과 메뉴를 함께 숨기고, hover flyout에서 펼친 표현을 사용하는
동안에는 다시 보인다.

## 메뉴 계층

메뉴를 열면 별도 카드나 중첩 popover를 추가하지 않고 같은 패널 안에서 화면을
교체한다. 첫 화면은 세로로 놓인 Browser tools·Memory 메뉴 행과
구분선 아래 Context 전환 행으로 구성한다. 현재 Workspace·Account 정보는
지속 Footer에서 확인할 수 있으므로 첫 화면에 반복하지 않는다.

1. **Browser tools**: 바로 찾기 어려운 Chromium 기능의 선별된 단축 진입점
2. **Memory**: 선택된 Workspace에만 속하는 Yee Memory 상태와 제어
3. **Switch workspace**: Account와 Tenant/Workspace 전환

Browser tools에는 Reopen closed tab, Downloads, History, Tabs from other devices,
Manage extensions만 둔다. Appearance, Search, Privacy, Profile 같은 범용 설정은
기존 Chromium Settings에 남기고, 예약된 Sidebar Bookmarks 슬롯도 이 메뉴에
복제하지 않는다. 아직 product provider나 실제 명령이 없는 context 관리·로그아웃
행도 placeholder로 노출하지 않는다.

```text
Footer row
└─ Root
   ├─ Browser tools
   │  ├─ Reopen closed tab
   │  ├─ Downloads
   │  ├─ History
   │  ├─ Tabs from other devices
   │  └─ Manage extensions
   ├─ Memory
   │  └─ Workspace memory state
   └─ Switch workspace
      └─ Tenant / Workspace choices
```

## 상호작용

- 열릴 때 Root의 첫 actionable control인 Browser tools로 focus를 옮긴다.
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
  배경만, open·press에는 active 배경을 사용한다. keyboard focus에는 system
  focus ring 한 겹만 표시하며 내부 stroke를 겹쳐 그리지 않는다.
- 지속 행의 background와 hover·focus·press highlight는 같은 6 DIP 곡률을
  공유해 상태가 바뀌어도 사각 모서리가 드러나지 않는다.
- hover와 open 상태는 한 겹의 Yee surface로 표현하고 keyboard focus는 같은
  rounded path를 따르는 system focus ring으로 구분한다. trigger는 접근성 트리에
  menu popup과 expanded/collapsed 상태를 알린다. hover surface의 진입·퇴장
  진행값과 easing은 Sidebar 탭 행과 동일하게 유지하며 reduced motion에서는
  즉시 최종 상태로 전환한다. 포인터 hover·click으로 생긴 직접 focus에는 ring을
  그리지 않고, 키보드 focus traversal에서만 표시한다.
- Root는 28 DIP Browser tools·Memory 행을 세로로 두고,
  구분선 다음에 28 DIP Context 전환 행을 둔다. 지속 행의 Workspace mark는 20 DIP,
  메뉴 내부 icon slot은 20 DIP다. Workspace 선택 행에는 mark를 중복하지 않고
  이름과 보조 계정 정보만 표시한다. glyph는 14 DIP이고 메뉴 내부 hover·focus
  곡률은 6 DIP다. Root는 개별 버튼 윤곽 없이 공통 icon·title·disclosure 열을
  사용하고, 하위 메뉴 action도 같은 icon column을 쓴다.
- 현재 Workspace mark만 accent tonal surface를 사용한다. 일반 action은 배경 없는
  neutral glyph slot으로 표시한다. 선택·hover는
  기존 Sidebar 상태 팔레트보다 강해지지 않는다.
- 지속 행의 Workspace title은 primary hierarchy, 보조 계정 정보는 system
  secondary foreground를 사용한다. 후행 disclosure는 선행 Workspace mark보다
  작아야 한다.
- 현재 Context는 작은 check로 구분하고 접근성 트리에도 selected 상태로 노출한다.
  선택되었다는 이유만으로 행 전체를 채우지 않는다.
- 메뉴를 포인터로 열 때 첫 행에 자동 focus fill을 그리지 않는다. Hover·press는
  하나의 옅은 상태 면으로, 키보드 이동은 system focus ring으로 표시한다.
  위·아래·Home·End는 활성 행 사이를 이동하고 왼쪽·Escape는 상위 화면으로 돌아간다.
- 이동과 선택은 native vector icon으로 표시한다. Memory 상태는 상세 화면에서
  작은 보조 텍스트로 표시하며 Root의 제목 공간을 차지하지 않는다.
- Root·하위 action은 한 줄 28 DIP다. Context 선택 행은 44 DIP 두 줄이다. Memory 설명은 가용 폭에
  맞춰 줄바꿈하고, provider가 없으면 조작 행을 비활성화한다.
- 생성된 로컬 이름, 메뉴, 상태와 접근성 문구는 GRIT의 애플리케이션 언어를
  따른다. 실제 provider와 Chromium Profile이 제공한 이름은 번역하지 않는다.
- 메뉴 폭은 사이드바와 독립적이다. 실제 언어의 명령·작업공간·계정 글자 폭을
  측정해 224–288 DIP 안에서 정하고 하위 화면에서도 같은 폭을 유지한다.
  사이드바 폭이 변하면 anchor 위치만 따라가며 메뉴 폭을 늘리거나 줄이지 않는다.
- 메뉴는 Footer와 8 DIP 간격을 두어 두 surface의 경계를 유지한다. 간격을 위해
  고정 화면 좌표를 쓰지 않고 Footer anchor를 이동시켜 창 이동·폭 변경을 따라간다.
- 메뉴는 elevated system surface, 10 DIP 곡률과 플랫폼 dialog shadow를 사용한다.
  macOS에서는 native window shadow에 소프트웨어 그림자 테두리를 덧붙이지 않는다.
  진입과 화면 교체는 위치 이동 없이 짧은 opacity 전환만 사용하며 시스템의
  reduced motion과 focus ring 정책을 그대로 따른다.
- 구분선은 팝업 양 끝까지 이어지고, 행·설명의 좌우 여백과 분리한다. 라이트
  모드에서는 system neutral outline으로 색 대비를 분명히 하고, 다크 모드에서는
  기존 menu separator를 유지한다. 두 모드 모두 선 두께는 1 DIP다.

## 회귀 계약

순수 테스트는 화면 계층, Back 목적지, Root 목적지 집합, Browser tools의 제한된 action
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
