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
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_context.h"
#include "chrome/browser/ui/views/tabs/dragging/tab_drag_controller.h"
#include "chrome/browser/ui/views/tabs/tab_slot_view.h"
#include "components/tabs/public/tab_interface.h"
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
      value.source_handle = 0;
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
        auto* context = const_cast<TabDragContext*>(attached);
        auto* model = context->GetTabStripModel();
        for (int i = 0; i < model->count(); ++i) {
          auto* tab = model->GetTabAtIndex(i);
          auto* view = context->GetTabForContents(tab->GetContents());
          if (view && view->GetBoundsInScreen().Contains(*last_input)) {
            value.target_handle = tab->GetHandle().raw_value();
            break;
          }
        }
      }
    }
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
    return AppliedSnapshot{.owner = token,
                           .value = Value(phase),
                           .serials = {.apply_serial = serial},
                           .coordinates = Coordinates()};
  }
  void OnEvent(const ui::Event&) override {
    Capture();
    observers->views().Notify();
  }
  void Capture() {
    last_coordinates = Coordinates();
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

}  // namespace yee::test
