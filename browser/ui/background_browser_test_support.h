// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_BACKGROUND_BROWSER_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_BACKGROUND_BROWSER_TEST_SUPPORT_H_

namespace yee::test {

// NSApp을 만들기 전에 설치하며 프로세스가 끝날 때까지 활성화 요청을 막는다.
void SuppressBackgroundBrowserActivation();

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_BACKGROUND_BROWSER_TEST_SUPPORT_H_
