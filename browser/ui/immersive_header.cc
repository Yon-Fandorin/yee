// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/immersive_header.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "ui/base/class_property.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(float, kSidebarHeaderReveal, 1.0f)

class YeeImmersiveHeaderBackdrop : public views::View {
  METADATA_HEADER(YeeImmersiveHeaderBackdrop, views::View)

 public:
  explicit YeeImmersiveHeaderBackdrop(views::View& shell_source)
      : shell_source_(&shell_source) {
    SetID(yee::kImmersiveHeaderBackdropViewId);
    SetCanProcessEventsWithinSubtree(false);
    fill_ = AddChildView(yee::CreateBrowserSurfaceFillView(std::nullopt));
    outline_ = AddChildView(yee::CreateCombinedSurfaceOutlineView(
        base::BindRepeating(&YeeImmersiveHeaderBackdrop::GetPresentation,
                            base::Unretained(this))));
  }

  void UpdatePresentation(
      std::optional<yee::BrowserSurfacePresentation> presentation) {
    presentation_ = std::move(presentation);
    yee::UpdateBrowserSurfaceFillView(*fill_, presentation_);
    outline_->SchedulePaint();
  }

  void Update(const gfx::Rect& surface,
              std::optional<yee::BrowserSurfacePresentation> presentation) {
    UpdatePresentation(std::move(presentation));
    fill_->SetBoundsRect(surface);
    outline_->SetBoundsRect(surface);
    yee::UpdateCombinedSurfaceOutlineLayout(
        *outline_, false, yee::kSidebarMetrics.browser_surface_header_height());
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // AppKit accessory 자체의 흰 바탕을 가리고 본문의 shell 색을 이어 준다.
    canvas->FillRect(GetLocalBounds(),
                     yee::ResolveShellBackgroundColor(
                         *shell_source_->GetColorProvider(),
                         shell_source_->GetWidget()->ShouldPaintAsActive()));
  }

 protected:
  void AddedToWidget() override {
    views::View::AddedToWidget();
    paint_as_active_subscription_ =
        shell_source_->GetWidget()->RegisterPaintAsActiveChangedCallback(
            base::BindRepeating(&views::View::SchedulePaint,
                                base::Unretained(this)));
  }

  void RemovedFromWidget() override {
    paint_as_active_subscription_ = {};
    views::View::RemovedFromWidget();
  }

 private:
  std::optional<yee::BrowserSurfacePresentation> GetPresentation() const {
    return presentation_;
  }

  raw_ptr<views::View> fill_;
  raw_ptr<views::View> outline_;
  raw_ptr<views::View> shell_source_;
  std::optional<yee::BrowserSurfacePresentation> presentation_;
  base::CallbackListSubscription paint_as_active_subscription_;
};

BEGIN_METADATA(YeeImmersiveHeaderBackdrop)
END_METADATA

}  // namespace

namespace yee {

void SetSidebarHeaderHost(views::View& sidebar,
                          views::View& header,
                          views::View* external_host,
                          double sidebar_header_reveal) {
  const float reveal =
      external_host
          ? 1.0f
          : std::clamp(static_cast<float>(sidebar_header_reveal), 0.0f, 1.0f);
  if (header.GetProperty(kSidebarHeaderReveal) != reveal) {
    header.SetProperty(kSidebarHeaderReveal, reveal);
    if (reveal == 1.0f) {
      header.ClearProperty(views::kFlexBehaviorKey);
    } else {
      // 고정으로 넘기는 동안 로컬 Header 행도 연속으로 줄여 목록을 잇는다.
      header.SetProperty(
          views::kFlexBehaviorKey,
          views::FlexSpecification(base::BindRepeating(
              [](float reveal, const views::View* view,
                 const views::SizeBounds& bounds) {
                gfx::Size size = view->GetPreferredSize(bounds);
                // 빈 행으로 숨겨지면 다음 host에서도 계속 숨겨질 수 있다.
                size.set_height(
                    std::max(1, base::ClampRound(size.height() * reveal)));
                return size;
              },
              reveal)));
    }
    sidebar.InvalidateLayout();
  }
  views::View* const host = external_host ? external_host : &sidebar;
  if (header.parent() != host) {
    host->AddChildViewAt(&header, 0);
  }
}

void UpdateImmersiveHeaderBackdrop(
    views::View& host,
    views::View& shell_source,
    const gfx::Rect& surface_bounds,
    std::optional<BrowserSurfacePresentation> presentation) {
  auto* backdrop = static_cast<YeeImmersiveHeaderBackdrop*>(
      host.GetViewByID(kImmersiveHeaderBackdropViewId));
  if (!backdrop) {
    backdrop = host.AddChildViewAt(
        std::make_unique<YeeImmersiveHeaderBackdrop>(shell_source), 0);
  }
  backdrop->SetVisible(true);
  backdrop->SetBoundsRect(host.GetLocalBounds());
  backdrop->Update(surface_bounds, std::move(presentation));
}

void ClearImmersiveHeaderBackdrop(views::View& host) {
  if (auto* backdrop = host.GetViewByID(kImmersiveHeaderBackdropViewId)) {
    host.RemoveChildViewT(backdrop);
  }
}

void UpdateImmersiveHeaderPresentation(
    views::View& host,
    std::optional<BrowserSurfacePresentation> presentation) {
  if (auto* backdrop = static_cast<YeeImmersiveHeaderBackdrop*>(
          host.GetViewByID(kImmersiveHeaderBackdropViewId))) {
    backdrop->UpdatePresentation(std::move(presentation));
  }
}

}  // namespace yee
