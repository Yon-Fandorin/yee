// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
// Evaluated in Yee's isolated world with the native-supplied selectors binding.
(() => {
  const sheet = new CSSStyleSheet(), valid = [];
  for (const selector of selectors) {
    try {
      sheet.insertRule(selector + '{display:none!important;}', 0);
      sheet.deleteRule(0);
      valid.push(selector);
    } catch {} // Isolate a malformed rule before combining native style chunks.
  }
  return valid;
})()
