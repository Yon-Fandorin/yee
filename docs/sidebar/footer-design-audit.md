# Service Design Audit: Sidebar Context Footer

## Intent

- Purpose: 현재 Workspace, Tenant, Account를 한눈에 확인하고 같은 진입점에서
  context 전환과 브라우저 자원으로 이동한다.
- User: 탭이 많은 데스크톱 브라우저 사용자 — tension: 콘텐츠를 방해하지 않는
  조용한 Sidebar 안에서 현재 작업 맥락은 즉시 확인해야 한다.
- Primary job: navigate / repeat
- Trust level: 현재 계정과 Workspace 경계를 오인하지 않을 정도의 높은 identity
  명확성
- Primary viewport(s): 244 DIP expanded desktop Sidebar (support-only: collapsed
  Sidebar에서는 Footer를 숨김)
- Scope: light/dark, active/inactive window, rest/hover/keyboard-focus/open popup,
  실제 계정명/local fallback, 짧고 긴 context 이름

## Findings

| id | severity | lens | title | evidence | fix | acceptance | verify | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| F1 | major | visual-craft-coherence | Trigger의 mark, 두 줄 text, `LP` avatar, chevron이 같은 세기로 경쟁함 (also: typography, color-contrast, information-architecture) | shot:`스크린샷 2026-08-29 오후 5.52.55.png`; ref: Linear/Notion workspace switcher는 한 개의 workspace mark와 name을 주 시선으로 사용 | Workspace mark만 tonal signature로 유지하고 subtitle, avatar, disclosure를 중립 계층으로 낮춘다. Local placeholder에는 파생 이니셜 대신 generic account icon을 사용한다. | 첫 시선은 Workspace name에 머물고, `Local profile`은 의미 없는 `LP`를 만들지 않으며, trailing cluster가 leading mark보다 작다. | test:`IdentityHierarchyUsesOneTonalMark`, `LocalPlaceholderUsesGenericAccountIcon`; needs-render: post-change native trigger | built |
| F2 | major | interaction-model | Yee state background와 HoverButton auto-highlight가 중복되어 hover/focus/open 표면이 무겁고 서로 다른 곡률이 드러났고, 중복 layer 제거 뒤에는 충분한 keyboard focus indicator와 popup/selection AX 의미가 필요했음 (also: accessibility-inclusion, component-consistency, surface-depth) | shot:`스크린샷 2026-08-29 오후 5.52.55.png`; source:`sidebar_footer.cc` custom `UpdateSurface`, explicit system `FocusRing`, popup/selection AX state | InkDrop은 ripple만 담당하고 visual state는 Yee의 한 rounded surface가 담당한다. Keyboard focus는 같은 path의 system focus ring으로, open/current state는 AX expanded/selected로 제공한다. | rest/hover/focus/open 배경은 한 겹이고 10 DIP path를 공유하며 system focus ring과 popup/current semantics가 존재한다. | test:`InteractionStatesUseOnlyTheYeeSurface`, `AccessibilityReportsPopupAndCurrentContext`; native macOS 13/13 pass | verified |
| F3 | minor | component-consistency | Popup의 action icon까지 container를 반복하고 Account/Memory에도 accent를 재사용해 Workspace identity와 일반 navigation의 위계가 흐려짐 (also: surface-depth, brand-expression) | source:`sidebar_footer.cc` `CreateMenuIcon`; needs-render; ref: Linear menu와 Notion/Slack sidebar action은 identity mark 외 action을 plain neutral glyph로 낮춤 | Workspace/context mark만 tonal container를 쓰고 action icon은 같은 크기의 plain neutral glyph slot으로 통일한다. Settings 하위 기능은 서로 다른 semantic icon을 쓰고 selected context는 check와 낮은 neutral surface로 표시한다. | popup에서 tonal square는 Workspace/context identity에만 나타나고 일반 action icon의 family, size, alignment가 일치한다. | test:`IdentityHierarchyUsesOneTonalMark`; needs-render: post-change native popup | built |
| F4 | minor | content-microcopy | 한 UTF-16 code unit로 initial을 만들면 supplementary Unicode 문자나 emoji가 깨질 수 있음 (also: accessibility-inclusion, trust-risk-consent) | source:`FirstMark`, `AccountMark`; skeptic pass | UTF-16 code point 경계로 표식을 추출하고 placeholder는 generic icon을 유지한다. | Workspace와 Account가 supplementary 문자로 시작해도 완전한 surrogate pair가 표시된다. | test:`IdentityMarksPreserveSupplementaryCodePoints` | verified |

## Lens coverage

- product-intent: examined — 하나의 Context Switcher라는 목적과 필요한 정보가 모두 있다.
- flow-continuity: examined — Root와 하위 화면의 Back/Escape/close 경로가 문서화되어 있다.
- information-architecture: F1 — identity 정보의 첫 시선 경쟁을 줄여야 한다.
- layout-composition: examined — 50 DIP 한 행과 Sidebar inset은 안정적이며 중첩 card를 추가하지 않는다.
- interaction-model: F2 — native highlight와 Yee state surface의 책임이 겹친다.
- forms-input: examined — 입력 surface가 없는 navigation component라 n/a.
- feedback-edge-states: examined — local fallback, provider unavailable, long-name tooltip 경계가 있다.
- efficiency-expert-use: examined — 한 행에서 context와 library/settings를 여는 반복 경로가 짧다.
- responsive-behavior: examined — desktop-only expanded Sidebar가 primary이고 collapse에서는 의도적으로 숨긴다.
- platform-convention-fit: examined — native HoverButton, BubbleDialog, vector icon, system theme를 유지한다.
- typography: F1 — Workspace와 context metadata의 계층을 더 분리해야 한다.
- color-contrast: F1 — 중립화하되 system color role과 최소 대비를 유지해야 한다.
- surface-depth: F2, F3 — 상태 layer 중복과 반복 icon container를 정리해야 한다.
- component-consistency: F2, F3 — trigger/menu가 동일 radius와 icon emphasis 규칙을 사용해야 한다.
- visual-craft-coherence: F1 — trigger가 assembled-from-parts처럼 보이는 원인을 통합한다.
- content-microcopy: F4 — 사용자 이름에서 만드는 짧은 표식도 Unicode 경계를 보존해야 한다.
- trust-risk-consent: examined — account/context identity는 표시하지만 위험 action은 Root에 노출하지 않는다.
- brand-expression: F3 — Yee의 tonal signature를 Workspace mark 한 곳에 집중한다.
- accessibility-inclusion: F2, F4 — keyboard focus, popup/current semantics,
  non-color disclosure/check, Unicode 표식을 유지해야 한다.
- perceived-performance: examined — 위치 이동 없이 짧은 opacity transition만 사용하고 reduced motion을 존중한다.

## Skeptic pass

- Reviewer: fresh-context `footer_polish_skeptic` — source, docs, pre-change shot,
  Linear/Notion/Slack references, native test output을 독립 검수했다.
- Missed: HoverButton에 FocusRing이 기본 설치되지 않는 점과 popup/current AX
  state 누락을 F2에 포함해 수정했다. UTF-16 initial 절단과 반복 gear icon도
  추가로 발견해 수정했다.
- Challenged: F1은 placeholder trust 문제 때문에 major를 유지하고, F2는 focus와
  AX까지 포함해 major를 유지했다. F3은 navigation을 막는 문제는 아니므로
  major에서 minor로 낮췄다.
- Re-verified: 실제 macOS GUI session에서 Sidebar Footer policy/view 13개가 모두
  통과했다. Computer Use가 `cgWindowNotFound`로 실패해 post-change native render는
  확보하지 못했으므로 F1/F3은 `built`로 남기고 시각 검수를 위임한다.
