// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_VIEWS_OBSERVERS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_VIEWS_OBSERVERS_H_

#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"

namespace views {
class AnimatingLayoutManager;
class LayoutManager;
class ScrollView;
class View;
class Widget;
}  // namespace views

namespace yee::test {

class AppliedBrowserObservers;
class DragAppliedObserver;

AppliedCoordinates GetAppliedViewCoordinates(
    const views::View& view,
    AppliedCoordinates::Space space = AppliedCoordinates::Space::kLocal);

// fixture가 session보다 먼저 만들고 나중에 파괴한다. 관측은 입력·layout을
// 실행하지 않으며 등록한 객체가 사라지면 그 token을 영구 폐기한다.
class AppliedViewsObservers final : public AppliedSignalSource {
 public:
  AppliedViewsObservers();
  ~AppliedViewsObservers() override;
  AppliedViewsObservers(const AppliedViewsObservers&) = delete;
  AppliedViewsObservers& operator=(const AppliedViewsObservers&) = delete;

  AppliedOwnerToken ObserveView(views::View& view);
  AppliedOwnerToken ObserveWidget(views::Widget& widget);
  // WidgetObserver가 제공하지 않는 capture 변경은 fixture의 실제 입력 완료나
  // capture-lost callback에서 전달한다. HasCapture()의 현재값만 다시 읽는다.
  void NotifyCaptureChanged(views::Widget& widget);
  AppliedOwnerToken ObserveScrollView(views::ScrollView& view);
  AppliedOwnerToken ObservePageHosts(views::View& clip_host,
                                     uint64_t pane_id,
                                     uint64_t tab_handle);
  AppliedOwnerToken ObservePreview(views::View& preview,
                                   views::View& source,
                                   uint64_t source_handle,
                                   uint64_t tab_handle);
  AppliedOwnerToken ObserveCapsule(views::View& decoration, uint64_t group_id);
  // manager에는 공개 progress 값이 없어 [0, 1]의 running/idle 조건만 지원한다.
  AppliedOwnerToken ObserveLayoutAnimation(
      views::View& host,
      views::AnimatingLayoutManager& manager,
      uint64_t group_id);
  AppliedOwnerToken ObserveNativeWindow(views::Widget& widget);
  // 관찰 중인 host의 layout manager를 교체·제거하기 직전에 호출한다.
  // Chromium에는 교체 전 알림이 없다. 이전 manager가 살아 있을 때 observer를
  // 해제하고 그 owner를 폐기한 뒤 대기를 깨운다.
  void WillChangeLayoutManager(views::View& host);

  // dependencies의 새 적용을 확인한 뒤 성공 presentation feedback을 요청한다.
  // 반환값의 sample identity는 이 등록에만 유효하다.
  AppliedCondition ObservePresentedAfterApply(views::View& view,
                                              AppliedConditions dependencies);

  bool Supports(const AppliedCondition& condition) const override;
  bool IsOwnerAlive(AppliedOwnerToken owner) const override;
  std::optional<AppliedSnapshot> Read(
      const AppliedSignalKey& key) const override;
  base::CallbackListSubscription Subscribe(
      const std::vector<AppliedSignalKey>& keys,
      Observer observer) override;

 private:
  friend class AppliedBrowserObservers;
  friend class DragAppliedObserver;
  using Reader = base::RepeatingCallback<std::optional<AppliedSnapshot>()>;
  void RegisterSignal(AppliedOwnerToken owner,
                      AppliedConditionKind kind,
                      Reader reader);
  void Notify(const std::vector<AppliedSnapshot>& captured = {});
  void OwnerDestroyed(AppliedOwnerToken owner);
  void TieToView(AppliedOwnerToken owner, views::View& view);
  // host가 manager를 교체·제거하면 owner를 폐기한다. Chromium에 교체 전 알림이
  // 없어 생존 조회와 다음 깨움에서 확인한다.
  void TieToLayoutManager(AppliedOwnerToken owner,
                          views::View& host,
                          const views::LayoutManager& manager);
  base::CallbackListSubscription SubscribeWake(base::RepeatingClosure callback);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_VIEWS_OBSERVERS_H_
