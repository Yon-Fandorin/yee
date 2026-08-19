// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_GROUP_COLOR_MARK_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_GROUP_COLOR_MARK_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/views/view.h"

namespace yee {

// Color mark that leads a vertical group header. The 8px disc stays a fixed
// size. Browser signals (ring, audio halo, unread dot) paint whether the
// group is expanded or collapsed. Agent states are not shown here.
class VerticalGroupColorMark : public views::View,
                               public gfx::AnimationDelegate {
  METADATA_HEADER(VerticalGroupColorMark, views::View)

 public:
  VerticalGroupColorMark();
  VerticalGroupColorMark(const VerticalGroupColorMark&) = delete;
  VerticalGroupColorMark& operator=(const VerticalGroupColorMark&) = delete;
  ~VerticalGroupColorMark() override;

  void SetMarkColor(SkColor color);
  void SetNeedsInput(bool needs_input);
  void SetAudioActive(bool audio_active);
  void SetUnread(bool unread);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  gfx::Size GetMinimumSize() const override;
  void OnPaint(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  void UpdateHaloAnimation();

  SkColor color_ = SK_ColorTRANSPARENT;
  bool needs_input_ = false;
  bool audio_active_ = false;
  bool unread_ = false;
  gfx::ThrobAnimation halo_animation_{this};
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_VERTICAL_GROUP_COLOR_MARK_H_
