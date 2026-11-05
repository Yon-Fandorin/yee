// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_KOMBUCHA_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_KOMBUCHA_H_

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"
#include "chrome/browser/ui/views/yee/applied_wait_test_util.h"
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
    return ArmApplied(
        id, session, [ticket] { return ticket; },
        [conditions] { return conditions; });
  }
  template <class Ticket, class Conditions>
  MultiStep ArmApplied(ui::test::StateIdentifier<AppliedStateObserver> id,
                       AppliedWaitSession& session,
                       Ticket ticket,
                       Conditions conditions) {
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
            id, std::ref(session), std::move(ticket), std::move(conditions),
            [sequence, failed]() -> base::OnceClosure {
              return base::BindOnce(
                  [](decltype(sequence) target, decltype(failed) failed) {
                    *failed = true;
                    if (*target) {
                      (*target)->FailForTesting();
                    }
                  },
                  sequence, failed);
            }),
        T::Do([this, context, id] {
          applied_states_.push_back({id.identifier(), **context, true});
        }));
  }
  template <class Source, class Conditions, class Action>
  MultiStep ApplyAndWait(AppliedTestContext<Source>& context,
                         Conditions conditions,
                         Action action,
                         AppliedDomain domain = AppliedDomain::kLayout,
                         bool no_op = false) {
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kAppliedAction);
    struct Request {
      AppliedTicket ticket;
      AppliedConditions conditions;
    };
    auto request = std::make_shared<Request>();
    return T::Steps(
        T::Do([&context, request, conditions = std::move(conditions), domain,
               no_op]() mutable {
          request->conditions = conditions();
          request->ticket =
              context.Begin(request->conditions, domain,
                            TestTimeouts::action_timeout(), no_op);
        }),
        ArmApplied(
            kAppliedAction, context.session(),
            [request] { return request->ticket; },
            [request] { return request->conditions; }),
        T::Do([&context, request, action = std::move(action)]() mutable {
          const auto error = context.session().Dispatch(
              request->ticket, base::BindLambdaForTesting(std::move(action)));
          EXPECT_FALSE(error) << (error ? error->ToString() : "");
        }),
        WaitForApplied(kAppliedAction));
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
  // Kombucha 입력 step이 반환할 때의 실제 입력 완료를 Dispatch ACK로 쓴다.
  template <class Source, class Conditions>
  MultiStep ApplyStepsAndWait(AppliedTestContext<Source>& context,
                              Conditions conditions,
                              MultiStep input) {
    DEFINE_LOCAL_STATE_IDENTIFIER_VALUE(AppliedStateObserver, kAppliedInput);
    struct Request {
      AppliedTicket ticket;
      AppliedConditions conditions;
      DispatchDone acknowledged;
    };
    auto request = std::make_shared<Request>();
    return T::Steps(
        T::Do(
            [&context, request, conditions = std::move(conditions)]() mutable {
              request->conditions = conditions();
              request->ticket = context.Begin(request->conditions);
            }),
        ArmApplied(
            kAppliedInput, context.session(),
            [request] { return request->ticket; },
            [request] { return request->conditions; }),
        T::Do([&context, request] {
          EXPECT_FALSE(context.session().DispatchAsync(
              request->ticket,
              base::BindLambdaForTesting([request](DispatchDone done) {
                request->acknowledged = std::move(done);
              })));
        }),
        std::move(input), T::Do([request] {
          if (request->acknowledged)
            std::move(request->acknowledged)
                .Run(AppliedDispatchStatus::kAccepted);
        }),
        WaitForApplied(kAppliedInput));
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
