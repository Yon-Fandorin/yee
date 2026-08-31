// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/notreached.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/chrome_typography.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/ui_base_features.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/compositor/layer.h"
#include "ui/compositor_extra/shadow.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/outsets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/painter.h"
#include "ui/views/style/typography_provider.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_shadow.h"
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
constexpr float kRestingSeparatorOpacity = 0.34f;
constexpr int kRestingOriginMaximumWidth = 180;
constexpr float kHeaderPrimaryPreferredOpacity = 0.48f;
constexpr float kHeaderSecondaryPreferredOpacity = 0.34f;
constexpr float kHeaderDisabledOpacity = 0.28f;
constexpr float kHeaderFocusStrokePreferredOpacity = 0.44f;
constexpr float kOmniboxPopupHoverOpacity = 0.06f;
constexpr float kOmniboxPopupOutlineOpacity = 0.18f;
constexpr int kBrowserSurfaceShadowElevation = 3;
constexpr int kBrowserSurfaceOutlineAlpha = 0x18;
constexpr int kBrowserSurfaceActiveOutlineAlpha = 0x30;
constexpr int kSurfaceSeparatorAlpha = 0x14;
constexpr int kBrowserSurfaceShadowKeyAlphaLight = 0x14;
constexpr int kBrowserSurfaceShadowAmbientAlphaLight = 0x09;
constexpr int kBrowserSurfaceShadowKeyAlphaDark = 0x20;
constexpr int kBrowserSurfaceShadowAmbientAlphaDark = 0x10;
constexpr double kDarkShellMinimumLightness = 0.16;
constexpr double kDarkShellMaximumLightness = 0.26;
constexpr double kDarkShellMaximumSaturation = 0.44;
constexpr double kInactiveShellSaturationScale = 0.82;
constexpr double kInactiveShellLightnessScale = 0.90;
constexpr double kSidebarPrimaryForegroundOpacity = 0.88;
constexpr double kSidebarSecondaryForegroundOpacity = 0.64;

struct BrowserSurfaceShadowColors {
  SkColor key;
  SkColor ambient;
};

BrowserSurfaceShadowColors ResolveBrowserSurfaceShadowColors(
    const ui::ColorProvider& color_provider) {
  const bool dark =
      color_utils::IsDark(yee::ResolveShellContrastBackground(color_provider));
  return {
      SkColorSetA(SK_ColorBLACK, dark ? kBrowserSurfaceShadowKeyAlphaDark
                                      : kBrowserSurfaceShadowKeyAlphaLight),
      SkColorSetA(SK_ColorBLACK, dark ? kBrowserSurfaceShadowAmbientAlphaDark
                                      : kBrowserSurfaceShadowAmbientAlphaLight),
  };
}

void UpdateBrowserSurfaceShadow(views::ViewShadow& view_shadow,
                                const ui::ColorProvider& color_provider) {
  const BrowserSurfaceShadowColors colors =
      ResolveBrowserSurfaceShadowColors(color_provider);
  const ui::Shadow::ElevationToColorsMap elevation_colors{
      {kBrowserSurfaceShadowElevation, {colors.key, colors.ambient}}};
  view_shadow.shadow()->SetElevationToColorsMap(elevation_colors);
}

SkColor ResolveBrowserSurfaceOutlineColor(
    const ui::ColorProvider& color_provider,
    bool emphasized) {
  // The outer boundary belongs to Yee chrome, not to the current page. Using
  // the shell's opaque contrast proxy keeps single and split cards stable when
  // page-aware Header colors change during navigation or scrolling.
  return color_utils::BlendTowardMaxContrast(
      yee::ResolveShellContrastBackground(color_provider),
      emphasized ? kBrowserSurfaceActiveOutlineAlpha
                 : kBrowserSurfaceOutlineAlpha);
}

void PaintBrowserSurfaceOutline(gfx::Canvas* canvas,
                                const gfx::Rect& local_bounds,
                                float outer_radius,
                                const ui::ColorProvider& color_provider,
                                bool emphasized) {
  const float stroke_width = yee::kSidebarMetrics.browser_surface_outline_width;
  gfx::RectF stroke_bounds(local_bounds);
  stroke_bounds.Inset(stroke_width / 2.0f);
  if (stroke_bounds.IsEmpty()) {
    return;
  }

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeWidth(stroke_width);
  stroke.setColor(
      ResolveBrowserSurfaceOutlineColor(color_provider, emphasized));
  canvas->DrawRoundRect(stroke_bounds,
                        std::max(0.0f, outer_radius - stroke_width / 2.0f),
                        stroke);
}

SkColor ResolveSurfaceSeparatorColor(SkColor surface_color) {
  return color_utils::BlendTowardMaxContrast(surface_color,
                                             kSurfaceSeparatorAlpha);
}

SkColor ResolveCombinedSurfaceColor(const views::View& surface_outline,
                                    const ui::ColorProvider& color_provider);
bool IsCombinedSurfaceSplitPresentation(const views::View& surface_outline);

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
    SkColor shell_color =
        yee::ResolveShellBackgroundColor(*color_provider, is_active);

    const ui::NativeTheme* const native_theme = view->GetNativeTheme();
    const bool use_glass = is_active && features::IsGlassFrameEnabled() &&
                           native_theme &&
                           !native_theme->prefers_reduced_transparency();
    if (use_glass) {
      shell_color = SkColorSetA(shell_color, kGlassSurfaceTintAlpha);
    }

    // The native material supplies blur and desktop sampling on supported
    // macOS versions. Yee supplies the theme tint and opacity above it. Other
    // platforms, inactive windows, and reduced-transparency mode paint the
    // same theme color fully opaque.
    canvas->FillRect(bounds, shell_color);
    const views::View* const surface_outline =
        view->GetViewByID(yee::kCombinedSurfaceOutlineViewId);
    if (!surface_outline ||
        IsCombinedSurfaceSplitPresentation(*surface_outline)) {
      return;
    }
    gfx::RectF surface_rect(surface_outline->bounds());
    if (surface_rect.IsEmpty()) {
      return;
    }
    surface_rect.Inset(0.5f);
    const SkColor surface_color =
        ResolveCombinedSurfaceColor(*surface_outline, *color_provider);
    cc::PaintFlags surface_flags;
    surface_flags.setAntiAlias(true);
    surface_flags.setColor(surface_color);
    surface_flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(surface_rect,
                          yee::kSidebarMetrics.content_corner_radius,
                          surface_flags);
  }
};

class YeeOmniboxBackground : public views::Background {
 public:
  YeeOmniboxBackground(SkColor background_color, SkColor focus_stroke_color)
      : fill_painter_(views::Painter::CreateSolidRoundRectPainter(
            background_color,
            yee::kSidebarMetrics.content_corner_radius,
            gfx::Insets(),
            SkBlendMode::kSrcOver,
            /*antialias=*/true)) {
    if (focus_stroke_color != SK_ColorTRANSPARENT) {
      focus_painter_ = views::Painter::CreateRoundRectWith1PxBorderPainter(
          SK_ColorTRANSPARENT, focus_stroke_color,
          yee::kSidebarMetrics.content_corner_radius -
              yee::kSidebarMetrics.location_bar_focus_stroke_inset,
          SkBlendMode::kSrcOver, /*antialias=*/true,
          /*should_border_scale=*/true);
    }
  }
  YeeOmniboxBackground(const YeeOmniboxBackground&) = delete;
  YeeOmniboxBackground& operator=(const YeeOmniboxBackground&) = delete;
  ~YeeOmniboxBackground() override = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    views::Painter::PaintPainterAt(canvas, fill_painter_.get(),
                                   view->GetLocalBounds());
    if (!focus_painter_) {
      return;
    }

    gfx::Rect focus_bounds = view->GetLocalBounds();
    focus_bounds.Inset(yee::kSidebarMetrics.location_bar_focus_stroke_inset);
    if (!focus_bounds.IsEmpty()) {
      views::Painter::PaintPainterAt(canvas, focus_painter_.get(),
                                     focus_bounds);
    }
  }

 private:
  std::unique_ptr<views::Painter> fill_painter_;
  std::unique_ptr<views::Painter> focus_painter_;
};

class YeeCombinedSurfaceOutlineView : public views::View {
  METADATA_HEADER(YeeCombinedSurfaceOutlineView, views::View)

 public:
  explicit YeeCombinedSurfaceOutlineView(
      yee::BrowserSurfacePresentationCallback presentation_callback)
      : presentation_callback_(std::move(presentation_callback)) {
    SetID(yee::kCombinedSurfaceOutlineViewId);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
    view_shadow_ = std::make_unique<views::ViewShadow>(
        this, kBrowserSurfaceShadowElevation);
    view_shadow_->SetRoundedCornerRadius(
        yee::kSidebarMetrics.content_corner_radius);
  }
  YeeCombinedSurfaceOutlineView(const YeeCombinedSurfaceOutlineView&) = delete;
  YeeCombinedSurfaceOutlineView& operator=(
      const YeeCombinedSurfaceOutlineView&) = delete;
  ~YeeCombinedSurfaceOutlineView() override = default;

  void SetSplitPresentation(bool split_presentation) {
    if (split_presentation_ == split_presentation) {
      return;
    }
    split_presentation_ = split_presentation;
    view_shadow_->shadow()->layer()->SetVisible(!split_presentation_);
    SchedulePaint();
    if (parent()) {
      parent()->SchedulePaint();
    }
  }

  SkColor ResolveSurfaceColor(const ui::ColorProvider& color_provider) const {
    if (split_presentation_) {
      return yee::ResolveSplitCanvasColor(color_provider);
    }
    const std::optional<yee::BrowserSurfacePresentation> presentation =
        presentation_callback_.Run();
    const bool native_colors = GetNativeTheme()->preferred_contrast() ==
                               ui::NativeTheme::PreferredContrast::kMore;
    return presentation.has_value() && !native_colors &&
                   presentation->palette_mode ==
                       yee::BrowserSurfacePresentation::PaletteMode::
                           kCustomSurface
               ? presentation->surface
               : color_provider.GetColor(kColorToolbar);
  }

  bool split_presentation() const { return split_presentation_; }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    UpdateBrowserSurfaceShadow(*view_shadow_, *GetColorProvider());
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (split_presentation_) {
      return;
    }
    const gfx::Rect surface_bounds = GetLocalBounds();
    if (surface_bounds.IsEmpty()) {
      return;
    }
    const std::optional<yee::BrowserSurfacePresentation> presentation =
        presentation_callback_.Run();
    const bool custom_colors =
        presentation.has_value() &&
        GetNativeTheme()->preferred_contrast() !=
            ui::NativeTheme::PreferredContrast::kMore &&
        presentation->palette_mode ==
            yee::BrowserSurfacePresentation::PaletteMode::kCustomSurface;
    const SkColor separator_color =
        custom_colors
            ? presentation->header_separator
            : GetColorProvider()->GetColor(kColorToolbarContentAreaSeparator);
    PaintBrowserSurfaceOutline(canvas, surface_bounds,
                               yee::kSidebarMetrics.content_corner_radius,
                               *GetColorProvider(), /*emphasized=*/false);

    const float separator_y = yee::kSidebarMetrics.titlebar_height -
                              yee::kSidebarMetrics.content_gutter;
    if (!split_presentation_ && separator_y > surface_bounds.y() &&
        separator_y < surface_bounds.bottom()) {
      const float horizontal_inset =
          yee::kSidebarMetrics.browser_surface_outline_width / 2.0f;
      canvas->DrawLine(
          gfx::PointF(surface_bounds.x() + horizontal_inset, separator_y),
          gfx::PointF(surface_bounds.right() - horizontal_inset, separator_y),
          separator_color);
    }
  }

 private:
  yee::BrowserSurfacePresentationCallback presentation_callback_;
  std::unique_ptr<views::ViewShadow> view_shadow_;
  bool split_presentation_ = false;
};

SkColor ResolveCombinedSurfaceColor(const views::View& surface_outline,
                                    const ui::ColorProvider& color_provider) {
  CHECK_EQ(surface_outline.GetID(), yee::kCombinedSurfaceOutlineViewId);
  return static_cast<const YeeCombinedSurfaceOutlineView&>(surface_outline)
      .ResolveSurfaceColor(color_provider);
}

bool IsCombinedSurfaceSplitPresentation(const views::View& surface_outline) {
  CHECK_EQ(surface_outline.GetID(), yee::kCombinedSurfaceOutlineViewId);
  return static_cast<const YeeCombinedSurfaceOutlineView&>(surface_outline)
      .split_presentation();
}

class YeeSplitPaneEmphasisView : public views::View {
 public:
  YeeSplitPaneEmphasisView() {
    SetID(yee::kSplitPaneEmphasisViewId);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsInvisible(true);
    view_shadow_ = std::make_unique<views::ViewShadow>(
        this, kBrowserSurfaceShadowElevation);
    view_shadow_->SetRoundedCornerRadius(
        yee::kSidebarMetrics.split_card_corner_radius);
    SetVisible(false);
  }
  YeeSplitPaneEmphasisView(const YeeSplitPaneEmphasisView&) = delete;
  YeeSplitPaneEmphasisView& operator=(const YeeSplitPaneEmphasisView&) = delete;
  ~YeeSplitPaneEmphasisView() override = default;

  void SetState(bool visible, bool emphasized) {
    if (GetVisible() == visible && emphasized_ == emphasized) {
      return;
    }
    emphasized_ = emphasized;
    SetVisible(visible);
    if (visible) {
      UpdateShadowColors();
    }
    SchedulePaint();
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (GetVisible()) {
      UpdateShadowColors();
    }
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const ui::ColorProvider* const color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }
    PaintBrowserSurfaceOutline(canvas, GetLocalBounds(),
                               yee::kSidebarMetrics.split_card_corner_radius,
                               *color_provider, emphasized_);
  }

 private:
  void UpdateShadowColors() {
    UpdateBrowserSurfaceShadow(*view_shadow_, *GetColorProvider());
    SchedulePaint();
  }

  std::unique_ptr<views::ViewShadow> view_shadow_;
  bool emphasized_ = false;
};

class YeeOmniboxRestingTextView : public views::View {
 public:
  YeeOmniboxRestingTextView() {
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsIgnored(true);

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    layout->set_cross_axis_alignment(views::LayoutAlignment::kCenter);

    const gfx::FontList omnibox_font = views::TypographyProvider::Get().GetFont(
        CONTEXT_OMNIBOX_PRIMARY, views::style::STYLE_PRIMARY);

    auto origin = std::make_unique<views::Label>();
    origin->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    origin->SetElideBehavior(gfx::ELIDE_HEAD);
    origin->SetFontList(
        omnibox_font.Derive(0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
    origin->SetAutoColorReadabilityEnabled(false);
    origin->GetViewAccessibility().SetIsIgnored(true);
    origin_ = AddChildView(std::move(origin));

    auto separator = std::make_unique<views::View>();
    separator->SetPreferredSize(gfx::Size(1, 18));
    separator->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 12));
    separator->GetViewAccessibility().SetIsIgnored(true);
    separator_ = AddChildView(std::move(separator));

    auto title = std::make_unique<views::Label>();
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetElideBehavior(gfx::FADE_TAIL);
    title->SetFontList(omnibox_font);
    title->SetAutoColorReadabilityEnabled(false);
    title->GetViewAccessibility().SetIsIgnored(true);
    title_ = AddChildView(std::move(title));
    layout->SetFlexForView(title_, 1);
  }

  YeeOmniboxRestingTextView(const YeeOmniboxRestingTextView&) = delete;
  YeeOmniboxRestingTextView& operator=(const YeeOmniboxRestingTextView&) =
      delete;
  ~YeeOmniboxRestingTextView() override = default;

  void Update(std::u16string_view title,
              std::u16string_view origin,
              SkColor background_color,
              SkColor primary,
              SkColor secondary,
              SkColor divider,
              bool visible) {
    title_->SetText(std::u16string(title));
    origin_->SetText(std::u16string(origin));
    origin_->SetPreferredSize(std::nullopt);
    gfx::Size origin_size = origin_->GetPreferredSize();
    origin_size.set_width(
        std::min(origin_size.width(), kRestingOriginMaximumWidth));
    origin_->SetPreferredSize(origin_size);

    if (background_color_ != background_color) {
      background_color_ = background_color;
      SetBackground(views::CreateSolidBackground(background_color));
      origin_->SetBackgroundColor(background_color);
      title_->SetBackgroundColor(background_color);
    }

    origin_->SetEnabledColor(primary);
    title_->SetEnabledColor(secondary);
    separator_->SetBackground(views::CreateSolidBackground(divider));
    SetVisible(visible);
  }

 private:
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::View> separator_ = nullptr;
  raw_ptr<views::Label> origin_ = nullptr;
  SkColor background_color_ = gfx::kPlaceholderColor;
};

BEGIN_METADATA(YeeCombinedSurfaceOutlineView)
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

class YeeSidebarHeaderActionsView : public views::View {
 public:
  YeeSidebarHeaderActionsView(yee::ShellCreateCallback create_callback,
                              views::Button::PressedCallback agent_callback) {
    SetID(yee::kSidebarHeaderActionsViewId);
    auto add_button = yee::CreateShellAddButton(std::move(create_callback));
    add_button->SetVectorIcon(kAddIcon);
    add_button->SetID(yee::kSidebarHeaderCreateViewId);
    add_button_ = AddChildView(std::move(add_button));
    auto agent_button =
        yee::CreateAgentToolbarButton(std::move(agent_callback));
    agent_button->SetID(yee::kSidebarHeaderAgentViewId);
    agent_button_ = AddChildView(std::move(agent_button));
  }
  YeeSidebarHeaderActionsView(const YeeSidebarHeaderActionsView&) = delete;
  YeeSidebarHeaderActionsView& operator=(const YeeSidebarHeaderActionsView&) =
      delete;
  ~YeeSidebarHeaderActionsView() override = default;

  void SetLeadingExclusion(int leading_exclusion) {
    leading_exclusion = std::max(0, leading_exclusion);
    if (leading_exclusion_ == leading_exclusion) {
      return;
    }
    leading_exclusion_ = leading_exclusion;
    InvalidateLayout();
  }

  void SetControlsVisible(bool visible) {
    add_button_->SetVisible(visible);
    agent_button_->SetVisible(visible);
  }

  bool IsPositionInWindowCaption(const gfx::Point& point) const {
    for (const ToolbarButton* button : {add_button_, agent_button_}) {
      if (button && button->GetVisible() && button->bounds().Contains(point)) {
        return false;
      }
    }
    return true;
  }

  void Layout(PassKey) override {
    const int control_size = yee::kSidebarMetrics.shell_control_size;
    const int control_margin =
        yee::kSidebarMetrics.shell_control_horizontal_margin;
    int x =
        std::max(leading_exclusion_,
                 yee::kSidebarMetrics.sidebar_header_controls_leading_inset());
    const int y = std::max(0, (height() - control_size) / 2);

    for (ToolbarButton* button : {add_button_, agent_button_}) {
      x += control_margin;
      const gfx::Rect leading_bounds(x, y, control_size, control_size);
      button->SetBoundsRect(GetMirroredRect(leading_bounds));
      x += control_size + control_margin;
    }
  }

  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    const int controls_width =
        2 * (yee::kSidebarMetrics.shell_control_size +
             2 * yee::kSidebarMetrics.shell_control_horizontal_margin);
    return gfx::Size(
        yee::kSidebarMetrics.sidebar_header_controls_leading_inset() +
            controls_width,
        yee::kSidebarMetrics.titlebar_height);
  }

 private:
  raw_ptr<ToolbarButton> add_button_ = nullptr;
  raw_ptr<ToolbarButton> agent_button_ = nullptr;
  int leading_exclusion_ = 0;
};

}  // namespace

namespace yee {

SidebarItemColors ResolveSidebarItemColors(
    const ui::ColorProvider& color_provider,
    SidebarItemVisualState state,
    double hover_progress,
    bool frame_active,
    bool persistent_surface) {
  hover_progress = std::clamp(hover_progress, 0.0, 1.0);

  int fill_alpha =
      persistent_surface ? kSidebarMetrics.favorites_cell_fill_alpha : 0;
  int stroke_alpha =
      persistent_surface ? kSidebarMetrics.favorites_cell_stroke_alpha : 0;
  switch (state) {
    case SidebarItemVisualState::kResting:
      break;
    case SidebarItemVisualState::kHovered:
      if (persistent_surface) {
        fill_alpha = static_cast<int>(std::lround(std::lerp(
            static_cast<double>(kSidebarMetrics.favorites_cell_fill_alpha),
            static_cast<double>(
                kSidebarMetrics.favorites_cell_hover_fill_alpha),
            hover_progress)));
        stroke_alpha = static_cast<int>(std::lround(std::lerp(
            static_cast<double>(kSidebarMetrics.favorites_cell_stroke_alpha),
            static_cast<double>(
                kSidebarMetrics.favorites_cell_active_stroke_alpha),
            hover_progress * 0.42)));
      } else {
        fill_alpha = static_cast<int>(std::lround(
            kSidebarMetrics.favorites_cell_fill_alpha * hover_progress));
      }
      break;
    case SidebarItemVisualState::kActive:
      fill_alpha = kSidebarMetrics.favorites_cell_active_fill_alpha;
      stroke_alpha = kSidebarMetrics.favorites_cell_active_stroke_alpha;
      break;
    case SidebarItemVisualState::kDragging:
      fill_alpha = kSidebarMetrics.favorites_cell_drag_fill_alpha;
      stroke_alpha = kSidebarMetrics.favorites_cell_drag_stroke_alpha;
      break;
  }

  if (!frame_active) {
    fill_alpha = static_cast<int>(std::lround(fill_alpha * 0.78));
    stroke_alpha = static_cast<int>(std::lround(stroke_alpha * 0.78));
  }

  const SkColor shell =
      ResolveShellBackgroundColor(color_provider, frame_active);
  const SkColor endpoint = color_utils::GetColorWithMaxContrast(shell);
  const SkColor fill = SkColorSetA(endpoint, fill_alpha);
  int strongest_fill_alpha = kSidebarMetrics.favorites_cell_drag_fill_alpha;
  int resting_fill_alpha = kSidebarMetrics.favorites_cell_fill_alpha;
  if (!frame_active) {
    strongest_fill_alpha =
        static_cast<int>(std::lround(strongest_fill_alpha * 0.78));
    resting_fill_alpha =
        static_cast<int>(std::lround(resting_fill_alpha * 0.78));
  }
  // Resolve each role against its strongest applicable state layer. Components
  // keep identical RGB roles without making the resting role as bright as the
  // active and dragging roles.
  const SkColor strongest_painted_fill = color_utils::GetResultingPaintColor(
      SkColorSetA(endpoint, strongest_fill_alpha), shell);
  const SkColor resting_painted_fill = color_utils::GetResultingPaintColor(
      SkColorSetA(endpoint, resting_fill_alpha), shell);
  const auto foreground_for_opacity = [endpoint](SkColor painted_fill,
                                                 double opacity) {
    const SkColor preferred = color_utils::AlphaBlend(
        endpoint, painted_fill, static_cast<float>(opacity));
    return color_utils::BlendForMinContrast(
               preferred, painted_fill, endpoint,
               color_utils::kMinimumReadableContrastRatio)
        .color;
  };
  const SkColor primary = foreground_for_opacity(
      strongest_painted_fill, kSidebarPrimaryForegroundOpacity);
  const SkColor secondary = foreground_for_opacity(
      resting_painted_fill, kSidebarSecondaryForegroundOpacity);
  SkColor foreground = secondary;
  if (frame_active) {
    switch (state) {
      case SidebarItemVisualState::kResting:
        break;
      case SidebarItemVisualState::kHovered:
        foreground = color_utils::AlphaBlend(
            primary, secondary, static_cast<float>(hover_progress));
        break;
      case SidebarItemVisualState::kActive:
      case SidebarItemVisualState::kDragging:
        foreground = primary;
        break;
    }
  }

  return {
      .fill = fill,
      .stroke = SkColorSetA(endpoint, stroke_alpha),
      .foreground = foreground,
  };
}

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

SkColor ResolveBrowserSurfaceHeaderColor(
    const ui::ColorProvider& color_provider,
    std::optional<SkColor> page_surface_color) {
  const SkColor toolbar = color_provider.GetColor(kColorToolbar);
  if (!page_surface_color.has_value()) {
    return toolbar;
  }
  return SkColorSetA(*page_surface_color, SK_AlphaOPAQUE);
}

BrowserSurfaceHeaderColors ResolveBrowserSurfaceHeaderColors(
    SkColor surface_color) {
  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  // Chromium's general-purpose dark endpoint is intentionally softer than
  // black, so it cannot reach 4.5:1 on every mid-luminance page color. Header
  // text is small and must meet the readable-text contract for every sampled
  // surface; choose the higher-contrast physical black/white endpoint here.
  const SkColor endpoint = color_utils::PickContrastingColor(
      SK_ColorBLACK, SK_ColorWHITE, surface_color);
  const SkColor preferred_primary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderPrimaryPreferredOpacity);
  const SkColor preferred_secondary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderSecondaryPreferredOpacity);
  return {
      .primary = color_utils::BlendForMinContrast(
                     preferred_primary, surface_color, endpoint,
                     color_utils::kMinimumReadableContrastRatio)
                     .color,
      .secondary = color_utils::BlendForMinContrast(
                       preferred_secondary, surface_color, endpoint,
                       color_utils::kMinimumReadableContrastRatio)
                       .color,
      .disabled = color_utils::AlphaBlend(endpoint, surface_color,
                                          kHeaderDisabledOpacity),
  };
}

SkColor ResolveBrowserSurfaceFocusStrokeColor(SkColor surface_color) {
  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  const SkColor dark_endpoint = SK_ColorBLACK;
  const SkColor endpoint =
      color_utils::GetContrastRatio(dark_endpoint, surface_color) >=
              color_utils::kMinimumVisibleContrastRatio
          ? dark_endpoint
          : SK_ColorWHITE;
  const SkColor preferred = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderFocusStrokePreferredOpacity);
  return color_utils::BlendForMinContrast(
             preferred, surface_color, endpoint,
             color_utils::kMinimumVisibleContrastRatio)
      .color;
}

std::unique_ptr<views::Background> CreateBrowserSurfaceOmniboxBackground(
    SkColor background_color,
    SkColor focus_stroke_color) {
  return std::make_unique<YeeOmniboxBackground>(background_color,
                                                focus_stroke_color);
}

BrowserSurfacePresentation ResolveBrowserSurfacePresentation(
    SkColor surface,
    uint64_t source_id,
    uint64_t revision,
    uint64_t popup_revision) {
  surface = SkColorSetA(surface, SK_AlphaOPAQUE);
  const BrowserSurfaceHeaderColors colors =
      ResolveBrowserSurfaceHeaderColors(surface);
  const SkColor endpoint = color_utils::GetColorWithMaxContrast(surface);
  return {
      .palette_mode = BrowserSurfacePresentation::PaletteMode::kCustomSurface,
      .source_id = source_id,
      .revision = revision,
      .popup_revision = popup_revision,
      .surface = surface,
      .primary = colors.primary,
      .secondary = colors.secondary,
      .disabled = colors.disabled,
      .location_hover = color_utils::AlphaBlend(endpoint, surface, 0.02f),
      .focus_stroke = ResolveBrowserSurfaceFocusStrokeColor(surface),
      .header_separator = ResolveSurfaceSeparatorColor(surface),
      .resting_divider =
          color_utils::AlphaBlend(endpoint, surface, kRestingSeparatorOpacity),
      .popup_hover =
          color_utils::AlphaBlend(endpoint, surface, kOmniboxPopupHoverOpacity),
      .popup_outline = color_utils::AlphaBlend(endpoint, surface,
                                               kOmniboxPopupOutlineOpacity),
  };
}

BrowserSurfacePresentation UseNativeBrowserSurfaceColors(
    const BrowserSurfacePresentation& presentation) {
  BrowserSurfacePresentation native = presentation;
  native.palette_mode = BrowserSurfacePresentation::PaletteMode::kNativeColors;
  return native;
}

void AddBrowserSurfaceOmniboxPopupColorMixer(
    ui::ColorProvider& provider,
    const BrowserSurfacePresentation& presentation) {
  CHECK_EQ(presentation.palette_mode,
           BrowserSurfacePresentation::PaletteMode::kCustomSurface);
  const SkColor endpoint =
      color_utils::GetColorWithMaxContrast(presentation.surface);
  ui::ColorMixer& mixer = provider.AddMixer();
  mixer[kColorOmniboxResultsBackground] = {presentation.surface};
  mixer[kColorOmniboxResultsBackgroundHovered] = {presentation.popup_hover};
  mixer[kColorOmniboxResultsBackgroundSelected] = {presentation.popup_hover};
  mixer[kColorOmniboxResultsBackgroundIph] = {presentation.popup_hover};
  mixer[kColorOmniboxResultsBackgroundHoverOverlay] = {
      SkColorSetA(endpoint, 0x0F)};
  mixer[kColorOmniboxBubbleOutline] = {presentation.popup_outline};
  mixer[kColorOmniboxResultsChipBackground] = {presentation.popup_hover};

  mixer[kColorOmniboxText] = {presentation.primary};
  mixer[kColorOmniboxTextDimmed] = {presentation.secondary};
  mixer[kColorOmniboxResultsTextSelected] = {presentation.primary};
  mixer[kColorOmniboxResultsTextAnswer] = {presentation.primary};
  mixer[kColorOmniboxResultsTextDimmed] = {presentation.secondary};
  mixer[kColorOmniboxResultsTextDimmedSelected] = {presentation.secondary};
  mixer[kColorOmniboxResultsTextSecondary] = {presentation.secondary};
  mixer[kColorOmniboxResultsTextSecondarySelected] = {presentation.secondary};
  mixer[kColorOmniboxResultsUrl] = {presentation.primary};
  mixer[kColorOmniboxResultsUrlSelected] = {presentation.primary};
  mixer[kColorOmniboxKeywordSelected] = {presentation.primary};
  mixer[kColorOmniboxKeywordSeparator] = {presentation.secondary};

  mixer[kColorOmniboxResultsIcon] = {presentation.primary};
  mixer[kColorOmniboxResultsIconSelected] = {presentation.primary};
  mixer[kColorOmniboxResultsButtonIcon] = {presentation.primary};
  mixer[kColorOmniboxResultsButtonIconSelected] = {presentation.primary};
  mixer[kColorOmniboxResultsButtonBorder] = {presentation.popup_outline};
  mixer[kColorOmniboxResultsIconGM3Background] = {presentation.popup_hover};
}

std::unique_ptr<ui::ColorProvider>
CreateBrowserSurfaceOmniboxPopupColorProvider(
    ui::ColorProviderKey key,
    std::optional<BrowserSurfacePresentation> presentation) {
  const bool use_native_colors =
      key.contrast_mode == ui::ColorProviderKey::ContrastMode::kHigh ||
      key.forced_colors != ui::ColorProviderKey::ForcedColors::kNone;
  const bool use_custom_presentation =
      presentation.has_value() &&
      presentation->palette_mode ==
          BrowserSurfacePresentation::PaletteMode::kCustomSurface &&
      !use_native_colors;
  if (use_custom_presentation) {
    key.color_mode = color_utils::IsDark(presentation->surface)
                         ? ui::ColorProviderKey::ColorMode::kDark
                         : ui::ColorProviderKey::ColorMode::kLight;
  }

  std::unique_ptr<ui::ColorProvider> provider =
      ui::ColorProviderManager::Get().CreateUncachedColorProvider(key);
  if (use_custom_presentation) {
    AddBrowserSurfaceOmniboxPopupColorMixer(*provider, *presentation);
  }
  return provider;
}

std::unique_ptr<views::Background> CreateShellBackground() {
  return std::make_unique<YeeShellBackground>();
}

SkColor ResolveShellBackgroundColor(const ui::ColorProvider& color_provider,
                                    bool frame_active) {
  const SkColor frame_color = color_provider.GetColor(
      frame_active ? ui::kColorFrameActive : ui::kColorFrameInactive);
  const bool dark_mode =
      color_utils::IsDark(color_provider.GetColor(ui::kColorSysBase));
  if (!dark_mode) {
    return SkColorSetA(frame_color, SK_AlphaOPAQUE);
  }

  color_utils::HSL hsl;
  color_utils::SkColorToHSL(frame_color, &hsl);
  hsl.l =
      std::clamp(hsl.l, kDarkShellMinimumLightness, kDarkShellMaximumLightness);
  hsl.s = std::min(hsl.s, kDarkShellMaximumSaturation);
  if (!frame_active) {
    hsl.s *= kInactiveShellSaturationScale;
    hsl.l *= kInactiveShellLightnessScale;
  }
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

SkColor ResolveShellContrastBackground(
    const ui::ColorProvider& color_provider) {
  // Both native tint and Yee's overlay use this color, so their composition
  // resolves to the same opaque contrast anchor without sampling desktop
  // pixels. This keeps text contrast stable while a glass window moves.
  return ResolveShellBackgroundColor(color_provider, /*frame_active=*/true);
}

SkColor ResolveSplitCanvasColor(const ui::ColorProvider& color_provider) {
  return ResolveShellContrastBackground(color_provider);
}

SkColor ResolveBrowserSurfaceSeparatorColor(SkColor surface_color) {
  return ResolveSurfaceSeparatorColor(surface_color);
}

double GetNativeGlassTintOpacity(bool is_dark_mode) {
  return is_dark_mode ? kNativeGlassTintOpacityDark
                      : kNativeGlassTintOpacityLight;
}

std::unique_ptr<views::View> CreateCombinedSurfaceOutlineView(
    BrowserSurfacePresentationCallback presentation_callback) {
  return std::make_unique<YeeCombinedSurfaceOutlineView>(
      std::move(presentation_callback));
}

void UpdateCombinedSurfaceOutlineView(views::View& view,
                                      bool split_presentation) {
  CHECK_EQ(view.GetID(), kCombinedSurfaceOutlineViewId);
  static_cast<YeeCombinedSurfaceOutlineView&>(view).SetSplitPresentation(
      split_presentation);
}

gfx::RoundedCornersF ResolveSplitPaneRoundedCorners() {
  return gfx::RoundedCornersF(kSidebarMetrics.split_card_corner_radius);
}

std::unique_ptr<views::View> CreateSplitPaneEmphasisView() {
  return std::make_unique<YeeSplitPaneEmphasisView>();
}

void UpdateSplitPaneEmphasisView(views::View& view,
                                 bool visible,
                                 bool emphasized) {
  CHECK_EQ(view.GetID(), kSplitPaneEmphasisViewId);
  static_cast<YeeSplitPaneEmphasisView&>(view).SetState(visible, emphasized);
}

std::unique_ptr<views::View> CreateOmniboxRestingTextView() {
  return std::make_unique<YeeOmniboxRestingTextView>();
}

void UpdateOmniboxRestingTextView(views::View& view,
                                  std::u16string_view title,
                                  std::u16string_view origin,
                                  SkColor background_color,
                                  bool visible) {
  const BrowserSurfacePresentation presentation =
      ResolveBrowserSurfacePresentation(background_color, 0, 0, 0);
  UpdateOmniboxRestingTextView(view, title, origin, presentation, visible);
}

void UpdateOmniboxRestingTextView(
    views::View& view,
    std::u16string_view title,
    std::u16string_view origin,
    const BrowserSurfacePresentation& presentation,
    bool visible) {
  static_cast<YeeOmniboxRestingTextView&>(view).Update(
      title, origin, presentation.surface, presentation.primary,
      presentation.secondary, presentation.resting_divider, visible);
}

gfx::Rect AdjustVerticalTabHoverCardAnchor(const gfx::Rect& bounds) {
  gfx::Rect adjusted = bounds;
  adjusted.Outset(
      gfx::Outsets().set_right(kSidebarMetrics.tab_hover_card_offset));
  return adjusted;
}

void ApplyShellControlStyle(ToolbarButton& button) {
  button.SetPreferredSize(gfx::Size(kSidebarMetrics.shell_control_size,
                                    kSidebarMetrics.shell_control_size));
  button.SetCustomCornerRadius(kSidebarMetrics.shell_control_corner_radius);
  button.SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(0, kSidebarMetrics.shell_control_horizontal_margin));
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

std::unique_ptr<views::View> CreateSidebarHeaderActionsView(
    ShellCreateCallback create_callback,
    views::Button::PressedCallback agent_callback) {
  return std::make_unique<YeeSidebarHeaderActionsView>(
      std::move(create_callback), std::move(agent_callback));
}

void SetSidebarHeaderActionsLeadingExclusion(views::View& view,
                                             int leading_exclusion) {
  static_cast<YeeSidebarHeaderActionsView&>(view).SetLeadingExclusion(
      leading_exclusion);
}

void SetSidebarHeaderActionsControlsVisible(views::View& view, bool visible) {
  auto& actions = static_cast<YeeSidebarHeaderActionsView&>(view);
  actions.SetControlsVisible(visible);
}

bool IsSidebarHeaderActionsPositionInWindowCaption(const views::View& view,
                                                   const gfx::Point& point) {
  return static_cast<const YeeSidebarHeaderActionsView&>(view)
      .IsPositionInWindowCaption(point);
}

}  // namespace yee
