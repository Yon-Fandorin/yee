// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_TAB_GROUP_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_TAB_GROUP_PRESENTATION_H_

#include <memory>
#include <optional>

#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/gfx/geometry/insets.h"

class TabGroup;
namespace tabs {
class TabInterface;
}
namespace views {
class Background;
class View;
}  // namespace views

namespace yee {
enum class TabGroupPreviewStyle { kHorizontal, kSidebar };

bool UsesHorizontalTabGroupCapsule(const views::View& view);
// 접힌 알약 안의 멤버는 native 탭을 유지하며 내부 조작만 숨긴다.
bool ShouldHideHorizontalTabGroupMemberContents(const views::View& view,
                                                const tabs::TabInterface* tab);
int ResolveHorizontalTabGroupEdgeInset(const views::View& decoration,
                                       int native_marker_inset,
                                       int native_tab_paint_inset);
// 이름과 접힌 favicon 요약을 공통 배경의 안쪽 여백에 배치한다.
gfx::Insets ResolveHorizontalTabGroupHeaderInsets(const views::View& header,
                                                  int native_content_inset,
                                                  int native_tab_paint_inset,
                                                  bool collapsed);
// 일반 그룹 멤버의 파비콘 전체와 공통 배경 안쪽 여백을 보장한다.
int ResolveHorizontalTabGroupMinimumWidth(const views::View& tab,
                                          int native_minimum_width,
                                          int native_content_inset,
                                          int native_tab_paint_inset);
// 논리적 앞쪽 경계의 DIP 이동량이다. native가 뒤쪽·RTL 방향을 반영하고,
// 같은 그룹 안의 구분선은 기존 위치를 유지한다.
int ResolveHorizontalTabGroupSeparatorOffset(
    const views::View& tab,
    std::optional<tab_groups::TabGroupId> group,
    std::optional<tab_groups::TabGroupId> adjacent_group);

// native 호스트가 실제 행 안의 inset 멤버 범위를 전달한다.
gfx::Rect ResolveHorizontalTabGroupCapsuleBounds(gfx::Rect group_bounds);

// native 행·클릭 범위는 유지하고 장식의 높이만 접힘 상태에 맞춰 전환한다.
void SetHorizontalTabGroupCapsuleCollapsed(views::View& decoration,
                                           bool collapsed);

// 닫히는 첫 멤버가 그룹 이름을 가로지르지 않게 Header 뒤에서 수축한다.
gfx::Rect ResolveHorizontalTabGroupClosingBounds(gfx::Rect closing_bounds,
                                                 int header_end,
                                                 int overlap);
// 연속 닫기 중에도 살아 있는 Header의 새 위치를 따라간다.
void TrackHorizontalTabGroupClosingHeader(views::View& tab,
                                          views::View& header);
views::View* GetHorizontalTabGroupClosingHeader(const views::View& tab);
// 폭이 그대로인 첫 전환 프레임에도 멤버의 제목·아이콘 가시성을 갱신한다.
void InvalidateHorizontalTabGroupMemberLayout(views::View& member);
void PaintHorizontalTabGroupCapsule(gfx::Canvas& canvas,
                                    const views::View& decoration,
                                    SkColor group_color);
std::unique_ptr<views::Background> CreateHorizontalTabGroupCapsuleBackground(
    SkColor group_color);

// 그룹의 공통 알약은 접힘·펼침을 계속 소유하며 Header에는 hover만 더한다.
// 세로 Sidebar는 별도 행 표현을 유지한다.
// insets는 논리적 여백이며 그릴 때 RTL 방향을 반영한다.
std::unique_ptr<views::Background> CreateHorizontalTabGroupBackground(
    SkColor group_color,
    bool hovered = false,
    gfx::Insets insets = {});

std::optional<HorizontalTabColors> ResolveHorizontalTabGroupColors(
    const views::View& header,
    SkColor group_color,
    bool hovered);
SkColor ResolveHorizontalTabGroupForeground(const views::View& header,
                                            SkColor group_color,
                                            bool hovered = false);
// 모델 순서의 파비콘 요약이다. native 헤더가 입력과 접근성을 유지하며,
// `before`가 요약 뒤에 놓을 버튼의 기준이 된다.
void UpdateTabGroupPreview(views::View& header,
                           const TabGroup& group,
                           bool collapsed,
                           TabGroupPreviewStyle style,
                           views::View* before = nullptr);
void UpdateTabGroupPreviewColors(views::View& header, bool hovered);
// hover 갱신은 파비콘 요약의 배치와 폭을 다시 늘리지 않는다.
void UpdateHorizontalTabGroupChipColors(views::View& header,
                                        views::View& chip,
                                        SkColor group_color,
                                        bool hovered);
void UpdateHorizontalTabGroupChip(views::View& header,
                                  views::View& chip,
                                  const TabGroup& group,
                                  SkColor group_color,
                                  bool collapsed);
int GetTabGroupPreviewWidth(const views::View& header);
}  // namespace yee
#endif
