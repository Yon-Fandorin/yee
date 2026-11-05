// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/tab_data.h"

#include <memory>
#include <string>
#include <vector>

#include "base/byte_size.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_performance_tuning_manager.h"
#include "chrome/browser/ui/performance_controls/tab_resource_usage_tab_helper.h"
#include "chrome/browser/ui/tab_ui_helper.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/saved_tab_groups/collaboration_messaging_tab_data.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/thumbnails/thumbnail_image.h"
#include "chrome/browser/ui/thumbnails/thumbnail_tab_helper.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/tab.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/data_sharing/public/features.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/mediastream/media_stream_request.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "url/url_constants.h"

namespace tabs {

class YeeTabDataObserverBrowserTest : public InProcessBrowserTest {
 public:
  YeeTabDataObserverBrowserTest() {
    scoped_feature_list_.InitAndEnableFeature(
        data_sharing::features::kDataSharingFeature);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(YeeTabDataObserverBrowserTest, FaviconAndIconFlags) {
  TabStripModel* tab_strip_model = browser()->tab_strip_model();
  {  // Initial favicon data matches default
    TabInterface* const tab_interface = tab_strip_model->GetTabAtIndex(0);
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::CURRENT_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    auto tab_data_observer = std::make_unique<TabDataObserver>(tab_interface);
    const TabData& data = tab_data_observer->tab_data();
    ASSERT_FALSE(data.favicon.IsEmpty());
    EXPECT_EQ(data.favicon, TabUIHelper::From(tab_interface)->GetFavicon());
    EXPECT_TRUE(data.should_themify_favicon);
    EXPECT_FALSE(data.is_monochrome_favicon);
    EXPECT_TRUE(data.should_display_favicon);

    // Native favicon driver가 유효하다고 판정한 사이트 아이콘은 원래 색을
    // 유지한다.
    auto* entry =
        tab_interface->GetContents()->GetController().GetLastCommittedEntry();
    ASSERT_NE(nullptr, entry);
    entry->SetURL(GURL("https://example.com/"));
    entry->SetVirtualURL(entry->GetURL());
    SkBitmap bitmap;
    bitmap.allocN32Pixels(16, 16);
    bitmap.eraseColor(SK_ColorBLUE);
    *bitmap.getAddr32(0, 0) = SK_ColorRED;
    const gfx::Image site_favicon = gfx::Image::CreateFrom1xBitmap(bitmap);
    entry->GetFavicon().valid = true;
    entry->GetFavicon().url = GURL("https://example.com/favicon.ico");
    entry->GetFavicon().image = site_favicon;
    const TabData valid_icon_data = TabData::FromTabInterface(tab_interface);
    EXPECT_FALSE(valid_icon_data.should_themify_favicon);
    EXPECT_EQ(ui::ImageModel::FromImage(site_favicon), valid_icon_data.favicon);
  }

  {  // Themeable by virtual URL only.
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    TabInterface* const tab_interface = tab_strip_model->GetTabAtIndex(1);
    content::WebContents* const wc_virtual = tab_interface->GetContents();
    auto* entry_virtual = wc_virtual->GetController().GetLastCommittedEntry();
    ASSERT_NE(nullptr, entry_virtual);
    const GURL themeable_virtual_url("chrome://feedback/");
    entry_virtual->SetVirtualURL(themeable_virtual_url);
    auto tab_data_observer = std::make_unique<TabDataObserver>(tab_interface);
    const TabData& virtual_data = tab_data_observer->tab_data();
    EXPECT_TRUE(virtual_data.should_themify_favicon);
  }

  {  // Themeable by actual URL only.
    ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
        browser(), GURL(url::kAboutBlankURL),
        WindowOpenDisposition::NEW_FOREGROUND_TAB,
        ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
    TabInterface* const tab_interface = tab_strip_model->GetTabAtIndex(2);
    content::WebContents* const wc_actual = tab_interface->GetContents();
    auto* entry_actual = wc_actual->GetController().GetLastCommittedEntry();
    ASSERT_NE(nullptr, entry_actual);
    const GURL themeable_url("chrome://new-tab-page/");
    entry_actual->SetURL(themeable_url);
    auto tab_data_observer = std::make_unique<TabDataObserver>(tab_interface);
    const TabData& actual_data = tab_data_observer->tab_data();
    EXPECT_TRUE(actual_data.should_themify_favicon);
  }
}

}  // namespace tabs
