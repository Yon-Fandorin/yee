// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr char kAgentStatusDemoSwitch[] = "yee-agent-status-demo";
constexpr char kAgentStatusSwitch[] = "yee-agent-status";
constexpr char kDisableYeeShellScaffoldSwitch[] = "disable-yee-shell-scaffold";

// Restore the pilot's original material strength while keeping both layers
// theme-derived. Native tint at 38%/72% plus Yee's 22/255 surface tint yields
// approximately 43% light and 74% dark effective opacity.
constexpr double kNativeGlassTintOpacityLight = 0.38;
constexpr double kNativeGlassTintOpacityDark = 0.72;
constexpr int kGlassSurfaceTintAlpha = 22;
constexpr int kShellMenuIconSize = 16;

enum class ShellCreateCommand {
  kNewTab = 1,
  kNewGroup,
  kChat,
};

class YeeShellBackground : public views::Background {
 public:
  YeeShellBackground() = default;
  YeeShellBackground(const YeeShellBackground&) = delete;
  YeeShellBackground& operator=(const YeeShellBackground&) = delete;
  ~YeeShellBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const ui::ColorProvider* const color_provider = view->GetColorProvider();
    CHECK(color_provider);
    const views::Widget* const widget = view->GetWidget();
    const bool is_active = !widget || widget->IsActive();
    SkColor shell_color = color_provider->GetColor(
        is_active ? ui::kColorFrameActive : ui::kColorFrameInactive);

    const ui::NativeTheme* const native_theme = view->GetNativeTheme();
    const bool use_glass =
        is_active && features::IsGlassFrameEnabled() && native_theme &&
        !native_theme->prefers_reduced_transparency();
    if (use_glass) {
      shell_color = SkColorSetA(shell_color, kGlassSurfaceTintAlpha);
    }

    // The native material supplies blur and desktop sampling on supported
    // macOS versions. Yee supplies the theme tint and opacity above it. Other
    // platforms, inactive windows, and reduced-transparency mode paint the
    // same theme color fully opaque.
    canvas->FillRect(bounds, shell_color);
    const views::View* const content_outline =
        view->GetViewByID(yee::kContentOutlineViewId);
    if (!content_outline) {
      return;
    }
    gfx::RectF content_rect(content_outline->bounds());
    if (content_rect.IsEmpty()) {
      return;
    }
    content_rect.Inset(0.5f);

    cc::PaintFlags content_surface_flags;
    content_surface_flags.setAntiAlias(true);
    content_surface_flags.setColor(SK_ColorWHITE);
    content_surface_flags.setStyle(cc::PaintFlags::kFill_Style);
    content_surface_flags.setLooper(gfx::CreateShadowDrawLooper({
        gfx::ShadowValue(gfx::Vector2d(0, 1), 3.0, SkColorSetARGB(13, 0, 0, 0)),
    }));
    canvas->DrawRoundRect(content_rect, yee::kContentCornerRadius,
                          content_surface_flags);

    cc::PaintFlags content_outline_flags;
    content_outline_flags.setAntiAlias(true);
    content_outline_flags.setColor(SkColorSetRGB(223, 223, 223));
    content_outline_flags.setStrokeWidth(1.0f);
    content_outline_flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawRoundRect(content_rect, yee::kContentCornerRadius,
                          content_outline_flags);
  }

};

class YeeContentOutlineView : public views::View {
  METADATA_HEADER(YeeContentOutlineView, views::View)

 public:
  YeeContentOutlineView() {
    SetID(yee::kContentOutlineViewId);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }
  YeeContentOutlineView(const YeeContentOutlineView&) = delete;
  YeeContentOutlineView& operator=(const YeeContentOutlineView&) = delete;
  ~YeeContentOutlineView() override = default;

  void OnPaint(gfx::Canvas* canvas) override {
    gfx::RectF content_rect(GetLocalBounds());
    if (content_rect.IsEmpty()) {
      return;
    }
    content_rect.Inset(0.5f);

    cc::PaintFlags content_outline_flags;
    content_outline_flags.setAntiAlias(true);
    content_outline_flags.setColor(SkColorSetRGB(223, 223, 223));
    content_outline_flags.setStrokeWidth(1.0f);
    content_outline_flags.setStyle(cc::PaintFlags::kStroke_Style);
    canvas->DrawRoundRect(content_rect, yee::kContentCornerRadius,
                          content_outline_flags);
  }
};

BEGIN_METADATA(YeeContentOutlineView)
END_METADATA

class YeeShellToolbarButton : public ToolbarButton {
 public:
  explicit YeeShellToolbarButton(PressedCallback callback)
      : ToolbarButton(std::move(callback)) {
    yee::ApplyShellControlStyle(*this);
  }
  YeeShellToolbarButton(PressedCallback callback,
                        std::unique_ptr<ui::MenuModel> menu_model)
      : ToolbarButton(std::move(callback),
                      std::move(menu_model),
                      nullptr,
                      /*trigger_menu_on_long_press=*/false) {
    yee::ApplyShellControlStyle(*this);
  }
  YeeShellToolbarButton(const YeeShellToolbarButton&) = delete;
  YeeShellToolbarButton& operator=(const YeeShellToolbarButton&) = delete;
  ~YeeShellToolbarButton() override = default;
};

class YeeShellAddButton : public ui::SimpleMenuModel::Delegate,
                          public YeeShellToolbarButton {
 public:
  explicit YeeShellAddButton(yee::ShellCreateCallback callback)
      : YeeShellToolbarButton(base::BindRepeating(&YeeShellAddButton::OpenMenu,
                                                  base::Unretained(this)),
                              std::make_unique<ui::SimpleMenuModel>(this)),
        callback_(std::move(callback)) {
    auto* const menu = static_cast<ui::SimpleMenuModel*>(menu_model());
    menu->AddItemWithIcon(
        static_cast<int>(ShellCreateCommand::kNewTab), u"New tab",
        ui::ImageModel::FromVectorIcon(kTabIcon, ui::kColorMenuIcon,
                                       kShellMenuIconSize));
    menu->AddItemWithIcon(
        static_cast<int>(ShellCreateCommand::kNewGroup), u"New group",
        ui::ImageModel::FromVectorIcon(kGroupCustomIcon, ui::kColorMenuIcon,
                                       kShellMenuIconSize));
    menu->AddItemWithIcon(
        static_cast<int>(ShellCreateCommand::kChat), u"Chat",
        ui::ImageModel::FromVectorIcon(vector_icons::kChatIcon,
                                       ui::kColorMenuIcon, kShellMenuIconSize));

    SetTooltipText(u"Create");
    GetViewAccessibility().SetName(u"Create");
  }

  YeeShellAddButton(const YeeShellAddButton&) = delete;
  YeeShellAddButton& operator=(const YeeShellAddButton&) = delete;
  ~YeeShellAddButton() override = default;

  void ExecuteCommand(int command_id, int event_flags) override {
    switch (static_cast<ShellCreateCommand>(command_id)) {
      case ShellCreateCommand::kNewTab:
        callback_.Run(yee::ShellCreateAction::kNewTab, event_flags);
        return;
      case ShellCreateCommand::kNewGroup:
        callback_.Run(yee::ShellCreateAction::kNewGroup, event_flags);
        return;
      case ShellCreateCommand::kChat:
        callback_.Run(yee::ShellCreateAction::kChat, event_flags);
        return;
    }
    NOTREACHED();
  }

 private:
  void OpenMenu(const ui::Event& event) {
    ShowDropDownMenu(ui::GetMenuSourceTypeForEvent(event));
  }

  yee::ShellCreateCallback callback_;
};

class YeeAgentToolbarButton : public YeeShellToolbarButton {
 public:
  enum class Status {
    kReady,
    kWorking,
    kNeedsInput,
  };

  explicit YeeAgentToolbarButton(PressedCallback callback)
      : YeeShellToolbarButton(std::move(callback)) {
    const base::CommandLine* const command_line =
        base::CommandLine::ForCurrentProcess();
    const std::string requested_status =
        command_line->GetSwitchValueASCII(kAgentStatusSwitch);
    if (requested_status == "working") {
      status_ = Status::kWorking;
    } else if (requested_status == "needs-input") {
      status_ = Status::kNeedsInput;
    }
    UpdateAccessibleText();

    if (requested_status.empty() &&
        command_line->HasSwitch(kAgentStatusDemoSwitch)) {
      demo_timer_.Start(FROM_HERE, base::Seconds(2.5), this,
                        &YeeAgentToolbarButton::AdvanceDemo);
    }
  }

  YeeAgentToolbarButton(const YeeAgentToolbarButton&) = delete;
  YeeAgentToolbarButton& operator=(const YeeAgentToolbarButton&) = delete;
  ~YeeAgentToolbarButton() override = default;

 protected:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    ToolbarButton::PaintButtonContents(canvas);

    SkColor status_color = SkColorSetRGB(91, 148, 134);
    if (status_ == Status::kNeedsInput) {
      status_color = SkColorSetRGB(190, 132, 78);
    }

    cc::PaintFlags halo_flags;
    halo_flags.setAntiAlias(true);
    halo_flags.setColor(SkColorSetA(status_color, 34));
    halo_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawCircle(gfx::PointF(15.0f, 15.0f), 7.0f, halo_flags);

    cc::PaintFlags pulse_flags;
    pulse_flags.setAntiAlias(true);
    pulse_flags.setColor(status_color);
    pulse_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawCircle(gfx::PointF(15.0f, 15.0f), 4.0f, pulse_flags);

    if (status_ == Status::kReady) {
      return;
    }

    gfx::RectF badge_rect(17.0f, 1.0f, 13.0f, 13.0f);
    cc::PaintFlags badge_border_flags;
    badge_border_flags.setAntiAlias(true);
    badge_border_flags.setColor(SkColorSetRGB(243, 243, 243));
    badge_border_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(badge_rect, 6.5f, badge_border_flags);

    badge_rect.Inset(1.5f);
    cc::PaintFlags badge_flags;
    badge_flags.setAntiAlias(true);
    badge_flags.setColor(status_ == Status::kWorking
                             ? SkColorSetRGB(15, 108, 92)
                             : SkColorSetRGB(166, 105, 48));
    badge_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(badge_rect, 5.0f, badge_flags);

    const gfx::FontList badge_font =
        gfx::FontList().Derive(-5, gfx::Font::NORMAL, gfx::Font::Weight::BOLD);
    canvas->DrawStringRectWithFlags(
        status_ == Status::kWorking ? u"2" : u"!", badge_font, SK_ColorWHITE,
        gfx::Rect(18, 1, 11, 12), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  void AdvanceDemo() {
    switch (status_) {
      case Status::kReady:
        status_ = Status::kWorking;
        break;
      case Status::kWorking:
        status_ = Status::kNeedsInput;
        break;
      case Status::kNeedsInput:
        status_ = Status::kReady;
        break;
    }
    UpdateAccessibleText();
    SchedulePaint();
  }

  void UpdateAccessibleText() {
    std::u16string label;
    switch (status_) {
      case Status::kReady:
        label = u"Agent activity, ready";
        break;
      case Status::kWorking:
        label = u"Agent activity, 2 findings ready";
        break;
      case Status::kNeedsInput:
        label = u"Agent activity, needs input";
        break;
    }
    SetTooltipText(label);
    GetViewAccessibility().SetName(label);
  }

  Status status_ = Status::kReady;
  base::RepeatingTimer demo_timer_;
};

}  // namespace

namespace yee {

bool IsShellEnabled() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      kDisableYeeShellScaffoldSwitch);
}

bool UsesExpandedSidebarPresentation() {
  return IsShellEnabled();
}

bool ShouldPrioritizeSidebarTabDrag(int dragged_tab_count,
                                    int source_tab_count,
                                    bool is_group_drag,
                                    bool uses_vertical_tab_strip) {
  return UsesExpandedSidebarPresentation() && uses_vertical_tab_strip &&
         !is_group_drag && dragged_tab_count == 1 && source_tab_count == 1;
}

std::unique_ptr<views::Background> CreateShellBackground() {
  return std::make_unique<YeeShellBackground>();
}

SkColor ResolveShellContrastBackground(
    const ui::ColorProvider& color_provider) {
  // Both native tint and Yee's overlay use this color, so their composition
  // resolves to the same opaque contrast anchor without sampling desktop
  // pixels. This keeps text contrast stable while a glass window moves.
  return color_provider.GetColor(ui::kColorFrameActive);
}

double GetNativeGlassTintOpacity(bool is_dark_mode) {
  return is_dark_mode ? kNativeGlassTintOpacityDark
                      : kNativeGlassTintOpacityLight;
}

std::unique_ptr<views::View> CreateContentOutlineView() {
  return std::make_unique<YeeContentOutlineView>();
}

gfx::Rect AdjustVerticalTabHoverCardAnchor(const gfx::Rect& bounds) {
  gfx::Rect adjusted = bounds;
  adjusted.Outset(
      gfx::Outsets().set_right(kSidebarMetrics.tab_hover_card_offset));
  return adjusted;
}

void ApplyShellControlStyle(ToolbarButton& button) {
  button.SetPreferredSize(gfx::Size(kShellControlSize, kShellControlSize));
  button.SetCustomCornerRadius(kShellControlCornerRadius);
  button.SetProperty(views::kMarginsKey,
                     gfx::Insets::VH(0, kShellControlHorizontalMargin));
}

std::unique_ptr<ToolbarButton> CreateShellToolbarButton(
    views::Button::PressedCallback callback) {
  return std::make_unique<YeeShellToolbarButton>(std::move(callback));
}

std::unique_ptr<ToolbarButton> CreateShellAddButton(
    ShellCreateCallback callback) {
  return std::make_unique<YeeShellAddButton>(std::move(callback));
}

std::unique_ptr<ToolbarButton> CreateAgentToolbarButton(
    views::Button::PressedCallback callback) {
  return std::make_unique<YeeAgentToolbarButton>(std::move(callback));
}

}  // namespace yee
