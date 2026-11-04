// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_HOSTS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_HOSTS_H_

#include <cstdint>
#include <memory>

#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/unguessable_token.h"
#include "chrome/browser/ui/views/yee/page_viewport_geometry.h"

namespace views {
class View;
}  // namespace views

namespace yee {

inline constexpr int kPageViewportClipHostViewId = 92015;
inline constexpr int kPageTargetHostViewId = 92016;
inline constexpr int kViewportOverlayHostViewId = 92017;

struct PageViewportAppliedSnapshot {
  base::UnguessableToken owner;
  uint64_t apply_serial = 0;
  PageViewportGeometry geometry;
  gfx::Rect clip_host_bounds_in_container;
  gfx::Rect target_host_bounds_in_clip;
  gfx::Rect overlay_host_bounds_in_clip;
  gfx::RoundedCornersF clip_host_radii;
  bool masks_to_bounds = false;

  bool operator==(const PageViewportAppliedSnapshot&) const = default;
};

// clip host의 적용만 기록한다. 이후 native clip이나 BrowserView layout의
// 완료는 소비자가 해당 소유자의 관측값으로 따로 확인한다.
class PageViewportAppliedState {
 public:
  using Callback =
      base::RepeatingCallback<void(const PageViewportAppliedSnapshot&)>;

  PageViewportAppliedState(const PageViewportAppliedState&) = delete;
  PageViewportAppliedState& operator=(const PageViewportAppliedState&) = delete;
  ~PageViewportAppliedState();

  // Yee가 만든 clip host에만 상태가 있다. 첫 적용 전 serial은 0이다.
  static PageViewportAppliedState* From(views::View& clip_host);
  PageViewportAppliedSnapshot GetSnapshot() const;
  // 초기 알림 없이 적용 당시 값의 복사본을 다음 task에서 전달한다.
  // owner 파괴 시 구독과 아직 전달하지 않은 알림을 무효화한다.
  base::CallbackListSubscription Subscribe(Callback callback);
  base::WeakPtr<PageViewportAppliedState> GetWeakPtr();

 private:
  friend class PageViewportClipHost;
  friend void ApplyPageViewportHostGeometry(views::View&,
                                            const PageViewportGeometry&);
  PageViewportAppliedState();
  void Publish(views::View& clip_host, const PageViewportGeometry& geometry);
  void NotifySubscribers(PageViewportAppliedSnapshot snapshot);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Creates the persistent, accessibility-transparent page host hierarchy. The
// returned clip host owns one target host followed by one viewport overlay
// host, but page families are migrated into those hosts only after their
// individual coordinate, focus, native-surface, and teardown gates pass.
std::unique_ptr<views::View> CreatePageViewportHostTree();

views::View& GetPageTargetHost(views::View& page_viewport_clip_host);
views::View& GetViewportOverlayHost(views::View& page_viewport_clip_host);

// Applies container-local geometry to a clip host whose own bounds have
// already been set to `current_viewport_in_container` by its parent layout.
void ApplyPageViewportHostGeometry(views::View& page_viewport_clip_host,
                                   const PageViewportGeometry& geometry);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_HOSTS_H_
