// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"

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

class YeeTabGroupHeaderViewTest : public views::ViewsTestBase {};

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

  // Yee는 compact rail을 숨긴다. 확장 헤더에서 focus mode의 mark와 버튼을 검사한다.
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

}  // namespace
}  // namespace yee
