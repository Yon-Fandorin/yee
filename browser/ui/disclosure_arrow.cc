// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "chrome/browser/ui/views/yee/disclosure_arrow.h"

#include <utility>

#include "base/i18n/rtl.h"
#include "base/time/time.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/vector_icons/vector_icons.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/paint_vector_icon.h"

namespace yee {
namespace {

class ArrowImageSource : public gfx::CanvasImageSource {
 public:
  ArrowImageSource(SkColor foreground, double progress)
      : CanvasImageSource(gfx::Size(kSidebarMetrics.group_mark_slot_size,
                                    kSidebarMetrics.group_mark_slot_size)),
        foreground_(foreground),
        degrees_(progress * (base::i18n::IsRTL() ? -90.0f : 90.0f)) {}

  void Draw(gfx::Canvas* canvas) override {
    const float center = size().width() / 2.0f;
    canvas->sk_canvas()->translate(center, center);
    canvas->sk_canvas()->rotate(degrees_);
    canvas->sk_canvas()->translate(-center, -center);
    canvas->DrawImageInt(
        gfx::CreateVectorIcon(vector_icons::kKeyboardArrowRightFlippableIcon,
                              size().width(), foreground_),
        0, 0);
  }

 private:
  const SkColor foreground_;
  const float degrees_;
};

}  // namespace

DisclosureArrow::DisclosureArrow(base::RepeatingClosure repaint)
    : repaint_(std::move(repaint)) {
  animation_.SetSlideDuration(base::Milliseconds(160));
  animation_.SetTweenType(gfx::Tween::EASE_IN_OUT);
}

DisclosureArrow::~DisclosureArrow() = default;

void DisclosureArrow::SetExpanded(bool expanded, bool animate) {
  if (!animate || !gfx::Animation::ShouldRenderRichAnimation()) {
    expanded_ = expanded;
    animation_.Reset(expanded ? 1.0 : 0.0);
    return;
  }
  if (expanded_ == expanded)
    return;
  expanded_ = expanded;
  if (expanded)
    animation_.Show();
  else
    animation_.Hide();
}

gfx::ImageSkia DisclosureArrow::GetImage(SkColor foreground) const {
  return gfx::CanvasImageSource::MakeImageSkia<ArrowImageSource>(
      foreground, animation_.GetCurrentValue());
}

void DisclosureArrow::AnimationProgressed(const gfx::Animation*) {
  repaint_.Run();
}

void DisclosureArrow::AnimationEnded(const gfx::Animation*) {
  repaint_.Run();
}

}  // namespace yee
