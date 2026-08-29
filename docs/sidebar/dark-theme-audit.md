# Service Design Audit: Sidebar dark theme

## Intent
- Purpose: 페이지를 가리지 않으면서 Favorite, Group, Tab을 빠르게 탐색하는 데스크톱 브라우저 Sidebar
- User: 여러 탭을 반복적으로 오가는 사용자 — tension: 긴 세션에서도 상태를 즉시 구분하되 chrome이 콘텐츠보다 튀지 않아야 함
- Primary job: navigate
- Trust level: OS 테마와 일치하고 모든 탭 상태를 예측할 수 있는 안정적인 browser chrome
- Primary viewport(s): macOS 데스크톱의 펼친 Sidebar (support-only: 접힌 rail, 비활성 창)
- Scope: light/dark × active/inactive window × resting/hover/active/dragging × Favorite/Group/Tab

## Findings

| id | severity | lens | title | evidence | fix | acceptance | verify | status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| F1 | major | color-contrast | 다크 모드가 밝은 frame theme를 그대로 사용해 OS appearance와 어긋남 (also: accessibility-inclusion, platform-convention-fit, brand-expression) | shot:/Users/yongjunkim/Desktop/스크린샷 2026-08-29 오전 12.40.45.png | frame hue를 보존하면서 color scheme별 shell 명도·채도를 정규화하고 native material과 Views가 같은 resolved surface를 사용 | 밝은 frame seed도 dark scheme에서는 어두운 shell이 되며 primary/secondary foreground가 실제 surface에서 AA 대비를 유지 | test:YeeShellColorTest.*; test:YeeSidebarItemColorTest.EveryStateSharesOneThemeAwarePalette | verified |
| F2 | major | surface-depth | 높은 상태 alpha가 active Tab과 Favorite을 별도 불투명 카드처럼 보이게 함 (also: interaction-model, feedback-edge-states) | shot:/Users/yongjunkim/Desktop/스크린샷 2026-08-29 오전 12.40.45.png; reference:/Users/yongjunkim/Desktop/스크린샷 2026-08-29 오전 12.40.11.png | shell의 최대 대비 endpoint에서 낮은 alpha state layer를 만들고 resting→hover→active→dragging 순서를 통일 | 모든 상태가 같은 shell material 안에서 구분되고 fill/stroke 강도가 단조 증가하며 active가 거의 검은 pill로 분리되지 않음 | test:YeeSidebarItemColorTest.* | verified |
| F3 | major | component-consistency | Tab, Group Header, Favorite과 native tint가 서로 다른 foreground/surface source를 사용함 (also: visual-craft-coherence) | shot:/Users/yongjunkim/Desktop/스크린샷 2026-08-29 오전 12.40.45.png | native tint와 Views shell이 하나의 shell resolver를 공유하고 Tab, Group, Favorite이 하나의 Sidebar item resolver를 공유 | light/dark와 active/inactive 창에서 shell material이 일치하고 모든 Sidebar 항목이 같은 primary/secondary/state 역할을 소비 | test:YeeSidebarItemColorTest.*; test:TabGroupHeaderViewTest.YeeVerticalHoverUpdatesSurfaceAndForegroundTogether | verified |

## Lens coverage
- product-intent: examined — Sidebar의 단일 목적은 반복 탐색이며 기능·정보 구조 변경은 필요하지 않음
- flow-continuity: examined — n/a; 이번 표면은 독립 탐색 chrome이며 다단계 입력 흐름이 없음
- information-architecture: examined — Favorite, Group, Tab의 현재 순서와 우선순위는 유지
- layout-composition: examined — geometry보다 색상 재료 계층이 문제이며 기존 메트릭은 유지
- interaction-model: F2 — 상태 구분은 있으나 강도 계층이 과도함
- forms-input: examined — n/a; Sidebar 자체 입력 form 없음
- feedback-edge-states: F2 — hover/active/dragging 상태 ladder를 함께 검증
- efficiency-expert-use: examined — 현재 직접 탐색과 drag 경로를 유지
- responsive-behavior: examined — 데스크톱 전용 primary viewport; 기존 폭·접힘 테스트 유지
- platform-convention-fit: F1 — macOS appearance와 shell 밝기가 일치해야 함
- typography: examined — 글꼴 크기·행간 문제는 두 이미지에서 발견되지 않음
- color-contrast: F1 — 실제 shell에 대한 scheme와 foreground 대비가 root cause
- surface-depth: F2 — opaque state card가 material continuity를 깨뜨림
- component-consistency: F3 — 동일 상태의 색 source가 컴포넌트별로 분산됨
- visual-craft-coherence: F3 — 분산된 색 source가 조립된 듯한 인상을 만듦
- content-microcopy: examined — n/a; 문구 변경 범위 아님
- trust-risk-consent: examined — n/a; 민감 행동이나 consent surface 없음
- brand-expression: F1 — theme hue는 유지하되 OS appearance와 tonal continuity 확보
- accessibility-inclusion: F1 — foreground와 focus/state가 색상 역할만으로 흐려지지 않게 대비 보장
- perceived-performance: examined — blur나 animation을 추가하지 않고 기존 단일 paint 경로를 유지

## Skeptic pass
- Reviewer: fresh-context `gpt-5.6-terra` implementation skeptic
- Missed: Group Header hover가 배경만 바꾸고 전경은 resting에 머물던 경로와, macOS native tint가 resolved shell과 별도 scheme 판단을 하던 경로. 전자는 foreground/background 동시 갱신과 창 활성 callback으로, 후자는 resolved tint 자체의 명도로 native material을 선택하도록 수정함.
- Challenged: split Tab이 Favorite surface를 잘못 소비할 수 있다는 지적은 paint 호출부를 다시 확인해 `pinned_`인 split에만 적용되는 의도된 Favorite 표현으로 분류함. F1·F2의 major 판단은 유지하고, F3는 native/shell material 역할과 item state 역할을 구분해 acceptance를 명확히 함.
- Re-verified: test:YeeShellColorTest.*; test:YeeSidebarItemColorTest.*; test:TabGroupHeaderViewTest.YeeVerticalHoverUpdatesSurfaceAndForegroundTogether. 최종 픽셀 인상은 자동 캡처 환경이 Yee 창을 읽지 못하므로 새 빌드의 실제 앱에서 light/dark × active/inactive 및 pinned/unpinned split을 수동 확인하는 경계로 남김.
