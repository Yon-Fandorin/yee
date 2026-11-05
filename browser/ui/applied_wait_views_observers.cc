// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_views_observers.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/views/yee/favorites.h"
#include "chrome/browser/ui/views/yee/native_window_test_support.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "components/viz/common/frame_timing_details.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/layer_observer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/animating_layout_manager.h"
#include "ui/views/layout/layout_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace yee::test {
namespace {

AppliedSnapshot Snapshot(AppliedOwnerToken owner,
                         AppliedConditionValue value,
                         uint64_t serial = 0,
                         uint64_t generation = 0,
                         AppliedCoordinates coordinates = {}) {
  return {.owner = owner,
          .value = std::move(value),
          .serials = {.apply_serial = serial, .native_generation = generation},
          .coordinates = coordinates};
}

bool SameObservedValue(const AppliedSnapshot& left,
                       const AppliedSnapshot& right) {
  return left.owner == right.owner && left.value == right.value &&
         left.coordinates == right.coordinates &&
         left.request_epoch == right.request_epoch &&
         left.serials.apply_serial == right.serials.apply_serial &&
         left.serials.native_generation == right.serials.native_generation;
}

}  // namespace

class AppliedViewsObservers::Impl {
 public:
  explicit Impl(AppliedViewsObservers& api) : api_(api) {}
  ~Impl() {
    releases.Clear();
    for (const auto& view : views) {
      view->deleting.clear();
    }
  }

  struct Signal {
    AppliedSignalKey key;
    Reader read;
    uint64_t observation = 0;
    bool full_progress_only = false;
    bool deferred = false;
    uint64_t delivered_serial = 0;
    std::optional<AppliedSnapshot> last;
    size_t subscribers = 0;
  };

  struct LayerWatch : ui::LayerObserver, ui::LayerAnimationObserver {
    explicit LayerWatch(Impl& owner) : owner(owner) {}
    ~LayerWatch() override { Release(); }
    void Bind(ui::Layer& value) {
      layer = &value;
      layer->AddObserver(this);
      layer->GetAnimator()->AddObserver(this);
    }
    void Release() {
      if (!layer)
        return;
      layer->GetAnimator()->RemoveObserver(this);
      layer->RemoveObserver(this);
      owner->layer_by_address.erase(layer);
      layer = nullptr;
    }
    void LayerDestroyed(ui::Layer*) override {
      Release();
      owner->Notify();
    }
    void OnLayerRecreated(ui::Layer*, ui::Layer* replacement) override {
      Release();
      if (replacement) {
        owner->layer_by_address[replacement] = this;
        Bind(*replacement);
      }
      owner->Notify();
    }
    void OnLayerAnimationStarted(ui::LayerAnimationSequence*) override {
      owner->Notify();
    }
    void OnLayerAnimationEnded(ui::LayerAnimationSequence*) override {
      owner->Notify();
    }
    void OnLayerAnimationAborted(ui::LayerAnimationSequence*) override {
      owner->Notify();
    }
    void OnLayerAnimationScheduled(ui::LayerAnimationSequence*) override {
      owner->Notify();
    }
    raw_ref<Impl> owner;
    raw_ptr<ui::Layer> layer = nullptr;
  };
  void WatchLayer(ui::Layer* layer) {
    if (!layer || layer_by_address.contains(layer))
      return;
    auto watch = std::make_unique<LayerWatch>(*this);
    auto* ptr = watch.get();
    layer_by_address[layer] = ptr;
    layers.push_back(std::move(watch));
    ptr->Bind(*layer);
  }

  struct ViewWatch : views::ViewObserver {
    ViewWatch(Impl& owner, views::View& view) : owner(owner), tracker(&view) {
      observation.Observe(&view);
    }
    ~ViewWatch() override = default;
    void Changed() {
      if (tracker.view())
        owner->WatchLayer(tracker.view()->layer());
      ++serial;
      owner->Notify();
    }
    void OnViewBoundsChanged(views::View*) override { Changed(); }
    void OnViewLayerBoundsSet(views::View*) override { Changed(); }
    void OnViewLayerClipRectChanged(views::View*) override { Changed(); }
    void OnViewVisibilityChanged(views::View*, views::View*, bool) override {
      Changed();
    }
    void OnViewLayoutInvalidated(views::View*) override { Changed(); }
    void OnViewPreferredSizeChanged(views::View*) override { Changed(); }
    void OnViewPropertyChanged(views::View*, const void*, int64_t) override {
      Changed();
    }
    void OnViewFocused(views::View*) override { Changed(); }
    void OnViewBlurred(views::View*) override { Changed(); }
    void OnViewThemeChanged(views::View*) override { Changed(); }
    void OnViewAddedToWidget(views::View* view) override {
      if (view->GetWidget())
        owner->WidgetFor(*view->GetWidget());
      Changed();
    }
    void OnViewRemovedFromWidget(views::View*) override { Changed(); }
    void OnViewHierarchyChanged(
        views::View* view,
        const views::ViewHierarchyChangedDetails&) override {
      if (view->parent()) {
        owner->ViewToken(*view->parent());
      }
      Changed();
    }
    void OnChildViewAdded(views::View*, views::View* child) override {
      owner->ViewToken(*child);
      Changed();
    }
    void OnChildViewRemoved(views::View*, views::View*) override { Changed(); }
    void OnChildViewReordered(views::View*, views::View*) override {
      Changed();
    }
    void Retire() {
      if (!tracker.view())
        return;
      for (auto& cleanup : deleting)
        std::move(cleanup).Run();
      deleting.clear();
      tracker.SetView(nullptr);
      for (auto owned_token : tokens)
        owner->Destroy(owned_token);
    }
    void OnViewHierarchyWillBeDeleted(views::View* view) override {
      owner->RetireSubtree(*view);
    }
    void OnViewIsDeleting(views::View* view) override {
      Retire();
      observation.Reset();
      owner->view_by_address.erase(view);
    }
    std::optional<AppliedSnapshot> ReadView() const {
      auto* view = tracker.view();
      if (!view) {
        return std::nullopt;
      }
      ViewStateMatches value{.drawn = view->IsDrawn(),
                             .visible = view->GetVisible(),
                             .bounds = view->bounds()};
      value.opacity = view->layer() ? view->layer()->opacity() : 1;
      value.hovered = view->IsMouseHovered();
      value.screen_bounds = view->GetBoundsInScreen();
      value.visible_bounds = view->GetVisibleBounds();
      value.contents_bounds_in_screen = view->GetContentsBounds();
      views::View::ConvertRectToScreen(view, &value.contents_bounds_in_screen);
      value.has_background = view->background() != nullptr;
      if (view->GetFocusManager())
        value.focus_change_reason =
            static_cast<int>(view->GetFocusManager()->focus_change_reason());
      if (!view->size().IsEmpty() && HorizontalTabGroupCapsuleState::From(
                                         *const_cast<views::View*>(view))) {
        // 같은 UI 표본 안에서 실제 paint 결과와 animation 진행값을 복사한다.
        gfx::Canvas canvas(view->size(), 1.f, false);
        PaintHorizontalTabGroupCapsule(canvas, *view, SK_ColorBLUE);
        const auto bitmap = canvas.GetBitmap();
        for (int y = 0; y < view->height(); ++y) {
          if (SkColorGetA(bitmap.getColor(view->width() / 2, y)))
            value.painted_bounds.Union(gfx::Rect(0, y, view->width(), 1));
        }
      }
      if (view->parent()) {
        const auto parent = owner->view_by_address.find(view->parent());
        if (parent == owner->view_by_address.end())
          return std::nullopt;
        value.parent = parent->second->token;
      }
      for (views::View* child : view->children()) {
        const auto found = owner->view_by_address.find(child);
        // hierarchy 알림은 OnChildViewAdded보다 먼저 올 수 있다.
        if (found == owner->view_by_address.end())
          return std::nullopt;
        value.children.push_back(found->second->token);
      }
      if (scroll) {
        const auto offset = gfx::ToRoundedPoint(scroll->CurrentOffset());
        value.scroll_offset = offset.OffsetFromOrigin();
      }
      return Snapshot(
          token, value, serial, 0,
          GetAppliedViewCoordinates(*view, AppliedCoordinates::Space::kParent));
    }
    raw_ref<Impl> owner;
    views::ViewTracker tracker;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    std::vector<AppliedOwnerToken> tokens{token};
    std::vector<base::OnceClosure> deleting;
    uint64_t serial = 0;
    raw_ptr<views::ScrollView> scroll = nullptr;
    base::CallbackListSubscription scroll_subscription;
    base::ScopedObservation<views::View, views::ViewObserver> observation{this};
  };

  struct WidgetWatch : views::WidgetObserver, ui::CompositorObserver {
    WidgetWatch(Impl& owner, views::Widget& widget)
        : owner(owner), widget(&widget) {
      observation.Observe(&widget);
      compositor = widget.GetCompositor();
      if (compositor) {
        compositor->AddObserver(this);
      }
    }
    ~WidgetWatch() override {
      if (compositor)
        compositor->RemoveObserver(this);
    }
    void OnCompositingDidCommit(ui::Compositor*) override {
      owner->Notify();
      committed.Notify();
    }
    void OnCompositingShuttingDown(ui::Compositor* dying) override {
      dying->RemoveObserver(this);
      owner->compositors.erase(dying);
      compositor = nullptr;
      owner->Notify();
    }
    void Changed() {
      ++serial;
      owner->Notify();
    }
    void OnWidgetVisibilityChanged(views::Widget*, bool) override { Changed(); }
    void OnWidgetVisibilityOnScreenChanged(views::Widget*, bool) override {
      Changed();
    }
    void OnWidgetActivationChanged(views::Widget*, bool) override { Changed(); }
    void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override {
      Changed();
    }
    void OnWidgetShowStateChanged(views::Widget*) override { Changed(); }
    void OnWidgetThemeChanged(views::Widget*) override { Changed(); }
    void OnWidgetUserDragEnded(views::Widget*) override { Changed(); }
    void OnWidgetDragDropCompleted(views::Widget*) override { Changed(); }
    void OnWidgetDestroying(views::Widget* dying) override {
      // macOS는 native 창을 먼저 닫고, OnWidgetDestroyed는 늦게 오거나
      // 오지 않을 수 있다. 파괴가 시작되면 바로 관찰을 끝내고 상태를 읽지
      // 않는다.
      destroying = true;
      if (dying->GetRootView())
        owner->RetireSubtree(*dying->GetRootView());
      Retire(dying);
    }
    void OnWidgetDestroyed(views::Widget* dying) override { Retire(dying); }
    void Retire(views::Widget* dying) {
      if (!widget) {
        return;
      }
      observation.Reset();
      native.reset();
      if (compositor) {
        compositor->RemoveObserver(this);
        compositor = nullptr;
      }
      widget = nullptr;
      owner->widget_by_address.erase(dying);
      owner->Destroy(token);
    }
    std::optional<AppliedSnapshot> ReadWidget() const {
      if (!widget || destroying || widget->IsClosed()) {
        return std::nullopt;
      }
      WidgetStateMatches value{.visible = widget->IsVisible(),
                               .bounds = widget->GetWindowBoundsInScreen(),
                               .capture = widget->HasCapture()};
      value.active = widget->IsActive();
      value.opacity = widget->GetLayer() ? widget->GetLayer()->opacity() : 1;
      value.visible_on_screen = widget->IsVisibleOnScreen();
      value.paint_as_active = widget->ShouldPaintAsActive();
      if (widget->IsFullscreen()) {
        value.show_state = WidgetStateMatches::ShowState::kFullscreen;
      } else if (widget->IsMinimized()) {
        value.show_state = WidgetStateMatches::ShowState::kMinimized;
      } else if (widget->IsMaximized()) {
        value.show_state = WidgetStateMatches::ShowState::kMaximized;
      }
      return Snapshot(
          token, value, serial, 0,
          GetAppliedViewCoordinates(*widget->GetRootView(),
                                    AppliedCoordinates::Space::kScreen));
    }
    raw_ref<Impl> owner;
    raw_ptr<views::Widget> widget;
    raw_ptr<ui::Compositor> compositor = nullptr;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
    bool destroying = false;
    base::RepeatingClosureList committed;
    std::unique_ptr<NativeWindowObservation> native;
    base::ScopedObservation<views::Widget, views::WidgetObserver> observation{
        this};
  };

  // running/idle 전이는 manager observer로 동기 기록한다. Chromium에는
  // manager 교체 전 알림이 없으므로 fixture가 WillChangeLayoutManager()로
  // 먼저 해제한다. 계약 없이 교체되면 이전 manager를 다시 만지지 않고 owner만
  // 폐기하지만, Chromium의 observer 목록 검사는 실패할 수 있다.
  struct LayoutWatch : views::AnimatingLayoutManager::Observer,
                       views::ViewObserver {
    LayoutWatch(Impl& owner,
                views::View& host,
                views::AnimatingLayoutManager& manager,
                uint64_t group)
        : owner(owner), host(&host), manager(&manager), group(group) {
      manager.AddObserver(this);
      host_observation.Observe(&host);
    }
    ~LayoutWatch() override { Release(); }
    bool Installed() const {
      return manager && host && host->GetLayoutManager() == manager.get();
    }
    // 아직 설치된 manager에서만 observer를 제거한다. ~View는
    // OnViewHierarchyWillBeDeleted 뒤, OnViewIsDeleting 전에 manager를
    // 파괴한다.
    void Release() {
      if (Installed()) {
        manager->RemoveObserver(this);
      }
      manager = nullptr;
      host_observation.Reset();
      host = nullptr;
    }
    void Retire() {
      Release();
      owner->Destroy(token);
    }
    void OnViewHierarchyWillBeDeleted(views::View*) override { Retire(); }
    void OnViewIsDeleting(views::View*) override { Retire(); }
    void OnLayoutIsAnimatingChanged(views::AnimatingLayoutManager*,
                                    bool) override {
      ++serial;
      owner->Notify();
    }
    std::optional<AppliedSnapshot> Read() {
      if (!Installed()) {
        return std::nullopt;
      }
      const bool running = manager->is_animating();
      return Snapshot(token,
                      AnimationMatches{group, running, running ? 0.0 : 1.0,
                                       running ? 0.0 : 1.0},
                      serial, 0, GetAppliedViewCoordinates(*host));
    }
    raw_ref<Impl> owner;
    raw_ptr<views::View> host;
    // Installed()가 확인한 뒤에만 역참조한다. BackupRefPtr가 이 참조가 남은
    // 동안 해제된 주소의 재사용을 막아 교체된 manager와 혼동하지 않는다.
    raw_ptr<views::AnimatingLayoutManager, DisableDanglingPtrDetection> manager;
    uint64_t group;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    uint64_t serial = 0;
    base::ScopedObservation<views::View, views::ViewObserver> host_observation{
        this};
  };

  // owner의 수명을 host에 설치된 특정 layout manager와 묶는다. 교체·제거는
  // 다음 깨움이나 생존 조회 때 확인하고 영구 폐기한다.
  struct LayoutTie {
    views::ViewTracker host;
    raw_ptr<const views::LayoutManager, DisableDanglingPtrDetection> manager;
  };
  bool LayoutTieValid(AppliedOwnerToken owner) const {
    const auto found = layout_ties.find(owner);
    if (found == layout_ties.end()) {
      return true;
    }
    const auto* host = found->second.host.view();
    return host && host->GetLayoutManager() == found->second.manager.get();
  }
  bool Alive(AppliedOwnerToken owner) const {
    return alive.contains(owner) && LayoutTieValid(owner);
  }
  void WillChangeLayoutManager(views::View& host) {
    for (const auto& watch : layouts) {
      if (watch->host == &host) {
        watch->Retire();
      }
    }
    std::vector<AppliedOwnerToken> retired;
    for (const auto& [owner, tie] : layout_ties) {
      if (tie.host.view() == &host) {
        retired.push_back(owner);
      }
    }
    for (auto owner : retired) {
      layout_ties.erase(owner);
      Destroy(owner);
    }
    Notify();
  }
  void RetireStaleLayoutTies() {
    std::vector<AppliedOwnerToken> stale;
    for (const auto& [owner, tie] : layout_ties) {
      if (!LayoutTieValid(owner)) {
        stale.push_back(owner);
      }
    }
    for (auto owner : stale) {
      layout_ties.erase(owner);
      Destroy(owner);
    }
  }

  struct FrameWatch : ui::CompositorObserver {
    FrameWatch(Impl& owner, views::View& view, AppliedConditions dependencies)
        : owner(owner), view(&view), dependencies(std::move(dependencies)) {}
    ~FrameWatch() override { SetCompositor(nullptr); }
    void Invalidate() {
      weak.InvalidateWeakPtrs();
      queued = false;
      armed_digest.reset();
      result.successful = false;
    }
    void CheckInvalidation(const std::vector<AppliedSnapshot>& captured) {
      if (!active)
        return;
      auto* host = view.view();
      if (!host || !host->GetWidget() ||
          host->GetWidget()->GetCompositor() != compositor ||
          (armed_digest && Digest(false) != armed_digest))
        Invalidate();
      for (const auto& value : captured) {
        for (const auto& dependency : dependencies) {
          if (dependency.owner == value.owner &&
              GetAppliedConditionKind(dependency.expected) ==
                  GetAppliedConditionKind(value.value) &&
              !AppliedValueMatches(dependency, value.value))
            Invalidate();
        }
      }
    }
    void SetCompositor(ui::Compositor* current) {
      if (compositor == current) {
        return;
      }
      if (compositor) {
        compositor->RemoveObserver(this);
      }
      compositor = current;
      weak.InvalidateWeakPtrs();
      queued = false;
      armed_digest.reset();
      result.successful = false;
      if (compositor) {
        compositor->AddObserver(this);
        result.compositor = owner->CompositorToken(*compositor);
      }
    }
    void OnCompositingShuttingDown(ui::Compositor*) override {
      owner->compositors.erase(compositor);
      SetCompositor(nullptr);
      owner->Notify();
    }
    void Schedule() {
      if (queued || !active || armed_digest) {
        return;
      }
      queued = true;
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&FrameWatch::Arm, weak.GetWeakPtr()));
    }
    std::optional<uint64_t> Digest(bool require_fresh) const {
      std::vector<AppliedSnapshot> geometry;
      for (size_t i = 0; i < dependencies.size(); ++i) {
        const auto& condition = dependencies[i];
        const auto snapshot = owner->Read(
            {condition.owner, GetAppliedConditionKind(condition.expected)});
        if (!snapshot || !AppliedValueMatches(condition, snapshot->value) ||
            snapshot->coordinates != condition.coordinates) {
          return std::nullopt;
        }
        if (require_fresh &&
            condition.freshness == AppliedFreshness::kNewApply &&
            snapshot->serials.apply_serial <= baselines[i].apply_serial) {
          return std::nullopt;
        }
        if (require_fresh &&
            condition.freshness == AppliedFreshness::kNewObservation &&
            snapshot->serials.observation_sequence <=
                baselines[i].observation_sequence) {
          return std::nullopt;
        }
        geometry.push_back(*snapshot);
      }
      return AppliedFrameGeometryDigest(geometry);
    }
    void Arm() {
      queued = false;
      if (!owner->alive.contains(token))
        return;
      auto* host = view.view();
      SetCompositor(host ? host->GetWidget()
                               ? host->GetWidget()->GetCompositor()
                               : nullptr
                         : nullptr);
      const auto digest = Digest(true);
      if (!digest || !compositor) {
        Invalidate();
        return;
      }
      if (armed_digest == digest) {
        return;
      }
      weak.InvalidateWeakPtrs();
      armed_digest = digest;
      result.successful = false;
      ++feedback_requests;
      compositor->RequestSuccessfulPresentationTimeForNextFrame(
          base::BindOnce(&FrameWatch::Presented, weak.GetWeakPtr(), *digest,
                         result.compositor));
      if (request_redraw)
        compositor->ScheduleFullRedraw();
    }
    void Presented(uint64_t digest,
                   AppliedOwnerToken identity,
                   const viz::FrameTimingDetails&) {
      ++feedback_callbacks;
      auto* host = view.view();
      if (!active || !owner->alive.contains(token) || !host ||
          !host->GetWidget() ||
          host->GetWidget()->GetCompositor() != compositor ||
          result.compositor != identity) {
        return;
      }
      if (Digest(false) != digest) {
        // callback 자체도 새 geometry를 발견한 이벤트다. View 알림이 없었던
        // 변화로 feedback을 거부해도 이전 digest에 묶인 채 멈추지 않는다.
        Invalidate();
        Schedule();
        return;
      }
      result.geometry_digest = digest;
      result.successful = true;
      ++serial;
      owner->Notify();
    }
    std::string Diagnostics() {
      auto* host = view.view();
      auto* widget = host ? host->GetWidget() : nullptr;
      std::string text =
          "dependencies=" + std::to_string(dependencies.size()) +
          " ready=" + std::to_string(Digest(false).has_value()) +
          " fresh_ready=" + std::to_string(active && Digest(true).has_value()) +
          " feedback_requests=" + std::to_string(feedback_requests) +
          " feedback_callbacks=" + std::to_string(feedback_callbacks) +
          " armed=" + std::to_string(armed_digest.has_value()) +
          " widget_visible=" + std::to_string(widget && widget->IsVisible()) +
          " compositor_visible=" +
          std::to_string(compositor && compositor->IsVisible());
      if (widget) {
        const auto found = owner->widget_by_address.find(widget);
        if (found != owner->widget_by_address.end())
          text += " widget=" + found->second->token.ToString();
      }
      for (size_t i = 0; i < dependencies.size(); ++i) {
        const auto& condition = dependencies[i];
        const auto snapshot = owner->Read(
            {condition.owner, GetAppliedConditionKind(condition.expected)});
        const bool matches =
            snapshot && AppliedValueMatches(condition, snapshot->value);
        const bool coordinates =
            snapshot && snapshot->coordinates == condition.coordinates;
        text += " dependency[" + std::to_string(i) +
                "]=" + (matches ? "value-ok" : "value-unmet") + "," +
                (coordinates ? "coordinates-ok" : "coordinates-unmet");
        if (!matches || !coordinates) {
          text += " expected={" + FormatAppliedValue(condition.expected) +
                  " DSF=" +
                  std::to_string(condition.coordinates.device_scale_factor) +
                  "} actual={" +
                  (snapshot ? FormatAppliedValue(snapshot->value) + " DSF=" +
                                  std::to_string(
                                      snapshot->coordinates.device_scale_factor)
                            : "missing") +
                  "}";
        }
      }
      return text;
    }
    void Start() {
      if (active++) {
        return;
      }
      baselines.clear();
      for (const auto& dependency : dependencies) {
        const auto value = owner->Read(
            {dependency.owner, GetAppliedConditionKind(dependency.expected)});
        baselines.push_back(value ? value->serials : AppliedSerials{});
      }
      // paint만 바꾸는 action도 첫 frame을 놓치지 않도록 Begin의 구독 시점에
      // feedback을 연결한다. geometry가 바뀌면 해당 알림에서 다시 arm한다.
      Arm();
    }
    void Stop() {
      if (--active == 0) {
        weak.InvalidateWeakPtrs();
        queued = false;
        SetCompositor(nullptr);
      }
    }
    raw_ref<Impl> owner;
    views::ViewTracker view;
    AppliedConditions dependencies;
    std::vector<AppliedSerials> baselines;
    AppliedOwnerToken token = AppliedOwnerToken::Create();
    PresentedAfterApply result{.applied_sample_id = 1, .successful = false};
    raw_ptr<ui::Compositor> compositor = nullptr;
    std::optional<uint64_t> armed_digest;
    uint64_t serial = 0;
    uint64_t feedback_requests = 0;
    uint64_t feedback_callbacks = 0;
    int active = 0;
    bool queued = false;
    bool request_redraw = true;
    base::WeakPtrFactory<FrameWatch> weak{this};
  };

  void RetireSubtree(views::View& root) {
    std::vector<ViewWatch*> retiring;
    for (const auto& [view, watch] : view_by_address) {
      if (watch->tracker.view() && root.Contains(view))
        retiring.push_back(watch);
    }
    for (auto* watch : retiring)
      watch->Retire();
  }

  ViewWatch& WatchView(views::View& view) {
    if (auto found = view_by_address.find(&view);
        found != view_by_address.end()) {
      return *found->second;
    }
    WatchLayer(view.layer());
    auto watch = std::make_unique<ViewWatch>(*this, view);
    auto* ptr = watch.get();
    view_by_address[&view] = ptr;
    views.push_back(std::move(watch));
    Register(ptr->token, AppliedConditionKind::kViewStateMatches,
             base::BindRepeating(&ViewWatch::ReadView, base::Unretained(ptr)));
    Register(
        ptr->token, AppliedConditionKind::kFocusMatches,
        base::BindRepeating(
            [](ViewWatch* watch) -> std::optional<AppliedSnapshot> {
              if (!watch->tracker.view()) {
                return std::nullopt;
              }
              return Snapshot(
                  watch->token,
                  FocusMatches{.target = watch->token,
                               .focused = watch->tracker.view()->HasFocus()},
                  watch->serial, 0,
                  GetAppliedViewCoordinates(*watch->tracker.view()));
            },
            base::Unretained(ptr)));
    return *ptr;
  }

  AppliedOwnerToken ViewToken(views::View& view) {
    auto& watch = WatchView(view);
    // parent/children identity에는 인접 View만 필요하다. 조상을 따라 형제의
    // 하위 트리까지 재귀 등록하면 한 pane이 브라우저 전체를 관측하게 된다.
    for (auto* ancestor = view.parent(); ancestor;
         ancestor = ancestor->parent())
      WatchView(*ancestor);
    for (views::View* child : view.children())
      WatchView(*child);
    if (view.GetWidget()) {
      WidgetFor(*view.GetWidget());
    }
    return watch.token;
  }

  // 닫기가 시작된 창은 새로 관찰하지 않는다. 해체 중 View 재배치로 만든
  // 관찰자는 destroying/destroyed 알림을 받지 못하고 해제된 창을 남긴다.
  WidgetWatch* WidgetFor(views::Widget& widget) {
    if (auto found = widget_by_address.find(&widget);
        found != widget_by_address.end()) {
      return found->second;
    }
    if (widget.IsClosed()) {
      return nullptr;
    }
    WatchLayer(widget.GetLayer());
    auto watch = std::make_unique<WidgetWatch>(*this, widget);
    auto* ptr = watch.get();
    widget_by_address[&widget] = ptr;
    widgets.push_back(std::move(watch));
    Register(
        ptr->token, AppliedConditionKind::kWidgetStateMatches,
        base::BindRepeating(&WidgetWatch::ReadWidget, base::Unretained(ptr)));
    return ptr;
  }

  // 이미 닫힌 창의 identity는 바로 폐기된 owner로 돌려 기다리던 조건이
  // OwnerDestroyed로 끝나게 한다.
  // 조건 종류는 등록해 지원 여부와 생존을 구분한다.
  AppliedOwnerToken ClosedWidgetToken(
      std::initializer_list<AppliedConditionKind> kinds) {
    const auto token = AppliedOwnerToken::Create();
    for (auto kind : kinds) {
      Register(token, kind, base::BindRepeating([]() {
                 return std::optional<AppliedSnapshot>();
               }));
    }
    Destroy(token);
    return token;
  }

  AppliedOwnerToken CompositorToken(ui::Compositor& compositor) {
    // FrameWatch의 shutdown 알림에서 이 주소의 identity를 폐기한다.
    auto [it, inserted] = compositors.try_emplace(&compositor);
    if (inserted) {
      it->second = AppliedOwnerToken::Create();
    }
    return it->second;
  }

  Signal& Register(AppliedOwnerToken owner,
                   AppliedConditionKind kind,
                   Reader read) {
    CHECK(!owner.is_empty());
    CHECK(!Find({owner, kind}));
    alive.insert(owner);
    return signals
        .emplace(std::make_pair(owner, kind),
                 Signal{{owner, kind}, std::move(read)})
        .first->second;
  }
  Signal* Find(const AppliedSignalKey& key) {
    const auto found = signals.find({key.owner, key.kind});
    return found == signals.end() ? nullptr : &found->second;
  }
  std::optional<AppliedSnapshot> Read(const AppliedSignalKey& key) {
    auto* signal = Find(key);
    if (!signal || !Alive(key.owner)) {
      return std::nullopt;
    }
    auto snapshot = signal->read.Run();
    if (snapshot) {
      snapshot->serials.observation_sequence = signal->observation;
      snapshot->sample_id = sequence;
    }
    return snapshot;
  }
  std::vector<AppliedSnapshot> CaptureCurrent(
      const std::vector<AppliedSnapshot>& captured = {}) {
    std::vector<AppliedSnapshot> snapshots;
    for (auto& [key, signal] : signals) {
      // 지난 ticket의 reader는 동기 layout 경로에서 실행하지 않는다.
      // 현재 구독의 순간 AND 표본과 frame 의존성만 함께 복사한다.
      if (!signal.subscribers || !Alive(key.first))
        continue;
      auto value = signal.read.Run();
      auto delivered = signal.delivered_serial;
      for (const auto& snapshot : captured) {
        if (snapshot.owner == key.first &&
            GetAppliedConditionKind(snapshot.value) == key.second)
          delivered = std::max(delivered, snapshot.serials.apply_serial);
      }
      if (value &&
          (!signal.deferred || value->serials.apply_serial <= delivered)) {
        snapshots.push_back(std::move(*value));
      }
    }
    return snapshots;
  }
  void Notify(const std::vector<AppliedSnapshot>& captured = {}) {
    // 재진입한 callback이 값을 다시 바꾸기 전에 순간 표본을 복사한다.
    // deferred apply는 제품 callback이 실제 전달한 serial까지만 포함한다.
    pending_notifications.push_back(
        {captured, notifying ? std::make_optional(CaptureCurrent(captured))
                             : std::nullopt});
    if (notifying) {
      return;
    }
    notifying = true;
    while (!pending_notifications.empty()) {
      auto pending = std::move(pending_notifications.front());
      pending_notifications.pop_front();
      NotifySample(pending);
    }
    notifying = false;
  }
  struct PendingNotification {
    std::vector<AppliedSnapshot> captured;
    std::optional<std::vector<AppliedSnapshot>> current;
  };
  void NotifySample(const PendingNotification& pending) {
    const auto& captured = pending.captured;
    RetireStaleLayoutTies();
    wakes.Notify();
    for (const auto& frame : frames) {
      frame->CheckInvalidation(captured);
      if (pending.current)
        frame->CheckInvalidation(*pending.current);
    }
    // C2는 적용 당시 값을 지연 전달한다. 현재값보다 오래된 표본을 다른
    // owner의 현재값과 같은 순간으로 묶지 않는다.
    bool historical = false;
    for (const auto& value : captured) {
      auto* signal = Find({value.owner, GetAppliedConditionKind(value.value)});
      const auto current = signal ? signal->read.Run() : std::nullopt;
      historical |= !current || current->value != value.value ||
                    current->serials.apply_serial != value.serials.apply_serial;
      if (signal) {
        signal->delivered_serial =
            std::max(signal->delivered_serial, value.serials.apply_serial);
      }
    }
    if (historical) {
      AppliedSignalSample past{captured};
      ++sequence;
      for (auto& value : past.snapshots) {
        value.sample_id = sequence;
        value.serials.observation_sequence = sequence;
        if (auto* signal =
                Find({value.owner, GetAppliedConditionKind(value.value)}))
          signal->observation = sequence;
      }
      callbacks.Notify(past);
    }
    ++sequence;
    AppliedSignalSample sample{pending.current ? *pending.current
                                               : CaptureCurrent()};
    std::erase_if(sample.snapshots,
                  [&](const auto& value) { return !Alive(value.owner); });
    for (auto& value : sample.snapshots) {
      auto* signal = Find({value.owner, GetAppliedConditionKind(value.value)});
      // SetLayerBounds는 동일 bounds에도 알린다. 다른 View의 알림이나
      // compositor commit을 이 owner의 새 적용·관측으로 전파하지 않는다.
      const auto& last = signal->last;
      if (!last || !SameObservedValue(*last, value))
        signal->observation = sequence;
      signal->last = value;
      value.serials.observation_sequence = signal->observation;
      value.sample_id = sequence;
    }
    callbacks.Notify(sample);
    for (const auto& frame : frames) {
      frame->Schedule();
    }
  }
  void Destroy(AppliedOwnerToken owner) {
    if (!alive.erase(owner)) {
      return;
    }
    for (const auto& frame : frames) {
      if (frame->token == owner) {
        frame->Invalidate();
        frame->SetCompositor(nullptr);
      }
      if (std::ranges::any_of(frame->dependencies, [&](const auto& condition) {
            return condition.owner == owner;
          }))
        Destroy(frame->token);
    }
    callbacks.Notify(AppliedSignalSample{.destroyed_owners = {owner}});
  }

  raw_ref<AppliedViewsObservers> api_;
  std::map<std::pair<AppliedOwnerToken, AppliedConditionKind>, Signal> signals;
  std::set<AppliedOwnerToken> alive;
  std::map<AppliedOwnerToken, LayoutTie> layout_ties;
  uint64_t sequence = 0;
  bool notifying = false;
  std::deque<PendingNotification> pending_notifications;
  base::RepeatingCallbackList<void(const AppliedSignalSample&)> callbacks;
  base::RepeatingClosureList wakes;
  base::RepeatingClosureList releases;
  std::map<ui::Layer*, LayerWatch*> layer_by_address;
  std::vector<std::unique_ptr<LayerWatch>> layers;
  std::map<const views::View*, ViewWatch*> view_by_address;
  std::map<views::Widget*, WidgetWatch*> widget_by_address;
  std::map<ui::Compositor*, AppliedOwnerToken> compositors;
  std::vector<std::unique_ptr<ViewWatch>> views;
  std::vector<std::unique_ptr<WidgetWatch>> widgets;
  std::vector<std::unique_ptr<LayoutWatch>> layouts;
  std::vector<base::CallbackListSubscription> subscriptions;
  std::vector<std::unique_ptr<FrameWatch>> frames;
  base::WeakPtrFactory<Impl> weak{this};
};

AppliedCoordinates GetAppliedViewCoordinates(const views::View& view,
                                             AppliedCoordinates::Space space) {
  AppliedCoordinates value{.space = space, .rtl = base::i18n::IsRTL()};
  if (const auto* widget = view.GetWidget(); widget && widget->GetCompositor())
    value.device_scale_factor = widget->GetCompositor()->device_scale_factor();
  return value;
}

AppliedViewsObservers::AppliedViewsObservers()
    : impl_(std::make_unique<Impl>(*this)) {}
AppliedViewsObservers::~AppliedViewsObservers() = default;
AppliedOwnerToken AppliedViewsObservers::ObserveView(views::View& view) {
  return impl_->ViewToken(view);
}
AppliedOwnerToken AppliedViewsObservers::ObserveWidget(views::Widget& widget) {
  auto* watch = impl_->WidgetFor(widget);
  return watch ? watch->token
               : impl_->ClosedWidgetToken(
                     {AppliedConditionKind::kWidgetStateMatches});
}
void AppliedViewsObservers::NotifyCaptureChanged(views::Widget& widget) {
  if (auto* watch = impl_->WidgetFor(widget))
    watch->Changed();
}
AppliedOwnerToken AppliedViewsObservers::ObserveScrollView(
    views::ScrollView& view) {
  const auto token = ObserveView(view);
  auto* watch = impl_->view_by_address.at(&view);
  watch->scroll = &view;
  watch->scroll_subscription = view.AddContentsScrolledCallback(
      base::BindRepeating(&Impl::ViewWatch::Changed, base::Unretained(watch)));
  return token;
}
AppliedOwnerToken AppliedViewsObservers::ObservePageHosts(
    views::View& clip_host,
    uint64_t pane_id,
    uint64_t tab_handle) {
  ObserveView(clip_host);
  auto* state = PageViewportAppliedState::From(clip_host);
  const auto token =
      state ? state->GetSnapshot().owner : AppliedOwnerToken::Create();
  if (!state) {
    return token;
  }
  if (impl_->Find({token, AppliedConditionKind::kPageHostsApplied})) {
    return token;
  }
  auto convert = base::BindRepeating(
      [](Impl::ViewWatch* view, uint64_t pane, uint64_t tab,
         const PageViewportAppliedSnapshot& s) {
        return Snapshot(
            s.owner,
            PageHostsApplied{
                pane, tab, s.clip_host_bounds_in_container,
                s.geometry.current_clip_in_target, s.clip_host_radii,
                s.target_host_bounds_in_clip, s.overlay_host_bounds_in_clip,
                s.clip_host_bounds_in_container,
                s.geometry.current_clip_in_target, s.target_host_bounds_in_clip,
                s.overlay_host_bounds_in_clip, true},
            s.apply_serial, s.geometry.generation,
            view->tracker.view()
                ? GetAppliedViewCoordinates(*view->tracker.view(),
                                            AppliedCoordinates::Space::kParent)
                : AppliedCoordinates{});
      },
      base::Unretained(impl_->view_by_address.at(&clip_host)), pane_id,
      tab_handle);
  auto* clip_watch = impl_->view_by_address.at(&clip_host);
  ObserveView(GetPageTargetHost(clip_host));
  auto* target_watch = impl_->view_by_address.at(&GetPageTargetHost(clip_host));
  target_watch->tokens.push_back(token);
  ObserveView(GetViewportOverlayHost(clip_host));
  auto* overlay_watch =
      impl_->view_by_address.at(&GetViewportOverlayHost(clip_host));
  overlay_watch->tokens.push_back(token);
  auto& signal = impl_->Register(
      token, AppliedConditionKind::kPageHostsApplied,
      base::BindRepeating(
          [](base::WeakPtr<PageViewportAppliedState> state,
             decltype(convert) convert, Impl::ViewWatch* clip,
             Impl::ViewWatch* target,
             Impl::ViewWatch* overlay) -> std::optional<AppliedSnapshot> {
            auto* view = clip->tracker.view();
            if (!state || state->GetSnapshot().apply_serial == 0 || !view ||
                !target->tracker.view() || !overlay->tracker.view() ||
                target->tracker.view()->parent() != view ||
                overlay->tracker.view()->parent() != view || !view->layer() ||
                !view->layer()->GetMasksToBounds()) {
              return std::nullopt;
            }
            auto snapshot = convert.Run(state->GetSnapshot());
            auto& value = std::get<PageHostsApplied>(snapshot.value);
            value.bounds = view->bounds();
            value.radii = view->layer()->rounded_corner_radii();
            value.target = target->tracker.view()->bounds();
            if (!view->layer()->clip_rect().IsEmpty()) {
              auto effective_clip = view->layer()->clip_rect();
              effective_clip.Offset(-value.target.OffsetFromOrigin());
              if (value.clip.IsEmpty())
                value.clip = effective_clip;
              else
                value.clip.Intersect(effective_clip);
            }
            value.overlay = overlay->tracker.view()->bounds();
            value.geometry_applied =
                value.bounds == value.applied_bounds &&
                value.clip == value.applied_clip &&
                value.target == value.applied_target &&
                value.overlay == value.applied_overlay &&
                value.radii == state->GetSnapshot().clip_host_radii;
            for (views::View* child : target->tracker.view()->children())
              value.geometry_applied &=
                  child->bounds() == target->tracker.view()->GetLocalBounds();
            return snapshot;
          },
          state->GetWeakPtr(), convert, base::Unretained(clip_watch),
          base::Unretained(target_watch), base::Unretained(overlay_watch)));
  signal.deferred = true;
  signal.delivered_serial = state->GetSnapshot().apply_serial;
  impl_->view_by_address.at(&clip_host)->tokens.push_back(token);
  impl_->subscriptions.push_back(state->Subscribe(base::BindRepeating(
      [](base::WeakPtr<Impl> impl, decltype(convert) convert,
         const PageViewportAppliedSnapshot& snapshot) {
        if (impl) {
          impl->Notify({convert.Run(snapshot)});
        }
      },
      impl_->weak.GetWeakPtr(), convert)));
  return token;
}
AppliedOwnerToken AppliedViewsObservers::ObservePreview(views::View& preview,
                                                        views::View& source,
                                                        uint64_t source_handle,
                                                        uint64_t tab_handle) {
  ObserveView(preview);
  ObserveView(source);
  auto* state = FavoritesDragPreviewAppliedState::From(preview);
  const auto token =
      state ? state->GetSnapshot().owner : AppliedOwnerToken::Create();
  if (!state || impl_->Find({token, AppliedConditionKind::kPreviewApplied})) {
    return token;
  }
  auto* source_watch = impl_->view_by_address.at(&source);
  auto convert = base::BindRepeating(
      [](Impl::ViewWatch* source, Impl::ViewWatch* preview, uint64_t handle,
         uint64_t tab, const FavoritesDragPreviewAppliedSnapshot& s) {
        const auto bounds = source->tracker.view()
                                ? source->tracker.view()->GetBoundsInScreen()
                                : gfx::Rect();
        auto snapshot = Snapshot(
            s.owner,
            PreviewApplied{
                handle, tab, s.visible, s.running, s.screen_bounds, bounds.x(),
                bounds.width(),
                preview->tracker.view() &&
                    s.visible == preview->tracker.view()->GetVisible() &&
                    s.screen_bounds ==
                        preview->tracker.view()->GetBoundsInScreen()},
            s.apply_serial, 0,
            source->tracker.view()
                ? GetAppliedViewCoordinates(*source->tracker.view(),
                                            AppliedCoordinates::Space::kScreen)
                : AppliedCoordinates{});
        return snapshot;
      },
      base::Unretained(source_watch),
      base::Unretained(impl_->view_by_address.at(&preview)), source_handle,
      tab_handle);
  auto& signal = impl_->Register(
      token, AppliedConditionKind::kPreviewApplied,
      base::BindRepeating(
          [](base::WeakPtr<FavoritesDragPreviewAppliedState> state,
             decltype(convert) convert) -> std::optional<AppliedSnapshot> {
            return state ? std::make_optional(convert.Run(state->GetSnapshot()))
                         : std::nullopt;
          },
          state->GetWeakPtr(), convert));
  signal.deferred = true;
  signal.delivered_serial = state->GetSnapshot().apply_serial;
  impl_->view_by_address.at(&preview)->tokens.push_back(token);
  source_watch->tokens.push_back(token);
  impl_->subscriptions.push_back(state->Subscribe(base::BindRepeating(
      [](base::WeakPtr<Impl> impl, decltype(convert) convert,
         const FavoritesDragPreviewAppliedSnapshot& snapshot) {
        if (impl) {
          impl->Notify({convert.Run(snapshot)});
        }
      },
      impl_->weak.GetWeakPtr(), convert)));
  return token;
}
AppliedOwnerToken AppliedViewsObservers::ObserveCapsule(views::View& decoration,
                                                        uint64_t group_id) {
  ObserveView(decoration);
  auto* state = HorizontalTabGroupCapsuleState::From(decoration);
  const auto token =
      state ? state->GetSnapshot().owner : AppliedOwnerToken::Create();
  if (!state || impl_->Find({token, AppliedConditionKind::kAnimationMatches})) {
    return token;
  }
  auto convert = base::BindRepeating(
      [](Impl::ViewWatch* view, uint64_t group,
         const HorizontalTabGroupCapsuleSnapshot& s) {
        return Snapshot(
            s.owner,
            AnimationMatches{group, s.running, s.expansion, s.expansion},
            s.apply_serial, 0,
            view->tracker.view()
                ? GetAppliedViewCoordinates(*view->tracker.view())
                : AppliedCoordinates{});
      },
      base::Unretained(impl_->view_by_address.at(&decoration)), group_id);
  auto& signal = impl_->Register(
      token, AppliedConditionKind::kAnimationMatches,
      base::BindRepeating(
          [](base::WeakPtr<HorizontalTabGroupCapsuleState> state,
             decltype(convert) convert) -> std::optional<AppliedSnapshot> {
            return state ? std::make_optional(convert.Run(state->GetSnapshot()))
                         : std::nullopt;
          },
          state->GetWeakPtr(), convert));
  signal.deferred = true;
  signal.delivered_serial = state->GetSnapshot().apply_serial;
  impl_->view_by_address.at(&decoration)->tokens.push_back(token);
  impl_->subscriptions.push_back(state->Subscribe(base::BindRepeating(
      [](base::WeakPtr<Impl> impl, decltype(convert) convert,
         const HorizontalTabGroupCapsuleSnapshot& s) {
        if (impl) {
          impl->Notify({convert.Run(s)});
        }
      },
      impl_->weak.GetWeakPtr(), convert)));
  return token;
}
AppliedOwnerToken AppliedViewsObservers::ObserveLayoutAnimation(
    views::View& host,
    views::AnimatingLayoutManager& manager,
    uint64_t group_id) {
  CHECK_EQ(host.GetLayoutManager(), &manager);
  ObserveView(host);
  auto watch =
      std::make_unique<Impl::LayoutWatch>(*impl_, host, manager, group_id);
  const auto token = watch->token;
  impl_->view_by_address.at(&host)->tokens.push_back(token);
  auto& signal =
      impl_->Register(token, AppliedConditionKind::kAnimationMatches,
                      base::BindRepeating(&Impl::LayoutWatch::Read,
                                          base::Unretained(watch.get())));
  signal.full_progress_only = true;
  impl_->layouts.push_back(std::move(watch));
  TieToLayoutManager(token, host, manager);
  return token;
}
AppliedOwnerToken AppliedViewsObservers::ObserveNativeWindow(
    views::Widget& widget) {
  auto* found = impl_->WidgetFor(widget);
  if (!found) {
    return impl_->ClosedWidgetToken({AppliedConditionKind::kNativeWindowMatches,
                                     AppliedConditionKind::kFocusMatches});
  }
  auto& watch = *found;
  if (watch.native) {
    return watch.token;
  }
  watch.native = std::make_unique<NativeWindowObservation>(
      widget, base::BindRepeating(&Impl::WidgetWatch::Changed,
                                  base::Unretained(&watch)));
  if (watch.native->supported()) {
    impl_->Register(
        watch.token, AppliedConditionKind::kNativeWindowMatches,
        base::BindRepeating(
            [](Impl::WidgetWatch* watch) -> std::optional<AppliedSnapshot> {
              const auto value = watch->native->Read();
              return value ? std::make_optional(Snapshot(
                                 watch->token, *value, watch->serial, 0,
                                 GetAppliedViewCoordinates(
                                     *watch->widget->GetRootView(),
                                     AppliedCoordinates::Space::kScreen)))
                           : std::nullopt;
            },
            base::Unretained(&watch)));
    impl_->Register(
        watch.token, AppliedConditionKind::kFocusMatches,
        base::BindRepeating(
            [](Impl::WidgetWatch* watch) -> std::optional<AppliedSnapshot> {
              const auto value = watch->native->Read();
              return value
                         ? std::make_optional(Snapshot(
                               watch->token,
                               FocusMatches{
                                   .kind = FocusMatches::Kind::kNativeKeyWindow,
                                   .target = watch->token,
                                   .focused = value->active},
                               watch->serial, 0,
                               GetAppliedViewCoordinates(
                                   *watch->widget->GetRootView(),
                                   AppliedCoordinates::Space::kScreen)))
                         : std::nullopt;
            },
            base::Unretained(&watch)));
  }
  return watch.token;
}
AppliedCondition AppliedViewsObservers::ObservePresentedAfterApply(
    views::View& view,
    AppliedConditions dependencies,
    bool request_redraw) {
  ObserveView(view);
  CHECK(!dependencies.empty());
  auto frame = std::make_unique<Impl::FrameWatch>(*impl_, view, dependencies);
  auto* ptr = frame.get();
  ptr->request_redraw = request_redraw;
  ptr->SetCompositor(view.GetWidget() ? view.GetWidget()->GetCompositor()
                                      : nullptr);
  PresentedAfterApply expected = ptr->result;
  expected.geometry_digest = ptr->Digest(false).value_or(0);
  expected.successful = true;
  impl_->view_by_address.at(&view)->tokens.push_back(ptr->token);
  const bool has_geometry =
      std::ranges::any_of(dependencies, [](const auto& condition) {
        const auto kind = GetAppliedConditionKind(condition.expected);
        return kind == AppliedConditionKind::kPageHostsApplied ||
               kind == AppliedConditionKind::kViewStateMatches ||
               kind == AppliedConditionKind::kWidgetStateMatches ||
               kind == AppliedConditionKind::kPaneLayoutMatches ||
               kind == AppliedConditionKind::kPreviewApplied ||
               kind == AppliedConditionKind::kRendererViewportMatches ||
               kind == AppliedConditionKind::kWebUiStateMatches;
      });
  if (!has_geometry)
    return {.owner = ptr->token, .expected = expected};
  impl_->Register(
      ptr->token, AppliedConditionKind::kPresentedAfterApply,
      base::BindRepeating(
          [](Impl::FrameWatch* frame) -> std::optional<AppliedSnapshot> {
            auto value = frame->result;
            auto* view = frame->view.view();
            if (!view || !view->GetWidget() ||
                view->GetWidget()->GetCompositor() != frame->compositor ||
                frame->Digest(false) != value.geometry_digest) {
              value.successful = false;
            }
            auto snapshot = Snapshot(
                frame->token, value, frame->serial, 0,
                view ? GetAppliedViewCoordinates(*view) : AppliedCoordinates{});
            if (!value.successful)
              snapshot.adapter_diagnostics = frame->Diagnostics();
            return snapshot;
          },
          base::Unretained(ptr)));
  impl_->frames.push_back(std::move(frame));
  return {.owner = ptr->token,
          .expected = expected,
          .coordinates = GetAppliedViewCoordinates(view),
          .match = AppliedMatch::kPresentedFrame};
}

bool AppliedViewsObservers::Supports(const AppliedCondition& condition) const {
  if (!IsAppliedMatchCompatible(condition))
    return false;
  auto* signal = impl_->Find(
      {condition.owner, GetAppliedConditionKind(condition.expected)});
  if (!signal) {
    return false;
  }
  if (const auto* presented =
          std::get_if<PresentedAfterApply>(&condition.expected)) {
    if (presented->compositor.is_empty())
      return false;
    const auto found = std::ranges::find_if(
        impl_->frames,
        [&](const auto& watch) { return watch->token == condition.owner; });
    if (found == impl_->frames.end())
      return false;
    return std::ranges::all_of(
        (*found)->dependencies, [&](const auto& dependency) {
          return dependency.mode == AppliedEvaluationMode::kCurrent &&
                 Supports(dependency);
        });
  }
  if (const auto* focus = std::get_if<FocusMatches>(&condition.expected)) {
    const auto value = impl_->Read(signal->key);
    return !IsOwnerAlive(condition.owner) ||
           (value && std::get<FocusMatches>(value->value).kind == focus->kind);
  }
  if (signal->full_progress_only) {
    const auto& animation = std::get<AnimationMatches>(condition.expected);
    return animation.progress_min == 0 && animation.progress_max == 1;
  }
  return true;
}
bool AppliedViewsObservers::IsOwnerAlive(AppliedOwnerToken owner) const {
  return impl_->Alive(owner);
}
void AppliedViewsObservers::WillChangeLayoutManager(views::View& host) {
  impl_->WillChangeLayoutManager(host);
}
void AppliedViewsObservers::TieToLayoutManager(
    AppliedOwnerToken owner,
    views::View& host,
    const views::LayoutManager& manager) {
  CHECK_EQ(host.GetLayoutManager(), &manager);
  auto& tie = impl_->layout_ties[owner];
  tie.host.SetView(&host);
  tie.manager = &manager;
}
std::optional<AppliedSnapshot> AppliedViewsObservers::Read(
    const AppliedSignalKey& key) const {
  return impl_->Read(key);
}
base::CallbackListSubscription AppliedViewsObservers::Subscribe(
    const std::vector<AppliedSignalKey>& keys,
    Observer observer) {
  using Key = std::pair<AppliedOwnerToken, AppliedConditionKind>;
  std::set<Key> observed;
  for (const auto& key : keys)
    observed.emplace(key.owner, key.kind);
  std::vector<Impl::FrameWatch*> frames;
  for (const auto& frame : impl_->frames) {
    if (std::ranges::find(
            keys,
            AppliedSignalKey{frame->token,
                             AppliedConditionKind::kPresentedAfterApply}) !=
        keys.end()) {
      for (const auto& dependency : frame->dependencies)
        observed.emplace(dependency.owner,
                         GetAppliedConditionKind(dependency.expected));
      frames.push_back(frame.get());
    }
  }
  for (const auto& [owner, kind] : observed) {
    if (auto* signal = impl_->Find({owner, kind}); signal) {
      if (!signal->subscribers++)
        signal->last = signal->read.Run();
    }
  }
  for (auto* frame : frames)
    frame->Start();
  std::map<Key, std::optional<AppliedSnapshot>> previous;
  for (const auto& key : keys) {
    auto* signal = impl_->Find(key);
    if (signal && !signal->last)
      signal->last = signal->read.Run();
    previous.emplace(std::make_pair(key.owner, key.kind), impl_->Read(key));
  }
  auto subscription = std::make_unique<base::CallbackListSubscription>(
      impl_->callbacks.Add(base::BindRepeating(
          [](base::WeakPtr<Impl> impl, std::vector<AppliedSignalKey> keys,
             Observer callback,
             std::map<Key, std::optional<AppliedSnapshot>>& previous,
             const AppliedSignalSample& sample) {
            if (!impl)
              return;
            AppliedSignalSample filtered;
            bool changed = false;
            for (const auto& value : sample.snapshots) {
              if (std::ranges::find(
                      keys, AppliedSignalKey{value.owner,
                                             GetAppliedConditionKind(
                                                 value.value)}) == keys.end())
                continue;
              filtered.snapshots.push_back(value);
              auto& last = previous.at(
                  {value.owner, GetAppliedConditionKind(value.value)});
              changed |= !last || !SameObservedValue(*last, value);
              last = value;
            }
            for (auto owner : sample.destroyed_owners) {
              if (std::ranges::any_of(keys, [&](const auto& key) {
                    return key.owner == owner;
                  }))
                filtered.destroyed_owners.push_back(owner);
            }
            if (!changed && filtered.destroyed_owners.empty())
              return;
            // 선택된 조건 하나가 변하면 변하지 않은 geometry도 같은 순간에
            // 관측한 새 표본이다. source Read의 watermark도 함께 올려
            // ObservedSinceDispatch의 묶음과 Current 재조회를 모두 보존한다.
            for (auto& value : filtered.snapshots) {
              auto* signal = impl->Find(
                  {value.owner, GetAppliedConditionKind(value.value)});
              signal->observation =
                  std::max(signal->observation, value.sample_id);
              value.serials.observation_sequence = signal->observation;
            }
            callback.Run(filtered);
          },
          impl_->weak.GetWeakPtr(), keys, std::move(observer),
          base::OwnedRef(std::move(previous)))));
  // callback list의 제거 callback으로 frame의 예약·feedback도 함께 해제한다.
  auto release = std::make_shared<base::ScopedClosureRunner>(base::BindOnce(
      [](std::unique_ptr<base::CallbackListSubscription> subscription,
         std::vector<Impl::FrameWatch*> frames, base::WeakPtr<Impl> impl,
         const std::set<Key>& observed) {
        subscription.reset();
        for (auto* frame : frames) {
          frame->Stop();
        }
        if (impl) {
          for (const auto& [owner, kind] : observed) {
            if (auto* signal = impl->Find({owner, kind}); signal)
              --signal->subscribers;
          }
        }
      },
      std::move(subscription), std::move(frames), impl_->weak.GetWeakPtr(),
      std::move(observed)));
  return impl_->releases.Add(base::BindRepeating(
      [](std::shared_ptr<base::ScopedClosureRunner>) {}, release));
}
base::CallbackListSubscription AppliedViewsObservers::SubscribeCompositorFrames(
    views::Widget& widget,
    std::vector<AppliedSignalKey> keys,
    Observer observer) {
  auto* watch = impl_->WidgetFor(widget);
  CHECK(watch);
  return watch->committed.Add(base::BindRepeating(
      [](base::WeakPtr<Impl> impl, const std::vector<AppliedSignalKey>& keys,
         Observer observer) {
        if (!impl)
          return;
        AppliedSignalSample sample;
        for (const auto& key : keys) {
          if (auto value = impl->Read(key))
            sample.snapshots.push_back(std::move(*value));
        }
        observer.Run(sample);
      },
      impl_->weak.GetWeakPtr(), std::move(keys), std::move(observer)));
}
void AppliedViewsObservers::RegisterSignal(AppliedOwnerToken owner,
                                           AppliedConditionKind kind,
                                           Reader reader) {
  impl_->Register(owner, kind, std::move(reader));
}
void AppliedViewsObservers::Notify(
    const std::vector<AppliedSnapshot>& captured) {
  impl_->Notify(captured);
}
void AppliedViewsObservers::TieToView(AppliedOwnerToken owner,
                                      views::View& view) {
  ObserveView(view);
  impl_->view_by_address.at(&view)->tokens.push_back(owner);
}
void AppliedViewsObservers::OwnerDestroyed(AppliedOwnerToken owner) {
  impl_->Destroy(owner);
}
base::CallbackListSubscription AppliedViewsObservers::SubscribeWake(
    base::RepeatingClosure callback) {
  return impl_->wakes.Add(std::move(callback));
}

}  // namespace yee::test
