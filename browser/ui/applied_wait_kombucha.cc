// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_kombucha.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace yee::test {

class AppliedStateObserver::Impl {
 public:
  Impl(AppliedStateObserver& observer,
       AppliedWaitSession& session,
       AppliedTicket ticket,
       const AppliedConditions& conditions,
       base::OnceClosure failed)
      : observer(observer), failed(std::move(failed)) {
    observation = session.ObserveApplied(
        ticket, conditions,
        base::BindOnce(&Impl::Complete, base::Unretained(this)));
  }
  void Complete(AppliedWaitResult result) {
    outcome = static_cast<AppliedOutcome>(static_cast<int>(result.kind) + 1);
    auto failure = result.kind == AppliedResultKind::kApplied
                       ? base::OnceClosure()
                       : std::move(failed);
    if (result.kind != AppliedResultKind::kApplied) {
      LOG(ERROR) << result.ToString();
    }
    // 상태 알림이 sequence를 끝내고 이 객체를 파괴할 수 있다.
    auto* target = &observer.get();
    const auto state = outcome;
    target->OnStateObserverStateChanged(state);
    if (failure) {
      std::move(failure).Run();
    }
  }
  raw_ref<AppliedStateObserver> observer;
  AppliedOutcome outcome = AppliedOutcome::kPending;
  base::OnceClosure failed;
  AppliedObservation observation;
};

AppliedStateObserver::AppliedStateObserver(AppliedWaitSession& session,
                                           AppliedTicket ticket,
                                           AppliedConditions conditions,
                                           base::OnceClosure failed)
    : impl_(std::make_unique<Impl>(*this,
                                   session,
                                   ticket,
                                   conditions,
                                   std::move(failed))) {}
AppliedStateObserver::~AppliedStateObserver() {
  impl_->observation.Reset();
}
AppliedOutcome AppliedStateObserver::GetStateObserverInitialState() const {
  return impl_->outcome;
}
std::ostream& operator<<(std::ostream& out, AppliedOutcome outcome) {
  return out << "AppliedOutcome(" << static_cast<int>(outcome) << ')';
}

}  // namespace yee::test
