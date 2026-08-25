// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
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
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
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
constexpr int kSplitPaneShadowElevation = 3;
constexpr int kSplitPaneIdleOutlineAlpha = 0x1F;
constexpr int kSplitPaneActiveOutlineAlpha = 0x3D;
constexpr double kSplitCanvasMaximumSaturation = 0.035;
// A half-percent offset rounds back to near-white for common light themes and
// makes the two pane cards read as one unbounded surface. Keep the canvas
// quiet, but leave enough neutral contrast for the card outline to remain
// visible without strengthening the stroke.
constexpr double kSplitCanvasLightLightnessOffset = 0.025;
constexpr double kSplitCanvasDarkLightnessOffset = 0.055;
constexpr int kSurfaceSeparatorAlpha = 0x14;
constexpr int kBrowserSurfaceShadowKeyAlphaLight = 0x14;
constexpr int kBrowserSurfaceShadowAmbientAlphaLight = 0x09;
constexpr int kBrowserSurfaceShadowKeyAlphaDark = 0x20;
constexpr int kBrowserSurfaceShadowAmbientAlphaDark = 0x10;

SkColor ResolveSurfaceSeparatorColor(SkColor surface_color) {
  return color_utils::BlendTowardMaxContrast(surface_color,
                                             kSurfaceSeparatorAlpha);
}

SkColor ResolveCombinedSurfaceColor(const views::View& surface_outline,
                                    const ui::ColorProvider& color_provider);

enum class ShellCreateCommand {
  kNewTab = 1,
  kNewGroup,
  kChat,
};

class YeeOmniboxPopupTheme final
    : public ui::ColorProviderKey::InitializerSupplier {
public:
  explicit YeeOmniboxPopupTheme(SkColor surface_color)
      : surface_color_(SkColorSetA(surface_color, SK_AlphaOPAQUE)) {}
  ~YeeOmniboxPopupTheme() override = default;

  void AddColorMixers(ui::ColorProvider *provider,
                      const ui::ColorProviderKey &key) const override {
    const yee::BrowserSurfaceHeaderColors colors =
        yee::ResolveBrowserSurfaceHeaderColors(surface_color_);
    const SkColor endpoint =
        color_utils::GetColorWithMaxContrast(surface_color_);
    const SkColor hover = color_utils::AlphaBlend(endpoint, surface_color_,
                                                  kOmniboxPopupHoverOpacity);
    const SkColor outline = color_utils::AlphaBlend(
        endpoint, surface_color_, kOmniboxPopupOutlineOpacity);

    // PopupWidget installs this supplier as the key's app controller. Chromium
    // invokes app-controller mixers after its own mixers and the user's theme,
    // so these neutral roles cannot be overwritten later in provider setup.
    // Warning, security, and product-semantic colors remain untouched.
    ui::ColorMixer &mixer = provider->AddMixer();
    mixer[kColorOmniboxResultsBackground] = {surface_color_};
    mixer[kColorOmniboxResultsBackgroundHovered] = {hover};
    mixer[kColorOmniboxResultsBackgroundSelected] = {hover};
    mixer[kColorOmniboxResultsBackgroundIph] = {hover};
    mixer[kColorOmniboxResultsBackgroundHoverOverlay] = {
        SkColorSetA(endpoint, 0x0F)};
    mixer[kColorOmniboxBubbleOutline] = {outline};
    mixer[kColorOmniboxResultsChipBackground] = {hover};

    mixer[kColorOmniboxText] = {colors.primary};
    mixer[kColorOmniboxTextDimmed] = {colors.secondary};
    mixer[kColorOmniboxResultsTextSelected] = {colors.primary};
    mixer[kColorOmniboxResultsTextAnswer] = {colors.primary};
    mixer[kColorOmniboxResultsTextDimmed] = {colors.secondary};
    mixer[kColorOmniboxResultsTextDimmedSelected] = {colors.secondary};
    mixer[kColorOmniboxResultsTextSecondary] = {colors.secondary};
    mixer[kColorOmniboxResultsTextSecondarySelected] = {colors.secondary};
    mixer[kColorOmniboxResultsUrl] = {colors.primary};
    mixer[kColorOmniboxResultsUrlSelected] = {colors.primary};
    mixer[kColorOmniboxKeywordSelected] = {colors.primary};
    mixer[kColorOmniboxKeywordSeparator] = {colors.secondary};

    mixer[kColorOmniboxResultsIcon] = {colors.primary};
    mixer[kColorOmniboxResultsIconSelected] = {colors.primary};
    mixer[kColorOmniboxResultsButtonIcon] = {colors.primary};
    mixer[kColorOmniboxResultsButtonIconSelected] = {colors.primary};
    mixer[kColorOmniboxResultsButtonBorder] = {outline};
    mixer[kColorOmniboxResultsIconGM3Background] = {hover};
  }

private:
  const SkColor surface_color_;
};

class YeeShellBackground : public views::Background {
public:
  YeeShellBackground() = default;
  YeeShellBackground(const YeeShellBackground &) = delete;
  YeeShellBackground &operator=(const YeeShellBackground &) = delete;
  ~YeeShellBackground() override = default;

  void Paint(gfx::Canvas *canvas, views::View *view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    if (bounds.IsEmpty()) {
      return;
    }

    const ui::ColorProvider *const color_provider = view->GetColorProvider();
    CHECK(color_provider);
    const views::Widget *const widget = view->GetWidget();
    const bool is_active = !widget || widget->IsActive();
    SkColor shell_color = color_provider->GetColor(
        is_active ? ui::kColorFrameActive : ui::kColorFrameInactive);

    const ui::NativeTheme *const native_theme = view->GetNativeTheme();
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
    const views::View *const surface_outline =
        view->GetViewByID(yee::kCombinedSurfaceOutlineViewId);
    if (!surface_outline) {
      return;
    }
    gfx::RectF surface_rect(surface_outline->bounds());
    if (surface_rect.IsEmpty()) {
      return;
    }
    surface_rect.Inset(0.5f);
    const SkColor surface_color =
        ResolveCombinedSurfaceColor(*surface_outline, *color_provider);
    const bool dark = color_utils::IsDark(
        yee::ResolveShellContrastBackground(*color_provider));
    const SkColor key_shadow = SkColorSetA(
        SK_ColorBLACK, dark ? kBrowserSurfaceShadowKeyAlphaDark
                            : kBrowserSurfaceShadowKeyAlphaLight);
    const SkColor ambient_shadow = SkColorSetA(
        SK_ColorBLACK, dark ? kBrowserSurfaceShadowAmbientAlphaDark
                            : kBrowserSurfaceShadowAmbientAlphaLight);
    cc::PaintFlags surface_flags;
    surface_flags.setAntiAlias(true);
    surface_flags.setColor(surface_color);
    surface_flags.setStyle(cc::PaintFlags::kFill_Style);
    surface_flags.setLooper(gfx::CreateShadowDrawLooper({
        gfx::ShadowValue(gfx::Vector2d(0, 1), 3.0, key_shadow),
        gfx::ShadowValue(gfx::Vector2d(0, 2), 7.0, ambient_shadow),
    }));
    canvas->DrawRoundRect(surface_rect,
                          yee::kSidebarMetrics.content_corner_radius,
                          surface_flags);
  }
};

class YeeOmniboxBackground : public views::Background {
public:
  YeeOmniboxBackground(SkColor background_color, SkColor focus_stroke_color)
      : fill_painter_(views::Painter::CreateSolidRoundRectPainter(
            background_color, yee::kSidebarMetrics.content_corner_radius,
            gfx::Insets(), SkBlendMode::kSrcOver,
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
  YeeOmniboxBackground(const YeeOmniboxBackground &) = delete;
  YeeOmniboxBackground &operator=(const YeeOmniboxBackground &) = delete;
  ~YeeOmniboxBackground() override = default;

  void Paint(gfx::Canvas *canvas, views::View *view) const override {
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
      yee::PageSurfaceColorCallback page_surface_color_callback)
      : page_surface_color_callback_(std::move(page_surface_color_callback)) {
    SetID(yee::kCombinedSurfaceOutlineViewId);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }
  YeeCombinedSurfaceOutlineView(const YeeCombinedSurfaceOutlineView &) = delete;
  YeeCombinedSurfaceOutlineView &
  operator=(const YeeCombinedSurfaceOutlineView &) = delete;
  ~YeeCombinedSurfaceOutlineView() override = default;

  void SetSplitPresentation(bool split_presentation) {
    if (split_presentation_ == split_presentation) {
      return;
    }
    split_presentation_ = split_presentation;
    SchedulePaint();
  }

  SkColor ResolveSurfaceColor(
      const ui::ColorProvider& color_provider) const {
    return split_presentation_
               ? yee::ResolveSplitCanvasColor(color_provider)
               : yee::ResolveBrowserSurfaceHeaderColor(
                     color_provider, page_surface_color_callback_.Run());
  }

  void OnPaint(gfx::Canvas *canvas) override {
    gfx::RectF surface_rect(GetLocalBounds());
    if (surface_rect.IsEmpty()) {
      return;
    }
    surface_rect.Inset(0.5f);
    const SkColor surface_color = ResolveSurfaceColor(*GetColorProvider());
    const SkColor outline_color =
        color_utils::BlendTowardMaxContrast(surface_color, 0x18);
    const SkColor separator_color = ResolveSurfaceSeparatorColor(surface_color);
    cc::PaintFlags outline_flags;
    outline_flags.setAntiAlias(true);
    outline_flags.setColor(outline_color);
    outline_flags.setStrokeWidth(1.0f);
    outline_flags.setStyle(cc::PaintFlags::kStroke_Style);

    canvas->DrawRoundRect(surface_rect,
                          yee::kSidebarMetrics.content_corner_radius,
                          outline_flags);

    const float separator_y = yee::kSidebarMetrics.titlebar_height -
                              yee::kSidebarMetrics.content_gutter;
    if (!split_presentation_ && separator_y > surface_rect.y() &&
        separator_y < surface_rect.bottom()) {
      canvas->DrawLine(gfx::PointF(surface_rect.x(), separator_y),
                       gfx::PointF(surface_rect.right(), separator_y),
                       separator_color);
    }
  }

private:
  yee::PageSurfaceColorCallback page_surface_color_callback_;
  bool split_presentation_ = false;
};

SkColor ResolveCombinedSurfaceColor(const views::View& surface_outline,
                                    const ui::ColorProvider& color_provider) {
  CHECK_EQ(surface_outline.GetID(), yee::kCombinedSurfaceOutlineViewId);
  return static_cast<const YeeCombinedSurfaceOutlineView&>(surface_outline)
      .ResolveSurfaceColor(color_provider);
}

class YeeSplitPaneEmphasisView : public views::View {
public:
  YeeSplitPaneEmphasisView() {
    SetID(yee::kSplitPaneEmphasisViewId);
    SetCanProcessEventsWithinSubtree(false);
    GetViewAccessibility().SetIsInvisible(true);
    view_shadow_ =
        std::make_unique<views::ViewShadow>(this, kSplitPaneShadowElevation);
    view_shadow_->SetRoundedCornerRadius(
        yee::kSidebarMetrics.split_card_corner_radius);
    SetVisible(false);
  }
  YeeSplitPaneEmphasisView(const YeeSplitPaneEmphasisView &) = delete;
  YeeSplitPaneEmphasisView &
  operator=(const YeeSplitPaneEmphasisView &) = delete;
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

  void OnPaint(gfx::Canvas *canvas) override {
    const ui::ColorProvider *const color_provider = GetColorProvider();
    if (!color_provider) {
      return;
    }

    const float stroke_width =
        yee::kSidebarMetrics.split_pane_content_stroke_inset;
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(stroke_width / 2.0f);
    if (bounds.IsEmpty()) {
      return;
    }

    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(stroke_width);
    const SkColor surface = yee::ResolveSplitCanvasColor(*color_provider);
    stroke.setColor(color_utils::BlendTowardMaxContrast(
        surface, emphasized_ ? kSplitPaneActiveOutlineAlpha
                             : kSplitPaneIdleOutlineAlpha));
    const float stroke_center_radius =
        yee::kSidebarMetrics.split_card_corner_radius - stroke_width / 2.0f;
    canvas->DrawRoundRect(bounds, stroke_center_radius, stroke);
  }

private:
  void UpdateShadowColors() {
    const SkColor surface =
        yee::ResolveShellContrastBackground(*GetColorProvider());
    const bool dark = color_utils::IsDark(surface);
    const SkColor key =
        SkColorSetA(SK_ColorBLACK, emphasized_ ? (dark ? 0x50 : 0x2A) : 0);
    const SkColor ambient =
        SkColorSetA(SK_ColorBLACK, emphasized_ ? (dark ? 0x2C : 0x16) : 0);
    const ui::Shadow::ElevationToColorsMap colors{
        {kSplitPaneShadowElevation, {key, ambient}}};
    view_shadow_->shadow()->SetElevationToColorsMap(colors);
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

    auto *layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    layout->set_cross_axis_alignment(views::LayoutAlignment::kCenter);

    const gfx::FontList omnibox_font = views::TypographyProvider::Get().GetFont(
        CONTEXT_OMNIBOX_PRIMARY, views::style::STYLE_PRIMARY);

    auto origin = std::make_unique<views::Label>();
    origin->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    origin->SetElideBehavior(gfx::ELIDE_HEAD);
    origin->SetFontList(
        omnibox_font.Derive(0, gfx::Font::NORMAL, gfx::Font::Weight::MEDIUM));
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
    title->GetViewAccessibility().SetIsIgnored(true);
    title_ = AddChildView(std::move(title));
    layout->SetFlexForView(title_, 1);
  }

  YeeOmniboxRestingTextView(const YeeOmniboxRestingTextView &) = delete;
  YeeOmniboxRestingTextView &
  operator=(const YeeOmniboxRestingTextView &) = delete;
  ~YeeOmniboxRestingTextView() override = default;

  void Update(std::u16string_view title, std::u16string_view origin,
              SkColor background_color, bool visible) {
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
    }

    const yee::BrowserSurfaceHeaderColors colors =
        yee::ResolveBrowserSurfaceHeaderColors(background_color);
    origin_->SetEnabledColor(colors.primary);
    title_->SetEnabledColor(colors.secondary);
    separator_->SetBackground(
        views::CreateSolidBackground(color_utils::AlphaBlend(
            color_utils::GetColorWithMaxContrast(background_color),
            background_color, kRestingSeparatorOpacity)));
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
      : ToolbarButton(std::move(callback), std::move(menu_model), nullptr,
                      /*trigger_menu_on_long_press=*/false) {
    yee::ApplyShellControlStyle(*this);
  }
  YeeShellToolbarButton(const YeeShellToolbarButton &) = delete;
  YeeShellToolbarButton &operator=(const YeeShellToolbarButton &) = delete;
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
    auto *const menu = static_cast<ui::SimpleMenuModel *>(menu_model());
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

  YeeShellAddButton(const YeeShellAddButton &) = delete;
  YeeShellAddButton &operator=(const YeeShellAddButton &) = delete;
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
  void OpenMenu(const ui::Event &event) {
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
    const base::CommandLine *const command_line =
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

  YeeAgentToolbarButton(const YeeAgentToolbarButton &) = delete;
  YeeAgentToolbarButton &operator=(const YeeAgentToolbarButton &) = delete;
  ~YeeAgentToolbarButton() override = default;

protected:
  void PaintButtonContents(gfx::Canvas *canvas) override {
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

} // namespace

namespace yee {

bool IsShellEnabled() {
  return !base::CommandLine::ForCurrentProcess()->HasSwitch(
      kDisableYeeShellScaffoldSwitch);
}

bool UsesExpandedSidebarPresentation() { return IsShellEnabled(); }

bool ShouldPrioritizeSidebarTabDrag(int dragged_tab_count, int source_tab_count,
                                    bool is_group_drag,
                                    bool uses_vertical_tab_strip) {
  return UsesExpandedSidebarPresentation() && uses_vertical_tab_strip &&
         !is_group_drag && dragged_tab_count == 1 && source_tab_count == 1;
}

SkColor
ResolveBrowserSurfaceHeaderColor(const ui::ColorProvider &color_provider,
                                 std::optional<SkColor> page_surface_color) {
  const SkColor toolbar = color_provider.GetColor(kColorToolbar);
  if (!page_surface_color.has_value()) {
    return toolbar;
  }
  return SkColorSetA(*page_surface_color, SK_AlphaOPAQUE);
}

BrowserSurfaceHeaderColors
ResolveBrowserSurfaceHeaderColors(SkColor surface_color) {
  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  const SkColor endpoint = color_utils::GetColorWithMaxContrast(surface_color);
  const SkColor preferred_primary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderPrimaryPreferredOpacity);
  const SkColor preferred_secondary = color_utils::AlphaBlend(
      endpoint, surface_color, kHeaderSecondaryPreferredOpacity);
  return {
      .primary =
          color_utils::BlendForMinContrast(preferred_primary, surface_color)
              .color,
      .secondary = color_utils::BlendForMinContrast(
                       preferred_secondary, surface_color, std::nullopt,
                       color_utils::kMinimumVisibleContrastRatio)
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

std::unique_ptr<views::Background>
CreateBrowserSurfaceOmniboxBackground(SkColor background_color,
                                      SkColor focus_stroke_color) {
  return std::make_unique<YeeOmniboxBackground>(background_color,
                                                focus_stroke_color);
}

ui::ColorProviderKey::InitializerSupplier *
GetBrowserSurfaceOmniboxPopupTheme(SkColor surface_color) {
  using PopupThemes = std::map<SkColor, std::unique_ptr<YeeOmniboxPopupTheme>>;
  static base::NoDestructor<PopupThemes> popup_themes;

  surface_color = SkColorSetA(surface_color, SK_AlphaOPAQUE);
  auto [it, inserted] = popup_themes->try_emplace(surface_color);
  if (inserted) {
    it->second = std::make_unique<YeeOmniboxPopupTheme>(surface_color);
  }
  return it->second.get();
}

std::unique_ptr<views::Background> CreateShellBackground() {
  return std::make_unique<YeeShellBackground>();
}

SkColor
ResolveShellContrastBackground(const ui::ColorProvider &color_provider) {
  // Both native tint and Yee's overlay use this color, so their composition
  // resolves to the same opaque contrast anchor without sampling desktop
  // pixels. This keeps text contrast stable while a glass window moves.
  return color_provider.GetColor(ui::kColorFrameActive);
}

SkColor ResolveSplitCanvasColor(const ui::ColorProvider &color_provider) {
  const SkColor toolbar =
      SkColorSetA(color_provider.GetColor(kColorToolbar), SK_AlphaOPAQUE);
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(toolbar, &hsl);
  hsl.s = std::min(hsl.s, kSplitCanvasMaximumSaturation);
  hsl.l = std::clamp(hsl.l + (color_utils::IsDark(toolbar)
                                  ? kSplitCanvasDarkLightnessOffset
                                  : -kSplitCanvasLightLightnessOffset),
                     0.0, 1.0);
  return color_utils::HSLToSkColor(hsl, SK_AlphaOPAQUE);
}

SkColor ResolveBrowserSurfaceSeparatorColor(SkColor surface_color) {
  return ResolveSurfaceSeparatorColor(surface_color);
}

double GetNativeGlassTintOpacity(bool is_dark_mode) {
  return is_dark_mode ? kNativeGlassTintOpacityDark
                      : kNativeGlassTintOpacityLight;
}

std::unique_ptr<views::View> CreateCombinedSurfaceOutlineView(
    PageSurfaceColorCallback page_surface_color_callback) {
  return std::make_unique<YeeCombinedSurfaceOutlineView>(
      std::move(page_surface_color_callback));
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

void UpdateSplitPaneEmphasisView(views::View &view, bool visible,
                                 bool emphasized) {
  CHECK_EQ(view.GetID(), kSplitPaneEmphasisViewId);
  static_cast<YeeSplitPaneEmphasisView &>(view).SetState(visible, emphasized);
}

std::unique_ptr<views::View> CreateOmniboxRestingTextView() {
  return std::make_unique<YeeOmniboxRestingTextView>();
}

void UpdateOmniboxRestingTextView(views::View &view, std::u16string_view title,
                                  std::u16string_view origin,
                                  SkColor background_color, bool visible) {
  static_cast<YeeOmniboxRestingTextView &>(view).Update(
      title, origin, background_color, visible);
}

gfx::Rect AdjustVerticalTabHoverCardAnchor(const gfx::Rect &bounds) {
  gfx::Rect adjusted = bounds;
  adjusted.Outset(
      gfx::Outsets().set_right(kSidebarMetrics.tab_hover_card_offset));
  return adjusted;
}

void ApplyShellControlStyle(ToolbarButton &button) {
  button.SetPreferredSize(gfx::Size(kSidebarMetrics.shell_control_size,
                                    kSidebarMetrics.shell_control_size));
  button.SetCustomCornerRadius(kSidebarMetrics.shell_control_corner_radius);
  button.SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(0, kSidebarMetrics.shell_control_horizontal_margin));
}

std::unique_ptr<ToolbarButton>
CreateShellToolbarButton(views::Button::PressedCallback callback) {
  return std::make_unique<YeeShellToolbarButton>(std::move(callback));
}

std::unique_ptr<ToolbarButton>
CreateShellAddButton(ShellCreateCallback callback) {
  return std::make_unique<YeeShellAddButton>(std::move(callback));
}

std::unique_ptr<ToolbarButton>
CreateAgentToolbarButton(views::Button::PressedCallback callback) {
  return std::make_unique<YeeAgentToolbarButton>(std::move(callback));
}

} // namespace yee
