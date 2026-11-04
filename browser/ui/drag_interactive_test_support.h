// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_DRAG_INTERACTIVE_TEST_SUPPORT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_DRAG_INTERACTIVE_TEST_SUPPORT_H_

#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"

class TabDragContext;
namespace yee::test {

class DragAppliedObserver {
 public:
  DragAppliedObserver(AppliedBrowserObservers& observers,
                      TabDragContext& context,
                      BrowserWindowInterface& browser,
                      uint64_t source_handle,
                      uint64_t session,
                      uint64_t step);
  ~DragAppliedObserver();
  DragAppliedObserver(const DragAppliedObserver&) = delete;
  DragAppliedObserver& operator=(const DragAppliedObserver&) = delete;

  AppliedOwnerToken ObservePhase(DragPhaseMatches::Phase phase);
  void AddContext(TabDragContext& context, BrowserWindowInterface& browser);
  // 가로 fixture만 단일 생성 callback의 소유권을 이 어댑터에 넘긴다.
  // 세로의 빈 callback에 의존하지 않는다.
  void ObserveHorizontalControllerCreation();
  // 실제 입력의 NotifyWhenDone에 전달한다. 이 메서드는 입력을 보내지 않는다.
  base::OnceClosure InputAck(DispatchDone done, gfx::Point screen);
  void BeginStep(uint64_t step);

 private:
  friend class DragAppliedObserverTestApi;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// native loop가 보관하는 once callback의 수명을 창 입력 없이 검증한다.
class DragAppliedObserverTestApi {
 public:
  static base::OnceClosure ArmLoopCallback(DragAppliedObserver& observer);
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_DRAG_INTERACTIVE_TEST_SUPPORT_H_
