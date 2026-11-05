// Copyright 2026 The Chromium Authors
// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"

#include <string>
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/i18n/message_formatter.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_command_line.h"
#include "build/build_config.h"
#include "build/buildflag.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/tabs/tab_group_theme.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_style.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/grit/generated_resources.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tabs/public/mock_tab_group.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/text_elider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"

#include <memory>
#include <vector>

#include "chrome/browser/ui/tabs/tab_group_data.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/ui/views/yee/vertical_group_color_mark.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/events/test/event_generator.h"
#include "ui/gfx/canvas.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

class FocusModeDelegate : public TabGroupHeaderView::Delegate {
 public:
  FocusModeDelegate() {
    ON_CALL(*this, GetTabClosingHelper).WillByDefault(testing::Return(nullptr));
  }
  MOCK_METHOD(void,
              ToggleCollapsedState,
              (ToggleTabGroupCollapsedStateOrigin),
              (override));
  MOCK_METHOD(std::unique_ptr<views::Widget>,
              ShowGroupEditorBubble,
              (bool),
              (override));
  MOCK_METHOD(bool, IsValid, (), (const, override));
  MOCK_METHOD(void, InitHeaderDrag, (const ui::LocatedEvent&), (override));
  MOCK_METHOD(bool, ContinueHeaderDrag, (const ui::LocatedEvent&), (override));
  MOCK_METHOD(void, CancelHeaderDrag, (), (override));
  MOCK_METHOD(const TabGroup&, GetTabGroup, (), (const, override));
  MOCK_METHOD(const tabs::TabGroupData&,
              GetTabGroupData,
              (),
              (const, override));
  MOCK_METHOD(void, UpdateHoverCard, (int), (const, override));
  MOCK_METHOD(void, HideHoverCard, (int), (const, override));
  MOCK_METHOD(bool, IsFocusInTabStrip, (), (override));
  MOCK_METHOD(std::unique_ptr<ExpandOnHoverLock>,
              AcquireExpandOnHoverLock,
              (),
              (override));
  MOCK_METHOD(void, ShiftGroupUp, (), (override));
  MOCK_METHOD(void, ShiftGroupDown, (), (override));
  MOCK_METHOD(bool, IsGroupFocused, (), (const, override));
  MOCK_METHOD(HorizontalTabClosingHelper*,
              GetTabClosingHelper,
              (),
              (const, override));
};

class YeeTabGroupHeaderViewTest : public views::ViewsTestBase {
 public:
  void MoveMouseTo(ui::test::EventGenerator& generator,
                   const views::View* view,
                   bool inside_view) {
    if (inside_view) {
      generator.MoveMouseTo(view->GetBoundsInScreen().CenterPoint());
    } else {
      generator.MoveMouseTo(view->GetBoundsInScreen().bottom_right() +
                            gfx::Vector2d(10, 10));
    }
  }
};

TEST_F(YeeTabGroupHeaderViewTest, FocusModeMarkAndExpandedButtons) {
  testing::NiceMock<FocusModeDelegate> delegate;
  ON_CALL(delegate, IsGroupFocused()).WillByDefault(testing::Return(true));
  tab_groups::TabGroupVisualData visual_data(
      u"Group Title", tab_groups::TabGroupColorId::kBlue, false);
  tabs::TabGroupData data;
  data.visual_data = visual_data;
  ON_CALL(delegate, GetTabGroupData()).WillByDefault(testing::ReturnRef(data));

  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
      delegate, TabStripOrientation::kVertical, nullptr, &visual_data));
  header->OnDataChanged(data);
  widget->SetBounds(gfx::Rect(0, 0, kSidebarMetrics.expanded_width,
                              kSidebarMetrics.tab_row_height));
  widget->Show();
  header->DeprecatedLayoutImmediately();

  VerticalGroupColorMark* mark = nullptr;
  for (const auto& child : header->children()) {
    if (auto* item = views::AsViewClass<VerticalGroupColorMark>(child)) {
      mark = item;
    }
  }
  ASSERT_TRUE(mark);
  ASSERT_FALSE(mark->size().IsEmpty());
  const auto raster = [&]() {
    gfx::Canvas canvas(mark->size(), 1, false);
    mark->OnPaint(&canvas);
    const auto bitmap = canvas.GetBitmap();
    std::vector<SkColor> pixels;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        pixels.push_back(bitmap.getColor(x, y));
      }
    }
    return pixels;
  };
  const auto check_buttons = [&]() {
    EXPECT_TRUE(header->editor_bubble_button()->GetVisible());
    EXPECT_GT(header->editor_bubble_button()->width(), 0);
    EXPECT_FALSE(header->collapse_icon_for_testing()->GetVisible());
  };
  ui::test::EventGenerator generator(GetContext(), widget->GetNativeWindow());
  const auto move_outside = [&]() {
    generator.MoveMouseTo(header->GetBoundsInScreen().bottom_right() +
                          gfx::Vector2d(10, 10));
  };
  move_outside();
  const auto resting = raster();
  check_buttons();

  // Yee는 compact rail을 숨긴다. 확장 헤더에서 focus mode의 mark와 버튼을
  // 검사한다.
  generator.MoveMouseTo(header->GetBoundsInScreen().CenterPoint());
  EXPECT_EQ(resting, raster());
  check_buttons();
  move_outside();
  EXPECT_EQ(resting, raster());
  check_buttons();
  widget->GetFocusManager()->SetKeyboardAccessible(true);
  header->RequestFocus();
  ASSERT_EQ(header, widget->GetFocusManager()->GetFocusedView());
  EXPECT_EQ(resting, raster());
  check_buttons();
}

TEST_F(YeeTabGroupHeaderViewTest, EditorBubbleButtonVisibilityOnHover) {
  FocusModeDelegate delegate;
  tab_groups::TabGroupVisualData visual_data(
      u"Group Title", tab_groups::TabGroupColorId::kBlue, false);

  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
      delegate, TabStripOrientation::kVertical, nullptr, &visual_data));
  // 포인터 상태만 검사하며 자동 키보드 초점이 보조 버튼을 켜지 않게 한다.
  widget->ShowInactive();

  ui::test::EventGenerator generator(GetContext(), widget->GetNativeWindow());

  auto check_editor_bubble_button_visible = [&](bool expected_visibility) {
    EXPECT_EQ(expected_visibility,
              header->editor_bubble_button()->GetVisible());
  };

  // Move mouse outside the header.
  MoveMouseTo(generator, header, false);
  check_editor_bubble_button_visible(false);

  // Move mouse over the header.
  MoveMouseTo(generator, header, true);
  check_editor_bubble_button_visible(true);

  // Move mouse outside the header again.
  MoveMouseTo(generator, header, false);
  check_editor_bubble_button_visible(false);
}

TEST_F(YeeTabGroupHeaderViewTest,
       YeeDisabledShellKeepsNativeVerticalHeaderOnHover) {
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->AppendSwitch(
      "disable-yee-shell-scaffold");
  testing::NiceMock<FocusModeDelegate> delegate;
  tab_groups::TabGroupVisualData visual_data(
      u"기본 그룹", tab_groups::TabGroupColorId::kBlue, false);
  tabs::TabGroupData data;
  data.visual_data = visual_data;
  ON_CALL(delegate, GetTabGroupData()).WillByDefault(testing::ReturnRef(data));
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
      delegate, TabStripOrientation::kVertical, nullptr, &visual_data));
  header->OnDataChanged(data);
  const ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(),
                               gfx::Point(), ui::EventTimeForNow(), 0, 0);
  header->OnMouseEntered(entered);
  const auto* provider = header->GetColorProvider();
  const SkColor native_background =
      provider->GetColor(GetTabGroupTabStripColorId(
          visual_data.color(), widget->ShouldPaintAsActive()));
  ASSERT_TRUE(header->background());
  EXPECT_EQ(native_background,
            header->background()->color().ResolveToSkColor(provider));
  EXPECT_EQ(color_utils::GetColorWithMaxContrast(native_background),
            header->title_label_for_testing()->GetEnabledColor());
  EXPECT_TRUE(header->collapse_icon_for_testing()->GetVisible());
  for (const views::View* child : header->children()) {
    EXPECT_FALSE(views::IsViewClass<yee::VerticalGroupColorMark>(child));
  }
  EXPECT_FALSE(widget->IsActive());
}

TEST_F(YeeTabGroupHeaderViewTest,
       YeeEphemeralGroupKeepsNativeColorsDuringHoverAndPress) {
  for (const auto orientation :
       {TabStripOrientation::kHorizontal, TabStripOrientation::kVertical}) {
    SCOPED_TRACE(static_cast<int>(orientation));
    testing::NiceMock<FocusModeDelegate> delegate;
    tab_groups::TabGroupVisualData visual_data(
        u"임시 그룹", tab_groups::TabGroupColorId::kBlue, false);
    tabs::MockTabGroup group(nullptr, tab_groups::TabGroupId::GenerateNew(),
                             visual_data);
    group.SetIsEphemeral(true);
    group.AddTab();
    tabs::TabGroupData data;
    data.visual_data = visual_data;
    ON_CALL(delegate, IsValid()).WillByDefault(testing::Return(true));
    ON_CALL(delegate, IsGroupFocused()).WillByDefault(testing::Return(true));
    ON_CALL(delegate, GetTabGroup()).WillByDefault(testing::ReturnRef(group));
    ON_CALL(delegate, GetTabGroupData())
        .WillByDefault(testing::ReturnRef(data));

    auto widget =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
        delegate, orientation, nullptr, &visual_data));
    header->OnDataChanged(data);
    const auto* provider = header->GetColorProvider();
    const bool active = widget->ShouldPaintAsActive();
    const SkColor native_background =
        provider->GetColor(active ? ui::kColorSysOnHeaderDivider
                                  : ui::kColorSysOnHeaderDividerInactive);
    const SkColor native_foreground =
        provider->GetColor(active ? ui::kColorSysOnSurfacePrimary
                                  : ui::kColorSysOnSurfacePrimaryInactive);
    const auto expect_native_colors = [&](SkColor background) {
      ASSERT_TRUE(header->background());
      EXPECT_EQ(background,
                header->background()->color().ResolveToSkColor(provider));
      EXPECT_EQ(native_foreground,
                header->title_label_for_testing()->GetEnabledColor());
    };
    expect_native_colors(native_background);
    const ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(),
                                 gfx::Point(), ui::EventTimeForNow(), 0, 0);
    header->OnMouseEntered(entered);
    expect_native_colors(native_background);

    views::Widget* editor = nullptr;
    EXPECT_CALL(delegate, ShowGroupEditorBubble(false)).WillOnce([&](bool) {
      auto bubble =
          CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
      editor = bubble.get();
      return bubble;
    });
    const ui::MouseEvent released(ui::EventType::kMouseReleased, gfx::Point(),
                                  gfx::Point(), ui::EventTimeForNow(),
                                  ui::EF_RIGHT_MOUSE_BUTTON,
                                  ui::EF_RIGHT_MOUSE_BUTTON);
    header->OnMouseReleased(released);
    ASSERT_TRUE(editor);
    const SkColor pressed_background = color_utils::GetResultingPaintColor(
        provider->GetColor(ui::kColorSysStateHoverOnSubtle), native_background);
    expect_native_colors(pressed_background);
    // editor가 열린 동안 hover 갱신도 native 눌림 표면을 보존한다.
    header->OnMouseEntered(entered);
    expect_native_colors(pressed_background);
    editor->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
    editor = nullptr;
    expect_native_colors(native_background);
    RunPendingMessages();
    EXPECT_FALSE(widget->IsActive());
  }
}

TEST_F(YeeTabGroupHeaderViewTest,
       YeeHorizontalNativeThemeKeepsNativeChipOnHover) {
  for (const bool shell_enabled : {true, false}) {
    SCOPED_TRACE(shell_enabled);
    base::test::ScopedCommandLine command_line;
    if (shell_enabled) {
      command_line.GetProcessCommandLine()->RemoveSwitch(
          "disable-yee-shell-scaffold");
    } else {
      command_line.GetProcessCommandLine()->AppendSwitch(
          "disable-yee-shell-scaffold");
    }
    testing::NiceMock<FocusModeDelegate> delegate;
    tab_groups::TabGroupVisualData visual_data(
        u"고대비 그룹", tab_groups::TabGroupColorId::kBlue, false);
    tabs::TabGroupData data;
    data.visual_data = visual_data;
    ON_CALL(delegate, GetTabGroupData())
        .WillByDefault(testing::ReturnRef(data));
    auto widget =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    auto* theme = widget->GetNativeTheme();
    const auto previous_contrast = theme->preferred_contrast();
    base::ScopedClosureRunner restore_contrast(base::BindOnce(
        [](ui::NativeTheme* theme,
           ui::NativeTheme::PreferredContrast contrast) {
          theme->set_preferred_contrast(contrast);
          theme->NotifyOnNativeThemeUpdated();
        },
        base::Unretained(theme), previous_contrast));
    theme->set_preferred_contrast(ui::NativeTheme::PreferredContrast::kMore);
    theme->NotifyOnNativeThemeUpdated();
    auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
        delegate, TabStripOrientation::kHorizontal, nullptr, &visual_data));
    header->OnDataChanged(data);
    const int expected_inset =
        shell_enabled ? yee::kSidebarMetrics.group_header_horizontal_inset
                      : TabGroupStyle::GetInsetsForHeaderChip(
                            TabStripOrientation::kHorizontal)
                            .left();
    const auto native_palette_margin = gfx::Insets::VH(0, expected_inset);
    EXPECT_EQ(native_palette_margin, header->GetInteriorMargin());
    const auto* provider = header->GetColorProvider();
    const SkColor native_background =
        provider->GetColor(GetTabGroupTabStripColorId(
            visual_data.color(), widget->ShouldPaintAsActive()));
    const ui::MouseEvent entered(ui::EventType::kMouseEntered, gfx::Point(),
                                 gfx::Point(), ui::EventTimeForNow(), 0, 0);
    header->OnMouseEntered(entered);
    ASSERT_TRUE(header->background());
    EXPECT_EQ(native_background,
              header->background()->color().ResolveToSkColor(provider));
    EXPECT_EQ(color_utils::GetColorWithMaxContrast(native_background),
              header->title_label_for_testing()->GetEnabledColor());
    EXPECT_EQ(native_palette_margin, header->GetInteriorMargin());
    EXPECT_FALSE(widget->IsActive());
  }
}

TEST_F(YeeTabGroupHeaderViewTest,
       YeeVerticalEditorKeepsSidebarPresentationWhenOpenedAndClosed) {
  base::test::ScopedCommandLine command_line;
  command_line.GetProcessCommandLine()->RemoveSwitch(
      "disable-yee-shell-scaffold");
  testing::NiceMock<FocusModeDelegate> delegate;
  tab_groups::TabGroupVisualData visual_data(
      u"일반 그룹", tab_groups::TabGroupColorId::kBlue, false);
  tabs::MockTabGroup group(nullptr, tab_groups::TabGroupId::GenerateNew(),
                           visual_data);
  tabs::TabGroupData data;
  data.visual_data = visual_data;
  ON_CALL(delegate, IsValid()).WillByDefault(testing::Return(true));
  ON_CALL(delegate, GetTabGroup()).WillByDefault(testing::ReturnRef(group));
  ON_CALL(delegate, GetTabGroupData()).WillByDefault(testing::ReturnRef(data));
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  ui::test::EventGenerator generator(GetContext(), widget->GetNativeWindow());
  // 부착 시 현재 화면 포인터로 hover를 읽으므로 먼저 창 밖으로 옮긴다.
  generator.MoveMouseTo(widget->GetWindowBoundsInScreen().bottom_right() +
                        gfx::Vector2d(10, 10));
  auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
      delegate, TabStripOrientation::kVertical, nullptr, &visual_data));
  header->OnDataChanged(data);
  widget->ShowInactive();
  EXPECT_FALSE(widget->IsActive());
  MoveMouseTo(generator, header, false);
  ASSERT_FALSE(header->IsMouseHovered());
  const auto resting = yee::ResolveSidebarItemColors(
      *header->GetColorProvider(), yee::SidebarItemVisualState::kResting, 0.0,
      widget->ShouldPaintAsActive(), /*persistent_surface=*/false);
  const auto expect_sidebar_presentation = [&](const char* phase) {
    SCOPED_TRACE(phase);
    EXPECT_FALSE(header->IsMouseHovered());
    EXPECT_FALSE(header->background())
        << "배경색: "
        << (header->background()
                ? color_utils::SkColorToRgbaString(
                      header->background()->color().ResolveToSkColor(
                          header->GetColorProvider()))
                : "없음");
    EXPECT_EQ(resting.foreground,
              header->title_label_for_testing()->GetEnabledColor());
    EXPECT_FALSE(header->collapse_icon_for_testing()->GetVisible());
  };
  expect_sidebar_presentation("편집기 열기 전");
  views::Widget* editor = nullptr;
  EXPECT_CALL(delegate, ShowGroupEditorBubble(false)).WillOnce([&](bool) {
    auto bubble =
        CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
    editor = bubble.get();
    return bubble;
  });
  const ui::MouseEvent released(ui::EventType::kMouseReleased, gfx::Point(),
                                gfx::Point(), ui::EventTimeForNow(),
                                ui::EF_RIGHT_MOUSE_BUTTON,
                                ui::EF_RIGHT_MOUSE_BUTTON);
  header->OnMouseReleased(released);
  ASSERT_TRUE(editor);
  // hover를 다시 보내기 전에 editor 콜백 자체가 Sidebar 표시를 유지해야 한다.
  expect_sidebar_presentation("편집기 열기 직후");
  EXPECT_TRUE(header->editor_bubble_button()->GetVisible());
  editor->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
  editor = nullptr;
  expect_sidebar_presentation("편집기 닫기 직후");
  EXPECT_FALSE(header->editor_bubble_button()->GetVisible());
  RunPendingMessages();
  expect_sidebar_presentation("닫기 알림 처리 후");
  EXPECT_FALSE(widget->IsActive());
}

TEST_F(YeeTabGroupHeaderViewTest,
       YeeVerticalHoverUpdatesSurfaceAndForegroundTogether) {
  testing::NiceMock<FocusModeDelegate> delegate;
  tab_groups::TabGroupVisualData visual_data(
      u"Group Title", tab_groups::TabGroupColorId::kBlue, false);

  std::unique_ptr<views::Widget> widget =
      CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  auto active = widget->LockPaintAsActive();
  auto* header = widget->SetContentsView(std::make_unique<TabGroupHeaderView>(
      delegate, TabStripOrientation::kVertical, nullptr, &visual_data));
  widget->ShowInactive();
  EXPECT_FALSE(widget->IsActive());

  ui::test::EventGenerator generator(GetContext(), widget->GetNativeWindow());
  const auto expected_colors = [&](yee::SidebarItemVisualState state) {
    return yee::ResolveSidebarItemColors(
        *header->GetColorProvider(), state,
        state == yee::SidebarItemVisualState::kHovered ? 1.0 : 0.0,
        widget->ShouldPaintAsActive(),
        /*persistent_surface=*/false);
  };
  const auto resting = expected_colors(yee::SidebarItemVisualState::kResting);
  const auto hovered = expected_colors(yee::SidebarItemVisualState::kHovered);
  ASSERT_NE(resting.foreground, hovered.foreground);

  MoveMouseTo(generator, header, false);
  EXPECT_FALSE(header->background());
  EXPECT_EQ(resting.foreground,
            header->title_label_for_testing()->GetEnabledColor());
  MoveMouseTo(generator, header, true);
  ASSERT_TRUE(header->background());
  EXPECT_EQ(hovered.fill, header->background()->color().ResolveToSkColor(
                              header->GetColorProvider()));
  EXPECT_EQ(hovered.foreground,
            header->title_label_for_testing()->GetEnabledColor());
  MoveMouseTo(generator, header, false);
  EXPECT_FALSE(header->background());
  EXPECT_EQ(resting.foreground,
            header->title_label_for_testing()->GetEnabledColor());
  EXPECT_FALSE(widget->IsActive());
}

}  // namespace
}  // namespace yee
