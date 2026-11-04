// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/view_utils.h"

namespace yee {

class PageViewportAppliedState::Impl {
 public:
  explicit Impl(PageViewportAppliedState& state) : weak_factory(&state) {
    snapshot.owner = base::UnguessableToken::Create();
  }

  PageViewportAppliedSnapshot snapshot;
  using CallbackList =
      base::RepeatingCallbackList<void(const PageViewportAppliedSnapshot&)>;
  // 알림 중 owner가 삭제되어도 순회를 마친다. 각 구독은 WeakPtr로 막는다.
  std::shared_ptr<CallbackList> callbacks = std::make_shared<CallbackList>();
  base::WeakPtrFactory<PageViewportAppliedState> weak_factory;
};

class PageViewportClipHost : public views::View {
  METADATA_HEADER(PageViewportClipHost, views::View)

 public:
  PageViewportAppliedState& applied_state() { return applied_state_; }

 private:
  PageViewportAppliedState applied_state_;
};

BEGIN_METADATA(PageViewportClipHost)
END_METADATA

PageViewportAppliedState::PageViewportAppliedState()
    : impl_(std::make_unique<Impl>(*this)) {}

PageViewportAppliedState::~PageViewportAppliedState() = default;

PageViewportAppliedState* PageViewportAppliedState::From(
    views::View& clip_host) {
  auto* host = views::AsViewClass<PageViewportClipHost>(&clip_host);
  return host ? &host->applied_state() : nullptr;
}

PageViewportAppliedSnapshot PageViewportAppliedState::GetSnapshot() const {
  return impl_->snapshot;
}

base::CallbackListSubscription PageViewportAppliedState::Subscribe(
    Callback callback) {
  return impl_->callbacks->Add(base::BindRepeating(
      [](base::WeakPtr<PageViewportAppliedState> owner, uint64_t after_serial,
         Callback callback, const PageViewportAppliedSnapshot& snapshot) {
        if (owner && snapshot.apply_serial > after_serial) {
          callback.Run(snapshot);
        }
      },
      GetWeakPtr(), impl_->snapshot.apply_serial, std::move(callback)));
}

base::WeakPtr<PageViewportAppliedState> PageViewportAppliedState::GetWeakPtr() {
  return impl_->weak_factory.GetWeakPtr();
}

void PageViewportAppliedState::Publish(views::View& clip_host,
                                       const PageViewportGeometry& geometry) {
  auto& snapshot = impl_->snapshot;
  ++snapshot.apply_serial;
  snapshot.geometry = geometry;
  snapshot.clip_host_bounds_in_container = clip_host.bounds();
  snapshot.target_host_bounds_in_clip = GetPageTargetHost(clip_host).bounds();
  snapshot.overlay_host_bounds_in_clip =
      GetViewportOverlayHost(clip_host).bounds();
  snapshot.clip_host_radii = clip_host.layer()->rounded_corner_radii();
  snapshot.masks_to_bounds = clip_host.layer()->GetMasksToBounds();
  if (!impl_->callbacks->empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&PageViewportAppliedState::NotifySubscribers,
                                  GetWeakPtr(), snapshot));
  }
}

void PageViewportAppliedState::NotifySubscribers(
    PageViewportAppliedSnapshot snapshot) {
  const auto callbacks = impl_->callbacks;
  callbacks->Notify(snapshot);
}

namespace {

// A full-viewport overlay host must not become an event target just because
// its transparent bounds cover the page. Only an actually visible overlay
// child may claim the event; otherwise targeting falls through to the sibling
// PageTargetHost and its WebContents.
class ChildOnlyTargeterDelegate : public views::ViewTargeterDelegate {
 public:
  ChildOnlyTargeterDelegate() = default;
  ChildOnlyTargeterDelegate(const ChildOnlyTargeterDelegate&) = delete;
  ChildOnlyTargeterDelegate& operator=(const ChildOnlyTargeterDelegate&) =
      delete;
  ~ChildOnlyTargeterDelegate() override = default;

  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    for (const views::View* child : target->children()) {
      if (!child->GetVisible() || !child->GetCanProcessEventsWithinSubtree()) {
        continue;
      }
      gfx::RectF rect_in_child(rect);
      views::View::ConvertRectToTarget(target, child, &rect_in_child);
      if (child->HitTestRect(gfx::ToEnclosingRect(rect_in_child))) {
        return true;
      }
    }
    return false;
  }
};

std::unique_ptr<views::View> CreateTransparentHost(int id, bool clips_page) {
  std::unique_ptr<views::View> host =
      clips_page ? std::make_unique<PageViewportClipHost>()
                 : std::make_unique<views::View>();
  host->SetID(id);
  host->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  host->GetViewAccessibility().SetIsIgnored(true);
  if (clips_page) {
    host->SetPaintToLayer(ui::LAYER_NOT_DRAWN);
    host->layer()->SetFillsBoundsOpaquely(false);
    host->layer()->SetIsFastRoundedCorner(false);
    host->layer()->SetMasksToBounds(true);
  }
  return host;
}

views::View& GetRequiredHost(views::View& root, int id) {
  views::View* const host = root.GetViewByID(id);
  CHECK(host);
  return *host;
}

}  // namespace

std::unique_ptr<views::View> CreatePageViewportHostTree() {
  auto clip_host =
      CreateTransparentHost(kPageViewportClipHostViewId, /*clips_page=*/true);
  clip_host->AddChildView(
      CreateTransparentHost(kPageTargetHostViewId, /*clips_page=*/false));
  auto overlay_host = CreateTransparentHost(kViewportOverlayHostViewId,
                                            /*clips_page=*/false);
  overlay_host->SetEventTargeter(std::make_unique<views::ViewTargeter>(
      std::make_unique<ChildOnlyTargeterDelegate>()));
  clip_host->AddChildView(std::move(overlay_host));
  return clip_host;
}

views::View& GetPageTargetHost(views::View& page_viewport_clip_host) {
  CHECK_EQ(page_viewport_clip_host.GetID(), kPageViewportClipHostViewId);
  return GetRequiredHost(page_viewport_clip_host, kPageTargetHostViewId);
}

views::View& GetViewportOverlayHost(views::View& page_viewport_clip_host) {
  CHECK_EQ(page_viewport_clip_host.GetID(), kPageViewportClipHostViewId);
  return GetRequiredHost(page_viewport_clip_host, kViewportOverlayHostViewId);
}

void ApplyPageViewportHostGeometry(views::View& page_viewport_clip_host,
                                   const PageViewportGeometry& geometry) {
  CHECK_EQ(page_viewport_clip_host.GetID(), kPageViewportClipHostViewId);
  CHECK_EQ(page_viewport_clip_host.size(),
           geometry.current_viewport_in_container.size());
  CHECK(page_viewport_clip_host.layer());

  page_viewport_clip_host.layer()->SetRoundedCornerRadius(
      geometry.viewport_radii);
  page_viewport_clip_host.layer()->SetMasksToBounds(true);

  gfx::Rect target_bounds = geometry.target_stack_in_container;
  target_bounds.Offset(
      -geometry.current_viewport_in_container.OffsetFromOrigin());
  views::View& target_host = GetPageTargetHost(page_viewport_clip_host);
  target_host.SetBoundsRect(target_bounds);
  // Every approved PageTargetHost family is renderer-sized. Apply their
  // shared target rectangle in the same parent layout pass so no child can
  // retain the preceding animation frame's size.
  for (views::View* child : target_host.children()) {
    child->SetBoundsRect(target_host.GetLocalBounds());
  }
  GetViewportOverlayHost(page_viewport_clip_host)
      .SetBoundsRect(page_viewport_clip_host.GetLocalBounds());
  PageViewportAppliedState::From(page_viewport_clip_host)
      ->Publish(page_viewport_clip_host, geometry);
}

}  // namespace yee
