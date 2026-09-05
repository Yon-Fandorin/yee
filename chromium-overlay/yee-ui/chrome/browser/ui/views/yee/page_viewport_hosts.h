// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_HOSTS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_PAGE_VIEWPORT_HOSTS_H_

#include <memory>

namespace views {
class View;
}  // namespace views

namespace yee {

struct PageViewportGeometry;

inline constexpr int kPageViewportClipHostViewId = 92015;
inline constexpr int kPageTargetHostViewId = 92016;
inline constexpr int kViewportOverlayHostViewId = 92017;

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
