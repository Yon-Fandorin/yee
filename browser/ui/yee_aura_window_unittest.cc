// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/scoped_observation.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/test/test_window_delegate.h"
#include "ui/aura/test/test_windows.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_not_drawn.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace aura::test {
namespace {

class YeeAuraWindowTest : public AuraTestBase, public WindowObserver {
 protected:
  struct WindowBoundsInfo {
    int changed_count = 0;
    gfx::Rect old_bounds;
    gfx::Rect new_bounds;
    ui::PropertyChangeReason reason =
        ui::PropertyChangeReason::NOT_FROM_ANIMATION;
  };

  const WindowBoundsInfo& window_bounds_info() const { return bounds_info_; }

 private:
  void OnWindowBoundsChanged(Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds,
                             ui::PropertyChangeReason reason) override {
    bounds_info_ = {bounds_info_.changed_count + 1, old_bounds, new_bounds,
                    reason};
  }

  WindowBoundsInfo bounds_info_;
};

// Views 조상 이동 뒤 로컬 layer 좌표가 같아도 화면 좌표와 알림은 갱신한다.
TEST_F(YeeAuraWindowTest, UnmanagedLayerKeepsScreenOriginAfterAncestorMoves) {
  std::unique_ptr<Window> parent(CreateTestWindow(
      {.parent = root_window(), .bounds = {100, 50, 800, 600}}));
  ui::LayerNotDrawn views_parent;
  views_parent.SetBounds(gfx::Rect(400, 30, 300, 200));
  parent->layer()->Add(&views_parent);
  std::unique_ptr<Window> child(
      CreateTestWindow({.parent = parent.get(), .bounds = {400, 30, 100, 80}}));
  child->SetLayerManagedByParent(false);
  views_parent.Add(child->layer());
  child->SetBounds(gfx::Rect(400, 30, 100, 80));
  const gfx::Rect local_bounds = child->layer()->bounds();
  EXPECT_EQ(gfx::Rect(0, 0, 100, 80), local_bounds);
  base::ScopedObservation<Window, WindowObserver> observation(this);
  observation.Observe(child.get());

  gfx::Rect previous_bounds = child->bounds();
  for (const gfx::Point origin : {gfx::Point(32, 60), gfx::Point(220, 15)}) {
    views_parent.SetBounds(gfx::Rect(origin, views_parent.bounds().size()));
    const int previous_notifications = window_bounds_info().changed_count;
    const gfx::Rect native_bounds(origin, child->bounds().size());
    child->SetBounds(native_bounds);
    EXPECT_EQ(local_bounds, child->layer()->bounds());
    EXPECT_EQ(native_bounds, child->bounds());
    EXPECT_EQ(child->GetBoundsInScreen(),
              child->GetBoundsInScreenWithoutTransform());
    ASSERT_EQ(previous_notifications + 1, window_bounds_info().changed_count);
    EXPECT_EQ(previous_bounds, window_bounds_info().old_bounds);
    EXPECT_EQ(native_bounds, window_bounds_info().new_bounds);
    EXPECT_EQ(ui::PropertyChangeReason::NOT_FROM_ANIMATION,
              window_bounds_info().reason);

    // 같은 위치를 반복 적용해도 추가 알림을 발생시키지 않는다.
    child->SetBounds(native_bounds);
    EXPECT_EQ(previous_notifications + 1, window_bounds_info().changed_count);
    previous_bounds = native_bounds;
  }
}

class OwningWindowDelegate : public TestWindowDelegate {
 public:
  OwningWindowDelegate() {}

  OwningWindowDelegate(const OwningWindowDelegate&) = delete;
  OwningWindowDelegate& operator=(const OwningWindowDelegate&) = delete;

  void SetOwnedWindow(Window* window) { owned_window_.reset(window); }

  void OnWindowDestroyed(Window* window) override { owned_window_.reset(); }

 private:
  std::unique_ptr<Window> owned_window_;
};

// 첫 자식이 파괴되면서 같은 부모의 두 번째 자식도 정리한다.
TEST_F(YeeAuraWindowTest, DeleteWindowFromOnWindowDestroyed) {
  std::unique_ptr<Window> parent(new Window(nullptr));
  parent->Init(ui::LAYER_NOT_DRAWN);
  OwningWindowDelegate delegate;
  Window* c1 = new Window(&delegate);
  c1->Init(ui::LAYER_NOT_DRAWN);
  parent->AddChild(c1);
  Window* c2 = new Window(nullptr);
  c2->Init(ui::LAYER_NOT_DRAWN);
  parent->AddChild(c2);
  delegate.SetOwnedWindow(c2);
  parent.reset();
}

}  // namespace
}  // namespace aura::test
