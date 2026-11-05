// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/animation/browser_animation_controller.h"

#include <concepts>
#include <memory>
#include <type_traits>
#include <vector>

#include "base/containers/map_util.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/browser/ui/animation/browser_animation_provider.h"
#include "chrome/browser/ui/animation/browser_animation_provider_internal.h"
#include "chrome/browser/ui/animation/browser_animation_types.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/test/mock_browser_window_interface.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/identifier/unique_identifier.h"
#include "ui/base/interaction/expect_call_in_scope.h"
#include "ui/base/interaction/safe_castable.h"
#include "ui/base/unowned_user_data/unowned_user_data_host.h"
#include "ui/gfx/animation/animation_test_api.h"
#include "ui/gfx/animation/tween.h"

namespace {

DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationGroup,
                                     kTestAnimationGroup);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationMotion,
                                     kTestAnimationMotion1);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationMotion,
                                     kTestAnimationMotion2);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationSequence,
                                     kTestAnimationSequence1);
DEFINE_LOCAL_UNIQUE_IDENTIFIER_VALUE(BrowserAnimationSequence,
                                     kTestAnimationSequence2);

class SingleGroupAnimationProvider : public CachingBrowserAnimationProvider {
 public:
  DECLARE_SAFE_CAST_TARGET()

  SingleGroupAnimationProvider() = default;
  ~SingleGroupAnimationProvider() override = default;

  GroupInfos GenerateAnimations() const override {
    MotionLookup lookup;
    for (auto& motion : GetMotions()) {
      lookup.emplace(std::move(motion));
    }
    return Groups(GroupInfo{kTestAnimationGroup, lookup});
  }

  template <typename... Args>
  void SetSequenceParams(const Args&... args) {
    CachingBrowserAnimationProvider::SetSequenceParams(kTestAnimationGroup,
                                                       args...);
  }

 protected:
  virtual std::vector<MotionInfo> GetMotions() const = 0;
};

DEFINE_SAFE_CAST_TARGET(SingleGroupAnimationProvider)

class ResetAnimationProvider : public SingleGroupAnimationProvider {
 public:
  ResetAnimationProvider() {
    SetSequenceParams(Persist(kTestAnimationSequence1),
                      Persist(kTestAnimationSequence2));
  }
  std::vector<MotionInfo> GetMotions() const override {
    return {
        Motion(kTestAnimationMotion1, TotalDurationMs(1000), gfx::Tween::LINEAR,
               Animate(kTestAnimationSequence1, FromValue(0.0), ToValue(1.0)),
               Animate(kTestAnimationSequence2, FromValue(1.0), ToValue(0.0))),
        Motion(kTestAnimationMotion2, TotalDurationMs(1000), gfx::Tween::LINEAR,
               Sequence(kTestAnimationSequence1,
                        Keyframe(AtPercent(0.25), Value(0.25)),
                        Keyframe(AtPercent(0.75), Value(0.75))),
               Sequence(kTestAnimationSequence2,
                        Keyframe(AtPercent(0.25), Value(0.25)),
                        Keyframe(AtPercent(0.75), Value(0.75))))};
  }
};

class YeeBrowserAnimationControllerTest : public testing::Test {
 public:
  YeeBrowserAnimationControllerTest() {
    EXPECT_CALL(browser_window_, GetUnownedUserDataHost)
        .WillRepeatedly(testing::ReturnRef(data_host_));
    controller_ = std::make_unique<BrowserAnimationController>(browser_window_);
  }

  ~YeeBrowserAnimationControllerTest() override = default;

  BrowserAnimationController* controller() { return controller_.get(); }
  BrowserWindowInterface& browser_window() { return browser_window_; }
  base::test::TaskEnvironment& task_environment() { return task_environment_; }

  void RemoveProvider() {
    if (animation_provider_) {
      auto ptr = controller_->RemoveProviderForTesting(animation_provider_);
      animation_provider_ = nullptr;
    }
  }

  template <typename T>
    requires std::derived_from<T, BrowserAnimationProvider>
  T* SetProvider() {
    RemoveProvider();
    auto t_ptr = std::make_unique<T>();
    T* const t = t_ptr.get();
    animation_provider_ = controller_->AddAnimationProvider(std::move(t_ptr));
    return t;
  }

  void FastForwardMs(int ms) {
    task_environment_.FastForwardBy(base::Milliseconds(ms));
  }

 protected:
  gfx::AnimationTestApi::RenderModeResetter animation_mode_reset_ =
      gfx::AnimationTestApi::SetRichAnimationRenderMode(
          gfx::Animation::RichAnimationRenderMode::FORCE_ENABLED);

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ui::UnownedUserDataHost data_host_;
  MockBrowserWindowInterface browser_window_;
  std::unique_ptr<BrowserAnimationController> controller_;
  raw_ptr<BrowserAnimationProvider> animation_provider_ = nullptr;
};

TEST_F(YeeBrowserAnimationControllerTest, Reset_MidMotion_SameMotion) {
  SetProvider<ResetAnimationProvider>();
  UNCALLED_MOCK_CALLBACK(BrowserAnimationCallback, callback);
  const auto subscription =
      controller()->Subscribe(kTestAnimationGroup, callback.Get());

  EXPECT_CALL(callback, Run(testing::_, BrowserAnimationUpdate::kStarted));
  EXPECT_CALL(callback, Run(testing::_, BrowserAnimationUpdate::kProgressed))
      .Times(testing::AnyNumber());
  controller()->Start(kTestAnimationGroup, kTestAnimationMotion1);
  FastForwardMs(500);
  EXPECT_CALL_IN_SCOPE(
      callback, Run(testing::_, BrowserAnimationUpdate::kEnded),
      controller()->Reset(kTestAnimationGroup, kTestAnimationMotion1));
  EXPECT_EQ(1.0, controller()->GetCurrentValue(kTestAnimationGroup,
                                               kTestAnimationSequence1));
  EXPECT_EQ(0.0, controller()->GetCurrentValue(kTestAnimationGroup,
                                               kTestAnimationSequence2));
  // Reset 후에도 원래 종료 시각을 지나 진행시키면 늦은 tick이 없어야 한다.
  FastForwardMs(600);
  EXPECT_FALSE(controller()->IsAnimating(kTestAnimationGroup));
  EXPECT_EQ(1.0, controller()->GetCurrentValue(kTestAnimationGroup,
                                               kTestAnimationSequence1));
}

TEST_F(YeeBrowserAnimationControllerTest, Reset_FromFinalProgressHasSingleEnd) {
  SetProvider<ResetAnimationProvider>();
  UNCALLED_MOCK_CALLBACK(BrowserAnimationCallback, callback);
  const auto subscription =
      controller()->Subscribe(kTestAnimationGroup, callback.Get());
  bool reset = false;
  EXPECT_CALL(callback, Run(testing::_, BrowserAnimationUpdate::kStarted));
  EXPECT_CALL(callback, Run(testing::_, BrowserAnimationUpdate::kEnded))
      .Times(1);
  EXPECT_CALL(callback, Run(testing::_, BrowserAnimationUpdate::kProgressed))
      .WillRepeatedly(
          [&](const BrowserAnimationController*, BrowserAnimationUpdate) {
            if (!reset &&
                controller()->GetCurrentValue(kTestAnimationGroup,
                                              kTestAnimationSequence1) == 1.0) {
              reset = true;
              controller()->Reset(kTestAnimationGroup, kTestAnimationMotion1);
            }
          });
  controller()->Start(kTestAnimationGroup, kTestAnimationMotion1);
  FastForwardMs(1100);
  EXPECT_TRUE(reset);
  EXPECT_FALSE(controller()->IsAnimating(kTestAnimationGroup));
}

}  // namespace
