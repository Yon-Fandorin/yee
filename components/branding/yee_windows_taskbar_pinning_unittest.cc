// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wrl/implements.h>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/shortcut.h"
#include "chrome/installer/util/taskbar_util.h"
#include "chrome/installer/util/taskbar_util_internal.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

using taskbar_util::internal::IPinnedList3;
using taskbar_util::internal::PinnedListModifyCaller;
using taskbar_util::internal::SetPinnedListFactoryForTesting;

// 실제 작업표시줄에 접근하지 않고 상태 조회와 고정 요청을 기록한다.
class FakePinnedList
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IPinnedList3> {
 public:
  HRESULT STDMETHODCALLTYPE EnumObjects() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetPinnableInfo() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE IsPinnable() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Resolve() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LegacyModify() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetChangeCount() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE IsPinned(PCIDLIST_ABSOLUTE item) override {
    ++query_count;
    wchar_t path[MAX_PATH] = {};
    EXPECT_TRUE(SHGetPathFromIDList(item, path));
    queried_shortcut = base::FilePath(path);
    return pin_status;
  }
  HRESULT STDMETHODCALLTYPE GetPinnedItem() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetAppIDForPinnedItem() override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE ItemChangeNotify() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE UpdateForRemovedItemsAsNecessary() override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE PinShellLink() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetPinnedItemForAppID() override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE Modify(PCIDLIST_ABSOLUTE unpin,
                                   PCIDLIST_ABSOLUTE pin,
                                   PinnedListModifyCaller caller) override {
    ++modify_count;
    EXPECT_EQ(nullptr, unpin);
    EXPECT_EQ(PinnedListModifyCaller::kExplorer, caller);
    wchar_t path[MAX_PATH] = {};
    EXPECT_TRUE(SHGetPathFromIDList(pin, path));
    requested_shortcut = base::FilePath(path);
    return modify_result;
  }

  HRESULT pin_status = S_FALSE;
  HRESULT modify_result = S_OK;
  int query_count = 0;
  int modify_count = 0;
  base::FilePath queried_shortcut;
  base::FilePath requested_shortcut;
};

FakePinnedList* g_pinned_list = nullptr;

class TaskbarPinningTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(com_initializer_.Succeeded());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    shortcut_ = temp_dir_.GetPath().Append(L"Yee.lnk");
    base::FilePath target;
    ASSERT_TRUE(base::PathService::Get(base::FILE_EXE, &target));
    base::win::ShortcutProperties properties;
    properties.set_target(target);
    properties.set_app_id(L"Yee.TaskbarPinningTest");
    ASSERT_TRUE(base::win::CreateOrUpdateShortcutLink(
        shortcut_, properties, base::win::ShortcutOperation::kCreateAlways));

    pinned_list_ = Microsoft::WRL::Make<FakePinnedList>();
    g_pinned_list = pinned_list_.Get();
    SetPinnedListFactoryForTesting(
        []() -> Microsoft::WRL::ComPtr<IPinnedList3> { return g_pinned_list; });
  }

  void TearDown() override {
    SetCanPinToTaskbarDelegate(nullptr);
    SetPinnedListFactoryForTesting(nullptr);
    g_pinned_list = nullptr;
  }

  base::win::ScopedCOMInitializer com_initializer_;
  base::ScopedTempDir temp_dir_;
  base::FilePath shortcut_;
  Microsoft::WRL::ComPtr<FakePinnedList> pinned_list_;
};

// 재설치 시 이미 고정된 앱에는 승인 요청을 다시 보내지 않는다.
TEST_F(TaskbarPinningTest, AlreadyPinnedDoesNotRequestAgain) {
  pinned_list_->pin_status = S_OK;
  EXPECT_TRUE(PinShortcutToTaskbar(shortcut_));
  EXPECT_EQ(1, pinned_list_->query_count);
  EXPECT_EQ(shortcut_, pinned_list_->queried_shortcut);
  EXPECT_EQ(0, pinned_list_->modify_count);
}

// 미등록이 확인된 바로가기만 같은 경로로 한 번 고정 요청한다.
TEST_F(TaskbarPinningTest, UnpinnedRequestsSameShortcut) {
  EXPECT_TRUE(PinShortcutToTaskbar(shortcut_));
  EXPECT_EQ(1, pinned_list_->query_count);
  EXPECT_EQ(shortcut_, pinned_list_->queried_shortcut);
  EXPECT_EQ(1, pinned_list_->modify_count);
  EXPECT_EQ(shortcut_, pinned_list_->requested_shortcut);
}

// 상태 조회가 실패하거나 해석할 수 없으면 기존 고정을 건드리지 않는다.
TEST_F(TaskbarPinningTest, UnknownPinStateDoesNotRequest) {
  for (HRESULT status : {E_FAIL, static_cast<HRESULT>(2)}) {
    pinned_list_->pin_status = status;
    EXPECT_FALSE(PinShortcutToTaskbar(shortcut_));
  }
  EXPECT_EQ(2, pinned_list_->query_count);
  EXPECT_EQ(0, pinned_list_->modify_count);
}

TEST_F(TaskbarPinningTest, PinFailureIsReported) {
  pinned_list_->modify_result = E_ACCESSDENIED;
  EXPECT_FALSE(PinShortcutToTaskbar(shortcut_));
  EXPECT_EQ(1, pinned_list_->modify_count);
}

TEST_F(TaskbarPinningTest, MissingShortcutDoesNotRequest) {
  EXPECT_FALSE(
      PinShortcutToTaskbar(temp_dir_.GetPath().Append(L"missing.lnk")));
  EXPECT_EQ(0, pinned_list_->query_count);
  EXPECT_EQ(0, pinned_list_->modify_count);
}

TEST_F(TaskbarPinningTest, UnavailablePinnedListDoesNotRequest) {
  SetPinnedListFactoryForTesting(
      []() -> Microsoft::WRL::ComPtr<IPinnedList3> { return nullptr; });
  EXPECT_FALSE(PinShortcutToTaskbar(shortcut_));
  EXPECT_EQ(0, pinned_list_->modify_count);
}

TEST_F(TaskbarPinningTest, PinningDisabledDoesNotQueryOrRequest) {
  SetCanPinToTaskbarDelegate([] { return false; });
  EXPECT_FALSE(PinShortcutToTaskbar(shortcut_));
  EXPECT_EQ(0, pinned_list_->query_count);
  EXPECT_EQ(0, pinned_list_->modify_count);
}

}  // namespace
