// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_DISCLOSURE_ARROW_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_DISCLOSURE_ARROW_H_

#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/image/image_skia.h"

namespace yee {

// 프로젝트와 native 그룹이 같은 leading 칸에서 쓰는 회전 화살표다.
// 상태 복원·숨겨진 조작은 즉시 갱신하고 실제 폴딩 입력만 애니메이션한다.
class DisclosureArrow : public gfx::AnimationDelegate {
 public:
  explicit DisclosureArrow(base::RepeatingClosure repaint);
  ~DisclosureArrow() override;
  void SetExpanded(bool expanded, bool animate);
  gfx::ImageSkia GetImage(SkColor foreground) const;

 private:
  friend class DisclosureArrowTest;
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  const base::RepeatingClosure repaint_;
  bool expanded_ = false;
  gfx::SlideAnimation animation_{this};
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_DISCLOSURE_ARROW_H_
