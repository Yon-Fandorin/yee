// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_

#include <memory>
#include <optional>
#include <string_view>

#include "base/functional/callback_forward.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation.h"
#include "chrome/browser/ui/views/yee/sidebar_presentation.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/button/button.h"

class ToolbarButton;

namespace ui {
class ColorProvider;
class NativeTheme;
}  // namespace ui

namespace views {
class Background;
class Border;
class View;
}  // namespace views

namespace yee {

struct ResolvedBrowserSurfaceDecoration;

// Header·그룹 chip·크기 조절 컨트롤이 공통 포커스 대비를 사용한다.
ui::ColorId ResolveBrowserSurfaceFocusColorId(
    const ui::ColorProvider& color_provider,
    SkColor background,
    ui::ColorId preferred);

// 행·Favorite·드래그 미리보기는 같은 테마와 전경 역할을 쓴다.
// 평평한 행의 선택 면은 지속 카드보다 옅게 표현한다.
enum class SidebarItemVisualState {
  kResting,
  kHovered,
  kActive,
  kDragging,
};

struct SidebarItemColors {
  SkColor fill = SK_ColorTRANSPARENT;
  SkColor stroke = SK_ColorTRANSPARENT;
  SkColor foreground = SK_ColorBLACK;
};

SidebarItemColors ResolveSidebarItemColors(
    const ui::ColorProvider& color_provider,
    SidebarItemVisualState state,
    double hover_progress,
    bool frame_active,
    bool persistent_surface);
enum class ShellCreateAction {
  kNewTab,
  kNewGroup,
  kNewProject,
  kChat,
};

using ShellCreateCallback =
    base::RepeatingCallback<void(ShellCreateAction action, int event_flags)>;

using BrowserSurfacePresentationCallback =
    base::RepeatingCallback<std::optional<BrowserSurfacePresentation>()>;

// Uses the page's rendered surface color as the Browser Surface Header, falling
// back to the current Toolbar color when the page does not provide one.
SkColor ResolveBrowserSurfaceHeaderColor(
    const ui::ColorProvider& color_provider,
    std::optional<SkColor> page_surface_color);

// 대비 선호와 별개인 강제 색 모드에서도 시스템 색상을 우선한다.
bool ShouldUseNativeBrowserSurfaceColors(const ui::NativeTheme& theme);

// Paints the compact Omnibox's full-height surface while keeping its optional
// one-DIP focus stroke inset from the control edge. This makes focus read as an
// internal state without changing native bounds or hit testing.
std::unique_ptr<views::Background> CreateBrowserSurfaceOmniboxBackground(
    SkColor background_color,
    SkColor focus_stroke_color);

// 입력창과 제안 목록을 분리하고 첫 행 위와 마지막 행 아래의 여백을 맞춘다.
// 결과 행의 좌우 여백과 곡률은 같은 값을 사용한다.
std::unique_ptr<views::Border> CreateBrowserSurfaceOmniboxResultsBorder();
std::unique_ptr<views::Background> CreateBrowserSurfaceOmniboxResultBackground(
    ui::ColorId color);

// Native 강조색 계산에 실제 컨트롤 바탕을 전달한다.
void SetBrowserSurfaceControlBackground(views::View& view,
                                        std::optional<SkColor> surface);

// Adds Yee's exact neutral popup roles after Chromium has populated an
// uncached provider with every registered platform, component, and Chrome
// mixer. High-contrast callers skip this function so native semantic and
// forced colors remain authoritative.
void AddBrowserSurfaceOmniboxPopupColorMixer(
    ui::ColorProvider& provider,
    const BrowserSurfacePresentation& presentation);
std::unique_ptr<ui::ColorProvider>
CreateBrowserSurfaceOmniboxPopupColorProvider(
    ui::ColorProviderKey key,
    std::optional<BrowserSurfacePresentation> presentation);

std::unique_ptr<views::Background> CreateShellBackground();

// Resolves the one opaque frame-derived material used by native glass, the
// Views shell, Sidebar items, and the split canvas. Dark mode retains the theme
// hue while bringing overly bright frame colors into a calm dark tonal range;
// light mode preserves the supplied frame color.
SkColor ResolveShellBackgroundColor(const ui::ColorProvider& color_provider,
                                    bool frame_active);

// Returns an opaque, theme-resolved proxy for the shell surface. Translucent
// Yee surfaces use this to calculate contrast without sampling desktop pixels.
SkColor ResolveShellContrastBackground(const ui::ColorProvider& color_provider);

// Returns the opaque Chrome Surface color inherited by Yee's split backing,
// pane outlines, and split controls. Split presentation deliberately does not
// follow either page's sampled color.
SkColor ResolveSplitCanvasColor(const ui::ColorProvider& color_provider);

std::unique_ptr<views::View> CreateCombinedSurfaceOutlineView(
    BrowserSurfacePresentationCallback presentation_callback);

// Creates the page-aware fill hosted by Chromium's existing animated main
// background region. The host owns compositor clipping and geometry; this
// non-interactive Yee child owns only Browser Surface color presentation.
std::unique_ptr<views::View> CreateBrowserSurfaceFillView(
    std::optional<BrowserSurfacePresentation> presentation);
void UpdateBrowserSurfaceFillView(
    views::View& view,
    std::optional<BrowserSurfacePresentation> presentation);

// Creates the non-interactive card backing for Chromium's native Side Panel.
// The native SidePanel remains the owner of content, focus, resizing, and
// commands; this view only supplies Yee's shared surface fill, outline, and
// shadow treatment.
std::unique_ptr<views::View> CreateSidePanelSurfaceView();

// 분할 구조가 바뀌면 다음 레이아웃의 최종 장식 적용 전에 공통 윤곽·그림자를
// 숨긴다.
void UpdateCombinedSurfaceOutlineView(views::View& view,
                                      bool split_presentation);

// native 배치 후 계산된 Browser Surface 장식을 적용한다. 구분선 좌표는
// 윤곽 호스트 기준이며, 그리기 단계에서 부모·형제의 구조로 정책을 추론하지
// 않는다.
void ApplyCombinedSurfaceDecoration(
    views::View& view,
    const ResolvedBrowserSurfaceDecoration& decoration);

// Side Panel의 채움·경계에 같은 표면 모드를 적용한다.
// native SidePanel이 공간 배분·가시성·입력·콘텐츠를 계속 소유한다.
void ApplySidePanelSurfaceDecoration(
    views::View& view,
    const ResolvedBrowserSurfaceDecoration& decoration);

// Split panes are independent cards aligned directly to Yee's Browser Surface.
// Chromium continues to own split layout, resizing, and semantic highlights.
gfx::RoundedCornersF ResolveSplitPaneRoundedCorners();
std::unique_ptr<views::View> CreateSplitPaneEmphasisView();
void UpdateSplitPaneEmphasisView(views::View& view,
                                 bool visible,
                                 bool emphasized);

// Non-interactive resting presentation layered over Chromium's real Omnibox.
// The native editor remains mounted underneath and is revealed on focus.
std::unique_ptr<views::View> CreateOmniboxRestingTextView();
void UpdateOmniboxRestingTextView(views::View& view,
                                  std::u16string_view title,
                                  std::u16string_view origin,
                                  SkColor background_color,
                                  bool visible);
void UpdateOmniboxRestingTextView(
    views::View& view,
    std::u16string_view title,
    std::u16string_view origin,
    const BrowserSurfacePresentation& presentation,
    bool visible,
    std::optional<SkColor> control_background = std::nullopt);

void ApplyShellControlStyle(ToolbarButton& button);
std::unique_ptr<views::View> CreateShellControlDivider();
void UpdateShellControlDivider(
    views::View& divider,
    const std::optional<BrowserSurfacePresentation>& presentation);

// Nudges a vertical-tab hover card away from the sidebar. Chromium's slide
// animator reads View::GetAnchorBoundsInScreen(), so TabView applies this
// there instead of inside the bubble.
gfx::Rect AdjustVerticalTabHoverCardAnchor(const gfx::Rect& bounds);

std::unique_ptr<ToolbarButton> CreateShellToolbarButton(
    views::Button::PressedCallback callback);
std::unique_ptr<ToolbarButton> CreateShellAddButton(
    ShellCreateCallback callback,
    SidebarPopupLockCallback acquire_popup_lock = {},
    bool projects_enabled = true);
std::unique_ptr<ToolbarButton> CreateAgentToolbarButton(
    views::Button::PressedCallback callback);

// Window-global Sidebar Header actions. This view owns one Create control and
// one Agent status control regardless of whether the Browser Surface is single
// or split. Chromium supplies command callbacks and the platform caption-button
// exclusion; Yee owns their presentation and geometry.
std::unique_ptr<views::View> CreateSidebarHeaderActionsView(
    ShellCreateCallback create_callback,
    views::Button::PressedCallback agent_callback,
    SidebarPopupLockCallback acquire_popup_lock = {},
    bool projects_enabled = true);
// 최소 폭은 움직이지 않는 창 버튼 영역을 기준으로 하여 hover 중에도 유지한다.
int GetSidebarHeaderMinimumWidth(int leading_exclusion);
void SetSidebarHeaderActionsLeadingExclusion(views::View& view,
                                             int leading_exclusion,
                                             int leading_offset = 0,
                                             int top_inset = 0);
void SetSidebarHeaderActionsControlsVisible(views::View& view, bool visible);
bool IsSidebarHeaderActionsPositionInWindowCaption(const views::View& view,
                                                   const gfx::Point& point);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
