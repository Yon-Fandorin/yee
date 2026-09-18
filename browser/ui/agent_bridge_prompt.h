// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_PROMPT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_PROMPT_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/gfx/geometry/point.h"

namespace views {

class View;
class Widget;

// Shows a browser-native request from the Yee agent. The returned Widget is
// owned by the native widget system; callers may safely call Close() when the
// request expires or its document/navigation changes.
//
// `reply` is run at most once. A dismissed, cancelled, expired, or otherwise
// externally closed prompt reports `accepted == false` and an empty answer.
Widget* ShowAgentBridgePrompt(
    View* owner,
    const std::u16string& question,
    bool approval,
    base::OnceCallback<void(bool accepted, std::string answer)> reply,
    const std::u16string& approval_label = u"Allow once");

// Shows a short-lived, non-intercepting visual marker at a screen coordinate
// corresponding to an agent action. The caller owns the lifetime and should
// close the returned widget when the action has been acknowledged.
Widget* ShowAgentBridgePointer(View* owner, gfx::Point screen_point);

}  // namespace views

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_PROMPT_H_
