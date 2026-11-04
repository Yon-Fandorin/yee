// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_KOMBUCHA_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_KOMBUCHA_H_

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "ui/base/interaction/interactive_test.h"
#include "ui/base/interaction/state_observer.h"

namespace yee::test {

// 기본값과 소멸 알림은 성공이 아니다.
enum class AppliedOutcome {
  kPending,
  kApplied,
  kTimedOut,
  kSuperseded,
  kOwnerDestroyed,
  kCancelled,
  kDispatchFailed,
  kUnsupportedCondition,
  kInvalidTicket,
};
std::ostream& operator<<(std::ostream& out, AppliedOutcome outcome);

class AppliedStateObserver : public ui::test::StateObserver<AppliedOutcome> {
 public:
  AppliedStateObserver(AppliedWaitSession& session,
                       AppliedTicket ticket,
                       AppliedConditions conditions,
                       base::OnceClosure failed = {});
  ~AppliedStateObserver() override;
  AppliedOutcome GetStateObserverInitialState() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Begin의 절대 deadline을 사용하며 abort 시 StateObserver부터 해제한다.
template <typename T>
class AppliedWaitInteractiveTestMixin : public T {
 protected:
  using typename T::MultiStep;
  template <typename... Args>
  bool RunTestSequence(Args&&... steps) {
    const size_t first = applied_states_.size();
    base::ScopedClosureRunner cleanup(
        base::BindOnce(&AppliedWaitInteractiveTestMixin::CleanUpAppliedStates,
                       base::Unretained(this), first));
    return T::RunTestSequence(std::forward<Args>(steps)...);
  }
  template <typename... Args>
  bool RunTestSequenceInContext(ui::ElementContext context, Args&&... steps) {
    const size_t first = applied_states_.size();
    base::ScopedClosureRunner cleanup(
        base::BindOnce(&AppliedWaitInteractiveTestMixin::CleanUpAppliedStates,
                       base::Unretained(this), first));
    return T::RunTestSequenceInContext(context, std::forward<Args>(steps)...);
  }
  MultiStep ArmApplied(ui::test::StateIdentifier<AppliedStateObserver> id,
                       AppliedWaitSession& session,
                       AppliedTicket ticket,
                       AppliedConditions conditions) {
    auto sequence = std::make_shared<base::WeakPtr<ui::InteractionSequence>>();
    auto context = std::make_shared<std::optional<ui::ElementContext>>();
    auto failed = std::make_shared<bool>(false);
    auto bind = T::Do([] {});
    bind.SetDescription("ArmApplied: sequence 수명 결합");
    bind.SetStartCallback(base::BindOnce(
        [](decltype(sequence) target, decltype(context) context,
           decltype(failed) failed, ui::InteractionSequence* current,
           ui::TrackedElement* element) {
          *target = current->AsWeakPtr();
          *context = element->context();
          if (*failed)
            current->FailForTesting();
        },
        sequence, context, failed));
    return T::Steps(
        std::move(bind),
        this->ObserveState(
            id, std::make_unique<AppliedStateObserver>(
                    session, std::move(ticket), std::move(conditions),
                    base::BindOnce(
                        [](decltype(sequence) target, decltype(failed) failed) {
                          *failed = true;
                          if (*target) {
                            (*target)->FailForTesting();
                          }
                        },
                        sequence, failed))),
        T::Do([this, context, id] {
          applied_states_.push_back({id.identifier(), **context, true});
        }));
  }
  MultiStep WaitForApplied(ui::test::StateIdentifier<AppliedStateObserver> id) {
    auto forget = T::Do([] {});
    forget.SetStartCallback(base::BindOnce(
        [](AppliedWaitInteractiveTestMixin* self,
           ui::test::UntypedStateIdentifier id, ui::InteractionSequence*,
           ui::TrackedElement* element) {
          for (auto& owned : self->applied_states_) {
            if (owned.id == id && owned.context == element->context())
              owned.active = false;
          }
        },
        base::Unretained(this), id.identifier()));
    return T::Steps(T::WaitForState(id, testing::Ne(AppliedOutcome::kPending)),
                    T::CheckState(id, AppliedOutcome::kApplied),
                    this->StopObservingState(id), std::move(forget));
  }

 private:
  void CleanUpAppliedStates(size_t first) {
    // Chromium은 외부 abort에서 state observer를 fixture teardown까지 남긴다.
    // sequence가 반환하는 즉시 요청 구독과 frame 예약부터 해제한다.
    for (size_t i = first; i < applied_states_.size(); ++i) {
      if (applied_states_[i].active)
        this->private_test_impl().RemoveStateObserver(
            applied_states_[i].id, applied_states_[i].context);
    }
    applied_states_.resize(first);
  }
  struct OwnedState {
    ui::test::UntypedStateIdentifier id;
    ui::ElementContext context;
    bool active;
  };
  std::vector<OwnedState> applied_states_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_KOMBUCHA_H_
