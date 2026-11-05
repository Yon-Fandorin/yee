// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/drag_interactive_test_support.h"

#include <map>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_group_view.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab_container.h"
#include "chrome/browser/ui/views/tabs/tab_slot_view.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
#include "chrome/browser/ui/views/yee/favorites.h"
#include "chrome/browser/ui/views/yee/sidebar_metrics.h"
#include "chrome/browser/ui/views/yee/tab_group_presentation.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"
#include "ui/views/event_monitor.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace yee::test {

class DragAppliedObserver::Impl : public views::WidgetObserver,
                                  public ui::EventObserver {
 public:
  using Phase = DragPhaseMatches::Phase;
  Impl(AppliedBrowserObservers& observers,
       uint64_t source,
       uint64_t session,
       uint64_t step)
      : observers(observers), source(source), session(session), step(step) {
    CHECK(session);
    CHECK(step);
    wake = observers.views().SubscribeWake(
        base::BindRepeating(&Impl::Capture, weak.GetWeakPtr()));
  }
  ~Impl() override {
    wake = {};
    for (const auto& [phase, token] : tokens) {
      observers->views().OwnerDestroyed(token);
    }
    if (!preview_token.is_empty())
      observers->views().OwnerDestroyed(preview_token);
    for (const auto& token : extra_tokens)
      observers->views().OwnerDestroyed(token);
  }
  struct Context {
    std::unique_ptr<views::ViewTracker> view;
    AppliedOwnerToken browser;
    std::unique_ptr<views::EventMonitor> monitor;
  };
  TabDragController* Controller() const {
    for (const auto& context : contexts) {
      auto* view = static_cast<TabDragContext*>(context.view->view());
      if (view && view->GetDragController()) {
        return view->GetDragController();
      }
    }
    return nullptr;
  }
  AppliedOwnerToken AttachedBrowser(const TabDragContext* attached) const {
    for (const auto& context : contexts) {
      if (context.view->view() == attached) {
        return context.browser;
      }
    }
    return {};
  }
  DragPhaseMatches Value(Phase requested) const {
    DragPhaseMatches value{.session = session,
                           .step = step,
                           .source_handle = source,
                           .phase = Phase::kInactive,
                           .input_acknowledged = acknowledged,
                           .last_input_screen = last_input};
    auto* controller = Controller();
    if (requested == Phase::kLoopEnded && ended_step == step) {
      value.phase = Phase::kLoopEnded;
    } else if (controller && controller->active()) {
      const auto& data = controller->GetSessionData();
      auto* contents = data.source_view_index_ < data.tab_drag_data_.size()
                           ? data.source_dragged_contents()
                           : nullptr;
      value.source_handle = source;
      if (const auto* tab = contents
                                ? tabs::TabInterface::GetFromContents(contents)
                                : nullptr) {
        value.source_handle = tab->GetHandle().raw_value();
      }
      const auto* attached = controller->attached_context();
      value.attached_browser = AttachedBrowser(attached);
      if (requested == Phase::kCreated && !controller->started_drag()) {
        value.phase = Phase::kCreated;
      } else if (requested == Phase::kStarted && controller->started_drag()) {
        value.phase = Phase::kStarted;
      } else if (requested == Phase::kAttached && controller->started_drag() &&
                 attached && TabDragController::IsAttachedTo(attached)) {
        value.phase = Phase::kAttached;
      } else {
        value.phase =
            controller->started_drag() ? Phase::kStarted : Phase::kCreated;
      }
      if (attached && last_input) {
        // 세로 GetTabForContents는 slot을 생성하고 기존 slot의 bounds도 쓴다.
        // controller가 이미 보유한 attached slot만 읽고 없으면 target=0이다.
        for (const auto& tab_data : data.tab_drag_data_) {
          const auto* view = tab_data.attached_view.get();
          const auto* tab =
              tab_data.contents
                  ? tabs::TabInterface::GetFromContents(tab_data.contents)
                  : nullptr;
          if (view && tab && view->GetBoundsInScreen().Contains(*last_input)) {
            value.target_handle = tab->GetHandle().raw_value();
            break;
          }
        }
      }
    }
    // controller 삭제 뒤에도 native move/DnD loop가 남았다면 종료가 아니다.
    if (!controller && loop_started && !loop_completed)
      value.phase = Phase::kStarted;
    return value;
  }
  AppliedCoordinates Coordinates() const {
    for (const auto& context : contexts) {
      if (context.view->view() && context.view->view()->GetWidget() &&
          context.view->view()->GetWidget()->GetCompositor()) {
        return GetAppliedViewCoordinates(*context.view->view(),
                                         AppliedCoordinates::Space::kScreen);
      }
    }
    return last_coordinates;
  }
  std::optional<AppliedSnapshot> Read(Phase phase,
                                      AppliedOwnerToken token) const {
    const auto* context =
        contexts.empty() ? nullptr : contexts.front().view->view();
    const auto* widget = context ? context->GetWidget() : nullptr;
    return AppliedSnapshot{
        .owner = token,
        .value = Value(phase),
        .serials = {.apply_serial = serial},
        .coordinates = Coordinates(),
        .adapter_diagnostics = base::StringPrintf(
            "contexts=%zu controller=%d widget_active=%d", contexts.size(),
            Controller() != nullptr, widget && widget->IsActive())};
  }
  void OnEvent(const ui::Event&) override {
    Capture();
    observers->views().Notify();
  }
  void Capture() {
    last_coordinates = Coordinates();
    BindPreview();
    auto* controller = Controller();
    if (controller) {
      if (!loop_hooked && !loop_started) {
        controller->SetDragLoopDoneCallbackForTesting(ArmLoopCallback());
      }
    } else if (!loop_started) {
      loop_hooked = false;
    }
    bool changed = false;
    for (const auto& [phase, token] : tokens) {
      const auto current = Value(phase);
      auto found = last.find(phase);
      changed |= found == last.end() || found->second != current;
      last[phase] = current;
    }
    if (changed)
      ++serial;
  }
  void BindPreview() {
    if (!preview_root || !preview_root->view() || preview_bound)
      return;
    auto* preview =
        preview_root->view()->GetViewByID(kSidebarFavoritesDragPreviewViewId);
    if (!preview)
      return;
    auto* state = FavoritesDragPreviewAppliedState::From(*preview);
    CHECK(state);
    preview_bound = true;
    preview_view = std::make_unique<views::ViewTracker>(preview);
    preview_state = state->GetWeakPtr();
    observers->views().ObserveView(*preview);
    observers->views().TieToView(preview_token, *preview);
    preview_subscription = state->Subscribe(base::BindRepeating(
        [](base::WeakPtr<Impl> self,
           const FavoritesDragPreviewAppliedSnapshot&) {
          if (self) {
            ++self->serial;
            self->observers->views().Notify();
          }
        },
        weak.GetWeakPtr()));
  }
  std::optional<AppliedSnapshot> ReadPreview() const {
    if (!preview_root || !preview_root->view() ||
        (preview_bound && (!preview_view->view() || !preview_state)))
      return std::nullopt;
    PreviewApplied value{.source_handle = source};
    uint64_t apply_serial = 0;
    if (preview_state) {
      const auto snapshot = preview_state->GetSnapshot();
      apply_serial = snapshot.apply_serial;
      value.visible = snapshot.visible;
      value.animation_running = snapshot.running;
      value.screen_bounds = snapshot.screen_bounds;
      value.geometry_applied =
          snapshot.visible == preview_view->view()->GetVisible() &&
          snapshot.screen_bounds == preview_view->view()->GetBoundsInScreen();
    }
    return AppliedSnapshot{
        .owner = preview_token,
        .value = value,
        .serials = {.apply_serial = apply_serial},
        .coordinates = GetAppliedViewCoordinates(
            *preview_root->view(), AppliedCoordinates::Space::kScreen)};
  }
  base::OnceClosure ArmLoopCallback() {
    loop_hooked = true;
    loop_completed = false;
    return base::BindOnce(&Impl::LoopEnded, weak.GetWeakPtr(),
                          ++loop_generation);
  }
  void LoopEnded(uint64_t generation) {
    if (generation != loop_generation || loop_completed)
      return;
    loop_completed = true;
    loop_started = false;
    loop_hooked = false;
    ended_step = step;
    ++serial;
    observers->views().Notify();
  }
  void Ack(uint64_t input_step, DispatchDone done, gfx::Point screen) {
    if (input_step != step) {
      std::move(done).Run(AppliedDispatchStatus::kRejected);
      return;
    }
    last_input = screen;
    acknowledged = true;
    ++serial;
    Capture();
    observers->views().Notify();
    std::move(done).Run(AppliedDispatchStatus::kAccepted);
  }
  void WidgetLoopStarted(views::Widget* widget) {
    Capture();
    if (!loop_generation || loop_completed) {
      ++loop_generation;
      loop_completed = false;
    }
    loop_started = true;
    ended_step.reset();
    widget_loops[widget] = loop_generation;
    ++serial;
    observers->views().Notify();
  }
  void WidgetLoopEnded(views::Widget* widget) {
    const auto loop = widget_loops.extract(widget);
    if (!loop.empty())
      LoopEnded(loop.mapped());
  }
  void OnWidgetUserDragStarted(views::Widget* widget) override {
    WidgetLoopStarted(widget);
  }
  void OnWidgetDragDropWillStart(views::Widget* widget) override {
    WidgetLoopStarted(widget);
  }
  void OnWidgetUserDragEnded(views::Widget* widget) override {
    WidgetLoopEnded(widget);
  }
  void OnWidgetDragDropCompleted(views::Widget* widget) override {
    WidgetLoopEnded(widget);
  }
  void OnWidgetDestroying(views::Widget* dying) override {
    for (auto& context : contexts) {
      if (context.view->view() && context.view->view()->GetWidget() == dying)
        context.monitor.reset();
    }
  }
  void OnWidgetDestroyed(views::Widget* dying) override {
    widget_loops.erase(dying);
    widgets.RemoveObservation(dying);
  }

  raw_ref<AppliedBrowserObservers> observers;
  uint64_t source;
  uint64_t session;
  uint64_t step;
  uint64_t serial = 0;
  bool acknowledged = false;
  std::optional<uint64_t> ended_step;
  std::optional<gfx::Point> last_input;
  AppliedCoordinates last_coordinates{.space =
                                          AppliedCoordinates::Space::kScreen};
  bool loop_hooked = false;
  uint64_t loop_generation = 0;
  bool loop_completed = false;
  bool loop_started = false;
  std::map<views::Widget*, uint64_t> widget_loops;
  std::vector<Context> contexts;
  std::map<Phase, AppliedOwnerToken> tokens;
  std::map<Phase, DragPhaseMatches> last;
  AppliedOwnerToken preview_token;
  bool preview_bound = false;
  std::unique_ptr<views::ViewTracker> preview_root;
  std::unique_ptr<views::ViewTracker> preview_view;
  base::WeakPtr<FavoritesDragPreviewAppliedState> preview_state;
  base::CallbackListSubscription preview_subscription;
  std::vector<AppliedOwnerToken> extra_tokens;
  base::CallbackListSubscription wake;
  base::ScopedMultiSourceObservation<views::Widget, views::WidgetObserver>
      widgets{this};
  base::WeakPtrFactory<Impl> weak{this};
};

DragAppliedObserver::DragAppliedObserver(AppliedBrowserObservers& observers,
                                         TabDragContext& context,
                                         BrowserWindowInterface& browser,
                                         uint64_t source_handle,
                                         uint64_t session,
                                         uint64_t step)
    : impl_(std::make_unique<Impl>(observers, source_handle, session, step)) {
  AddContext(context, browser);
}
DragAppliedObserver::~DragAppliedObserver() = default;
AppliedOwnerToken DragAppliedObserver::ObservePhase(
    DragPhaseMatches::Phase phase) {
  const auto [it, inserted] =
      impl_->tokens.try_emplace(phase, AppliedOwnerToken::Create());
  if (inserted) {
    impl_->observers->views().RegisterSignal(
        it->second, AppliedConditionKind::kDragPhaseMatches,
        base::BindRepeating(&Impl::Read, base::Unretained(impl_.get()), phase,
                            it->second));
  }
  return it->second;
}
AppliedOwnerToken DragAppliedObserver::ObserveVerticalPreview(
    views::View& tab_strip) {
  if (impl_->preview_root)
    return impl_->preview_token;
  impl_->preview_token = AppliedOwnerToken::Create();
  impl_->preview_root = std::make_unique<views::ViewTracker>(&tab_strip);
  impl_->observers->views().ObserveView(tab_strip);
  impl_->observers->views().RegisterSignal(
      impl_->preview_token, AppliedConditionKind::kPreviewApplied,
      base::BindRepeating(&Impl::ReadPreview, base::Unretained(impl_.get())));
  impl_->observers->views().TieToView(impl_->preview_token, tab_strip);
  impl_->BindPreview();
  return impl_->preview_token;
}
AppliedOwnerToken DragAppliedObserver::ObserveGroupDrag(views::View& group) {
  const auto token = AppliedOwnerToken::Create();
  impl_->extra_tokens.push_back(token);
  impl_->observers->views().ObserveView(group);
  impl_->observers->views().RegisterSignal(
      token, AppliedConditionKind::kViewStateMatches,
      base::BindRepeating(
          [](views::ViewTracker* tracker,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            auto* group = views::AsViewClass<TabGroupView>(tracker->view());
            if (!group)
              return std::nullopt;
            return AppliedSnapshot{
                .owner = token,
                .value =
                    ViewStateMatches{
                        .handling_drag = group->IsHandlingDrag(),
                        .collapsed = group->IsCollapsed(),
                        .vertical_drag_only =
                            group->drag_axes() ==
                            DraggedTabsContainer::DragAxes::kVerticalOnly},
                .coordinates = GetAppliedViewCoordinates(*group)};
          },
          base::Owned(new views::ViewTracker(&group)), token));
  impl_->observers->views().TieToView(token, group);
  return token;
}
AppliedOwnerToken DragAppliedObserver::ObserveHorizontalPreview(
    TabStrip& strip,
    TabSlotView& source) {
  const auto token = AppliedOwnerToken::Create();
  impl_->extra_tokens.push_back(token);
  auto& views = impl_->observers->views();
  views.ObserveView(strip);
  views.ObserveView(source);
  views.RegisterSignal(
      token, AppliedConditionKind::kPreviewApplied,
      base::BindRepeating(
          [](base::WeakPtr<Impl> self, views::ViewTracker* strip_tracker,
             views::ViewTracker* source_tracker,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            if (!self || !strip_tracker->view() || !source_tracker->view())
              return std::nullopt;
            auto* strip = static_cast<TabStrip*>(strip_tracker->view());
            auto* slot = static_cast<TabSlotView*>(source_tracker->view());
            auto* model = strip->GetDragContext()->GetTabStripModel();
            bool applied = strip->GetTabCount() == model->count();
            for (int i = 0; applied && i < model->count(); ++i) {
              applied &=
                  strip->tab_at(i)->group() == model->GetTabGroupForTab(i);
            }
            const auto bounds = slot->GetBoundsInScreen();
            if (slot->dragging())
              applied &= slot->IsDrawn() && self->last_input &&
                         bounds.Contains(*self->last_input);
            return AppliedSnapshot{
                .owner = token,
                .value = PreviewApplied{.source_handle = self->source,
                                        .visible = slot->dragging(),
                                        .animation_running = slot->animating(),
                                        .screen_bounds = bounds,
                                        .geometry_applied = applied},
                .serials = {.apply_serial = self->serial},
                .coordinates = GetAppliedViewCoordinates(
                    *strip, AppliedCoordinates::Space::kScreen),
                // drag slot은 일반 layout에서 제외될 수 있다. pending flag를
                // 완료 조건으로 쓰지 않고 실제 배치와 별도로 진단에 남긴다.
                .adapter_diagnostics = base::StringPrintf(
                    "strip_needs_layout=%d slot_needs_layout=%d "
                    "container_animating=%d widget_active=%d model_count=%d "
                    "view_count=%d dragging=%d",
                    strip->needs_layout(), slot->needs_layout(),
                    strip->tab_container_for_testing()->IsAnimating(),
                    strip->GetWidget() && strip->GetWidget()->IsActive(),
                    model->count(), strip->GetTabCount(), slot->dragging())};
          },
          impl_->weak.GetWeakPtr(), base::Owned(new views::ViewTracker(&strip)),
          base::Owned(new views::ViewTracker(&source)), token));
  views.TieToView(token, strip);
  views.TieToView(token, source);
  return token;
}
AppliedOwnerToken DragAppliedObserver::ObserveCapsuleTheme(
    views::View& decoration) {
  const auto token = AppliedOwnerToken::Create();
  impl_->extra_tokens.push_back(token);
  impl_->observers->views().ObserveView(decoration);
  impl_->observers->views().ObserveCapsule(decoration, 1);
  impl_->observers->views().RegisterSignal(
      token, AppliedConditionKind::kPresentationMatches,
      base::BindRepeating(
          [](views::ViewTracker* tracker,
             AppliedOwnerToken token) -> std::optional<AppliedSnapshot> {
            auto* view = tracker->view();
            if (!view)
              return std::nullopt;
            return AppliedSnapshot{
                .owner = token,
                .value = PresentationMatches{.use_native_theme =
                                                 !UsesHorizontalTabGroupCapsule(
                                                     *view)},
                .coordinates = GetAppliedViewCoordinates(*view)};
          },
          base::Owned(new views::ViewTracker(&decoration)), token));
  impl_->observers->views().TieToView(token, decoration);
  return token;
}
void DragAppliedObserver::AddContext(TabDragContext& context,
                                     BrowserWindowInterface& browser) {
  impl_->observers->views().ObserveView(context);
  impl_->contexts.push_back({std::make_unique<views::ViewTracker>(&context),
                             impl_->observers->BrowserToken(browser), nullptr});
  if (context.GetWidget()) {
    impl_->contexts.back().monitor =
        views::EventMonitor::CreateApplicationMonitor(
            impl_.get(), context.GetWidget()->GetNativeWindow(),
            {ui::EventType::kMousePressed, ui::EventType::kMouseReleased,
             ui::EventType::kMouseMoved, ui::EventType::kMouseDragged,
             ui::EventType::kGestureScrollBegin,
             ui::EventType::kGestureScrollUpdate,
             ui::EventType::kGestureScrollEnd});
  }
  if (context.GetWidget() &&
      !impl_->widgets.IsObservingSource(context.GetWidget())) {
    impl_->widgets.AddObservation(context.GetWidget());
  }
  impl_->Capture();
}
void DragAppliedObserver::ObserveHorizontalControllerCreation() {
  auto* context =
      static_cast<TabDragContext*>(impl_->contexts.front().view->view());
  CHECK(context);
  context->SetDragControllerCallbackForTesting(base::BindOnce(
      [](base::WeakPtr<Impl> impl, TabDragController*) {
        if (impl) {
          ++impl->serial;
          impl->Capture();
          impl->observers->views().Notify();
        }
      },
      impl_->weak.GetWeakPtr()));
}
base::OnceClosure DragAppliedObserver::InputAck(DispatchDone done,
                                                gfx::Point screen) {
  return base::BindOnce(&Impl::Ack, impl_->weak.GetWeakPtr(), impl_->step,
                        std::move(done), screen);
}
base::OnceClosure DragAppliedObserver::InputAckAtCursor(DispatchDone done) {
  return base::BindOnce(
      [](base::WeakPtr<Impl> impl, uint64_t step, DispatchDone done) {
        if (impl)
          impl->Ack(step, std::move(done),
                    display::Screen::Get()->GetCursorScreenPoint());
      },
      impl_->weak.GetWeakPtr(), impl_->step, std::move(done));
}
void DragAppliedObserver::BeginStep(uint64_t step) {
  CHECK_GT(step, impl_->step);
  impl_->step = step;
  impl_->acknowledged = false;
  impl_->last_input.reset();
  impl_->ended_step.reset();
  impl_->Capture();
}

base::OnceClosure DragAppliedObserverTestApi::ArmLoopCallback(
    DragAppliedObserver& observer) {
  return observer.impl_->ArmLoopCallback();
}

testing::AssertionResult PrepareNativeDragInput(
    AppliedTestContext<AppliedBrowserObservers>& applied,
    BrowserWindowInterface& browser) {
  auto* widget = BrowserView::GetBrowserViewForBrowser(&browser)->GetWidget();
  return applied.Run(
      {applied.Widget(*widget, {.visible = true}, AppliedMatch::kWidgetVisible),
       applied.Widget(*widget, {.active = true}, AppliedMatch::kWidgetActive)},
      [&browser] {
        ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(&browser));
      },
      AppliedDomain::kNativeWindow);
}

}  // namespace yee::test
