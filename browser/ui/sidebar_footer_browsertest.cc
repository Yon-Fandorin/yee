// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/sidebar_footer.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/controls/hover_button.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/yee/webui/grit/webui_strings.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"

namespace yee {
namespace {

views::View *FooterFor(BrowserWindowInterface *browser) {
  return BrowserView::GetBrowserViewForBrowser(browser)
      ->vertical_tab_strip_region_view_for_testing()
      ->sidebar_footer_for_testing();
}

void ClickFooterControl(views::View *view) {
  views::test::ButtonTestApi(static_cast<views::Button *>(view))
      .NotifyDefaultMouseClick();
}

HoverButton *ContextNamed(views::View *menu, std::u16string_view name) {
  for (size_t index = 2; index < menu->children().size(); ++index) {
    auto *row = static_cast<HoverButton *>(menu->children()[index]);
    if (row->title()->GetText() == name) {
      return row;
    }
  }
  return nullptr;
}

base::ScopedClosureRunner MakeProfileOmittable(ProfileAttributesEntry &entry) {
  const bool was_ephemeral = entry.IsEphemeral();
  const bool was_omitted = entry.IsOmitted();
  entry.SetIsEphemeral(true);
  return base::ScopedClosureRunner(base::BindOnce(
      [](ProfileAttributesEntry *entry, bool was_ephemeral, bool was_omitted) {
        entry->SetIsOmitted(was_omitted);
        entry->SetIsEphemeral(was_ephemeral);
      },
      base::Unretained(&entry), was_ephemeral, was_omitted));
}

class SidebarFooterBrowserTest : public InProcessBrowserTest {
 public:
  SidebarFooterBrowserTest() = default;

 protected:
  void PrepareCurrentProfile() {
    browser()->GetProfile()->GetPrefs()->SetString(prefs::kProfileName,
                                                   "테스트용");
    tabs::VerticalTabStripStateController::From(browser())
        ->SetVerticalTabsEnabled(true);
    RunScheduledLayouts();
  }

  Profile &CreatePersonalProfile() {
    ProfileManager *const manager = g_browser_process->profile_manager();
    Profile &profile = profiles::testing::CreateProfileSync(
        manager, manager->user_data_dir().AppendASCII("Footer Personal"));
    profile.GetPrefs()->SetString(prefs::kProfileName, "개인");
    profile.GetPrefs()->SetBoolean(prefs::kProfileUsingDefaultName, false);
    profile.GetPrefs()->SetBoolean(prefs::kVerticalTabsEnabled, true);
    auto *const entry =
        manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
            profile.GetPath());
    // 숨김 검사는 Chromium의 ephemeral 프로필 조건을 지킨다.
    entry->SetIsEphemeral(true);
    entry->SetLocalProfileName(u"개인", false);
    return profile;
  }

  views::View *OpenContexts(BrowserWindowInterface *owner) {
    views::View *const footer = FooterFor(owner);
    owner->GetWindow()->Activate();
    ClickFooterControl(footer);
    views::View *const menu = GetSidebarFooterMenuForTesting(*footer);
    if (menu) {
      ClickFooterControl(menu->GetViewByID(kSidebarFooterRootContextViewId));
      if (!base::test::RunUntil([&] {
            views::View *const current =
                GetSidebarFooterMenuForTesting(*footer);
            return !current ||
                   !current->GetViewByID(kSidebarFooterRootContextViewId);
          })) {
        return nullptr;
      }
      RunScheduledLayouts();
    }
    return GetSidebarFooterMenuForTesting(*footer);
  }
};

// 같은 프로필의 기존·새 창과 종료 뒤 설정 변경을 실제 native Footer에서
// 확인한다.
IN_PROC_BROWSER_TEST_F(SidebarFooterBrowserTest,
                       ProfileNameChangesReachEveryWindow) {
  ASSERT_TRUE(IsShellEnabled());
  auto *const state = tabs::VerticalTabStripStateController::From(browser());
  ASSERT_TRUE(state);
  state->SetVerticalTabsEnabled(true);
  RunScheduledLayouts();
  PrefService *const profile_prefs = browser()->GetProfile()->GetPrefs();
  views::View *const first = FooterFor(browser());
  ASSERT_TRUE(first);
  profile_prefs->SetString(prefs::kProfileName, "테스트용");
  EXPECT_EQ(static_cast<HoverButton *>(first)->title()->GetText(), u"테스트용");
  BrowserWindowInterface *const second_browser =
      CreateBrowser(browser()->GetProfile());
  views::View *const second = FooterFor(second_browser);
  ASSERT_TRUE(second);
  EXPECT_EQ(static_cast<HoverButton *>(second)->title()->GetText(),
            u"테스트용");
  profile_prefs->SetString(prefs::kProfileName, "개인 & 이름 변경");
  for (views::View *footer : {first, second}) {
    EXPECT_EQ(static_cast<HoverButton *>(footer)->title()->GetText(),
              u"개인 & 이름 변경");
    EXPECT_NE(footer->GetViewAccessibility().GetCachedName().find(
                  u"개인 & 이름 변경"),
              std::u16string::npos);
    // 계정이 없으면 로그인 상태 문자열을 계정 이름으로 취급하지 않는다.
    EXPECT_NE(
        footer->GetViewAccessibility().GetCachedName().find(
            l10n_util::GetStringUTF16(IDS_YEE_WEBUI_FOOTER_LOCAL_PROFILE)),
        std::u16string::npos);
  }
  CloseBrowserSynchronously(second_browser);
  profile_prefs->SetString(prefs::kProfileName, "테스트용");
  EXPECT_EQ(static_cast<HoverButton *>(first)->title()->GetText(), u"테스트용");
}

// 다른 프로필의 생성·이름·숨김·삭제가 열린 창의 실제 목록에 반영된다.
IN_PROC_BROWSER_TEST_F(SidebarFooterBrowserTest,
                       ProfileListTracksNativeChanges) {
  PrepareCurrentProfile();
  views::View *const footer = FooterFor(browser());
  ASSERT_TRUE(footer);
  views::View *menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 3u);
  EXPECT_TRUE(ContextNamed(menu, u"테스트용"));
  ClickFooterControl(footer);
  base::RunLoop().RunUntilIdle();

  Profile &personal = CreatePersonalProfile();
  const base::FilePath personal_path = personal.GetPath();
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 4u);
  EXPECT_TRUE(ContextNamed(menu, u"개인"));
  auto &storage =
      g_browser_process->profile_manager()->GetProfileAttributesStorage();
  auto *const entry = storage.GetProfileAttributesWithPath(personal_path);
  ASSERT_TRUE(entry);
  entry->SetLocalProfileName(u"개인 이름 변경", false);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer));
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_TRUE(ContextNamed(menu, u"개인 이름 변경"));
  EXPECT_FALSE(ContextNamed(menu, u"개인"));
  EXPECT_EQ(static_cast<HoverButton *>(footer)->title()->GetText(),
            u"테스트용");

  entry->SetIsOmitted(true);
  base::RunLoop().RunUntilIdle();
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 3u);
  EXPECT_FALSE(ContextNamed(menu, u"개인 이름 변경"));
  entry->SetIsOmitted(false);
  base::RunLoop().RunUntilIdle();
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 4u);
  auto *const current_entry =
      storage.GetProfileAttributesWithPath(browser()->GetProfile()->GetPath());
  ASSERT_TRUE(current_entry);
  auto restore_current_profile = MakeProfileOmittable(*current_entry);
  current_entry->SetIsOmitted(true);
  base::RunLoop().RunUntilIdle();
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 4u);
  EXPECT_TRUE(ContextNamed(menu, u"개인 이름 변경"));
  auto *const current_row = ContextNamed(menu, u"테스트용");
  ASSERT_TRUE(current_row);
  ui::AXNodeData selected;
  current_row->GetViewAccessibility().GetAccessibleNodeData(&selected);
  EXPECT_TRUE(selected.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  current_entry->SetIsOmitted(false);
  base::RunLoop().RunUntilIdle();
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  g_browser_process->profile_manager()
      ->GetDeleteProfileHelper()
      .MaybeScheduleProfileForDeletion(
          personal_path, base::DoNothing(),
          ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !storage.GetProfileAttributesWithPath(personal_path); }));
  EXPECT_FALSE(GetSidebarFooterMenuForTesting(*footer));
  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  EXPECT_EQ(menu->children().size(), 3u);
  EXPECT_TRUE(ContextNamed(menu, u"테스트용"));
  ClickFooterControl(footer);
  base::RunLoop().RunUntilIdle();
}

// 프로필 선택은 대상 창을 만들거나 재사용하고 원래 창의 탭과 정체성을 보존한다.
IN_PROC_BROWSER_TEST_F(SidebarFooterBrowserTest,
                       ProfileSelectionOpensAndReusesWindow) {
  PrepareCurrentProfile();
  Profile &personal = CreatePersonalProfile();
  // 현재 프로필이 native 목록 밖에 있어도 다른 실제 프로필은 열 수 있다.
  auto *const current_entry =
      g_browser_process->profile_manager()
          ->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(browser()->GetProfile()->GetPath());
  ASSERT_TRUE(current_entry);
  auto restore_current_profile = MakeProfileOmittable(*current_entry);
  current_entry->SetIsOmitted(true);
  const int initial_tabs = browser()->tab_strip_model()->count();
  const size_t initial_windows =
      GlobalBrowserCollection::GetInstance()->GetSize();
  views::View *menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  HoverButton *row = ContextNamed(menu, u"개인");
  ASSERT_TRUE(row);
  ui_test_utils::BrowserCreatedObserver new_window;
  ClickFooterControl(row);
  auto *const personal_window = new_window.Wait();
  ASSERT_TRUE(personal_window);
  BrowserWindowInterface *const target = personal_window;
  ASSERT_TRUE(target);
  RunScheduledLayouts();
  EXPECT_EQ(target->GetProfile(), &personal);
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(),
            initial_windows + 1);
  EXPECT_EQ(browser()->tab_strip_model()->count(), initial_tabs);
  EXPECT_EQ(
      static_cast<HoverButton *>(FooterFor(browser()))->title()->GetText(),
      u"테스트용");
  EXPECT_EQ(static_cast<HoverButton *>(FooterFor(target))->title()->GetText(),
            u"개인");

  menu = OpenContexts(browser());
  ASSERT_TRUE(menu);
  row = ContextNamed(menu, u"개인");
  ASSERT_TRUE(row);
  ClickFooterControl(row);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(GlobalBrowserCollection::GetInstance()->GetSize(),
            initial_windows + 1);
  EXPECT_EQ(browser()->tab_strip_model()->count(), initial_tabs);
  EXPECT_EQ(
      static_cast<HoverButton *>(FooterFor(browser()))->title()->GetText(),
      u"테스트용");
  EXPECT_EQ(static_cast<HoverButton *>(FooterFor(target))->title()->GetText(),
            u"개인");
  CloseBrowserSynchronously(target);
  browser()->GetProfile()->GetPrefs()->SetString(prefs::kProfileName,
                                                 "테스트용 이름 변경");
  EXPECT_EQ(
      static_cast<HoverButton *>(FooterFor(browser()))->title()->GetText(),
      u"테스트용 이름 변경");
}

} // namespace
} // namespace yee
