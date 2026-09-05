// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include <array>
#include <memory>
#include <utility>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/yee/browser_surface_layout.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

void SetSidebarPaletteForTesting(ui::ColorProvider& provider, bool dark) {
  provider.SetColorForTesting(
      ui::kColorSysBase,
      dark ? SkColorSetRGB(0x12, 0x13, 0x12) : SkColorSetRGB(0xFA, 0xFA, 0xF8));
  provider.SetColorForTesting(ui::kColorFrameActive,
                              SkColorSetRGB(0xD3, 0xE8, 0xCF));
  provider.SetColorForTesting(ui::kColorFrameInactive,
                              SkColorSetRGB(0xCB, 0xD5, 0xC8));
}

TEST(YeeSurfaceColorTest, HeaderRolesRemainReadableOnLightAndDarkPages) {
  for (SkColor surface :
       {SkColorSetRGB(0xFA, 0xF4, 0xE5), SkColorSetRGB(0x18, 0x1A, 0x1F)}) {
    const BrowserSurfaceHeaderColors colors =
        ResolveBrowserSurfaceHeaderColors(surface);
    EXPECT_EQ(SkColorGetA(colors.primary), SK_AlphaOPAQUE);
    EXPECT_EQ(SkColorGetA(colors.secondary), SK_AlphaOPAQUE);
    EXPECT_GE(color_utils::GetContrastRatio(colors.primary, surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_GE(color_utils::GetContrastRatio(colors.secondary, surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_LT(color_utils::GetContrastRatio(colors.disabled, surface),
              color_utils::GetContrastRatio(colors.primary, surface));
  }
}

TEST(YeeSurfaceColorTest, FocusStrokeKeepsNonTextContrast) {
  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7A, 0x75, 0x6C)}) {
    const SkColor stroke = ResolveBrowserSurfaceFocusStrokeColor(surface);
    EXPECT_GE(color_utils::GetContrastRatio(stroke, surface),
              color_utils::kMinimumVisibleContrastRatio);
  }
}

TEST(BrowserSurfacePresentationResolverTest,
     ExhaustiveGrayAndChromaticContrastContract) {
  std::array<SkColor, 7> chromatic = {
      SK_ColorRED,
      SK_ColorGREEN,
      SK_ColorBLUE,
      SK_ColorCYAN,
      SK_ColorMAGENTA,
      SK_ColorYELLOW,
      SkColorSetARGB(0x20, 0x7E, 0x7E, 0x7E),
  };
  const auto verify = [](SkColor input) {
    const BrowserSurfacePresentation first =
        ResolveBrowserSurfacePresentation(input, 7, 11, 13);
    const BrowserSurfacePresentation second =
        ResolveBrowserSurfacePresentation(input, 7, 11, 13);
    EXPECT_EQ(SK_AlphaOPAQUE, SkColorGetA(first.surface));
    EXPECT_GE(color_utils::GetContrastRatio(first.primary, first.surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_GE(color_utils::GetContrastRatio(first.secondary, first.surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_GE(color_utils::GetContrastRatio(first.focus_stroke, first.surface),
              color_utils::kMinimumVisibleContrastRatio);
    EXPECT_EQ(first.surface, second.surface);
    EXPECT_EQ(first.primary, second.primary);
    EXPECT_EQ(first.secondary, second.secondary);
    EXPECT_EQ(first.focus_stroke, second.focus_stroke);
  };

  for (int gray = 0; gray <= 0xFF; ++gray) {
    verify(SkColorSetRGB(gray, gray, gray));
  }
  for (SkColor color : chromatic) {
    verify(color);
  }
}

TEST(YeeSurfaceColorTest, PopupProvidersAreExactAndDoNotGrowGlobalCache) {
  ui::ColorProviderManager& manager = ui::ColorProviderManager::Get();
  const size_t cache_size = manager.color_provider_cache_size_for_testing();
  const auto expected_custom_colors = [](SkColor surface) {
    const BrowserSurfacePresentation presentation =
        ResolveBrowserSurfacePresentation(surface, 0, 0, 0);
    const SkColor hover_overlay = SkColorSetA(
        color_utils::GetColorWithMaxContrast(presentation.surface), 0x0F);
    return std::array<std::pair<ui::ColorId, SkColor>, 25>{
        {{kColorOmniboxResultsBackground, presentation.surface},
         {kColorOmniboxResultsBackgroundHovered, presentation.popup_hover},
         {kColorOmniboxResultsBackgroundSelected, presentation.popup_hover},
         {kColorOmniboxResultsBackgroundIph, presentation.popup_hover},
         {kColorOmniboxResultsBackgroundHoverOverlay, hover_overlay},
         {kColorOmniboxBubbleOutline, presentation.popup_outline},
         {kColorOmniboxResultsChipBackground, presentation.popup_hover},
         {kColorOmniboxText, presentation.primary},
         {kColorOmniboxTextDimmed, presentation.secondary},
         {kColorOmniboxResultsTextSelected, presentation.primary},
         {kColorOmniboxResultsTextAnswer, presentation.primary},
         {kColorOmniboxResultsTextDimmed, presentation.secondary},
         {kColorOmniboxResultsTextDimmedSelected, presentation.secondary},
         {kColorOmniboxResultsTextSecondary, presentation.secondary},
         {kColorOmniboxResultsTextSecondarySelected, presentation.secondary},
         {kColorOmniboxResultsUrl, presentation.primary},
         {kColorOmniboxResultsUrlSelected, presentation.primary},
         {kColorOmniboxKeywordSelected, presentation.primary},
         {kColorOmniboxKeywordSeparator, presentation.secondary},
         {kColorOmniboxResultsIcon, presentation.primary},
         {kColorOmniboxResultsIconSelected, presentation.primary},
         {kColorOmniboxResultsButtonIcon, presentation.primary},
         {kColorOmniboxResultsButtonIconSelected, presentation.primary},
         {kColorOmniboxResultsButtonBorder, presentation.popup_outline},
         {kColorOmniboxResultsIconGM3Background, presentation.popup_hover}}};
  };

  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7E, 0x7E, 0x7E),
        SkColorSetRGB(0x2A, 0x64, 0x91)}) {
    const BrowserSurfacePresentation presentation =
        ResolveBrowserSurfacePresentation(surface, 0, 0, 0);
    ui::ColorProviderKey key;
    ui::ColorProviderKey native_key = key;
    native_key.color_mode = color_utils::IsDark(surface)
                                ? ui::ColorProviderKey::ColorMode::kDark
                                : ui::ColorProviderKey::ColorMode::kLight;
    std::unique_ptr<ui::ColorProvider> native =
        manager.CreateUncachedColorProvider(native_key);
    std::unique_ptr<ui::ColorProvider> provider =
        CreateBrowserSurfaceOmniboxPopupColorProvider(key, presentation);
    for (const auto& [color_id, expected] : expected_custom_colors(surface)) {
      EXPECT_EQ(expected, provider->GetColor(color_id));
    }
    // Security warnings are semantic Chromium state, not Yee presentation.
    EXPECT_EQ(native->GetColor(kColorOmniboxSecurityChipDangerous),
              provider->GetColor(kColorOmniboxSecurityChipDangerous));
    EXPECT_EQ(cache_size, manager.color_provider_cache_size_for_testing());
  }

  const std::array<ui::ColorId, 25> overridden_ids = {
      kColorOmniboxResultsBackground,
      kColorOmniboxResultsBackgroundHovered,
      kColorOmniboxResultsBackgroundSelected,
      kColorOmniboxResultsBackgroundIph,
      kColorOmniboxResultsBackgroundHoverOverlay,
      kColorOmniboxBubbleOutline,
      kColorOmniboxResultsChipBackground,
      kColorOmniboxText,
      kColorOmniboxTextDimmed,
      kColorOmniboxResultsTextSelected,
      kColorOmniboxResultsTextAnswer,
      kColorOmniboxResultsTextDimmed,
      kColorOmniboxResultsTextDimmedSelected,
      kColorOmniboxResultsTextSecondary,
      kColorOmniboxResultsTextSecondarySelected,
      kColorOmniboxResultsUrl,
      kColorOmniboxResultsUrlSelected,
      kColorOmniboxKeywordSelected,
      kColorOmniboxKeywordSeparator,
      kColorOmniboxResultsIcon,
      kColorOmniboxResultsIconSelected,
      kColorOmniboxResultsButtonIcon,
      kColorOmniboxResultsButtonIconSelected,
      kColorOmniboxResultsButtonBorder,
      kColorOmniboxResultsIconGM3Background,
  };
  for (ui::ColorProviderKey key : [] {
         ui::ColorProviderKey high_contrast;
         high_contrast.contrast_mode =
             ui::ColorProviderKey::ContrastMode::kHigh;
         ui::ColorProviderKey forced_colors;
         forced_colors.forced_colors =
             ui::ColorProviderKey::ForcedColors::kSystem;
         return std::array{high_contrast, forced_colors};
       }()) {
    std::unique_ptr<ui::ColorProvider> native =
        manager.CreateUncachedColorProvider(key);
    std::unique_ptr<ui::ColorProvider> provider =
        CreateBrowserSurfaceOmniboxPopupColorProvider(
            key, ResolveBrowserSurfacePresentation(
                     SkColorSetRGB(0x7E, 0x7E, 0x7E), 0, 0, 0));
    for (ui::ColorId color_id : overridden_ids) {
      EXPECT_EQ(native->GetColor(color_id), provider->GetColor(color_id));
    }
    EXPECT_EQ(cache_size, manager.color_provider_cache_size_for_testing());
  }
}

class YeeRestingTextViewTest : public views::ViewsTestBase {
 public:
  void SetUp() override {
    views::ViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  }

  void TearDown() override {
    widget_.reset();
    views::ViewsTestBase::TearDown();
  }

 protected:
  std::unique_ptr<views::Widget> widget_;
};

TEST_F(YeeRestingTextViewTest,
       WidgetAttachedLabelsUseExactSurfaceAndPhysicalForeground) {
  std::unique_ptr<views::View> resting = CreateOmniboxRestingTextView();
  views::View* resting_ptr = resting.get();
  widget_->SetContentsView(std::move(resting));
  const BrowserSurfacePresentation presentation =
      ResolveBrowserSurfacePresentation(SkColorSetRGB(0x7E, 0x7E, 0x7E), 1, 1,
                                        1);
  UpdateOmniboxRestingTextView(*resting_ptr, u"Title", u"kimi.ai", presentation,
                               true);

  ASSERT_EQ(3u, resting_ptr->children().size());
  auto* origin = views::AsViewClass<views::Label>(resting_ptr->children()[0]);
  auto* title = views::AsViewClass<views::Label>(resting_ptr->children()[2]);
  ASSERT_TRUE(origin);
  ASSERT_TRUE(title);
  EXPECT_FALSE(origin->GetAutoColorReadabilityEnabled());
  EXPECT_FALSE(title->GetAutoColorReadabilityEnabled());
  EXPECT_EQ(presentation.surface, origin->GetBackgroundColor());
  EXPECT_EQ(presentation.surface, title->GetBackgroundColor());
  EXPECT_EQ(presentation.primary, origin->GetEnabledColor());
  EXPECT_EQ(presentation.secondary, title->GetEnabledColor());
}

TEST(YeeSurfaceGeometryTest, SplitAndSingleSurfacesShareCornerContract) {
  const gfx::RoundedCornersF corners = ResolveSplitPaneRoundedCorners();
  EXPECT_EQ(corners.upper_left(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.upper_right(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.lower_right(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(corners.lower_left(), kSidebarMetrics.content_corner_radius);
  EXPECT_EQ(kSidebarMetrics.split_pane_inner_corner_radius,
            kSidebarMetrics.content_corner_radius -
                kSidebarMetrics.split_pane_content_stroke_inset);
}

TEST(YeeSurfaceGeometryTest, SplitAndSingleHeadersShareMetricsContract) {
  EXPECT_EQ(42, kSidebarMetrics.browser_surface_header_height());
  EXPECT_EQ(kSidebarMetrics.browser_surface_header_height(),
            kSidebarMetrics.split_pane_header_height);
  EXPECT_EQ(34, kSidebarMetrics.browser_surface_location_bar_height);
  EXPECT_EQ(40, kSidebarMetrics.toolbar_height);
  EXPECT_EQ(kSidebarMetrics.browser_surface_header_center_y(),
            kSidebarMetrics.browser_surface_toolbar_top_inset() +
                kSidebarMetrics.toolbar_height / 2);
  EXPECT_EQ(kSidebarMetrics.browser_surface_header_height(),
            kSidebarMetrics.browser_surface_location_bar_height +
                2 * kSidebarMetrics.split_pane_address_bar_inset);
  EXPECT_EQ(8, kSidebarMetrics.browser_surface_header_control_edge_inset);
  EXPECT_EQ(kSidebarMetrics.browser_surface_header_center_y(),
            kSidebarMetrics.content_gutter +
                kSidebarMetrics.split_pane_header_height / 2);
}

TEST(YeeSurfaceGeometryTest, BrowserClassRoutingIsPerWindow) {
  const bool expected_normal = IsShellEnabled();
  EXPECT_EQ(expected_normal, UsesYeeBrowserSurfaceGeometry(
                                 BrowserWindowInterface::Type::TYPE_NORMAL,
                                 /*has_vertical_tab_strip=*/true));
  EXPECT_FALSE(
      UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type::TYPE_NORMAL,
                                    /*has_vertical_tab_strip=*/false));
  EXPECT_FALSE(
      UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type::TYPE_POPUP,
                                    /*has_vertical_tab_strip=*/true));
  EXPECT_FALSE(
      UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type::TYPE_APP,
                                    /*has_vertical_tab_strip=*/true));
  EXPECT_FALSE(UsesYeeBrowserSurfaceGeometry(
      BrowserWindowInterface::Type::TYPE_APP_POPUP,
      /*has_vertical_tab_strip=*/true));
#if !BUILDFLAG(IS_ANDROID)
  EXPECT_FALSE(
      UsesYeeBrowserSurfaceGeometry(BrowserWindowInterface::Type::TYPE_DEVTOOLS,
                                    /*has_vertical_tab_strip=*/true));
  EXPECT_FALSE(UsesYeeBrowserSurfaceGeometry(
      BrowserWindowInterface::Type::TYPE_PICTURE_IN_PICTURE,
      /*has_vertical_tab_strip=*/true));
#endif
}

TEST(YeeSurfaceGeometryTest,
     SurfaceFrameKeepsNativePanelAllocationAndYeeGutters) {
  BrowserSurfaceFrameInput input;
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/true, /*header_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(0, 48, 900, 746);
  input.native_body_allocation = gfx::Rect(0, 48, 900, 746);

  const ResolvedBrowserSurfaceFrame frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_EQ(gfx::Rect(250, 6, 944, 788), frame.surface_seed_bounds);
  EXPECT_EQ(gfx::Rect(250, 6, 650, 788), frame.main_surface_bounds);
  EXPECT_EQ(gfx::Rect(250, 6, 944, 42), frame.header_bounds);
  EXPECT_EQ(gfx::Rect(250, 48, 650, 746), frame.notice_flow_bounds);
  EXPECT_EQ(gfx::Rect(250, 48, 650, 746), frame.multi_contents_bounds);
}

TEST(YeeSurfaceGeometryTest,
     SidePanelShadowPaddingDoesNotBecomeASecondOuterGutter) {
  const gfx::Rect content_column(244, 0, 956, 800);
  const ResolvedBrowserSurfaceSeed closed_seed = ResolveBrowserSurfaceSeed(
      BrowserSurfaceSeedInput{content_column, gfx::Insets(6), /*split=*/false,
                              /*header_participates=*/true,
                              /*header_height=*/42});

  // A trailing 300-DIP panel has already consumed its width. The native
  // shadow box then consumes six DIPs at the top, bottom, and panel-facing
  // edge. Yee must restore those shadow insets before applying its own uniform
  // six-DIP Surface gutter.
  gfx::Rect native_remaining_bounds(244, 0, 656, 800);
  const gfx::Insets native_shadow_insets =
      gfx::Insets::TLBR(6, 0, 6, 6);
  native_remaining_bounds.Inset(native_shadow_insets);

  const gfx::Rect restored_content_column =
      RestoreBrowserSurfaceContentColumnAfterSidePanel(
          native_remaining_bounds, native_shadow_insets);
  const ResolvedBrowserSurfaceSeed open_seed = ResolveBrowserSurfaceSeed(
      BrowserSurfaceSeedInput{restored_content_column, gfx::Insets(6),
                              /*split=*/false,
                              /*header_participates=*/true,
                              /*header_height=*/42});

  EXPECT_EQ(gfx::Rect(244, 0, 656, 800), restored_content_column);
  EXPECT_EQ(closed_seed.surface_seed_bounds.y(),
            open_seed.surface_seed_bounds.y());
  EXPECT_EQ(closed_seed.surface_seed_bounds.bottom(),
            open_seed.surface_seed_bounds.bottom());
  EXPECT_EQ(gfx::Rect(250, 6, 644, 788), open_seed.surface_seed_bounds);
  EXPECT_EQ(gfx::Rect(250, 6, 644, 42), open_seed.header_bounds);
}

TEST(YeeSurfaceGeometryTest,
     NativeUnderlapCanWidenBodyWithoutWideningInfoBarFlow) {
  BrowserSurfaceFrameInput input;
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 476, 600), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/true, /*header_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(0, 48, 400, 546);
  input.native_body_allocation = gfx::Rect(0, 48, 500, 546);

  const ResolvedBrowserSurfaceFrame frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_EQ(gfx::Rect(250, 48, 150, 546), frame.notice_flow_bounds);
  EXPECT_EQ(gfx::Rect(250, 48, 250, 546), frame.multi_contents_bounds);
  EXPECT_EQ(400, frame.main_surface_bounds.right());
}

TEST(YeeSurfaceGeometryTest,
     SurfaceDecorationUsesOnlyTheResolvedStructuralFrame) {
  BrowserSurfaceFrameInput input;
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/true, /*header_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(250, 48, 944, 746);
  input.native_body_allocation = gfx::Rect(250, 48, 944, 746);

  ResolvedBrowserSurfaceFrame frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_EQ(frame.main_surface_bounds,
            frame.decoration.combined_outline_bounds);
  EXPECT_TRUE(frame.decoration.combined_outline_visible);
  EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kCombinedSurfaceOutline,
            frame.decoration.header_separator_owner);
  EXPECT_EQ(frame.header_bounds.bottom() - frame.main_surface_bounds.y(),
            frame.decoration.combined_header_separator_offset);
  EXPECT_FALSE(frame.decoration.show_native_multi_contents_separator);
  EXPECT_TRUE(frame.decoration.contain_infobar_shadow_horizontally);
  EXPECT_EQ(0.0f, frame.decoration.infobar_top_corner_radius);

  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/false, /*header_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(250, 6, 944, 788);
  input.native_body_allocation = gfx::Rect(250, 6, 944, 788);
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_TRUE(frame.decoration.combined_outline_visible);
  EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kNone,
            frame.decoration.header_separator_owner);
  EXPECT_EQ(0, frame.decoration.combined_header_separator_offset);
  EXPECT_EQ(kSidebarMetrics.content_corner_radius -
                kSidebarMetrics.browser_surface_outline_width,
            frame.decoration.infobar_top_corner_radius);

  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/true,
      /*header_participates=*/false, /*header_height=*/42});
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_FALSE(frame.decoration.combined_outline_visible);
  EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kSplitPane,
            frame.decoration.header_separator_owner);
  EXPECT_EQ(0, frame.decoration.combined_header_separator_offset);
  EXPECT_EQ(0.0f, frame.decoration.infobar_top_corner_radius);
}

TEST(YeeSurfaceGeometryTest, SplitInsetsResolveBeforeMultiContentsApplication) {
  const gfx::Insets native_insets = gfx::Insets::TLBR(0, 8, 8, 8);
  EXPECT_EQ(
      gfx::Insets(kSidebarMetrics.split_card_inset),
      ResolveBrowserSurfaceSplitViewInsets(/*split=*/true, native_insets));
  EXPECT_EQ(native_insets, ResolveBrowserSurfaceSplitViewInsets(
                               /*split=*/false, native_insets));
}

TEST(YeeSurfaceGeometryTest,
     HeaderBoundsTrackEveryPinnedSidebarTransitionFrame) {
  BrowserSurfaceFrameInput input;
  input.native_notice_flow_allocation = gfx::Rect(0, 48, 1200, 746);
  input.native_body_allocation = gfx::Rect(0, 48, 1200, 746);

  for (const int current_leading_inset : {250, 180, 96, 6}) {
    input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
        gfx::Rect(current_leading_inset - 6, 0, 1206 - current_leading_inset,
                  800),
        gfx::Insets(6), /*split=*/false,
        /*header_participates=*/true, /*header_height=*/42});
    const ResolvedBrowserSurfaceFrame frame = ResolveBrowserSurfaceFrame(input);

    EXPECT_EQ(current_leading_inset, frame.header_bounds.x());
    EXPECT_EQ(1194 - current_leading_inset, frame.header_bounds.width());
    EXPECT_EQ(
        gfx::Rect(current_leading_inset, 6, 1194 - current_leading_inset, 42),
        frame.header_bounds);
  }
}

TEST(YeeSurfaceGeometryTest, CurrentPaneGeometryIsPureAndDirectional) {
  CurrentPaneGeometryInput input;
  input.available_space = gfx::Rect(0, 0, 900, 600);
  input.split = true;
  input.axis = PaneSplitAxis::kHorizontal;
  input.start_ratio = 0.4;
  input.divider_size = 8;
  input.minimums = {200, 200};

  const CurrentPaneGeometry horizontal = ComputeCurrentPaneGeometry(input);
  EXPECT_EQ(gfx::Rect(0, 0, 357, 600), horizontal.start);
  EXPECT_EQ(gfx::Rect(357, 0, 8, 600), horizontal.divider);
  EXPECT_EQ(gfx::Rect(365, 0, 535, 600), horizontal.end);

  input.axis = PaneSplitAxis::kVertical;
  input.available_space = gfx::Rect(0, 0, 900, 500);
  input.start_ratio = 0.95;
  const CurrentPaneGeometry vertical = ComputeCurrentPaneGeometry(input);
  EXPECT_EQ(292, vertical.start.height());
  EXPECT_EQ(8, vertical.divider.height());
  EXPECT_EQ(200, vertical.end.height());
  EXPECT_EQ(input.available_space.x(), vertical.start.x());
  EXPECT_EQ(input.available_space.y(), vertical.start.y());
  EXPECT_EQ(input.available_space.width(), vertical.end.width());
  EXPECT_EQ(input.available_space.bottom(), vertical.end.bottom());
}

TEST(YeeSurfaceGeometryTest, InfoBarSlotUsesOnlyCurrentActiveBody) {
  ExternalInfoBarSlotInput input;
  input.manager_body_bounds_in_browser = gfx::Rect(727, 48, 466, 745);
  input.notice_flow_bounds = gfx::Rect(250, 6, 944, 788);
  input.semantic_height = 54;

  EXPECT_EQ(gfx::Rect(727, 48, 466, 54), ResolveExternalInfoBarSlot(input));

  input.semantic_height = 800;
  EXPECT_TRUE(ResolveExternalInfoBarSlot(input).IsEmpty());
}

TEST(YeeSurfaceGeometryTest, HoverCardAnchorOnlyAddsContentSideClearance) {
  const gfx::Rect original(10, 20, 100, 32);
  const gfx::Rect adjusted = AdjustVerticalTabHoverCardAnchor(original);

  EXPECT_EQ(adjusted.x(), original.x());
  EXPECT_EQ(adjusted.y(), original.y());
  EXPECT_EQ(adjusted.height(), original.height());
  EXPECT_EQ(adjusted.right(),
            original.right() + kSidebarMetrics.tab_hover_card_offset);
}

TEST(YeeSurfaceColorTest, DarkGlassUsesStrongerNativeTint) {
  const double light = GetNativeGlassTintOpacity(/*is_dark_mode=*/false);
  const double dark = GetNativeGlassTintOpacity(/*is_dark_mode=*/true);
  EXPECT_GT(light, 0.0);
  EXPECT_LT(dark, 1.0);
  EXPECT_GT(dark, light);
}

TEST(YeeShellColorTest, LightSchemePreservesOpaqueFrameColors) {
  ui::ColorProvider provider;
  SetSidebarPaletteForTesting(provider, /*dark=*/false);

  EXPECT_EQ(SkColorSetRGB(0xD3, 0xE8, 0xCF),
            ResolveShellBackgroundColor(provider, /*frame_active=*/true));
  EXPECT_EQ(SkColorSetRGB(0xCB, 0xD5, 0xC8),
            ResolveShellBackgroundColor(provider, /*frame_active=*/false));
}

TEST(YeeShellColorTest, DarkSchemeTonesBrightFrameWithoutLosingHue) {
  ui::ColorProvider provider;
  SetSidebarPaletteForTesting(provider, /*dark=*/true);

  const SkColor active =
      ResolveShellBackgroundColor(provider, /*frame_active=*/true);
  const SkColor inactive =
      ResolveShellBackgroundColor(provider, /*frame_active=*/false);
  color_utils::HSL seed_hsl;
  color_utils::HSL active_hsl;
  color_utils::HSL inactive_hsl;
  color_utils::SkColorToHSL(SkColorSetRGB(0xD3, 0xE8, 0xCF), &seed_hsl);
  color_utils::SkColorToHSL(active, &active_hsl);
  color_utils::SkColorToHSL(inactive, &inactive_hsl);

  EXPECT_TRUE(color_utils::IsDark(active));
  EXPECT_NEAR(seed_hsl.h, active_hsl.h, 0.01);
  EXPECT_GE(active_hsl.l, 0.16);
  // HSL round-trips through eight-bit RGB, so allow one quantization step.
  EXPECT_LE(active_hsl.l, 0.262);
  EXPECT_LE(active_hsl.s, 0.44);
  EXPECT_LT(inactive_hsl.l, active_hsl.l);
  EXPECT_LT(inactive_hsl.s, active_hsl.s);
}

TEST(YeeSidebarItemColorTest, EveryStateSharesOneThemeAwarePalette) {
  for (const bool dark : {false, true}) {
    ui::ColorProvider provider;
    SetSidebarPaletteForTesting(provider, dark);
    const SkColor shell =
        ResolveShellBackgroundColor(provider, /*frame_active=*/true);
    const SkColor endpoint = color_utils::GetColorWithMaxContrast(shell);

    const SidebarItemColors resting_row = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kResting, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(resting_row.fill)));
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(resting_row.stroke)));
    EXPECT_GE(color_utils::GetContrastRatio(resting_row.foreground, shell),
              color_utils::kMinimumReadableContrastRatio);

    const SidebarItemColors resting_favorite = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kResting, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/true);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_fill_alpha,
              static_cast<int>(SkColorGetA(resting_favorite.fill)));
    EXPECT_EQ(kSidebarMetrics.favorites_cell_stroke_alpha,
              static_cast<int>(SkColorGetA(resting_favorite.stroke)));
    EXPECT_EQ(SkColorSetA(endpoint, SK_AlphaOPAQUE),
              SkColorSetA(resting_favorite.fill, SK_AlphaOPAQUE));
    EXPECT_EQ(resting_row.foreground, resting_favorite.foreground);

    const SidebarItemColors hovered_row = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_fill_alpha,
              static_cast<int>(SkColorGetA(hovered_row.fill)));
    EXPECT_EQ(0, static_cast<int>(SkColorGetA(hovered_row.stroke)));
    EXPECT_GT(color_utils::GetContrastRatio(hovered_row.foreground, shell),
              color_utils::GetContrastRatio(resting_row.foreground, shell));

    const SidebarItemColors hovered_favorite = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
        /*frame_active=*/true, /*persistent_surface=*/true);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_hover_fill_alpha,
              static_cast<int>(SkColorGetA(hovered_favorite.fill)));
    EXPECT_GT(static_cast<int>(SkColorGetA(hovered_favorite.stroke)),
              static_cast<int>(SkColorGetA(resting_favorite.stroke)));

    const SidebarItemColors active = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kActive, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    const SidebarItemColors dragging = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kDragging, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/false);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_active_fill_alpha,
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_EQ(kSidebarMetrics.favorites_cell_active_stroke_alpha,
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.fill)),
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.stroke)),
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_EQ(active.foreground, dragging.foreground);
    const SkColor composited_active_fill =
        color_utils::GetResultingPaintColor(active.fill, shell);
    EXPECT_GE(color_utils::GetContrastRatio(active.foreground,
                                            composited_active_fill),
              color_utils::kMinimumReadableContrastRatio);

    const SidebarItemColors inactive = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kActive, /*hover_progress=*/0.0,
        /*frame_active=*/false, /*persistent_surface=*/false);
    EXPECT_LT(static_cast<int>(SkColorGetA(inactive.fill)),
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_LT(static_cast<int>(SkColorGetA(inactive.stroke)),
              static_cast<int>(SkColorGetA(active.stroke)));
    const SkColor inactive_shell =
        ResolveShellBackgroundColor(provider, /*frame_active=*/false);
    EXPECT_GE(
        color_utils::GetContrastRatio(inactive.foreground, inactive_shell),
        color_utils::kMinimumReadableContrastRatio);
    EXPECT_LT(
        color_utils::GetContrastRatio(inactive.foreground, inactive_shell),
        color_utils::GetContrastRatio(active.foreground, shell));
  }
}

TEST(YeeSidebarItemColorTest, HoverProgressClampsAtBothEnds) {
  ui::ColorProvider provider;
  SetSidebarPaletteForTesting(provider, /*dark=*/true);

  const SidebarItemColors below_zero = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/-1.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors at_zero = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/0.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors above_one = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/2.0,
      /*frame_active=*/true, /*persistent_surface=*/true);
  const SidebarItemColors at_one = ResolveSidebarItemColors(
      provider, SidebarItemVisualState::kHovered, /*hover_progress=*/1.0,
      /*frame_active=*/true, /*persistent_surface=*/true);

  EXPECT_EQ(at_zero.fill, below_zero.fill);
  EXPECT_EQ(at_zero.stroke, below_zero.stroke);
  EXPECT_EQ(at_one.fill, above_one.fill);
  EXPECT_EQ(at_one.stroke, above_one.stroke);
}

}  // namespace
}  // namespace yee
