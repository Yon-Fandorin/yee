// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "ui/views/controls/button/button.h"

class ToolbarButton;

namespace views {
class Background;
class View;
}  // namespace views

namespace yee {

inline constexpr int kContentOutlineViewId = 92003;
inline constexpr float kContentCornerRadius = 8.0f;
inline constexpr int kShellControlSize = 30;
inline constexpr int kShellControlCornerRadius = 8;
inline constexpr int kShellControlHorizontalMargin = 1;

enum class ShellCreateAction {
  kNewTab,
  kNewGroup,
  kChat,
};

using ShellCreateCallback =
    base::RepeatingCallback<void(ShellCreateAction action, int event_flags)>;

std::unique_ptr<views::Background> CreateShellBackground();
std::unique_ptr<views::View> CreateContentOutlineView();

void ApplyShellControlStyle(ToolbarButton& button);

std::unique_ptr<ToolbarButton> CreateShellToolbarButton(
    views::Button::PressedCallback callback);
std::unique_ptr<ToolbarButton> CreateShellAddButton(
    ShellCreateCallback callback);
std::unique_ptr<ToolbarButton> CreateAgentToolbarButton(
    views::Button::PressedCallback callback);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_YEE_UI_H_
