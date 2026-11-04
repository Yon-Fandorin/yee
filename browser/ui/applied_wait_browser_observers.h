// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_BROWSER_OBSERVERS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_BROWSER_OBSERVERS_H_

#include "chrome/browser/ui/views/yee/applied_wait_views_observers.h"

class BrowserWindowInterface;
class MultiContentsView;
class TabCollectionAnimatingLayoutManager;
class TabStripModel;
class BrowserAnimationController;
class BrowserAnimationGroup;
class BrowserAnimationSequence;
namespace content {
class RenderFrameHost;
class WebContents;
class RenderFrameMetadataProvider;
}  // namespace content
namespace yee {
class BrowserSurfacePresentationBinding;
}

namespace yee::test {

struct WebUiAppliedSelectors {
  // 각 selector 뒤의 open shadow root로 들어간다. 마지막 root 안에서 읽는다.
  std::vector<std::string> shadow_hosts;
  std::string root;
  std::string rows;
  std::string selected;
};

class AppliedBrowserObservers final : public AppliedSignalSource {
 public:
  AppliedBrowserObservers();
  ~AppliedBrowserObservers() override;
  AppliedBrowserObservers(const AppliedBrowserObservers&) = delete;
  AppliedBrowserObservers& operator=(const AppliedBrowserObservers&) = delete;

  AppliedViewsObservers& views();
  AppliedOwnerToken BrowserToken(BrowserWindowInterface& browser);
  uint64_t SplitId(const TabStripModel& model, int tab_index);
  AppliedOwnerToken ObserveTabModel(TabStripModel& model);
  AppliedOwnerToken ObserveBrowserSet();
  // 등록 이후 생성된 창 중 지정 tab을 소유한 창에만 한 번 bind한다.
  AppliedOwnerToken ExpectBrowserForTab(uint64_t tab_handle);
  AppliedOwnerToken ObservePaneLayout(BrowserWindowInterface& browser,
                                      MultiContentsView& panes);
  // 지정 tab을 가진 pane이 생길 때 한 번만 bind한다. 기존 pane의 교체는
  // 승계하지 않는다.
  AppliedOwnerToken ExpectPageHosts(BrowserWindowInterface& browser,
                                    MultiContentsView& panes,
                                    uint64_t pane_id,
                                    uint64_t tab_handle);
  AppliedOwnerToken ObserveSurfaceEpoch(views::View& browser_view);
  AppliedOwnerToken ObserveBrowserAnimation(
      views::View& lifetime,
      BrowserAnimationController& controller,
      BrowserAnimationGroup group,
      BrowserAnimationSequence sequence,
      uint64_t group_id);
  AppliedOwnerToken ObserveUnifiedAnimation(
      views::View& host,
      TabCollectionAnimatingLayoutManager& manager,
      uint64_t group_id);
  AppliedOwnerToken ObserveRendererViewport(content::WebContents& contents);
  // 미래 surface는 목표 viewport/scale의 첫 activation에 한 번 bind한다.
  // next_document이면 다음 primary document/RFH에도 같은 규칙을 적용한다.
  AppliedCondition ExpectRendererViewport(content::WebContents& contents,
                                          gfx::Size viewport_pixels,
                                          double scale,
                                          bool next_document = false);
  // ACK callback 이후에만 Begin/Dispatch한다. navigation은 기존 owner를
  // 폐기한다.
  AppliedOwnerToken InstallWebUiObserver(content::RenderFrameHost& frame,
                                         WebUiAppliedSelectors selectors,
                                         base::OnceCallback<void(bool)> ready);
  AppliedOwnerToken ObservePresentation(
      views::View& consumer,
      BrowserSurfacePresentationBinding& binding,
      views::View* horizontal_icon = nullptr);

  bool Supports(const AppliedCondition& condition) const override;
  bool IsOwnerAlive(AppliedOwnerToken owner) const override;
  std::optional<AppliedSnapshot> Read(
      const AppliedSignalKey& key) const override;
  base::CallbackListSubscription Subscribe(
      const std::vector<AppliedSignalKey>& keys,
      Observer observer) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_BROWSER_OBSERVERS_H_
