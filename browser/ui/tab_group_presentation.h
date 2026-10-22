// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_YEE_TAB_GROUP_PRESENTATION_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_TAB_GROUP_PRESENTATION_H_

#include <memory>
#include <optional>

#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"
#include "components/tab_groups/tab_group_id.h"
#include "ui/gfx/geometry/insets.h"

class TabGroup;
namespace views {
class Background;
class View;
}  // namespace views

namespace yee {
enum class TabGroupPreviewStyle { kHorizontal, kSidebar };

bool UsesHorizontalTabGroupCapsule(const views::View& view);
int ResolveHorizontalTabGroupEdgeInset(const views::View& decoration,
                                       int native_marker_inset,
                                       int native_tab_paint_inset);
// 펼친 그룹 이름은 공통 배경의 앞쪽 여백 안에 배치한다.
gfx::Insets ResolveHorizontalTabGroupHeaderInsets(const views::View& header,
                                                  int native_content_inset,
                                                  int native_tab_paint_inset,
                                                  bool collapsed);
// 논리적 앞쪽 경계의 DIP 이동량이다. native가 뒤쪽·RTL 방향을 반영하고,
// 같은 그룹 안의 구분선은 기존 위치를 유지한다.
int ResolveHorizontalTabGroupSeparatorOffset(
    const views::View& tab,
    std::optional<tab_groups::TabGroupId> group,
    std::optional<tab_groups::TabGroupId> adjacent_group);

// native 호스트가 실제 행 안의 inset 멤버 범위를 전달한다.
gfx::Rect ResolveHorizontalTabGroupCapsuleBounds(gfx::Rect group_bounds);
void PaintHorizontalTabGroupCapsule(gfx::Canvas& canvas,
                                    const views::View& decoration,
                                    SkColor group_color);
std::unique_ptr<views::Background> CreateHorizontalTabGroupCapsuleBackground(
    SkColor group_color);

// 두 native 가로 호스트는 전체 hit 영역 안에 같은 작은 chip을 그린다.
// 세로 Sidebar는 별도 행 표현을 유지한다.
std::unique_ptr<views::Background> CreateHorizontalTabGroupBackground(
    SkColor color,
    bool collapsed,
    bool hovered = false);

std::optional<HorizontalTabColors> ResolveHorizontalTabGroupColors(
    const views::View& header,
    SkColor group_color,
    bool hovered);
SkColor ResolveHorizontalTabGroupForeground(const views::View& header,
                                            SkColor group_color);
// 모델 순서의 파비콘 요약이다. native 헤더가 입력과 접근성을 유지하며,
// `before`가 요약 뒤에 놓을 버튼의 기준이 된다.
void UpdateTabGroupPreview(views::View& header,
                           const TabGroup& group,
                           bool collapsed,
                           TabGroupPreviewStyle style,
                           views::View* before = nullptr);
void UpdateTabGroupPreviewColors(views::View& header, bool hovered);
void UpdateHorizontalTabGroupChip(views::View& header,
                                  views::View& chip,
                                  const TabGroup& group,
                                  SkColor group_color,
                                  bool collapsed);
int GetTabGroupPreviewWidth(const views::View& header);
}  // namespace yee
#endif
