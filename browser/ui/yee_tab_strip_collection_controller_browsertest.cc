// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/tabs/common/tab_strip_collection_controller.h"

#include "build/build_config.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_group_data.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model.h"
#include "chrome/browser/ui/tabs/tab_menu_model_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/root_tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_node.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_header_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/hovercard/fade_label_view.h"
#include "chrome/browser/ui/views/tabs/hovercard/tab_hover_card_bubble_view.h"
#include "chrome/browser/ui/views/tabs/hovercard/tab_hover_card_controller.h"
#include "chrome/browser/ui/views/tabs/shared/tab_strip_types.h"
#include "chrome/browser/ui/views/tabs/tab/tab_context_menu_controller.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/ui/window_metadata/window_metadata_controller.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tab_groups/tab_group_id.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view_utils.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ui/aura/window.h"
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC)
#include "base/feature_list.h"
#include "chrome/browser/ui/views/frame/toolbar_button_provider.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/app_menu_control.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "components/tabs/public/tab_collection_types.h"
#include "content/public/common/content_features.h"
#include "ui/events/test/event_generator.h"
#endif

namespace {

class YeeTabStripCollectionControllerBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest>,
      public testing::WithParamInterface<TabStripOrientation> {
 public:
  TabStripOrientation orientation() const { return GetParam(); }
  bool is_horizontal() const {
    return orientation() == TabStripOrientation::kHorizontal;
  }

  void SetUpOnMainThread() override {
    VerticalTabsBrowserTestMixin<InProcessBrowserTest>::SetUpOnMainThread();
    if (is_horizontal()) {
      ExitVerticalTabsMode();
    }
  }

  const std::vector<base::test::FeatureRefAndParams> GetEnabledFeatures()
      override {
    auto enabled = VerticalTabsBrowserTestMixin<
        InProcessBrowserTest>::GetEnabledFeatures();
    enabled.push_back({tabs::kTabStripUnification, {}});
    return enabled;
  }
};

IN_PROC_BROWSER_TEST_P(YeeTabStripCollectionControllerBrowserTest,
                       YeeGroupRenameRefreshesInactiveTabAccessibleNames) {
  AppendTab();
  AppendTab();
  TabStripModel* model = browser()->GetTabStripModel();
  model->ActivateTabAt(0);
  const tab_groups::TabGroupId group = model->AddToNewGroup({1, 2});

  auto expect_names = [&](const std::u16string& group_title) {
    for (int index : {1, 2}) {
      const tabs::TabInterface* tab = model->GetTabAtIndex(index);
      TabCollectionNode* node = root_node()->GetNodeForHandle(tab->GetHandle());
      ASSERT_TRUE(node);
      auto* view = views::AsViewClass<TabView>(node->view());
      ASSERT_TRUE(view);
      ui::AXNodeData data;
      view->GetViewAccessibility().GetAccessibleNodeData(&data);
      const std::u16string title =
          WindowMetadataController::From(browser())->GetTitleForTab(
              tab->GetHandle());
      const std::u16string expected =
          group_title.empty()
              ? l10n_util::GetStringFUTF16(
                    IDS_TAB_AX_LABEL_UNNAMED_GROUP_FORMAT, title)
              : l10n_util::GetStringFUTF16(IDS_TAB_AX_LABEL_NAMED_GROUP_FORMAT,
                                           title, group_title);
      EXPECT_EQ(expected,
                data.GetString16Attribute(ax::mojom::StringAttribute::kName));
      EXPECT_EQ(group, tab->GetGroup());
    }
    EXPECT_EQ(0, model->active_index());
    EXPECT_EQ(3, model->count());
  };

  expect_names(u"");
  model->ChangeTabGroupVisuals(
      group,
      tab_groups::TabGroupVisualData(u"복원 그룹",
                                     tab_groups::TabGroupColorId::kBlue),
      false);
  // 초점·탐색·강제 배치 없이 이름 변경 알림만으로 캐시가 갱신되어야 한다.
  expect_names(u"복원 그룹");

  model->ChangeTabGroupVisuals(
      group,
      tab_groups::TabGroupVisualData(u"검수 그룹",
                                     tab_groups::TabGroupColorId::kBlue, true),
      false);
  expect_names(u"검수 그룹");
  model->ChangeTabGroupVisuals(
      group,
      tab_groups::TabGroupVisualData(u"", tab_groups::TabGroupColorId::kBlue,
                                     true),
      false);
  expect_names(u"");
}

INSTANTIATE_TEST_SUITE_P(
    All,
    YeeTabStripCollectionControllerBrowserTest,
    testing::Values(TabStripOrientation::kVertical,
                    TabStripOrientation::kHorizontal),
    [](const testing::TestParamInfo<TabStripOrientation>& info) {
      switch (info.param) {
        case TabStripOrientation::kVertical:
          return "Vertical";
        case TabStripOrientation::kHorizontal:
          return "Horizontal";
      }
    });

}  // namespace
