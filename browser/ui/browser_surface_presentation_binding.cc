// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/ui/views/yee/browser_surface_color_controller.h"
#include "content/public/browser/web_contents.h"

namespace yee {

BrowserSurfacePresentationBinding::BrowserSurfacePresentationBinding() =
    default;

BrowserSurfacePresentationBinding::~BrowserSurfacePresentationBinding() {
  ClearBinding(/*notify_observers=*/false);
}

void BrowserSurfacePresentationBinding::BindWebContents(
    content::WebContents* web_contents) {
  if (web_contents_ == web_contents) {
    return;
  }

  ClearBinding(/*notify_observers=*/false);
  web_contents_ = web_contents;

  if (web_contents_) {
    Observe(web_contents_);
    source_ =
        BrowserSurfaceColorController::GetOrCreateForWebContents(web_contents_);
    source_id_ = source_->source_id();
    if (theme_fallback_color_.has_value()) {
      source_->SetThemeFallbackColor(*theme_fallback_color_);
    }
    source_subscription_ =
        source_->AddPresentationChangedCallback(base::BindRepeating(
            &BrowserSurfacePresentationBinding::OnSourcePresentationChanged,
            base::Unretained(this), source_id_));
    PublishCurrentPresentation();
    return;
  }

  presentation_changed_callbacks_.Notify();
}

void BrowserSurfacePresentationBinding::WebContentsDestroyed() {
  ClearBinding(/*notify_observers=*/true);
}

void BrowserSurfacePresentationBinding::ClearBinding(bool notify_observers) {
  source_subscription_ = {};
  source_ = nullptr;
  web_contents_ = nullptr;
  source_id_ = 0;
  presentation_.reset();
  Observe(nullptr);
  if (notify_observers) {
    presentation_changed_callbacks_.Notify();
  }
}

void BrowserSurfacePresentationBinding::SetThemeFallbackColor(SkColor color) {
  color = SkColorSetA(color, SK_AlphaOPAQUE);
  theme_fallback_color_ = color;
  if (source_) {
    source_->SetThemeFallbackColor(color);
  }
}

void BrowserSurfacePresentationBinding::ActivateFrom(
    std::optional<SkColor> current_surface) {
  if (source_) {
    source_->ActivateFrom(current_surface);
  }
}

base::CallbackListSubscription
BrowserSurfacePresentationBinding::AddPresentationChangedCallback(
    base::RepeatingClosure callback) {
  return presentation_changed_callbacks_.Add(std::move(callback));
}

const std::optional<BrowserSurfacePresentation>&
BrowserSurfacePresentationBinding::GetPresentation() const {
  return presentation_;
}

void BrowserSurfacePresentationBinding::OnSourcePresentationChanged(
    uint64_t expected_source_id) {
  if (!source_ || source_->source_id() != expected_source_id ||
      source_id_ != expected_source_id) {
    return;
  }
  PublishCurrentPresentation();
}

void BrowserSurfacePresentationBinding::PublishCurrentPresentation() {
  if (!source_) {
    return;
  }
  const auto& source_presentation = source_->GetPresentation();
  if (source_presentation.has_value() &&
      source_presentation->source_id != source_id_) {
    return;
  }
  presentation_ = source_presentation;
  presentation_changed_callbacks_.Notify();
}

}  // namespace yee
