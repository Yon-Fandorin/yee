// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_INTERNAL_PAGE_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_INTERNAL_PAGE_TEST_SUPPORT_H_

#include <string>
#include <string_view>

#include "base/strings/strcat.h"

namespace yee::test {

// 내부 페이지의 실제 DOM·shadow DOM 변경과 리소스 완료로만 조건을 재평가한다.
// condition은 테스트가 제공하는 JS 표현식이며 Promise도 반환할 수 있다.
inline std::string WaitForInternalPageScript(std::string_view condition) {
  return base::StrCat({R"JS(
    new Promise((resolve, reject) => {
      const predicate = async () => (
  )JS",
                       condition, R"JS(
      );
      let finished = false, reading = false, changed = false, deadline;
      const roots = new Set();
      const events = ['load', 'error', 'input', 'change', 'popstate', 'hashchange'];
      const nativeEvents = [globalThis.chrome?.developerPrivate?.onItemStateChanged,
          globalThis.chrome?.developerPrivate?.onProfileStateChanged].filter(Boolean);
      const originalAttachShadow = Element.prototype.attachShadow;
      const finish = (value, error) => {
        if (finished) return;
        finished = true;
        observer.disconnect();
        clearTimeout(deadline);
        for (const root of roots)
          for (const event of events) root.removeEventListener(event, inspect, true);
        for (const event of nativeEvents) event.removeListener(inspect);
        window.navigation?.removeEventListener('currententrychange', inspect);
        if (Element.prototype.attachShadow === attachShadow)
          Element.prototype.attachShadow = originalAttachShadow;
        if (error) reject(error); else resolve(value);
      };
      const observeRoots = root => {
        if (!roots.has(root)) {
          roots.add(root);
          observer.observe(root,
              {subtree:true, childList:true, attributes:true, characterData:true});
          // load/error는 shadow 경계를 넘지 않으므로 각 root에서 직접 받는다.
          for (const event of events) root.addEventListener(event, inspect, true);
        }
        for (const host of root.querySelectorAll('*'))
          if (host.shadowRoot) observeRoots(host.shadowRoot);
      };
      const inspect = async () => {
        if (finished) return;
        if (reading) { changed = true; return; }
        reading = true;
        try {
          if (await predicate()) finish(true);
        } catch (error) { finish(false, error); }
        reading = false;
        // 비동기 predicate를 읽던 중 실제 변경이 온 경우에만 다시 읽는다.
        if (changed && !finished) { changed = false; queueMicrotask(inspect); }
      };
      const observer = new MutationObserver(() => {
        observeRoots(document);
        inspect();
      });
      const attachShadow = function(...args) {
        const root = originalAttachShadow.apply(this, args);
        // 기존 host에 나중에 붙는 root는 document의 mutation에 잡히지 않는다.
        observeRoots(root);
        inspect();
        return root;
      };
      Element.prototype.attachShadow = attachShadow;
      observeRoots(document);
      // URL 전환 이벤트는 window에서, DOM·리소스 이벤트는 각 root에서 받는다.
      roots.add(window);
      for (const event of events) window.addEventListener(event, inspect, true);
      for (const event of nativeEvents) event.addListener(inspect);
      window.navigation?.addEventListener('currententrychange', inspect);
      // 정상 완료를 지연하지 않는 단일 실패 deadline이다.
      deadline = setTimeout(() => finish(false), 5000);
      inspect();
    })
  )JS"});
}

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_INTERNAL_PAGE_TEST_SUPPORT_H_
