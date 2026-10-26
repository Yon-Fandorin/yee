// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/omnibox_popup_style.h"

#include "base/functional/callback_helpers.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace yee {

void ApplyOmniboxPopupStyle(content::WebContents* contents) {
  if (!contents || !contents->GetPrimaryMainFrame()) {
    return;
  }
  contents->GetPrimaryMainFrame()->ExecuteJavaScript(
      base::UTF8ToUTF16(base::StringPrintf(R"JS(
        (() => {
          const style = document.documentElement.style;
          const properties = {
            '--omnibox-popup-results-padding-top': '%dpx',
            '--omnibox-popup-results-padding-bottom': '%dpx',
            '--omnibox-popup-results-separator': '%dpx solid color-mix(in srgb, var(--color-searchbox-results-foreground) 18%%, var(--color-searchbox-results-background))',
            '--cr-searchbox-match-margin-inline-start': '%dpx',
            '--cr-searchbox-match-margin-inline-end': '%dpx',
            '--cr-searchbox-match-corner-radius': '%dpx',
            '--cr-searchbox-match-focus-indicator-display': 'none',
          };
          for (const [name, value] of Object.entries(properties)) {
            style.setProperty(name, value);
          }
        })();
      )JS",
          kSidebarMetrics.omnibox_popup_results_padding,
          kSidebarMetrics.omnibox_popup_results_padding,
          kSidebarMetrics.omnibox_popup_separator_height,
          kSidebarMetrics.omnibox_popup_row_inset,
          kSidebarMetrics.omnibox_popup_row_inset,
          kSidebarMetrics.omnibox_popup_row_corner_radius)),
      base::NullCallback());
}

}  // namespace yee
