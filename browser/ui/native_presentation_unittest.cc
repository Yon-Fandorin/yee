// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>
#include <set>
#include <utility>

#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/test/scoped_command_line.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/tabs/tab_data.h"
#include "chrome/browser/ui/views/infobars/confirm_infobar.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/browser/ui/views/yee/product_logo.h"
#include "chrome/browser/ui/views/yee/side_panel_colors.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/test/views/chrome_views_test_base.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/omnibox/browser/test_omnibox_client.h"
#include "components/open_from_clipboard/fake_clipboard_recent_content.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/color/color_provider_manager.h"
#include "ui/color/color_recipe.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/render_text.h"
#include "ui/gfx/render_text_test_api.h"
#include "ui/views/cascading_property.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/link.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

class EmptyOmniboxView : public OmniboxViewViews {
 public:
  explicit EmptyOmniboxView(OmniboxController* controller)
      : OmniboxViewViews(false, controller, nullptr, gfx::FontList()) {}
  using OmniboxViewViews::OnThemeChanged;
  using views::Textfield::GetRenderText;
};

class NoticeDelegate : public ConfirmInfoBarDelegate {
 public:
  explicit NoticeDelegate(int buttons = BUTTON_NONE, bool link_before = false)
      : buttons_(buttons), link_before_(link_before) {}
  InfoBarIdentifier GetIdentifier() const override { return TEST_INFOBAR; }
  std::u16string GetMessageText() const override {
    return u"Google API 키가 누락되었습니다. Yee의 일부 기능이 사용 "
           u"중지됩니다.";
  }
  std::u16string GetLinkText() const override { return u"자세히 알아보기"; }
  int GetButtons() const override { return buttons_; }
  bool ShouldShowLinkBeforeButton() const override { return link_before_; }

 private:
  const int buttons_;
  const bool link_before_;
};

views::Link* FindLink(views::View& root) {
  if (auto* link = views::AsViewClass<views::Link>(&root)) {
    return link;
  }
  for (views::View* child : root.children()) {
    if (auto* link = FindLink(*child)) {
      return link;
    }
  }
  return nullptr;
}

class YeeNativePresentationTest : public ChromeViewsTestBase {};

class YeeTabIconPresentationTest : public ChromeViewsTestBase {};

TEST_F(YeeNativePresentationTest,
       SidebarHeaderButtonsStayOutsideWindowCaption) {
  for (bool rtl : {false, true}) {
    base::i18n::ScopedRTLForTesting direction(rtl);
    auto widget =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    auto* header = widget->SetContentsView(CreateSidebarHeaderActionsView(
        ShellCreateCallback(), views::Button::PressedCallback()));
    for (int width :
         {kSidebarMetrics.expanded_width, GetSidebarHeaderMinimumWidth(86)}) {
      for (int top_inset : {0, kSidebarMetrics.content_gutter}) {
        SCOPED_TRACE(testing::Message()
                     << rtl << ", " << width << ", " << top_inset);
        SetSidebarHeaderActionsLeadingExclusion(*header, 86, 0, top_inset);
        widget->SetBounds(gfx::Rect(
            0, 0, width, kSidebarMetrics.titlebar_height - top_inset));
        widget->LayoutRootViewIfNecessary();
        for (int id : {kSidebarHeaderCreateViewId, kSidebarHeaderAgentViewId}) {
          auto* button = header->GetViewByID(id);
          ASSERT_TRUE(button);
          ASSERT_TRUE(button->GetVisible());
          gfx::Point point = button->GetLocalBounds().CenterPoint();
          views::View::ConvertPointToTarget(button, header, &point);
          EXPECT_FALSE(
              IsSidebarHeaderActionsPositionInWindowCaption(*header, point));
        }
        EXPECT_TRUE(IsSidebarHeaderActionsPositionInWindowCaption(
            *header, gfx::Point(width / 2, 0)));
      }
    }
    auto* agent = header->GetViewByID(kSidebarHeaderAgentViewId);
    gfx::Point hidden_button_point = agent->GetLocalBounds().CenterPoint();
    views::View::ConvertPointToTarget(agent, header, &hidden_button_point);
    SetSidebarHeaderActionsControlsVisible(*header, false);
    EXPECT_TRUE(IsSidebarHeaderActionsPositionInWindowCaption(
        *header, hidden_button_point));
  }
}

TEST_F(YeeTabIconPresentationTest, SameFaviconReappliesChangedThemePolicy) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* icon = widget->SetContentsView(std::make_unique<TabIcon>());
  SkBitmap bitmap;
  bitmap.allocN32Pixels(gfx::kFaviconSize, gfx::kFaviconSize);
  bitmap.eraseColor(SK_ColorBLUE);
  tabs::TabData data;
  data.favicon =
      ui::ImageModel::FromImageSkia(gfx::ImageSkia::CreateFrom1xBitmap(bitmap));
  icon->SetData(data);
  EXPECT_TRUE(icon->GetThemedIconForTesting().isNull());

  data.should_themify_favicon = true;
  icon->SetData(data);
  EXPECT_FALSE(icon->GetThemedIconForTesting().isNull());

  data.should_themify_favicon = false;
  icon->SetData(data);
  EXPECT_TRUE(icon->GetThemedIconForTesting().isNull());
}

TEST_F(YeeNativePresentationTest, EmptyAddressHintTracksSurfaceAndNativeReset) {
  ClipboardRecentContent::SetInstance(
      std::make_unique<FakeClipboardRecentContent>());
  OmniboxController controller(std::make_unique<TestOmniboxClient>());
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto view = std::make_unique<EmptyOmniboxView>(&controller);
  view->Init();
  auto* omnibox = widget->SetContentsView(std::move(view));
  views::Textfield* painted_textfield = omnibox;
  omnibox->SetPlaceholderText(u"Google에 물어보거나 URL을 입력하세요.");
  const SkColor native_placeholder =
      painted_textfield->GetPlaceholderTextColor();
  const auto native_placeholder_id = omnibox->placeholder_text_color_id();
  const SkColor native_text = painted_textfield->GetTextColor();
  const SkColor native_selected_text =
      painted_textfield->GetSelectionTextColor();
  const SkColor native_selected_background =
      painted_textfield->GetSelectionBackgroundColor();

  for (SkColor surface :
       {SkColorSetRGB(0x32, 0x63, 0xB5), SkColorSetRGB(0x4B, 0x2B, 0x85),
        SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x7E, 0x7E, 0x7E)}) {
    const auto presentation =
        ResolveBrowserSurfacePresentation(surface, 1, 1, 1);
    omnibox->SetCompactShellTextColors(presentation.primary,
                                       presentation.secondary);
    EXPECT_GE(color_utils::GetContrastRatio(
                  painted_textfield->GetPlaceholderTextColor(), surface),
              color_utils::kMinimumReadableContrastRatio);
    // 검색 공급자 힌트나 전역 테마가 바뀌어도 실제 Header 바탕의 대비색을
    // 유지한다.
    omnibox->SetPlaceholderTextColorId(kColorOmniboxText);
    omnibox->OnThemeChanged();
    EXPECT_EQ(presentation.secondary,
              painted_textfield->GetPlaceholderTextColor());
    EXPECT_GE(color_utils::GetContrastRatio(
                  gfx::test::RenderTextTestApi(omnibox->GetRenderText())
                      .colors()
                      .breaks()
                      .front()
                      .second,
                  surface),
              color_utils::kMinimumReadableContrastRatio);
    // Shell에 연결되지 않은 텍스트 필드는 native 선택색을 유지한다.
    EXPECT_EQ(native_selected_text,
              omnibox->GetRenderText()->selection_color());
    EXPECT_EQ(native_selected_background,
              omnibox->GetRenderText()->selection_background_focused_color());
  }

  omnibox->SetCompactShellTextColors(std::nullopt, std::nullopt);
  omnibox->SetPlaceholderTextColorId(native_placeholder_id);
  EXPECT_EQ(native_placeholder, painted_textfield->GetPlaceholderTextColor());
  EXPECT_EQ(native_text, painted_textfield->GetTextColor());
  EXPECT_EQ(native_selected_text, painted_textfield->GetSelectionTextColor());
  EXPECT_EQ(native_selected_background,
            painted_textfield->GetSelectionBackgroundColor());
  EXPECT_EQ(native_selected_text, omnibox->GetRenderText()->selection_color());
  EXPECT_EQ(native_selected_background,
            omnibox->GetRenderText()->selection_background_focused_color());
}

TEST_F(YeeNativePresentationTest, SidePanelNativeAndWebContentShareSurface) {
  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0xEB, 0xEB, 0xF5)}) {
    ui::ColorProvider provider;
    auto& native = provider.AddMixer();
    native[ui::kColorSysBase] = {surface};
    native[ui::kColorSysOnSurface] = {SK_ColorRED};
    native[ui::kColorSysOnSurfaceSubtle] = {SK_ColorGRAY};
    native[ui::kColorSysStateDisabled] = {SK_ColorGREEN};
    AddSidePanelColorMixer(provider, ui::ColorProviderKey());
    provider.GenerateColorMapForTesting();
    for (auto id : {kColorSidePanelBackground, kColorSidePanelContentBackground,
                    kColorSidePanelCardBackground}) {
      EXPECT_EQ(surface, provider.GetColor(id));
    }
    EXPECT_EQ(SK_ColorRED,
              provider.GetColor(kColorSidePanelCardPrimaryForeground));
  }

  for (auto key : [] {
         ui::ColorProviderKey high_contrast;
         high_contrast.contrast_mode =
             ui::ColorProviderKey::ContrastMode::kHigh;
         ui::ColorProviderKey forced_colors;
         forced_colors.forced_colors =
             ui::ColorProviderKey::ForcedColors::kSystem;
         return std::array{high_contrast, forced_colors};
       }()) {
    ui::ColorProvider provider;
    auto& native = provider.AddMixer();
    native[kColorToolbar] = {SK_ColorWHITE};
    native[kColorSidePanelContentBackground] = {SK_ColorYELLOW};
    AddSidePanelColorMixer(provider, key);
    provider.GenerateColorMapForTesting();
    EXPECT_EQ(SK_ColorYELLOW,
              provider.GetColor(kColorSidePanelContentBackground));
  }
}

TEST_F(YeeNativePresentationTest, NativeActionAccentUsesActualHeaderSurface) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* header = widget->SetContentsView(std::make_unique<views::View>());
  auto* action = header->AddChildView(std::make_unique<views::View>());
  for (SkColor surface :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x32, 0x63, 0xB5)}) {
    SetBrowserSurfaceControlBackground(*header, surface);
    EXPECT_EQ(surface, views::GetCascadingBackgroundColor(action));
    EXPECT_GE(color_utils::GetContrastRatio(
                  views::GetCascadingAccentColor(action), surface),
              color_utils::kMinimumVisibleContrastRatio);
  }
  SetBrowserSurfaceControlBackground(*header, std::nullopt);
  EXPECT_FALSE(header->GetProperty(views::kCascadingBackgroundColor));
}

TEST_F(YeeNativePresentationTest,
       SidePanelThemeChangeInvalidatesNativeSurface) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* surface = widget->SetContentsView(CreateSidePanelSurfaceView());
  widget->SetBounds(gfx::Rect(0, 0, 320, 480));
  surface->SetVisible(true);
  widget->ShowInactive();
  ASSERT_TRUE(surface->layer());
  ASSERT_FALSE(surface->GetLocalBounds().IsEmpty());

  // WebUI와 별도로 그리는 native 제목·gutter도 열린 패널의 테마 변경 때 다시
  // 그린다.
  surface->layer()->SendDamagedRects();
  ASSERT_TRUE(surface->layer()->damaged_region().IsEmpty());
  widget->ThemeChanged();
  EXPECT_EQ(surface->GetLocalBounds(),
            surface->layer()->damaged_region().bounds());
}

TEST_F(YeeNativePresentationTest,
       SidePanelWebTextAndThemeControlsStayReadable) {
  for (auto mode : {ui::ColorProviderKey::ColorMode::kLight,
                    ui::ColorProviderKey::ColorMode::kDark}) {
    for (SkColor accent :
         {SK_ColorBLUE, SK_ColorGREEN, SkColorSetRGB(0x91, 0x36, 0xB4)}) {
      ui::ColorProviderKey key;
      key.color_mode = mode;
      key.user_color = accent;
      auto provider =
          ui::ColorProviderManager::Get().CreateUncachedColorProvider(key);
      constexpr std::array<ui::ColorId, 3> interactive_ids = {
          ui::kColorSysPrimary, kColorSidePanelFilterChipBackgroundHover,
          kColorSidePanelFilterChipBackgroundSelected};
      std::array<SkColor, interactive_ids.size()> original{};
      for (size_t i = 0; i < interactive_ids.size(); ++i) {
        original[i] = provider->GetColor(interactive_ids[i]);
      }
      auto& frame = provider->AddMixer();
      frame[kColorToolbar] = {SK_ColorRED};
      AddSidePanelColorMixer(*provider, key);
      provider->GenerateColorMapForTesting();
      const SkColor surface =
          provider->GetColor(kColorSidePanelContentBackground);
      EXPECT_EQ(provider->GetColor(ui::kColorSysBase), surface);
      for (auto text_id :
           {ui::kColorSysOnSurface, ui::kColorSysOnSurfaceSubtle}) {
        EXPECT_GE(
            color_utils::GetContrastRatio(provider->GetColor(text_id), surface),
            color_utils::kMinimumReadableContrastRatio);
      }
      for (size_t i = 0; i < interactive_ids.size(); ++i) {
        EXPECT_EQ(original[i], provider->GetColor(interactive_ids[i]));
      }
    }
  }

  base::test::ScopedCommandLine command_line;
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      "disable-yee-shell-scaffold");
  ui::ColorProvider provider;
  provider.AddMixer()[kColorSidePanelContentBackground] = {SK_ColorYELLOW};
  AddSidePanelColorMixer(provider, ui::ColorProviderKey());
  provider.GenerateColorMapForTesting();
  EXPECT_EQ(SK_ColorYELLOW,
            provider.GetColor(kColorSidePanelContentBackground));
}

TEST_F(YeeNativePresentationTest, NoticeLinkSharesMessageBaseline) {
  for (int buttons :
       std::array<int, 3>{ConfirmInfoBarDelegate::BUTTON_NONE,
                          ConfirmInfoBarDelegate::BUTTON_OK,
                          ConfirmInfoBarDelegate::BUTTON_OK |
                              ConfirmInfoBarDelegate::BUTTON_CANCEL}) {
    for (bool link_before : {false, true}) {
      auto infobar = ConfirmInfoBar::Create(
          std::make_unique<NoticeDelegate>(buttons, link_before));
      auto widget =
          CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
      widget->SetContentsView(infobar.get());
      widget->ShowInactive();
      auto* message = infobar->label_for_testing();
      auto* link = FindLink(*infobar);
      ASSERT_TRUE(message);
      ASSERT_TRUE(link);
      for (int width : {1200, 650, 420}) {
        SCOPED_TRACE(testing::Message()
                     << buttons << ", " << link_before << ", " << width);
        widget->SetBounds(gfx::Rect(0, 0, width, 50));
        widget->LayoutRootViewIfNecessary();
        EXPECT_NEAR(message->GetBoundsInScreen().y() + message->GetBaseline(),
                    link->GetBoundsInScreen().y() + link->GetBaseline(), 1);
        EXPECT_LE(link->GetBoundsInScreen().right(),
                  infobar->close_button()->GetBoundsInScreen().x());
      }
    }
  }
}

TEST_F(YeeNativePresentationTest, ProductNoticeHasTwoColorMarkInBothThemes) {
  std::set<SkColor> light_colors;
  std::set<SkColor> dark_colors;
  for (bool dark : {false, true}) {
    const auto image = branding::ProductLogoIcon(dark);
    ASSERT_TRUE(image.IsImage());
    const auto& bitmap =
        image.GetImage().AsImageSkia().GetRepresentation(1.f).GetBitmap();
    ASSERT_FALSE(bitmap.empty());
    EXPECT_EQ(0u, SkColorGetA(bitmap.getColor(0, 0)));
    auto& colors = dark ? dark_colors : light_colors;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        const SkColor color = bitmap.getColor(x, y);
        if (SkColorGetA(color) == SK_AlphaOPAQUE) {
          colors.insert(color);
        }
      }
    }
    EXPECT_GE(colors.size(), 2u);
  }
  EXPECT_NE(light_colors, dark_colors);
}

}  // namespace
}  // namespace yee
