// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_browser_observers.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/animation/browser_animation_controller.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/contents_container_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_resize_area.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_collection_animating_layout_manager.h"
#include "chrome/browser/ui/views/yee/browser_surface_presentation_binding.h"
#include "chrome/browser/ui/views/yee/browser_surface_transition.h"
#include "chrome/browser/ui/views/yee/horizontal_tab_presentation.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/browser/ui/views/yee/yee_ui.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_metadata_provider.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace yee::test {
namespace {

AppliedSnapshot Snapshot(AppliedOwnerToken owner,
                         AppliedConditionValue value,
                         uint64_t serial = 0,
                         AppliedCoordinates coordinates = {}) {
  return {.owner = owner,
          .value = std::move(value),
          .serials = {.apply_serial = serial},
          .coordinates = coordinates};
}

AppliedAxis Axis(split_tabs::SplitTabLayout layout) {
  return layout == split_tabs::SplitTabLayout::kStacked
             ? AppliedAxis::kVertical
             : AppliedAxis::kHorizontal;
}

SurfaceEpochCommitted::Destination Destination(
    SurfaceGeometryStableState state) {
  switch (state) {
    case SurfaceGeometryStableState::kWindowed:
      return SurfaceEpochCommitted::Destination::kWindowed;
    case SurfaceGeometryStableState::kFullscreenHiddenChrome:
      return SurfaceEpochCommitted::Destination::kFullscreen;
    case SurfaceGeometryStableState::kFullscreenWithToolbar:
      return SurfaceEpochCommitted::Destination::kImmersive;
  }
}

}  // namespace

class AppliedBrowserObservers::Impl : public BrowserCollectionObserver {
 public:
  Impl() {
    collection.Observe(GlobalBrowserCollection::GetInstance());
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          AddBrowser(*browser);
          return true;
        });
    wake = signals.SubscribeWake(
        base::BindRepeating(&Impl::OnNativeWake, weak.GetWeakPtr()));
  }
  ~Impl() override = default;

  struct Lifetime : views::ViewObserver {
    Lifetime(Impl& owner, views::View& view) : owner(owner), view(&view) {
      owner.signals.ObserveView(view);
      observation.Observe(&view);
    }
    void Own(AppliedOwnerToken token) {
      tokens.push_back(token);
      if (view.view())
        owner->signals.TieToView(token, *view.view());
    }
    void OnViewIsDeleting(views::View*) override {
      observation.Reset();
      view.SetView(nullptr);
      subscriptions.clear();
      for (auto token : tokens) {
        owner->signals.OwnerDestroyed(token);
      }
    }
    raw_ref<Impl> owner;
    views::ViewTracker view;
    std::vector<AppliedOwnerToken> tokens;
    std::vector<base::CallbackListSubscription> subscriptions;
    base::ScopedObservation<views::View, views::ViewObserver> observation{this};
  };

  struct ModelWatch : TabStripModelObserver {
    ModelWatch(Impl& owner, TabStripModel& model)
        : owner(owner), model(&model) {
      model.AddObserver(this);
    }
    void Changed() {
      ++serial;
      owner->BindBrowsers();
      owner->RetireChangedPanes();
      owner->signals.Notify();
      owner->ScheduleBindings();
    }
    void OnTabStripModelChanged(TabStripModel*,
                                const TabStripModelChange&,
                                const TabStripSelectionChange&) override {
      Changed();
    }
    void OnTabPinnedStateChanged(tabs::TabInterface*, int) override {
      Changed();
    }
    void OnTabGroupChanged(const TabGroupChange&) override { Changed(); }
    void TabGroupedStateChanged(TabStripModel*,
                                std::optional<tab_groups::TabGroupId>,
                                std::optional<tab_groups::TabGroupId>,
                                tabs::TabInterface*,
                                int) override {
      Changed();
    }
    void OnSplitTabChanged(const SplitTabChange&) override { Changed(); }
    void OnTabStripModelDestroyed(TabStripModel*) override {
      model = nullptr;
      owner->signals.OwnerDestroyed(token);
      owner->RetireChangedPanes();
    }
    raw_ref<Impl> owner;
    raw_ptr<TabStripModel> model;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
  };

  struct PaneBinding {
    raw_ptr<ModelWatch> model;
    raw_ptr<Lifetime> lifetime;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    AppliedOwnerToken bound_owner;
    uint64_t pane_id = 0;
    uint64_t handle = 0;
    bool bound = false;
    uint64_t bound_baseline = 0;
    std::map<AppliedOwnerToken, uint64_t> baselines;
    std::unique_ptr<views::ViewTracker> container;
    std::optional<split_tabs::SplitTabId> split;
    base::CallbackListSubscription contents_subscription;
  };

  struct RendererWatch : content::WebContentsObserver,
                         content::RenderWidgetHostObserver {
    struct Metadata : content::RenderFrameSubmissionObserver {
      Metadata(RendererWatch& owner, content::RenderFrameHost& frame)
          : content::RenderFrameSubmissionObserver(&frame), owner(owner) {}
      void OnRenderFrameMetadataChangedBeforeActivation(
          const cc::RenderFrameMetadata&) override {}
      void OnRenderFrameMetadataChangedAfterActivation(
          base::TimeTicks) override {
        owner->Changed(LastRenderFrameMetadata());
      }
      void OnRenderFrameSubmission() override {}
      void OnLocalSurfaceIdChanged(const cc::RenderFrameMetadata&) override {}
      raw_ref<RendererWatch> owner;
    };
    RendererWatch(Impl& owner,
                  content::WebContents& contents,
                  std::optional<gfx::Size> target = std::nullopt,
                  double scale = 1,
                  bool next_document = false)
        : content::WebContentsObserver(&contents),
          owner(owner),
          target(target),
          target_scale(scale),
          awaiting_document(next_document),
          next_document(next_document) {
      Attach(*contents.GetPrimaryMainFrame());
    }
    ~RendererWatch() override = default;
    void Attach(content::RenderFrameHost& host) {
      metadata_observer.reset();
      host_observation.Reset();
      document = host.GetWeakDocumentPtr();
      frame_id = host.GetGlobalId();
      host_observation.Observe(host.GetRenderWidgetHost());
      metadata_observer = std::make_unique<Metadata>(*this, host);
      metadata = metadata_observer->LastRenderFrameMetadata();
      baseline_surface = metadata.local_surface_id;
      if (!target)
        UpdateSurface();
    }
    void Destroy() {
      metadata_observer.reset();
      host_observation.Reset();
      alive = false;
      owner->signals.OwnerDestroyed(token);
    }
    void PrimaryPageChanged(content::Page&) override {
      if (awaiting_document) {
        awaiting_document = false;
        Attach(*web_contents()->GetPrimaryMainFrame());
      } else {
        Destroy();
      }
    }
    void WebContentsDestroyed() override { Destroy(); }
    void RenderFrameDeleted(content::RenderFrameHost* host) override {
      if (host->GetGlobalId() == frame_id && !awaiting_document)
        Destroy();
    }
    void RenderWidgetHostDestroyed(content::RenderWidgetHost*) override {
      if (awaiting_document) {
        metadata_observer.reset();
        host_observation.Reset();
      } else {
        Destroy();
      }
    }
    void UpdateSurface() {
      if (metadata.local_surface_id != surface_id) {
        surface_id = metadata.local_surface_id;
        surface = AppliedOwnerToken::Create();
      }
    }
    void Changed(const cc::RenderFrameMetadata& value) {
      if (!alive || awaiting_document)
        return;
      metadata = value;
      if (target) {
        if (!bound_surface) {
          if (!metadata.local_surface_id ||
              (!next_document &&
               metadata.local_surface_id == baseline_surface) ||
              metadata.viewport_size_in_pixels != *target ||
              metadata.device_scale_factor != target_scale)
            return;
          bound_surface = metadata.local_surface_id;
        } else if (metadata.local_surface_id != bound_surface) {
          // 이미 결합한 surface를 다른 resize의 표본으로 승계하지 않는다.
          Destroy();
          return;
        }
      } else {
        UpdateSurface();
      }
      ++serial;
      owner->signals.Notify();
    }
    RendererViewportMatches Value(gfx::Size pixels, double scale) const {
      return {contents, frame, document_token, surface, pixels, scale};
    }
    std::optional<AppliedSnapshot> Read() const {
      if (!alive || awaiting_document || !document.AsRenderFrameHostIfValid() ||
          !metadata.local_surface_id || (target && !bound_surface))
        return std::nullopt;
      auto result = Snapshot(
          token,
          Value(metadata.viewport_size_in_pixels, metadata.device_scale_factor),
          serial);
      result.serials.native_generation =
          (uint64_t{metadata.local_surface_id->parent_sequence_number()}
           << 32) |
          metadata.local_surface_id->child_sequence_number();
      result.coordinates.units = AppliedCoordinates::Units::kPixel;
      result.coordinates.device_scale_factor = metadata.device_scale_factor;
      result.coordinates.rtl = base::i18n::IsRTL();
      return result;
    }
    raw_ref<Impl> owner;
    content::WeakDocumentPtr document;
    content::GlobalRenderFrameHostId frame_id;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    AppliedOwnerToken contents = AppliedOwnerToken::Create();
    AppliedOwnerToken frame = AppliedOwnerToken::Create();
    AppliedOwnerToken document_token = AppliedOwnerToken::Create();
    AppliedOwnerToken surface = AppliedOwnerToken::Create();
    cc::RenderFrameMetadata metadata;
    std::optional<gfx::Size> target;
    double target_scale;
    std::optional<viz::LocalSurfaceId> baseline_surface;
    std::optional<viz::LocalSurfaceId> bound_surface;
    std::optional<viz::LocalSurfaceId> surface_id;
    uint64_t serial = 0;
    bool alive = true;
    bool awaiting_document;
    bool next_document;
    std::unique_ptr<Metadata> metadata_observer;
    base::ScopedObservation<content::RenderWidgetHost,
                            content::RenderWidgetHostObserver>
        host_observation{this};
  };

  struct DomWatch : content::WebContentsObserver {
    DomWatch(Impl& owner,
             content::RenderFrameHost& frame,
             base::OnceCallback<void(bool)> ready)
        : content::WebContentsObserver(
              content::WebContents::FromRenderFrameHost(&frame)),
          owner(owner),
          document(frame.GetWeakDocumentPtr()),
          ready(std::move(ready)) {
      value.document = token;
    }
    ~DomWatch() override {
      if (auto* frame = document.AsRenderFrameHostIfValid()) {
        content::ExecuteScriptAsync(
            frame, content::JsReplace("window[$1]?.stop()", Key()));
      }
    }
    std::string Key() const { return "yee-applied-" + token.ToString(); }
    void Destroy() {
      installed = false;
      owner->signals.OwnerDestroyed(token);
      if (ready) {
        std::move(ready).Run(false);
      }
    }
    void PrimaryPageChanged(content::Page&) override { Destroy(); }
    void WebContentsDestroyed() override { Destroy(); }
    void RenderFrameDeleted(content::RenderFrameHost* frame) override {
      if (!document.AsRenderFrameHostIfValid() ||
          document.AsRenderFrameHostIfValid() == frame) {
        Destroy();
      }
    }
    void DomOperationResponse(content::RenderFrameHost* frame,
                              const std::string& json) override {
      if (document.AsRenderFrameHostIfValid() != frame) {
        return;
      }
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_string()) {
        parsed =
            base::JSONReader::Read(parsed->GetString(), base::JSON_PARSE_RFC);
      }
      if (!parsed || !parsed->is_dict()) {
        return;
      }
      const auto& data = parsed->GetDict();
      const auto* key = data.FindString("yeeWait");
      if (!key || *key != Key()) {
        return;
      }
      const auto* rows = data.FindList("rows");
      const auto x = data.FindDouble("x");
      const auto y = data.FindDouble("y");
      const auto width = data.FindDouble("width");
      const auto height = data.FindDouble("height");
      const auto opacity = data.FindDouble("opacity");
      const auto selected = data.FindInt("selected");
      const auto focused = data.FindBool("focused");
      const auto scale = data.FindDouble("scale");
      const auto rtl = data.FindBool("rtl");
      if (!rows || !x || !y || !width || !height || !opacity || !selected ||
          !focused || !scale || *scale <= 0 || !rtl) {
        Destroy();
        return;
      }
      value.popup_rows.clear();
      for (const auto& row : *rows) {
        if (!row.is_int() || row.GetInt() <= 0) {
          Destroy();
          return;
        }
        value.popup_rows.push_back(row.GetInt());
      }
      value.selected_index = *selected;
      value.rect = gfx::RectF(*x, *y, *width, *height);
      value.opacity = *opacity;
      value.focused = *focused;
      coordinates.device_scale_factor = *scale;
      coordinates.rtl = *rtl;
      installed = true;
      ++serial;
      owner->signals.Notify();
      if (ready) {
        std::move(ready).Run(true);
      }
    }
    raw_ref<Impl> owner;
    content::WeakDocumentPtr document;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    WebUiStateMatches value;
    AppliedCoordinates coordinates;
    uint64_t serial = 0;
    bool installed = false;
    base::OnceCallback<void(bool)> ready;
  };

  Lifetime& Life(views::View& view) {
    auto life = std::make_unique<Lifetime>(*this, view);
    auto* ptr = life.get();
    lifetimes.push_back(std::move(life));
    return *ptr;
  }
  ModelWatch& Model(TabStripModel& model) {
    for (const auto& watch : models) {
      if (watch->model == &model) {
        return *watch;
      }
    }
    auto watch = std::make_unique<ModelWatch>(*this, model);
    auto* ptr = watch.get();
    models.push_back(std::move(watch));
    signals.RegisterSignal(
        ptr->token, AppliedConditionKind::kTabModelMatches,
        base::BindRepeating(&Impl::ReadModel, base::Unretained(this),
                            base::Unretained(ptr)));
    return *ptr;
  }
  uint64_t Split(const split_tabs::SplitTabId& id) {
    auto [it, inserted] = splits.try_emplace(id);
    if (inserted) {
      it->second = splits.size();
    }
    return it->second;
  }
  std::optional<AppliedSnapshot> ReadModel(ModelWatch* watch) {
    if (!watch->model) {
      return std::nullopt;
    }
    TabModelMatches value;
    for (int i = 0; i < watch->model->count(); ++i) {
      const auto* tab = watch->model->GetTabAtIndex(i);
      value.tabs.push_back(
          {static_cast<uint64_t>(tab->GetHandle().raw_value()), tab->IsPinned(),
           tab->GetGroup() ? *AppliedOwnerToken::Deserialize(
                                 tab->GetGroup()->token().high(),
                                 tab->GetGroup()->token().low())
                           : AppliedOwnerToken{},
           tab->GetSplit() ? Split(*tab->GetSplit()) : 0});
    }
    if (auto* active = watch->model->GetActiveTab()) {
      value.active_tab = active->GetHandle().raw_value();
    }
    for (const auto& id : watch->model->ListSplits()) {
      const auto* data = watch->model->GetSplitData(id);
      AppliedSplitState split{Split(id),
                              Axis(data->visual_data()->split_layout()),
                              data->visual_data()->split_ratio()};
      for (const auto* tab : data->ListTabs()) {
        split.tab_handles.push_back(tab->GetHandle().raw_value());
      }
      value.splits.push_back(std::move(split));
    }
    return Snapshot(watch->token, value, watch->serial);
  }
  struct BrowserBinding {
    uint64_t tab_handle;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    std::set<AppliedOwnerToken> existing;
    bool bound = false;
  };
  AppliedOwnerToken BrowserIdentity(AppliedOwnerToken canonical) const {
    const auto alias = browser_aliases.find(canonical);
    return alias == browser_aliases.end() ? canonical : alias->second;
  }
  void BindBrowsers() {
    for (auto& binding : browser_bindings) {
      if (binding.bound)
        continue;
      for (const auto& [browser, canonical] : browsers) {
        if (binding.existing.contains(canonical) ||
            browser_aliases.contains(canonical))
          continue;
        auto* model = browser->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          if (static_cast<uint64_t>(
                  model->GetTabAtIndex(i)->GetHandle().raw_value()) ==
              binding.tab_handle) {
            browser_aliases[canonical] = binding.token;
            binding.bound = true;
            break;
          }
        }
        if (binding.bound)
          break;
      }
    }
  }
  void AddBrowser(BrowserWindowInterface& browser) {
    browsers[&browser] = AppliedOwnerToken::Create();
    Model(*browser.GetTabStripModel());
  }
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    AddBrowser(*browser);
    created.push_back(browsers.at(browser));
    BindBrowsers();
    ++browser_serial;
    signals.Notify();
  }
  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    if (const auto found = browsers.find(browser); found != browsers.end()) {
      closed.push_back(found->second);
      signals.OwnerDestroyed(found->second);
      signals.OwnerDestroyed(BrowserIdentity(found->second));
      browsers.erase(found);
    }
    ++browser_serial;
    signals.Notify();
  }
  std::optional<AppliedSnapshot> ReadBrowsers() {
    BrowserSetMatches value;
    for (auto token : created)
      value.created.push_back(BrowserIdentity(token));
    for (auto token : closed)
      value.closed.push_back(BrowserIdentity(token));
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          AppliedBrowserState state{.browser =
                                        BrowserIdentity(browsers.at(browser))};
          auto* model = browser->GetTabStripModel();
          for (int i = 0; i < model->count(); ++i) {
            state.tab_handles.push_back(
                model->GetTabAtIndex(i)->GetHandle().raw_value());
          }
          value.browsers.push_back(std::move(state));
          return true;
        });
    return Snapshot(browser_set, value, browser_serial);
  }
  bool PaneIsCurrent(const PaneBinding& binding) const {
    if (!binding.model->model || !binding.lifetime->view.view())
      return false;
    if (!binding.bound)
      return true;
    auto* container =
        static_cast<ContentsContainerView*>(binding.container->view());
    auto* contents =
        container ? container->contents_view()->web_contents() : nullptr;
    const auto* tab =
        contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
    if (!tab ||
        static_cast<uint64_t>(tab->GetHandle().raw_value()) != binding.handle ||
        tab->GetSplit() != binding.split ||
        !signals.IsOwnerAlive(binding.bound_owner))
      return false;
    auto* outlet = container->GetYeeBrowserSurfacePresentationBinding();
    return !outlet || outlet->web_contents() == contents;
  }
  void RetireChangedPanes() {
    for (const auto& binding : panes) {
      if (signals.IsOwnerAlive(binding->token) && !PaneIsCurrent(*binding)) {
        binding->contents_subscription = {};
        signals.OwnerDestroyed(binding->token);
      }
    }
  }
  void OnNativeWake() {
    RetireChangedPanes();
    ScheduleBindings();
  }
  void ScheduleBindings() {
    if (binding_queued) {
      return;
    }
    binding_queued = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Impl::BindPanes, weak.GetWeakPtr()));
  }
  void BindPanes() {
    binding_queued = false;
    bool changed = false;
    for (const auto& binding : panes) {
      if (binding->bound || !signals.IsOwnerAlive(binding->token) ||
          !binding->model->model || !binding->lifetime->view.view()) {
        continue;
      }
      auto* view =
          static_cast<MultiContentsView*>(binding->lifetime->view.view());
      for (const auto& container : view->contents_container_views()) {
        auto* contents = container->contents_view()->web_contents();
        const auto* tab =
            contents ? tabs::TabInterface::GetFromContents(contents) : nullptr;
        if (!tab || static_cast<uint64_t>(tab->GetHandle().raw_value()) !=
                        binding->handle) {
          continue;
        }
        auto* clip = container->GetViewByID(kPageViewportClipHostViewId);
        if (!clip || !PageViewportAppliedState::From(*clip)) {
          continue;
        }
        binding->bound_owner =
            signals.ObservePageHosts(*clip, binding->pane_id, binding->handle);
        if (const auto baseline = binding->baselines.find(binding->bound_owner);
            baseline != binding->baselines.end())
          binding->bound_baseline = baseline->second;
        binding->container =
            std::make_unique<views::ViewTracker>(container.get());
        binding->bound = true;
        binding->split = tab->GetSplit();
        if (auto* outlet =
                container->GetYeeBrowserSurfacePresentationBinding()) {
          binding->contents_subscription =
              outlet->AddPresentationChangedCallback(base::BindRepeating(
                  &Impl::RetireChangedPanes, weak.GetWeakPtr()));
        }
        changed = true;
        break;
      }
    }
    if (changed) {
      signals.Notify();
    }
  }

  AppliedViewsObservers signals;
  std::map<BrowserWindowInterface*, AppliedOwnerToken> browsers;
  std::map<AppliedOwnerToken, AppliedOwnerToken> browser_aliases;
  std::vector<BrowserBinding> browser_bindings;
  std::map<split_tabs::SplitTabId, uint64_t> splits;
  std::vector<AppliedOwnerToken> created;
  std::vector<AppliedOwnerToken> closed;
  AppliedOwnerToken browser_set = AppliedOwnerToken::Create();
  uint64_t browser_serial = 0;
  bool browser_set_registered = false;
  bool binding_queued = false;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      collection{this};
  std::vector<std::unique_ptr<ModelWatch>> models;
  std::vector<std::unique_ptr<Lifetime>> lifetimes;
  std::vector<std::unique_ptr<PaneBinding>> panes;
  std::vector<std::unique_ptr<RendererWatch>> renderers;
  std::vector<std::unique_ptr<DomWatch>> doms;
  base::CallbackListSubscription wake;
  base::WeakPtrFactory<Impl> weak{this};
};

AppliedBrowserObservers::AppliedBrowserObservers()
    : impl_(std::make_unique<Impl>()) {}
AppliedBrowserObservers::~AppliedBrowserObservers() = default;
AppliedViewsObservers& AppliedBrowserObservers::views() {
  return impl_->signals;
}
AppliedOwnerToken AppliedBrowserObservers::BrowserToken(
    BrowserWindowInterface& browser) {
  return impl_->BrowserIdentity(impl_->browsers.at(&browser));
}
uint64_t AppliedBrowserObservers::SplitId(const TabStripModel& model,
                                          int tab_index) {
  const auto split = model.GetSplitForTab(tab_index);
  return split ? impl_->Split(*split) : 0;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveTabModel(
    TabStripModel& model) {
  return impl_->Model(model).token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveBrowserSet() {
  if (!impl_->browser_set_registered) {
    impl_->browser_set_registered = true;
    impl_->signals.RegisterSignal(
        impl_->browser_set, AppliedConditionKind::kBrowserSetMatches,
        base::BindRepeating(&Impl::ReadBrowsers,
                            base::Unretained(impl_.get())));
  }
  return impl_->browser_set;
}
AppliedOwnerToken AppliedBrowserObservers::ExpectBrowserForTab(
    uint64_t handle) {
  Impl::BrowserBinding binding{.tab_handle = handle};
  for (const auto& [browser, canonical] : impl_->browsers)
    binding.existing.insert(canonical);
  const auto token = binding.token;
  impl_->browser_bindings.push_back(std::move(binding));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObservePaneLayout(
    BrowserWindowInterface& browser,
    MultiContentsView& panes) {
  auto& model = impl_->Model(*browser.GetTabStripModel());
  auto& lifetime = impl_->Life(panes);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kPaneLayoutMatches,
      base::BindRepeating(
          [](Impl* impl, Impl::Lifetime* life, Impl::ModelWatch* watch,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            auto* view = static_cast<MultiContentsView*>(life->view.view());
            if (!view || !watch->model) {
              return std::nullopt;
            }
            PaneLayoutMatches value{
                .axis = Axis(view->GetSplitLayout()),
                .parent_extent = view->size(),
                .ratio = view->GetSplitRatio(),
                .insets = view->split_view_insets_for_testing(),
                .divider = view->resize_area_for_testing()->bounds()};
            if (const auto* active = watch->model->GetActiveTab();
                active && active->GetSplit()) {
              value.split_id = impl->Split(*active->GetSplit());
            }
            for (const auto& container : view->contents_container_views()) {
              if (container->GetVisible() &&
                  container->contents_view()->web_contents()) {
                value.tab_handles.push_back(
                    tabs::TabInterface::GetFromContents(
                        container->contents_view()->web_contents())
                        ->GetHandle()
                        .raw_value());
              }
            }
            return Snapshot(token, value, watch->serial,
                            GetAppliedViewCoordinates(*view));
          },
          base::Unretained(impl_.get()), base::Unretained(&lifetime),
          base::Unretained(&model), token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ExpectPageHosts(
    BrowserWindowInterface& browser,
    MultiContentsView& panes,
    uint64_t pane_id,
    uint64_t tab_handle) {
  auto binding = std::make_unique<Impl::PaneBinding>();
  auto* ptr = binding.get();
  ptr->model = &impl_->Model(*browser.GetTabStripModel());
  ptr->lifetime = &impl_->Life(panes);
  ptr->lifetime->Own(ptr->token);
  ptr->pane_id = pane_id;
  ptr->handle = tab_handle;
  for (const auto& container : panes.contents_container_views()) {
    if (auto* clip = container->GetViewByID(kPageViewportClipHostViewId)) {
      if (auto* state = PageViewportAppliedState::From(*clip)) {
        const auto value = state->GetSnapshot();
        ptr->baselines[value.owner] = value.apply_serial;
      }
    }
  }
  impl_->panes.push_back(std::move(binding));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kPageHostsApplied,
      base::BindRepeating(
          [](Impl* impl,
             Impl::PaneBinding* binding) -> std::optional<AppliedSnapshot> {
            if (!binding->bound || !impl->PaneIsCurrent(*binding)) {
              return std::nullopt;
            }
            auto snapshot =
                impl->signals.Read({binding->bound_owner,
                                    AppliedConditionKind::kPageHostsApplied});
            if (snapshot &&
                snapshot->serials.apply_serial <= binding->bound_baseline)
              return std::nullopt;
            if (snapshot) {
              snapshot->owner = binding->token;
              auto& value = std::get<PageHostsApplied>(snapshot->value);
              value.pane_id = binding->pane_id;
              value.tab_handle = binding->handle;
            }
            return snapshot;
          },
          base::Unretained(impl_.get()), base::Unretained(ptr)));
  impl_->BindPanes();
  return ptr->token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveSurfaceEpoch(
    views::View& view) {
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  if (view.GetWidget()) {
    impl_->signals.ObserveWidget(*view.GetWidget());
    impl_->signals.ObserveNativeWindow(*view.GetWidget());
  }
  if (!GetSurfaceGeometryTransition(view)) {
    return token;
  }
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kSurfaceEpochCommitted,
      base::BindRepeating(
          [](Impl::Lifetime* life,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!life->view.view()) {
              return std::nullopt;
            }
            const auto* epoch =
                GetSurfaceGeometryTransition(*life->view.view());
            if (!epoch) {
              return std::nullopt;
            }
            auto result = Snapshot(
                token,
                SurfaceEpochCommitted{
                    epoch->generation,
                    epoch->phase == SurfaceGeometryTransitionPhase::kStable
                        ? SurfaceEpochCommitted::Phase::kStable
                        : SurfaceEpochCommitted::Phase::kTransitioning,
                    epoch->committed_generation,
                    Destination(epoch->destination.stable_state)},
                epoch->committed_generation);
            result.coordinates = GetAppliedViewCoordinates(*life->view.view());
            result.request_epoch = epoch->generation;
            result.serials.native_generation = epoch->generation;
            return result;
          },
          base::Unretained(&lifetime), token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveBrowserAnimation(
    views::View& view,
    BrowserAnimationController& controller,
    BrowserAnimationGroup group,
    BrowserAnimationSequence sequence,
    uint64_t group_id) {
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  auto serial = std::make_shared<uint64_t>(0);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, BrowserAnimationController* controller,
             BrowserAnimationGroup group, BrowserAnimationSequence sequence,
             uint64_t id, AppliedOwnerToken token,
             std::shared_ptr<uint64_t> serial)
              -> std::optional<AppliedSnapshot> {
            if (!life->view.view()) {
              return std::nullopt;
            }
            const auto progress = controller->GetCurrentValue(group, sequence);
            if (!progress) {
              return std::nullopt;
            }
            return Snapshot(token,
                            AnimationMatches{id, controller->IsAnimating(group),
                                             *progress, *progress},
                            *serial,
                            GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(&lifetime), base::Unretained(&controller), group,
          sequence, group_id, token, serial));
  lifetime.subscriptions.push_back(controller.Subscribe(
      group, base::BindRepeating(
                 [](Impl* impl, std::shared_ptr<uint64_t> serial,
                    const BrowserAnimationController*, BrowserAnimationUpdate) {
                   ++*serial;
                   impl->signals.Notify();
                 },
                 base::Unretained(impl_.get()), serial)));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveUnifiedAnimation(
    views::View& view,
    TabCollectionAnimatingLayoutManager& manager,
    uint64_t group_id) {
  CHECK_EQ(view.GetLayoutManager(), &manager);
  auto& lifetime = impl_->Life(view);
  const auto token = AppliedOwnerToken::Create();
  const auto host_token = impl_->signals.ObserveView(view);
  lifetime.Own(token);
  impl_->signals.TieToLayoutManager(token, view, manager);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](Impl* impl, AppliedOwnerToken host_token, Impl::Lifetime* life,
             uint64_t group_id,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            // TieToLayoutManager가 교체된 manager의 읽기를 먼저 막는다.
            if (!life->view.view() || !impl->signals.IsOwnerAlive(token)) {
              return std::nullopt;
            }
            auto* manager = static_cast<TabCollectionAnimatingLayoutManager*>(
                life->view.view()->GetLayoutManager());
            const bool running = manager->is_animating();
            const double progress =
                running ? manager->animation_coordinator().GetCurrentProgress()
                        : 1.0;
            const auto host = impl->signals.Read(
                {host_token, AppliedConditionKind::kViewStateMatches});
            return Snapshot(
                token, AnimationMatches{group_id, running, progress, progress},
                host ? host->serials.apply_serial : 0,
                GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(impl_.get()), host_token,
          base::Unretained(&lifetime), group_id, token));
  return token;
}
AppliedOwnerToken AppliedBrowserObservers::ObserveRendererViewport(
    content::WebContents& contents) {
  auto watch = std::make_unique<Impl::RendererWatch>(*impl_, contents);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kRendererViewportMatches,
      base::BindRepeating(&Impl::RendererWatch::Read, base::Unretained(ptr)));
  impl_->renderers.push_back(std::move(watch));
  return ptr->token;
}
AppliedCondition AppliedBrowserObservers::ExpectRendererViewport(
    content::WebContents& contents,
    gfx::Size pixels,
    double scale,
    bool next_document) {
  CHECK_GT(scale, 0);
  auto watch = std::make_unique<Impl::RendererWatch>(*impl_, contents, pixels,
                                                     scale, next_document);
  auto* ptr = watch.get();
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kRendererViewportMatches,
      base::BindRepeating(&Impl::RendererWatch::Read, base::Unretained(ptr)));
  impl_->renderers.push_back(std::move(watch));
  AppliedCondition condition{ptr->token, ptr->Value(pixels, scale)};
  condition.coordinates.units = AppliedCoordinates::Units::kPixel;
  condition.coordinates.device_scale_factor = scale;
  condition.coordinates.rtl = base::i18n::IsRTL();
  return condition;
}
AppliedOwnerToken AppliedBrowserObservers::InstallWebUiObserver(
    content::RenderFrameHost& frame,
    WebUiAppliedSelectors selectors,
    base::OnceCallback<void(bool)> ready) {
  auto watch =
      std::make_unique<Impl::DomWatch>(*impl_, frame, std::move(ready));
  auto* ptr = watch.get();
  impl_->doms.push_back(std::move(watch));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kWebUiStateMatches,
      base::BindRepeating(
          [](Impl::DomWatch* watch) -> std::optional<AppliedSnapshot> {
            if (!watch->installed ||
                !watch->document.AsRenderFrameHostIfValid()) {
              return std::nullopt;
            }
            return Snapshot(watch->token, watch->value, watch->serial,
                            watch->coordinates);
          },
          base::Unretained(ptr)));
  impl_->signals.RegisterSignal(
      ptr->token, AppliedConditionKind::kFocusMatches,
      base::BindRepeating(
          [](Impl::DomWatch* watch) -> std::optional<AppliedSnapshot> {
            if (!watch->installed ||
                !watch->document.AsRenderFrameHostIfValid()) {
              return std::nullopt;
            }
            return Snapshot(
                watch->token,
                FocusMatches{FocusMatches::Kind::kRendererDocument,
                             watch->token, watch->token, watch->value.focused},
                watch->serial, watch->coordinates);
          },
          base::Unretained(ptr)));
  base::ListValue shadow;
  for (const auto& selector : selectors.shadow_hosts) {
    shadow.Append(selector);
  }
  content::ExecuteScriptAsync(
      &frame, content::JsReplace(R"JS(
    (() => {
      const key = $1;
      let tree = document;
      for (const selector of $2) {
        tree = tree?.querySelector(selector)?.shadowRoot;
        if (!tree) {
          domAutomationController.send({yeeWait:key});
          return;
        }
      }
      const root = tree?.querySelector($3);
      if (!root) {
        domAutomationController.send({yeeWait:key});
        return;
      }
      let next = 0;
      const ids = new WeakMap();
      let queued = false, stopped = false;
      const send = () => {
        queued = false;
        if (stopped) return;
        const rows = $4 ? Array.from(root.querySelectorAll($4)) : [];
        const rect = root.getBoundingClientRect();
        domAutomationController.send({yeeWait:key,
          rows: rows.map(row => {
            if (!ids.has(row)) ids.set(row, ++next);
            return ids.get(row);
          }),
          selected: rows.findIndex(row => $5 && row.matches($5)),
          x:rect.x, y:rect.y, width:rect.width, height:rect.height,
          opacity:Number(getComputedStyle(root).opacity), focused:document.hasFocus(),
          scale:window.devicePixelRatio, rtl:getComputedStyle(root).direction === 'rtl'});
      };
      const wake = () => {
        if (!queued && !stopped) { queued = true; queueMicrotask(send); }
      };
      const mutation = new MutationObserver(wake);
      mutation.observe(root, {subtree:true, attributes:true, childList:true});
      const resize = new ResizeObserver(wake);
      resize.observe(root);
      const events = ['focus','blur','focusin','focusout','transitionrun',
                      'transitionend','transitioncancel','animationstart',
                      'animationend','animationcancel'];
      for (const name of events) window.addEventListener(name, wake, true);
      window[key] = {stop() {
        stopped = true; mutation.disconnect(); resize.disconnect();
        for (const name of events) window.removeEventListener(name, wake, true);
        delete window[key];
      }};
      send();
    })();
  )JS",
                                 ptr->Key(), std::move(shadow), selectors.root,
                                 selectors.rows, selectors.selected));
  return ptr->token;
}
AppliedOwnerToken AppliedBrowserObservers::ObservePresentation(
    views::View& consumer,
    BrowserSurfacePresentationBinding& binding,
    views::View* horizontal_icon) {
  auto& lifetime = impl_->Life(consumer);
  auto* icon_life = horizontal_icon ? &impl_->Life(*horizontal_icon) : nullptr;
  const auto token = AppliedOwnerToken::Create();
  lifetime.Own(token);
  if (icon_life)
    icon_life->Own(token);
  impl_->signals.RegisterSignal(
      token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(
          [](Impl::Lifetime* life, Impl::Lifetime* icon_life,
             BrowserSurfacePresentationBinding* binding,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!life->view.view() || !binding->GetPresentation()) {
              return std::nullopt;
            }
            const auto& value = *binding->GetPresentation();
            auto* icon = icon_life ? icon_life->view.view() : nullptr;
            if (icon_life && !icon)
              return std::nullopt;
            const auto foreground =
                icon ? GetHorizontalTabIconForeground(*icon) : std::nullopt;
            const bool native =
                icon ? ShouldUseNativeHorizontalTabTheme(
                           *icon->GetNativeTheme(), icon->GetThemeProvider())
                     : value.palette_mode == BrowserSurfacePresentation::
                                                 PaletteMode::kNativeColors ||
                           ShouldUseNativeBrowserSurfaceColors(
                               *life->view.view()->GetNativeTheme());
            // icon_id 1은 Yee의 전경색 정책, 0은 native/default 정책이다.
            return Snapshot(token,
                            PresentationMatches{
                                value.source_id, value.revision, value.surface,
                                foreground.value_or(value.primary),
                                foreground && !native ? 1u : 0u, native},
                            value.revision,
                            GetAppliedViewCoordinates(*life->view.view()));
          },
          base::Unretained(&lifetime), base::Unretained(icon_life),
          base::Unretained(&binding), token));
  lifetime.subscriptions.push_back(
      binding.AddPresentationChangedCallback(base::BindRepeating(
          &AppliedViewsObservers::Notify, base::Unretained(&impl_->signals),
          std::vector<AppliedSnapshot>{})));
  return token;
}

bool AppliedBrowserObservers::Supports(
    const AppliedCondition& condition) const {
  for (const auto& dom : impl_->doms) {
    if (condition.owner == dom->token && !dom->installed) {
      return false;
    }
  }
  return impl_->signals.Supports(condition);
}
bool AppliedBrowserObservers::IsOwnerAlive(AppliedOwnerToken owner) const {
  if (!impl_->signals.IsOwnerAlive(owner))
    return false;
  for (const auto& binding : impl_->panes) {
    if (binding->token == owner)
      return impl_->PaneIsCurrent(*binding);
  }
  return true;
}
std::optional<AppliedSnapshot> AppliedBrowserObservers::Read(
    const AppliedSignalKey& key) const {
  if (!IsOwnerAlive(key.owner))
    return std::nullopt;
  return impl_->signals.Read(key);
}
base::CallbackListSubscription AppliedBrowserObservers::Subscribe(
    const std::vector<AppliedSignalKey>& keys,
    Observer observer) {
  return impl_->signals.Subscribe(keys, std::move(observer));
}

}  // namespace yee::test
