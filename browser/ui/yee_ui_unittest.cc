// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/yee_ui.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/rtl.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/views/yee/browser_surface_layout.h"
#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"
#include "chrome/browser/ui/views/yee/sidebar_presentation.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "chrome/grit/theme_resources.h"
#include "chrome/test/base/test_theme_provider.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_manager.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme_base.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

class SurfaceTestNativeTheme final : public ui::NativeThemeBase {
 public:
  ~SurfaceTestNativeTheme() override = default;
};

class HorizontalTabImageTheme final : public TestThemeProvider {
 public:
  bool HasCustomImage(int id) const override { return id == image_id; }
  int image_id = 0;
};

TEST(YeeSurfaceColorTest, HorizontalGroupsAndMembersShareNativeThemeFallback) {
  SurfaceTestNativeTheme native_theme;
  native_theme.set_preferred_contrast(
      ui::NativeTheme::PreferredContrast::kNoPreference);
  native_theme.set_forced_colors(ui::ColorProviderKey::ForcedColors::kNone);
  HorizontalTabImageTheme theme;
  EXPECT_FALSE(ShouldUseNativeHorizontalTabTheme(native_theme, &theme));
  for (int id : {IDR_THEME_TOOLBAR, IDR_THEME_FRAME, IDR_THEME_FRAME_INACTIVE,
                 IDR_THEME_FRAME_INCOGNITO, IDR_THEME_FRAME_INCOGNITO_INACTIVE,
                 IDR_THEME_TAB_BACKGROUND, IDR_THEME_TAB_BACKGROUND_INACTIVE,
                 IDR_THEME_TAB_BACKGROUND_INCOGNITO,
                 IDR_THEME_TAB_BACKGROUND_INCOGNITO_INACTIVE}) {
    SCOPED_TRACE(id);
    theme.image_id = id;
    EXPECT_TRUE(ShouldUseNativeHorizontalTabTheme(native_theme, &theme));
  }
  theme.image_id = 0;
  native_theme.set_preferred_contrast(
      ui::NativeTheme::PreferredContrast::kMore);
  EXPECT_TRUE(ShouldUseNativeHorizontalTabTheme(native_theme, &theme));
  native_theme.set_preferred_contrast(
      ui::NativeTheme::PreferredContrast::kNoPreference);
  native_theme.set_forced_colors(ui::ColorProviderKey::ForcedColors::kSystem);
  EXPECT_TRUE(ShouldUseNativeHorizontalTabTheme(native_theme, &theme));
  native_theme.set_forced_colors(ui::ColorProviderKey::ForcedColors::kNone);
  EXPECT_FALSE(ShouldUseNativeHorizontalTabTheme(native_theme, &theme));
}

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
       {SkColorSetRGB(0xFA, 0xF4, 0xE5), SkColorSetRGB(0x18, 0x1A, 0x1F),
        SkColorSetRGB(0x82, 0x82, 0x82), SkColorSetRGB(0x31, 0x76, 0x77)}) {
    const BrowserSurfaceHeaderColors colors =
        ResolveBrowserSurfaceHeaderColors(surface);
    EXPECT_EQ(SkColorGetA(colors.primary), SK_AlphaOPAQUE);
    EXPECT_EQ(SkColorGetA(colors.secondary), SK_AlphaOPAQUE);
    const float strongest =
        std::max(color_utils::GetContrastRatio(SK_ColorBLACK, surface),
                 color_utils::GetContrastRatio(SK_ColorWHITE, surface));
    EXPECT_GE(color_utils::GetContrastRatio(colors.primary, surface),
              std::min(7.0f, strongest));
    EXPECT_GE(color_utils::GetContrastRatio(colors.secondary, surface),
              color_utils::kMinimumReadableContrastRatio);
    EXPECT_GT(color_utils::GetContrastRatio(colors.primary, surface),
              color_utils::GetContrastRatio(colors.secondary, surface));
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

TEST(YeeSurfaceColorTest, ThemeTintKeepsPageColorAndReadableInteractionStates) {
  for (SkColor accent : {SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE,
                         SK_ColorWHITE, SK_ColorBLACK}) {
    for (int gray = 0; gray <= 255; ++gray) {
      const SkColor surface = SkColorSetRGB(gray, gray, gray);
      const auto neutral = ResolveBrowserSurfacePresentation(surface, 1, 2, 3);
      const auto tinted =
          ResolveBrowserSurfacePresentation(surface, 1, 2, 3, accent);
      EXPECT_EQ(tinted.surface, neutral.surface);
      EXPECT_EQ(tinted.primary, neutral.primary);
      EXPECT_EQ(tinted.secondary, neutral.secondary);
      for (SkColor ink : {tinted.primary, tinted.secondary}) {
        EXPECT_GE(color_utils::GetContrastRatio(ink, tinted.location_hover),
                  color_utils::kMinimumReadableContrastRatio);
      }
      EXPECT_GE(color_utils::GetContrastRatio(tinted.focus_stroke, surface),
                color_utils::kMinimumVisibleContrastRatio);
      EXPECT_GE(color_utils::GetContrastRatio(tinted.focus_stroke,
                                              tinted.location_hover),
                color_utils::kMinimumVisibleContrastRatio);
      EXPECT_GE(
          color_utils::GetContrastRatio(tinted.editor_selection_foreground,
                                        tinted.editor_selection_background),
          color_utils::kMinimumReadableContrastRatio);
      for (SkColor fill : {tinted.popup_hover, tinted.popup_selected}) {
        EXPECT_GE(color_utils::GetContrastRatio(tinted.focus_stroke, fill),
                  color_utils::kMinimumVisibleContrastRatio);
        EXPECT_GE(
            color_utils::GetContrastRatio(tinted.popup_selected_primary, fill),
            color_utils::kMinimumReadableContrastRatio);
        EXPECT_GE(color_utils::GetContrastRatio(tinted.popup_selected_secondary,
                                                fill),
                  color_utils::kMinimumReadableContrastRatio);
      }
    }
  }
}

TEST(YeeSurfaceColorTest, FocusFollowsTextPolarityOnChromaticPages) {
  for (SkColor surface :
       {SkColorSetRGB(0x32, 0x63, 0xB5), SkColorSetRGB(0x4B, 0x2B, 0x85),
        SK_ColorWHITE, SK_ColorBLACK}) {
    for (SkColor accent : {SK_ColorRED, SK_ColorBLUE, SK_ColorGREEN}) {
      const auto p =
          ResolveBrowserSurfacePresentation(surface, 0, 0, 0, accent);
      const float background_luminance =
          color_utils::GetRelativeLuminance(surface);
      EXPECT_EQ(
          color_utils::GetRelativeLuminance(p.primary) > background_luminance,
          color_utils::GetRelativeLuminance(p.focus_stroke) >
              background_luminance);
      EXPECT_GE(color_utils::GetContrastRatio(p.focus_stroke, p.location_hover),
                color_utils::kMinimumVisibleContrastRatio);
      EXPECT_GE(color_utils::GetContrastRatio(p.editor_selection_foreground,
                                              p.editor_selection_background),
                color_utils::kMinimumReadableContrastRatio);
    }
  }
}

TEST(YeeSurfaceColorTest, PopupProvidersAreExactAndDoNotGrowGlobalCache) {
  ui::ColorProviderManager& manager = ui::ColorProviderManager::Get();
  const size_t cache_size = manager.num_cached_providers_for_testing();
  const auto expected_custom_colors = [](const BrowserSurfacePresentation&
                                             presentation) {
    return std::array<std::pair<ui::ColorId, SkColor>, 26>{
        {{kColorOmniboxResultsBackground, presentation.surface},
         {kColorOmniboxResultsBackgroundHovered, presentation.popup_hover},
         {kColorOmniboxResultsBackgroundSelected, presentation.popup_selected},
         {kColorOmniboxResultsBackgroundIph, presentation.popup_hover},
         {kColorOmniboxResultsBackgroundHoverOverlay, presentation.popup_hover},
         {kColorOmniboxBubbleOutline, presentation.popup_outline},
         {kColorOmniboxResultsFocusIndicator, presentation.focus_stroke},
         {kColorOmniboxResultsChipBackground, presentation.popup_hover},
         {kColorOmniboxText, presentation.popup_selected_primary},
         {kColorOmniboxTextDimmed, presentation.popup_selected_secondary},
         {kColorOmniboxResultsTextSelected,
          presentation.popup_selected_primary},
         {kColorOmniboxResultsTextAnswer, presentation.popup_selected_primary},
         {kColorOmniboxResultsTextDimmed,
          presentation.popup_selected_secondary},
         {kColorOmniboxResultsTextDimmedSelected,
          presentation.popup_selected_secondary},
         {kColorOmniboxResultsTextSecondary,
          presentation.popup_selected_secondary},
         {kColorOmniboxResultsTextSecondarySelected,
          presentation.popup_selected_secondary},
         {kColorOmniboxResultsUrl, presentation.popup_selected_primary},
         {kColorOmniboxResultsUrlSelected, presentation.popup_selected_primary},
         {kColorOmniboxKeywordSelected, presentation.popup_selected_primary},
         {kColorOmniboxKeywordSeparator, presentation.popup_selected_secondary},
         {kColorOmniboxResultsIcon, presentation.popup_selected_primary},
         {kColorOmniboxResultsIconSelected,
          presentation.popup_selected_primary},
         {kColorOmniboxResultsButtonIcon, presentation.popup_selected_primary},
         {kColorOmniboxResultsButtonIconSelected,
          presentation.popup_selected_primary},
         {kColorOmniboxResultsButtonBorder, presentation.popup_outline},
         {kColorOmniboxResultsIconGM3Background, presentation.popup_hover}}};
  };

  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7E, 0x7E, 0x7E),
        SkColorSetRGB(0x2A, 0x64, 0x91)}) {
    const BrowserSurfacePresentation presentation =
        ResolveBrowserSurfacePresentation(surface, 0, 0, 0,
                                          SkColorSetRGB(0xAD, 0x32, 0x94));
    ui::ColorProviderKey key;
    ui::ColorProviderKey native_key = key;
    native_key.color_mode = color_utils::IsDark(surface)
                                ? ui::ColorProviderKey::ColorMode::kDark
                                : ui::ColorProviderKey::ColorMode::kLight;
    std::unique_ptr<ui::ColorProvider> native =
        manager.CreateUncachedColorProvider(native_key);
    std::unique_ptr<ui::ColorProvider> provider =
        CreateBrowserSurfaceOmniboxPopupColorProvider(key, presentation);
    for (const auto& [color_id, expected] :
         expected_custom_colors(presentation)) {
      EXPECT_EQ(expected, provider->GetColor(color_id));
    }
    // Security warnings are semantic Chromium state, not Yee presentation.
    EXPECT_EQ(native->GetColor(kColorOmniboxSecurityChipDangerous),
              provider->GetColor(kColorOmniboxSecurityChipDangerous));
    EXPECT_EQ(cache_size, manager.num_cached_providers_for_testing());
  }

  const std::array<ui::ColorId, 26> overridden_ids = {
      kColorOmniboxResultsBackground,
      kColorOmniboxResultsBackgroundHovered,
      kColorOmniboxResultsBackgroundSelected,
      kColorOmniboxResultsBackgroundIph,
      kColorOmniboxResultsBackgroundHoverOverlay,
      kColorOmniboxBubbleOutline,
      kColorOmniboxResultsFocusIndicator,
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
    SurfaceTestNativeTheme theme;
    theme.set_forced_colors(key.forced_colors);
    theme.set_preferred_contrast(
        key.contrast_mode == ui::ColorProviderKey::ContrastMode::kHigh
            ? ui::NativeTheme::PreferredContrast::kMore
            : ui::NativeTheme::PreferredContrast::kCustom);
    EXPECT_TRUE(ShouldUseNativeBrowserSurfaceColors(theme));
    // 강제 색의 낮은 대비도 시스템 팔레트를 보존한다. 일반 낮은 대비는
    // 기존 페이지색을 사용한다.
    if (key.forced_colors != ui::ColorProviderKey::ForcedColors::kNone) {
      theme.set_preferred_contrast(ui::NativeTheme::PreferredContrast::kLess);
      EXPECT_TRUE(ShouldUseNativeBrowserSurfaceColors(theme));
      theme.set_forced_colors(ui::ColorProviderKey::ForcedColors::kNone);
      EXPECT_FALSE(ShouldUseNativeBrowserSurfaceColors(theme));
    }
    std::unique_ptr<ui::ColorProvider> native =
        manager.CreateUncachedColorProvider(key);
    std::unique_ptr<ui::ColorProvider> provider =
        CreateBrowserSurfaceOmniboxPopupColorProvider(
            key, ResolveBrowserSurfacePresentation(
                     SkColorSetRGB(0x7E, 0x7E, 0x7E), 0, 0, 0));
    for (ui::ColorId color_id : overridden_ids) {
      EXPECT_EQ(native->GetColor(color_id), provider->GetColor(color_id));
    }
    EXPECT_EQ(cache_size, manager.num_cached_providers_for_testing());
  }
}

TEST(BrowserSurfacePresentationResolverTest,
     PopupSelectionAndHoverKeepDistinctReadableRoles) {
  for (int gray = 0; gray < 256; ++gray) {
    const auto p = ResolveBrowserSurfacePresentation(
        SkColorSetRGB(gray, gray, gray), 1, 1, 1);
    EXPECT_NE(p.popup_hover, p.popup_selected) << gray;
    EXPECT_GE(color_utils::GetContrastRatio(p.popup_selected_primary,
                                            p.popup_selected),
              4.5f)
        << gray;
    EXPECT_GE(color_utils::GetContrastRatio(p.popup_selected_secondary,
                                            p.popup_selected),
              4.5f)
        << gray;
    for (SkColor chip_surface : {p.surface, p.popup_hover}) {
      EXPECT_GE(
          color_utils::GetContrastRatio(p.popup_selected_primary, chip_surface),
          4.5f)
          << gray;
      EXPECT_GE(color_utils::GetContrastRatio(p.popup_selected_secondary,
                                              chip_surface),
                4.5f)
          << gray;
    }
  }
}

TEST(BrowserSurfacePresentationResolverTest,
     SemanticWarningPreservesReadableNativeColorAndRepairsLowContrast) {
  constexpr SkColor warning = SkColorSetRGB(0xC5, 0x22, 0x1F);
  for (int gray = 0; gray < 256; ++gray) {
    const SkColor surface = SkColorSetRGB(gray, gray, gray);
    const SkColor result = ResolveBrowserSurfaceSemanticColor(surface, warning);
    EXPECT_GE(color_utils::GetContrastRatio(result, surface), 4.5f) << gray;
    if (color_utils::GetContrastRatio(warning, surface) >= 4.5f) {
      EXPECT_EQ(warning, result) << gray;
    }
    EXPECT_GT(SkColorGetR(result), SkColorGetG(result));
    EXPECT_GT(SkColorGetR(result), SkColorGetB(result));
  }
  EXPECT_GE(color_utils::GetContrastRatio(
                ResolveBrowserSurfaceSemanticColor(warning, warning), warning),
            4.5f);
}

class YeeRestingTextViewTest : public ChromeViewsTestBase {
 public:
  void SetUp() override {
    ChromeViewsTestBase::SetUp();
    widget_ = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  }

  void TearDown() override {
    widget_.reset();
    ChromeViewsTestBase::TearDown();
  }

 protected:
  std::unique_ptr<views::Widget> widget_;
};

TEST_F(YeeRestingTextViewTest, HorizontalMinimumKeepsFaviconGap) {
  auto* view = widget_->SetContentsView(std::make_unique<views::View>());
  EXPECT_EQ(38, ResolveHorizontalTabMinimumInactiveWidth(*view, 32, 18));
  EXPECT_EQ(64, ResolveHorizontalTabMinimumInactiveWidth(*view, 64, 18));
  EXPECT_EQ(gfx::Insets::TLBR(0, 18, 0, 18),
            ResolveHorizontalTabGroupHeaderInsets(*view, 8, 12, true));
  EXPECT_EQ(gfx::Insets::TLBR(0, 18, 0, 8),
            ResolveHorizontalTabGroupHeaderInsets(*view, 8, 12, false));
  EXPECT_EQ(54, ResolveHorizontalTabGroupMinimumWidth(*view, 38, 20, 12));
  EXPECT_EQ(64, ResolveHorizontalTabGroupMinimumWidth(*view, 64, 20, 12));
}

TEST_F(YeeRestingTextViewTest, HorizontalGroupHoverStaysInsideCapsule) {
  auto* view = widget_->SetContentsView(std::make_unique<views::View>());
  view->SetBounds(0, 0, 120, 40);
  const bool original_rtl = base::i18n::IsRTL();
  for (bool rtl : {false, true}) {
    base::i18n::SetRTLForTesting(rtl);
    for (bool collapsed : {false, true}) {
      const auto background = CreateHorizontalTabGroupBackground(
          SK_ColorGREEN, true, gfx::Insets::TLBR(0, 10, 0, collapsed ? 10 : 0));
      gfx::Canvas canvas(view->size(), 1.f, false);
      background->Paint(&canvas, view);
      const SkBitmap bitmap = canvas.GetBitmap();
      const SkAlpha alpha = SkColorGetA(bitmap.getColor(60, 20));
      EXPECT_EQ(SK_AlphaTRANSPARENT,
                SkColorGetA(bitmap.getColor(rtl ? 119 : 0, 20)));
      // 둥근 외곽의 끝 픽셀은 anti-alias되므로 한 픽셀 안쪽의 채움을 본다.
      EXPECT_EQ(collapsed ? SK_AlphaTRANSPARENT : alpha,
                SkColorGetA(bitmap.getColor(rtl ? 1 : 118, 20)));
      EXPECT_EQ(SK_AlphaTRANSPARENT,
                SkColorGetA(bitmap.getColor(rtl ? 0 : 119, 8)));
      EXPECT_GT(alpha, SK_AlphaTRANSPARENT);
      EXPECT_LT(alpha, SK_AlphaOPAQUE);
    }
  }
  base::i18n::SetRTLForTesting(original_rtl);
}

TEST_F(YeeRestingTextViewTest,
       HorizontalCapsuleRestoresCompactWithoutHidingGlass) {
  const bool original_motion = gfx::Animation::PrefersReducedMotion();
  base::ScopedClosureRunner restore_motion(base::BindOnce(
      &gfx::Animation::SetPrefersReducedMotionForTesting, original_motion));
  gfx::Animation::SetPrefersReducedMotionForTesting(true);
  auto* view = widget_->SetContentsView(std::make_unique<views::View>());
  view->SetBounds(0, 0, 120, kSidebarMetrics.horizontal_group_capsule_height);
  for (bool collapsed : {true, false, true}) {
    SetHorizontalTabGroupCapsuleCollapsed(*view, collapsed);
    gfx::Canvas canvas(view->size(), 1.f, false);
    PaintHorizontalTabGroupCapsule(canvas, *view, SK_ColorGREEN);
    const SkBitmap bitmap = canvas.GetBitmap();
    const SkAlpha center = SkColorGetA(bitmap.getColor(60, 16));
    EXPECT_GT(center, SK_AlphaTRANSPARENT);
    EXPECT_LT(center, SK_AlphaOPAQUE);
    EXPECT_EQ(collapsed ? SK_AlphaTRANSPARENT : center,
              SkColorGetA(bitmap.getColor(60, 0)));
    EXPECT_EQ(collapsed ? SK_AlphaTRANSPARENT : center,
              SkColorGetA(bitmap.getColor(60, 31)));
    EXPECT_EQ(center, SkColorGetA(bitmap.getColor(60, 4)));
    // 탭과 같은 외곽 곡률로 모서리의 사각 채움을 남기지 않는다.
    EXPECT_EQ(SK_AlphaTRANSPARENT,
              SkColorGetA(bitmap.getColor(2, collapsed ? 5 : 1)));
    EXPECT_EQ(center, SkColorGetA(bitmap.getColor(4, collapsed ? 9 : 5)));
  }
}

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

TEST_F(YeeRestingTextViewTest, RestingTextSharesHoveredControlSurface) {
  auto* resting = widget_->SetContentsView(CreateOmniboxRestingTextView());
  for (SkColor surface : {SK_ColorWHITE, SK_ColorBLACK, SK_ColorRED}) {
    const auto presentation =
        ResolveBrowserSurfacePresentation(surface, 1, 1, 1);
    for (SkColor background : {surface, presentation.location_hover, surface}) {
      UpdateOmniboxRestingTextView(*resting, u"Title", u"example.test",
                                   presentation, true, background);
      EXPECT_EQ(background, resting->background()->color().ResolveToSkColor(
                                resting->GetColorProvider()));
      for (size_t index : {0u, 2u}) {
        auto* label =
            views::AsViewClass<views::Label>(resting->children()[index]);
        ASSERT_TRUE(label);
        EXPECT_EQ(background, label->GetBackgroundColor());
      }
    }
  }
}

TEST_F(YeeRestingTextViewTest, ControlDividerTracksSurfaceAndNativeReset) {
  auto* divider = widget_->SetContentsView(CreateShellControlDivider());
  EXPECT_EQ(gfx::Size(1, kSidebarMetrics.shell_control_separator_height),
            divider->GetPreferredSize());
  const auto native_color = divider->background()->color();
  for (SkColor surface : {SK_ColorWHITE, SK_ColorBLACK, SK_ColorRED}) {
    const auto presentation =
        ResolveBrowserSurfacePresentation(surface, 1, 1, 1);
    UpdateShellControlDivider(*divider, presentation);
    EXPECT_EQ(presentation.resting_divider,
              divider->background()->color().ResolveToSkColor(
                  divider->GetColorProvider()));
    UpdateShellControlDivider(*divider,
                              UseNativeBrowserSurfaceColors(presentation));
    EXPECT_EQ(native_color, divider->background()->color());
  }
  UpdateShellControlDivider(*divider, std::nullopt);
  EXPECT_EQ(native_color, divider->background()->color());
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
  EXPECT_EQ(158, GetSidebarHeaderMinimumWidth(86));
  const auto flyout = ResolveSidebarFlyoutGeometry(0.0, 1.0, true);
  EXPECT_EQ(12, flyout.leading_offset);
  EXPECT_EQ(54, flyout.top_inset);
  EXPECT_EQ(6, flyout.header_inset);
  EXPECT_EQ(6, flyout.hit_test_top_inset);
  EXPECT_EQ(12, flyout.bottom_inset + kSidebarMetrics.content_gutter);
  const auto hidden = ResolveSidebarFlyoutGeometry(0.0, -1.0, true);
  EXPECT_EQ(0, hidden.leading_offset);
  EXPECT_EQ(48, hidden.top_inset);
  EXPECT_EQ(0, hidden.hit_test_top_inset);
  EXPECT_EQ(0, hidden.bottom_inset);
  EXPECT_EQ(0, ResolveSidebarFlyoutGeometry(1.0, 0.0, true).top_inset);
  EXPECT_EQ(12, ResolveSidebarFlyoutGeometry(0.0, 1.0, false).top_inset);
  const auto external = ResolveSidebarFlyoutGeometry(0.0, 1.0, false, true);
  EXPECT_EQ(flyout.top_inset,
            kSidebarMetrics.titlebar_height + external.top_inset);
  EXPECT_EQ(flyout.hit_test_top_inset, external.hit_test_top_inset);
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
     ClosingGroupMemberFollowsTheHeaderInBothDirections) {
  EXPECT_EQ(gfx::Rect(112, 3, 16, 32), ResolveHorizontalTabGroupClosingBounds(
                                           gfx::Rect(0, 3, 16, 32), 128, 16));
  EXPECT_EQ(gfx::Rect(112, 3, 16, 32), ResolveHorizontalTabGroupClosingBounds(
                                           gfx::Rect(240, 3, 16, 32), 128, 16));
}

TEST(YeeSurfaceGeometryTest, ClosingHeaderTrackingEndsWithTheHeaderLifetime) {
  views::View tab;
  EXPECT_FALSE(GetHorizontalTabGroupClosingHeader(tab));
  {
    views::View header;
    TrackHorizontalTabGroupClosingHeader(tab, header);
    EXPECT_EQ(&header, GetHorizontalTabGroupClosingHeader(tab));
  }
  EXPECT_FALSE(GetHorizontalTabGroupClosingHeader(tab));
}

TEST(YeeSurfaceGeometryTest, CustomizationPanelKeepsBothHeadersAboveItsClose) {
  EXPECT_TRUE(
      UsesFullWidthBrowserSurfaceHeader(BrowserSurfaceMode::kCard, true));
  EXPECT_TRUE(UsesFullWidthBrowserSurfaceHeader(
      BrowserSurfaceMode::kConnectedTabs, true));
  EXPECT_TRUE(UsesFullWidthBrowserSurfaceHeader(
      BrowserSurfaceMode::kConnectedTabs, false));
  EXPECT_FALSE(
      UsesFullWidthBrowserSurfaceHeader(BrowserSurfaceMode::kCard, false));
}

TEST(YeeSurfaceGeometryTest, HorizontalTabsReserveOneRowBeforeTheSurface) {
  const gfx::Rect client(11, 17, 1200, 900);
  for (int advance : {0, 40, 52}) {
    const gfx::Rect column = ResolveBrowserSurfaceContentColumn(
        client, kSidebarMetrics.content_gutter, advance);
    const auto seed = ResolveBrowserSurfaceSeed(
        {column, gfx::Insets(kSidebarMetrics.content_gutter), false, true,
         kSidebarMetrics.browser_surface_header_height()});
    EXPECT_EQ(client.y() + std::max(kSidebarMetrics.content_gutter, advance),
              seed.header_bounds.y());
    EXPECT_EQ(client.bottom() - kSidebarMetrics.content_gutter,
              seed.surface_seed_bounds.bottom());
    EXPECT_EQ(client.x() + kSidebarMetrics.content_gutter,
              seed.header_bounds.x());
  }
  EXPECT_TRUE(ResolveBrowserSurfaceContentColumn(client, 5000, 5000).IsEmpty());
}

TEST(YeeSurfaceColorTest, HorizontalTabsFollowThemeAndKeepReadableStates) {
  for (bool dark : {false, true}) {
    ui::ColorProvider colors;
    SetSidebarPaletteForTesting(colors, dark);
    const SkColor surface = dark ? SK_ColorBLACK : SK_ColorWHITE;
    const SkColor foreground = dark ? SK_ColorWHITE : SK_ColorBLACK;
    for (auto id : {kColorTabBackgroundActiveFrameActive,
                    kColorTabBackgroundActiveFrameInactive})
      colors.SetColorForTesting(id, surface);
    for (auto id : {kColorTabForegroundActiveFrameActive,
                    kColorTabForegroundActiveFrameInactive})
      colors.SetColorForTesting(id, foreground);
    for (bool frame_active : {false, true}) {
      const auto active =
          ResolveHorizontalTabColors(colors, true, true, 1.0, frame_active);
      EXPECT_EQ(surface, active.background);
      EXPECT_EQ(foreground, active.foreground);
      const auto active_split = ResolveHorizontalTabColors(
          colors, true, true, 1.0, frame_active, std::nullopt, true);
      EXPECT_EQ(active.background, active_split.background);
      EXPECT_EQ(active.foreground, active_split.foreground);
      EXPECT_EQ(surface, ResolveHorizontalTabColors(colors, true, true, 1.0,
                                                    frame_active, SK_ColorBLUE)
                             .background);
      // 전역 테마와 반대인 페이지도 실제 Header 색과 같은 면으로 이어진다.
      for (SkColor page :
           {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7E, 0x7E, 0x7E),
            SkColorSetRGB(0x13, 0x4E, 0x4A)}) {
        const auto connected = ResolveHorizontalTabColors(
            colors, true, true, 1.0, frame_active, SK_ColorBLUE, false, page);
        EXPECT_EQ(page, connected.background);
        EXPECT_GE(color_utils::GetContrastRatio(connected.foreground,
                                                connected.background),
                  color_utils::kMinimumReadableContrastRatio);
      }
      EXPECT_GE(
          color_utils::GetContrastRatio(active.foreground, active.background),
          color_utils::kMinimumReadableContrastRatio);
      for (bool selected : {false, true}) {
        for (double hover : {0.0, 0.5, 1.0}) {
          const auto tab = ResolveHorizontalTabColors(colors, false, selected,
                                                      hover, frame_active);
          EXPECT_GE(
              color_utils::GetContrastRatio(tab.foreground, tab.background),
              color_utils::kMinimumReadableContrastRatio);
          const auto grouped = ResolveHorizontalTabColors(
              colors, false, selected, hover, frame_active, SK_ColorBLUE);
          EXPECT_NE(tab.background, grouped.background);
          EXPECT_GE(color_utils::GetContrastRatio(grouped.foreground,
                                                  grouped.background),
                    color_utils::kMinimumReadableContrastRatio);
          const auto split = ResolveHorizontalTabColors(
              colors, false, selected, hover, frame_active, std::nullopt, true);
          EXPECT_NE(tab.background, split.background);
          EXPECT_GE(
              color_utils::GetContrastRatio(split.foreground, split.background),
              color_utils::kMinimumReadableContrastRatio);
        }
      }
    }
  }
}

TEST(YeeSurfaceGeometryTest, WindowControlsStayInsideTheRoundedHeader) {
  const gfx::Rect frame(0, 0, 1100, 800);
  const gfx::Rect trailing =
      ResolveBrowserWindowControlsBounds(frame, 138, false);
  const gfx::Rect leading =
      ResolveBrowserWindowControlsBounds(frame, 138, true);
  EXPECT_EQ(gfx::Rect(948, 7, 138, 40), trailing);
  EXPECT_EQ(gfx::Rect(14, 7, 138, 40), leading);
  EXPECT_EQ(kSidebarMetrics.browser_surface_header_center_y(),
            trailing.CenterPoint().y());
  EXPECT_EQ(kSidebarMetrics.content_gutter +
                kSidebarMetrics.browser_surface_header_control_edge_inset,
            frame.right() - trailing.right());
  EXPECT_EQ(trailing.y(), leading.y());
  EXPECT_EQ(trailing.size(), leading.size());
}

TEST(YeeSurfaceGeometryTest, WindowControlsDoNotEnlargeHorizontalTabRow) {
  const gfx::Rect frame(20, 30, 1100, 800);
  for (int row_height : {41, 49}) {
    for (bool leading : {false, true}) {
      const gfx::Rect controls =
          ResolveBrowserWindowControlsBounds(frame, 138, leading, row_height);
      const gfx::Rect row(frame.x(), frame.y(), frame.width(), row_height);
      EXPECT_TRUE(row.Contains(controls));
      EXPECT_EQ(frame.y() + (row_height - kSidebarMetrics.toolbar_height) / 2,
                controls.y());
      EXPECT_EQ(kSidebarMetrics.toolbar_height, controls.height());
      EXPECT_EQ(138, controls.width());
      EXPECT_EQ(leading ? frame.x() + kSidebarMetrics.content_gutter
                        : frame.right() - kSidebarMetrics.content_gutter,
                leading ? controls.x() : controls.right());
    }
  }
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

  input.mode = BrowserSurfaceMode::kConnectedTabs;
  input.side_panel_surface_visible = true;
  for (bool leading : {false, true}) {
    const gfx::Rect body(leading ? 560 : 250, 48, 634, 746);
    const gfx::Rect gutter(leading ? 550 : 884, 48, 10, 746);
    input.native_notice_flow_allocation = body;
    input.native_body_allocation = body;
    input.connected_side_panel_resize_gutter = gutter;
    const auto connected = ResolveBrowserSurfaceFrame(input);
    EXPECT_EQ(body, connected.multi_contents_bounds);
    EXPECT_EQ(body, connected.notice_flow_bounds);
    EXPECT_EQ(connected.surface_seed_bounds,
              connected.decoration.combined_outline_bounds);
    EXPECT_EQ(gutter.CenterPoint().x() - connected.header_bounds.x(),
              connected.decoration.side_panel_divider_x);
    EXPECT_TRUE(connected.decoration.side_panel_gutter_on_page_surface);
    input.connected_side_panel_resize_gutter->set_height(gutter.height() + 10);
    EXPECT_FALSE(ResolveBrowserSurfaceFrame(input)
                     .decoration.side_panel_gutter_on_page_surface);
    input.connected_side_panel_resize_gutter.reset();
    const auto restored = ResolveBrowserSurfaceFrame(input);
    EXPECT_EQ(restored.main_surface_bounds,
              restored.decoration.combined_outline_bounds);
    EXPECT_FALSE(restored.decoration.side_panel_divider_x);
    EXPECT_FALSE(restored.decoration.side_panel_gutter_on_page_surface);
  }
}

TEST(YeeSurfaceGeometryTest, WindowControlsReserveOnlyTheOverlappingHeaderRow) {
  const gfx::Rect controls(948, 7, 138, 40);
  EXPECT_EQ(gfx::Rect(850, 6, 98, 42),
            ExcludeBrowserWindowControls(gfx::Rect(850, 6, 244, 42), controls));
  EXPECT_EQ(gfx::Rect(250, 6, 400, 42),
            ExcludeBrowserWindowControls(gfx::Rect(250, 6, 400, 42), controls));
  EXPECT_EQ(
      gfx::Rect(850, 400, 244, 42),
      ExcludeBrowserWindowControls(gfx::Rect(850, 400, 244, 42), controls));
  EXPECT_EQ(gfx::Rect(152, 6, 154, 42),
            ExcludeBrowserWindowControls(gfx::Rect(6, 6, 300, 42),
                                         gfx::Rect(14, 7, 138, 40)));
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
  const gfx::Insets native_shadow_insets = gfx::Insets::TLBR(6, 0, 6, 6);
  native_remaining_bounds.Inset(native_shadow_insets);

  const gfx::Rect restored_content_column =
      RestoreBrowserSurfaceContentColumnAfterSidePanel(native_remaining_bounds,
                                                       native_shadow_insets);
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

  // 모드와 패널 표시 여부는 명시적 입력이다. 호스팅 Widget이나 sibling View가
  // 윤곽·그림자의 소유권을 다시 결정하면 안 된다.
  for (const BrowserSurfaceMode mode :
       {BrowserSurfaceMode::kCard, BrowserSurfaceMode::kConnectedTabs}) {
    for (bool panel_visible : {false, true}) {
      input.mode = mode;
      input.side_panel_surface_visible = panel_visible;
      const auto resolved = ResolveBrowserSurfaceFrame(input);
      EXPECT_EQ(frame.header_bounds, resolved.header_bounds);
      EXPECT_EQ(frame.main_surface_bounds, resolved.main_surface_bounds);
      EXPECT_EQ(mode, resolved.decoration.mode);
      EXPECT_TRUE(resolved.decoration.combined_outline_visible);
      EXPECT_EQ(mode == BrowserSurfaceMode::kCard,
                resolved.decoration.draw_outer_outline);
      EXPECT_EQ(mode == BrowserSurfaceMode::kCard && !panel_visible,
                resolved.decoration.show_shadow);
      EXPECT_EQ(mode == BrowserSurfaceMode::kConnectedTabs,
                resolved.decoration.side_panel_joins_header);
      EXPECT_EQ(frame.decoration.header_separator_owner,
                resolved.decoration.header_separator_owner);
      EXPECT_EQ(frame.decoration.combined_header_separator_offset,
                resolved.decoration.combined_header_separator_offset);
    }
  }
  input.mode = BrowserSurfaceMode::kCard;
  input.side_panel_surface_visible = false;

  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/false, /*header_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(250, 6, 944, 788);
  input.native_body_allocation = gfx::Rect(250, 6, 944, 788);
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_TRUE(frame.decoration.combined_outline_visible);
  EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kNone,
            frame.decoration.header_separator_owner);
  EXPECT_FALSE(frame.decoration.side_panel_joins_header);
  EXPECT_EQ(0, frame.decoration.combined_header_separator_offset);
  EXPECT_EQ(kSidebarMetrics.content_corner_radius -
                kSidebarMetrics.browser_surface_outline_width,
            frame.decoration.infobar_top_corner_radius);
  input.mode = BrowserSurfaceMode::kConnectedTabs;
  EXPECT_FALSE(
      ResolveBrowserSurfaceFrame(input).decoration.side_panel_joins_header);
  input.mode = BrowserSurfaceMode::kCard;

  // 외부 Header는 본문 행을 더하지 않고 같은 카드의 위쪽을 이어 준다.
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/false,
      /*header_participates=*/false, /*header_height=*/42,
      /*header_above_content_height=*/42});
  input.native_notice_flow_allocation = gfx::Rect(250, 0, 944, 794);
  input.native_body_allocation = input.native_notice_flow_allocation;
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_EQ(0, frame.header_bounds.bottom());
  EXPECT_EQ(0, frame.multi_contents_bounds.y());
  EXPECT_EQ(-42, frame.decoration.combined_outline_bounds.y());
  EXPECT_EQ(42, frame.decoration.combined_header_separator_offset);
  EXPECT_EQ(0.0f, frame.decoration.infobar_top_corner_radius);

  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), /*split=*/true,
      /*header_participates=*/false, /*header_height=*/42});
  for (const BrowserSurfaceMode mode :
       {BrowserSurfaceMode::kCard, BrowserSurfaceMode::kConnectedTabs}) {
    for (bool panel_visible : {false, true}) {
      input.mode = mode;
      input.side_panel_surface_visible = panel_visible;
      frame = ResolveBrowserSurfaceFrame(input);
      EXPECT_EQ(mode, frame.decoration.mode);
      EXPECT_FALSE(frame.decoration.combined_outline_visible);
      EXPECT_FALSE(frame.decoration.draw_outer_outline);
      EXPECT_FALSE(frame.decoration.show_shadow);
      EXPECT_FALSE(frame.decoration.side_panel_joins_header);
      EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kSplitPane,
                frame.decoration.header_separator_owner);
      EXPECT_EQ(0, frame.decoration.combined_header_separator_offset);
      EXPECT_EQ(0.0f, frame.decoration.infobar_top_corner_radius);
    }
  }
}

TEST(YeeSurfaceGeometryTest, SidebarCustomizationSharesOneHeaderDecoration) {
  BrowserSurfaceFrameInput input;
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), false, true, 42});
  input.native_notice_flow_allocation = gfx::Rect(250, 48, 568, 746);
  input.native_body_allocation = input.native_notice_flow_allocation;
  input.side_panel_surface_visible = true;
  input.connected_side_panel_resize_gutter = gfx::Rect(818, 48, 8, 746);
  auto frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_EQ(BrowserSurfaceMode::kCard, frame.decoration.mode);
  EXPECT_TRUE(frame.decoration.side_panel_joins_header);
  EXPECT_FALSE(frame.decoration.draw_outer_outline);
  EXPECT_FALSE(frame.decoration.show_shadow);
  EXPECT_EQ(frame.header_bounds.width(),
            frame.decoration.combined_outline_bounds.width());
  EXPECT_TRUE(frame.decoration.side_panel_divider_x);

  // 자동 숨김 전체화면에서는 연결할 Header가 없어 본문 카드 윤곽을 유지한다.
  const auto visible_header_input = input;
  input.seed = ResolveBrowserSurfaceSeed(BrowserSurfaceSeedInput{
      gfx::Rect(244, 0, 956, 800), gfx::Insets(6), false, false, 42});
  input.native_notice_flow_allocation = gfx::Rect(250, 6, 568, 788);
  input.native_body_allocation = input.native_notice_flow_allocation;
  input.connected_side_panel_resize_gutter = gfx::Rect(818, 6, 8, 788);
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_TRUE(frame.header_bounds.IsEmpty());
  EXPECT_TRUE(frame.decoration.combined_outline_visible);
  EXPECT_TRUE(frame.decoration.draw_outer_outline);
  EXPECT_FALSE(frame.decoration.side_panel_joins_header);
  EXPECT_FALSE(frame.decoration.side_panel_divider_x);
  EXPECT_EQ(BrowserSurfaceHeaderSeparatorOwner::kNone,
            frame.decoration.header_separator_owner);
  EXPECT_EQ(frame.main_surface_bounds,
            frame.decoration.combined_outline_bounds);
  input.mode = BrowserSurfaceMode::kConnectedTabs;
  EXPECT_FALSE(ResolveBrowserSurfaceFrame(input).decoration.draw_outer_outline);
  input = visible_header_input;

  // 다른 세로 패널로 전환하면 원래 카드의 윤곽과 폭으로 복귀한다.
  input.connected_side_panel_resize_gutter.reset();
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_FALSE(frame.decoration.side_panel_joins_header);
  EXPECT_TRUE(frame.decoration.draw_outer_outline);
  EXPECT_EQ(frame.main_surface_bounds,
            frame.decoration.combined_outline_bounds);
  input.seed.split = true;
  input.connected_side_panel_resize_gutter = gfx::Rect(818, 48, 8, 746);
  frame = ResolveBrowserSurfaceFrame(input);
  EXPECT_FALSE(frame.decoration.side_panel_joins_header);
  EXPECT_FALSE(frame.decoration.combined_outline_visible);
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
    EXPECT_EQ(kSidebarMetrics.sidebar_row_hover_fill_alpha,
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
    EXPECT_EQ(kSidebarMetrics.sidebar_row_active_fill_alpha,
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_GT(SkColorGetA(active.fill), SkColorGetA(hovered_row.fill));
    const SidebarItemColors active_favorite = ResolveSidebarItemColors(
        provider, SidebarItemVisualState::kActive, /*hover_progress=*/0.0,
        /*frame_active=*/true, /*persistent_surface=*/true);
    EXPECT_LT(SkColorGetA(active.fill), SkColorGetA(active_favorite.fill));
    EXPECT_EQ(active.foreground, active_favorite.foreground);
    EXPECT_EQ(kSidebarMetrics.favorites_cell_active_stroke_alpha,
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.fill)),
              static_cast<int>(SkColorGetA(active.fill)));
    EXPECT_GT(static_cast<int>(SkColorGetA(dragging.stroke)),
              static_cast<int>(SkColorGetA(active.stroke)));
    EXPECT_EQ(active.foreground, dragging.foreground);
    const SkColor composited_active_fill =
        color_utils::GetResultingPaintColor(active.fill, shell);
#if BUILDFLAG(IS_WIN)
    EXPECT_GE(color_utils::GetContrastRatio(composited_active_fill, shell),
              1.3f);
    EXPECT_GE(color_utils::GetContrastRatio(
                  color_utils::GetResultingPaintColor(hovered_row.fill, shell),
                  shell),
              1.15f);
#endif
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

    // 고정과 hover에서 실제 합성되는 상태 면 모두 글자 대비를 유지한다.
    for (const bool frame_active : {false, true}) {
      const SkColor flyout = ResolveSidebarFlyoutColor(provider, frame_active);
      EXPECT_NE(provider.GetColor(ui::kColorSysBase), flyout);
      for (const auto state :
           {SidebarItemVisualState::kResting, SidebarItemVisualState::kHovered,
            SidebarItemVisualState::kActive,
            SidebarItemVisualState::kDragging}) {
        for (const bool persistent : {false, true}) {
          const SidebarItemColors colors = ResolveSidebarItemColors(
              provider, state, 1.0, frame_active, persistent);
          EXPECT_GE(color_utils::GetContrastRatio(
                        colors.foreground, color_utils::GetResultingPaintColor(
                                               colors.fill, flyout)),
                    color_utils::kMinimumReadableContrastRatio);
        }
      }
    }
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
