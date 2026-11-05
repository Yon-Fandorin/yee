// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee::test {
namespace {

class FakeSignalSource : public AppliedSignalSource {
 public:
  bool Supports(const AppliedCondition& condition) const override {
    return !unsupported.contains(GetAppliedConditionKind(condition.expected));
  }

  bool IsOwnerAlive(AppliedOwnerToken owner) const override {
    return alive_.contains(owner);
  }

  std::optional<AppliedSnapshot> Read(
      const AppliedSignalKey& key) const override {
    ++read_count;
    if (read_override)
      return read_override;
    const auto it = states_.find({key.owner, key.kind});
    return it == states_.end() ? std::nullopt : std::make_optional(it->second);
  }

  base::CallbackListSubscription Subscribe(
      const std::vector<AppliedSignalKey>& keys,
      Observer observer) override {
    auto subscription = observers_.Add(base::BindRepeating(
        [](const std::vector<AppliedSignalKey>& keys, Observer observer,
           const AppliedSignalSample& sample) {
          if (std::ranges::any_of(keys, [&](const auto& key) {
                return std::ranges::contains(sample.destroyed_owners,
                                             key.owner) ||
                       std::ranges::any_of(
                           sample.snapshots, [&](const auto& s) {
                             return s.owner == key.owner &&
                                    GetAppliedConditionKind(s.value) ==
                                        key.kind;
                           });
              })) {
            observer.Run(sample);
          }
        },
        keys, std::move(observer)));
    if (on_subscribe) {
      std::move(on_subscribe).Run();
    }
    return subscription;
  }

  void AddOwner(AppliedOwnerToken owner) { alive_.insert(owner); }

  void Set(AppliedSnapshot snapshot) {
    states_[{snapshot.owner, GetAppliedConditionKind(snapshot.value)}] =
        std::move(snapshot);
  }

  void Erase(const AppliedSignalKey& key) {
    states_.erase({key.owner, key.kind});
  }

  void Emit(std::vector<AppliedSnapshot> snapshots) {
    for (const auto& snapshot : snapshots) {
      Set(snapshot);
    }
    observers_.Notify(AppliedSignalSample{std::move(snapshots), {}});
  }

  void Destroy(AppliedOwnerToken owner) {
    alive_.erase(owner);
    observers_.Notify(AppliedSignalSample{{}, {owner}});
  }

  bool has_observers() const { return !observers_.empty(); }
  mutable size_t read_count = 0;
  std::set<AppliedConditionKind> unsupported;
  base::OnceClosure on_subscribe;
  std::optional<AppliedSnapshot> read_override;

 private:
  std::set<AppliedOwnerToken> alive_;
  std::map<std::pair<AppliedOwnerToken, AppliedConditionKind>, AppliedSnapshot>
      states_;
  base::RepeatingCallbackList<void(const AppliedSignalSample&)> observers_;
};

PageHostsApplied Page(int width = 100) {
  return {7,
          42,
          gfx::Rect(0, 0, width, 60),
          gfx::Rect(1, 1, width - 2, 58),
          gfx::RoundedCornersF(8),
          gfx::Rect(0, 0, width, 60)};
}

AppliedSnapshot Snapshot(AppliedOwnerToken owner,
                         AppliedConditionValue value,
                         uint64_t serial = 1) {
  return {owner, std::move(value), {serial, serial, serial}, serial};
}

class AppliedWaitTest : public testing::Test {
 protected:
  AppliedWaitTest() {
    source_.AddOwner(owner_);
    source_.AddOwner(other_);
    source_.Set(Snapshot(owner_, Page()));
    source_.Set(Snapshot(other_, Page()));
  }

  WaitOptions Options() const {
    WaitOptions options;
    options.deadline = base::TimeTicks::Now() + base::Seconds(10);
    options.test_name = "AppliedWaitTest";
    options.parameter = "fake";
    return options;
  }

  AppliedCondition Condition(AppliedConditionValue value = Page(),
                             AppliedOwnerToken owner = {}) const {
    return {owner.is_empty() ? owner_ : owner, std::move(value)};
  }

  AppliedIntent Intent(const AppliedConditions& conditions,
                       AppliedDomain domain = AppliedDomain::kLayout) const {
    AppliedIntent intent;
    intent.domain = domain;
    for (const auto& condition : conditions) {
      if (!std::ranges::contains(intent.owner_tokens, condition.owner)) {
        intent.owner_tokens.push_back(condition.owner);
      }
    }
    return intent;
  }

  AppliedTicket Begin(const AppliedConditions& conditions,
                      AppliedDomain domain = AppliedDomain::kLayout) {
    return session_.BeginRequest(Intent(conditions, domain), conditions,
                                 Options());
  }

  void Dispatch(const AppliedTicket& ticket, base::OnceClosure action) {
    EXPECT_FALSE(session_.Dispatch(ticket, std::move(action)));
  }

  void Apply(AppliedConditionValue value = Page(), uint64_t serial = 2) {
    source_.Emit({Snapshot(owner_, std::move(value), serial)});
  }

  base::test::TaskEnvironment tasks_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  const AppliedOwnerToken owner_ = AppliedOwnerToken::Create();
  const AppliedOwnerToken other_ = AppliedOwnerToken::Create();
  FakeSignalSource source_;
  AppliedWaitSession session_{source_};
};

TEST_F(AppliedWaitTest, CallbackBeforeWaitRetainsAppliedResult) {
  const AppliedConditions conditions{Condition(Page(200))};
  const auto ticket = Begin(conditions);
  EXPECT_EQ(1u, ticket.baselines[0].serials.apply_serial);
  EXPECT_EQ(ticket.intent.targets[0].value, conditions[0].expected);
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(Page(200)); }));
  tasks_.RunUntilIdle();
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
  EXPECT_EQ(2u, result.diagnostics.conditions[0].last->serials.apply_serial);
  EXPECT_FALSE(source_.has_observers());
}

TEST_F(AppliedWaitTest, SameValueReapplyRequiresNewApplySerial) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             auto observation = Snapshot(owner_, Page(), 2);
             observation.serials.apply_serial = 1;
             source_.Emit({observation});
           }));
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  Apply(Page(), 3);
  EXPECT_EQ(AppliedResultKind::kApplied, future.Get().kind);
}

TEST_F(AppliedWaitTest, AlreadyAppliedIsOnlyAllowedForExplicitNoOp) {
  const AppliedConditions conditions{Condition()};
  const auto fresh = Begin(conditions);
  Dispatch(fresh, base::DoNothing());
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session_.WaitForApplied(fresh, conditions).kind);

  auto intent = Intent(conditions);
  intent.operation = AppliedIntent::Operation::kNoOp;
  auto options = Options();
  options.already_applied = WaitOptions::AlreadyApplied::kAllowAlreadyApplied;
  const auto noop = session_.BeginRequest(intent, conditions, options);
  Dispatch(noop, base::DoNothing());
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(noop, conditions).kind);

  intent.operation = AppliedIntent::Operation::kApply;
  const auto invalid = session_.BeginRequest(intent, conditions, options);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(invalid, conditions).kind);
}

TEST_F(AppliedWaitTest, OneOfTwoOwnersApplyingCannotCompleteAnd) {
  const AppliedConditions conditions{Condition(), Condition(Page(), other_)};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  EXPECT_TRUE(result.diagnostics.conditions[0].satisfied);
  EXPECT_FALSE(result.diagnostics.conditions[1].satisfied);
  EXPECT_EQ(1u, result.diagnostics.conditions[1].last->serials.apply_serial);
}

TEST_F(AppliedWaitTest, StaleApplyAndReverseNativeGenerationAreRejected) {
  for (bool reverse_native : {false, true}) {
    const AppliedConditions conditions{Condition(Page(200))};
    const auto ticket = Begin(conditions);
    Dispatch(ticket, base::BindLambdaForTesting([&] {
               Apply(Page(90), reverse_native ? 5 : 3);
               auto stale = Snapshot(owner_, Page(200), reverse_native ? 6 : 4);
               if (reverse_native) {
                 stale.serials.native_generation = 4;
               } else {
                 stale.serials.apply_serial = 2;
               }
               source_.Emit({stale});
             }));
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    EXPECT_TRUE(
        std::ranges::any_of(result.diagnostics.events, [](const auto& e) {
          return e.kind == AppliedEventKind::kStaleSignal;
        }));
  }
}

TEST_F(AppliedWaitTest, StaleObservationSequenceDoesNotLatchTransientState) {
  auto condition = Condition(AnimationMatches{9, true, 0.2, 0.8});
  condition.freshness = AppliedFreshness::kNewObservation;
  condition.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  source_.Set(Snapshot(owner_, AnimationMatches{9, false, 0, 0}, 1));
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             Apply(AnimationMatches{9, false, 1, 1}, 3);
             auto stale =
                 Snapshot(owner_, AnimationMatches{9, true, 0.5, 0.5}, 4);
             stale.serials.observation_sequence = 2;
             source_.Emit({stale});
           }));
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, DispatchFenceExcludesChangesBetweenBeginAndDispatch) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Apply();
  Dispatch(ticket, base::DoNothing());
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  EXPECT_EQ(1u, result.diagnostics.conditions[0].baseline.apply_serial);
  EXPECT_EQ(2u, result.diagnostics.conditions[0].dispatch_fence.apply_serial);
}

TEST_F(AppliedWaitTest, NewRequestSupersedesAnyOverlappingOwnerInSameDomain) {
  const AppliedConditions both{Condition(), Condition(Page(), other_)};
  const auto old = Begin(both);
  Dispatch(old, base::DoNothing());
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation = session_.ObserveApplied(old, both, future.GetCallback());
  const AppliedConditions one{Condition(Page(), other_)};
  const auto next = Begin(one);
  EXPECT_EQ(AppliedResultKind::kSuperseded, future.Get().kind);
  Dispatch(next, base::BindLambdaForTesting(
                     [&] { source_.Emit({Snapshot(other_, Page(), 2)}); }));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(next, one).kind);
}

TEST_F(AppliedWaitTest, OtherOwnersAndDomainsStayIndependent) {
  const AppliedConditions first_conditions{Condition()};
  const AppliedConditions other_conditions{Condition(Page(), other_)};
  const auto first = Begin(first_conditions);
  const auto other = Begin(other_conditions);
  const auto other_domain = Begin(first_conditions, AppliedDomain::kSurface);
  Dispatch(first, base::DoNothing());
  Dispatch(other, base::DoNothing());
  Dispatch(other_domain, base::DoNothing());
  source_.Emit({Snapshot(owner_, Page(), 2), Snapshot(other_, Page(), 2)});
  for (const auto& [ticket, conditions] :
       std::vector<std::pair<AppliedTicket, AppliedConditions>>{
           {first, first_conditions},
           {other, other_conditions},
           {other_domain, first_conditions}}) {
    EXPECT_EQ(AppliedResultKind::kApplied,
              session_.WaitForApplied(ticket, conditions).kind);
  }
}

TEST_F(AppliedWaitTest, UnexpectedOwnerDestructionIsTerminal) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket,
           base::BindLambdaForTesting([&] { source_.Destroy(owner_); }));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, result.kind);
  EXPECT_EQ(
      std::vector<AppliedConditionBlocker>{
          AppliedConditionBlocker::kOwnerDestroyed},
      result.diagnostics.conditions[0].blockers);
  EXPECT_NE(std::string::npos, result.ToString().find("owner-destroyed,"));
}

TEST_F(AppliedWaitTest, ExpectedViewDestructionAndWidgetCloseLatchSuccess) {
  ViewStateMatches view;
  view.destroyed = true;
  WidgetStateMatches widget;
  widget.closed = true;
  for (const AppliedConditionValue& value :
       std::vector<AppliedConditionValue>{view, widget}) {
    const auto owner = AppliedOwnerToken::Create();
    source_.AddOwner(owner);
    const AppliedConditions conditions{Condition(value, owner)};
    const auto ticket = Begin(conditions);
    Dispatch(ticket,
             base::BindLambdaForTesting([&] { source_.Destroy(owner); }));
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
    EXPECT_TRUE(result.diagnostics.conditions[0].owner_destroyed);
  }
}

TEST_F(AppliedWaitTest, DestructionStillFailsIfAnotherConditionNeedsLiveOwner) {
  ViewStateMatches view;
  view.destroyed = true;
  const AppliedConditions conditions{Condition(view), Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket,
           base::BindLambdaForTesting([&] { source_.Destroy(owner_); }));
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, ReplacementOwnerCannotInheritOldTicket) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             source_.Destroy(owner_);
             const auto replacement = AppliedOwnerToken::Create();
             source_.AddOwner(replacement);
             source_.Emit({Snapshot(replacement, Page(), 2)});
           }));
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, DestructionBeforeDispatchIsNotExpectedDestruction) {
  ViewStateMatches view;
  view.destroyed = true;
  const AppliedConditions conditions{Condition(view)};
  const auto ticket = Begin(conditions);
  source_.Destroy(owner_);
  Dispatch(ticket, base::DoNothing());
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kOwnerDestroyed, result.kind);
  EXPECT_EQ(
      std::vector<AppliedConditionBlocker>{
          AppliedConditionBlocker::kDestroyedBeforeDispatch},
      result.diagnostics.conditions[0].blockers);
  EXPECT_NE(std::string::npos,
            result.ToString().find("owner-destroyed-before-dispatch"));
}

TEST_F(AppliedWaitTest,
       TimeoutKeepsExpectedLastSerialsAndLast32TimestampedEvents) {
  const AppliedConditions conditions{Condition(Page(200))};
  const auto options = Options();
  const auto ticket =
      session_.BeginRequest(Intent(conditions), conditions, options);
  tasks_.RunUntilIdle();
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             for (uint64_t serial = 2; serial < 42; ++serial) {
               tasks_.AdvanceClock(base::Milliseconds(1));
               Apply(Page(90), serial);
             }
           }));
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  tasks_.FastForwardBy(options.deadline - base::TimeTicks::Now());
  const auto result = future.Take();
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  const auto& diagnostics = result.diagnostics;
  EXPECT_EQ(base::Seconds(10), diagnostics.captured_at - diagnostics.began_at);
  EXPECT_EQ(32u, diagnostics.events.size());
  EXPECT_EQ(1u, diagnostics.conditions[0].baseline.apply_serial);
  EXPECT_EQ(41u, diagnostics.conditions[0].last->serials.apply_serial);
  EXPECT_GT(diagnostics.wake_count, diagnostics.evaluation_count);
  EXPECT_TRUE(std::ranges::is_sorted(diagnostics.events, {},
                                     &AppliedWaitEvent::timestamp));
  const auto text = result.ToString();
  for (const char* field : {"TimedOut",
                            "test=AppliedWaitTest",
                            "parameter=fake",
                            "backend=",
                            "OS=",
                            "domain=",
                            "intent=",
                            "owner=",
                            "elapsed_ms=10000",
                            "budget_ms=10000",
                            "expected:",
                            "last:",
                            "baseline:",
                            "dispatch_fence:",
                            "apply=41",
                            "generation=41",
                            "observation=41",
                            "event +",
                            "DIP",
                            "DSF=1",
                            "RTL=0",
                            "clip=",
                            "radii=",
                            "target="}) {
    EXPECT_NE(std::string::npos, text.find(field)) << field;
  }
  EXPECT_FALSE(source_.has_observers());
}

TEST_F(AppliedWaitTest, CancelDeliversTerminalOnceAndIgnoresLateAckAndSignals) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  DispatchDone ack;
  EXPECT_FALSE(session_.DispatchAsync(
      ticket, base::BindLambdaForTesting(
                  [&](DispatchDone done) { ack = std::move(done); })));
  int callbacks = 0;
  auto observation = session_.ObserveApplied(
      ticket, conditions,
      base::BindLambdaForTesting([&](AppliedWaitResult result) {
        ++callbacks;
        EXPECT_EQ(AppliedResultKind::kCancelled, result.kind);
      }));
  EXPECT_FALSE(session_.Cancel(ticket));
  EXPECT_FALSE(session_.Cancel(ticket));
  std::move(ack).Run(AppliedDispatchStatus::kAccepted);
  Apply();
  tasks_.FastForwardBy(base::Seconds(20));
  EXPECT_EQ(1, callbacks);
  EXPECT_FALSE(source_.has_observers());
}

TEST_F(AppliedWaitTest, HandleReleaseStopsSubscriptionsQueuedWorkAndCallback) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  const auto before = session_.GetDiagnostics(ticket)->evaluation_count;
  observation.Reset();
  EXPECT_FALSE(source_.has_observers());
  Apply(Page(), 3);
  tasks_.FastForwardBy(base::Seconds(20));
  EXPECT_FALSE(future.IsReady());
  EXPECT_EQ(before, session_.GetDiagnostics(ticket)->evaluation_count);
}

TEST_F(AppliedWaitTest, HandleReleaseAlsoDropsQueuedTerminalDelivery) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  session_.Cancel(ticket);
  observation.Reset();
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
}

TEST_F(AppliedWaitTest, SessionDestructionStopsCallbacksAndHandleCanOutliveIt) {
  const AppliedConditions conditions{Condition()};
  auto session = std::make_unique<AppliedWaitSession>(source_);
  const auto ticket =
      session->BeginRequest(Intent(conditions), conditions, Options());
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session->ObserveApplied(ticket, conditions, future.GetCallback());
  session->Dispatch(ticket, base::DoNothing());
  session.reset();
  Apply();
  tasks_.FastForwardBy(base::Seconds(20));
  observation.Reset();
  EXPECT_FALSE(future.IsReady());
  EXPECT_FALSE(source_.has_observers());
}

TEST_F(AppliedWaitTest, TerminalCallbackMayDestroySession) {
  const AppliedConditions conditions{Condition()};
  auto session = std::make_unique<AppliedWaitSession>(source_);
  const auto ticket =
      session->BeginRequest(Intent(conditions), conditions, Options());
  int callbacks = 0;
  auto observation = session->ObserveApplied(
      ticket, conditions,
      base::BindLambdaForTesting([&](AppliedWaitResult result) {
        EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
        ++callbacks;
        session.reset();
      }));
  session->Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  tasks_.RunUntilIdle();
  EXPECT_EQ(1, callbacks);
  EXPECT_FALSE(source_.has_observers());
}

TEST_F(AppliedWaitTest,
       ObservedRunningLatchesButCurrentRunningSeesIdleInSameTask) {
  auto condition = Condition(AnimationMatches{9, true, 0.2, 0.8});
  condition.freshness = AppliedFreshness::kNewObservation;
  condition.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  source_.Set(Snapshot(owner_, AnimationMatches{9, false, 0, 0}));
  const AppliedConditions observed_conditions{condition};
  const auto observed = Begin(observed_conditions, AppliedDomain::kAnimation);
  condition.mode = AppliedEvaluationMode::kCurrent;
  const AppliedConditions current_conditions{condition};
  const auto current = Begin(current_conditions);
  DispatchDone first_ack;
  DispatchDone second_ack;
  session_.DispatchAsync(
      observed, base::BindLambdaForTesting(
                    [&](DispatchDone ack) { first_ack = std::move(ack); }));
  session_.DispatchAsync(
      current, base::BindLambdaForTesting(
                   [&](DispatchDone ack) { second_ack = std::move(ack); }));
  // callback 두 번이 평가 task 하나보다 먼저 실행되는 실제 순서를 재현한다.
  Apply(AnimationMatches{9, true, 0.5, 0.5}, 2);
  Apply(AnimationMatches{9, false, 1, 1}, 3);
  std::move(first_ack).Run(AppliedDispatchStatus::kAccepted);
  std::move(second_ack).Run(AppliedDispatchStatus::kAccepted);
  const auto result = session_.WaitForApplied(observed, observed_conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
  const auto& diagnostic = result.diagnostics.conditions[0];
  ASSERT_TRUE(diagnostic.observed);
  EXPECT_TRUE(std::get<AnimationMatches>(diagnostic.observed->value).running);
  EXPECT_FALSE(std::get<AnimationMatches>(diagnostic.last->value).running);
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session_.WaitForApplied(current, current_conditions).kind);
}

TEST_F(AppliedWaitTest, DeferredObservedSampleSurvivesNewerCurrentRead) {
  auto condition = Condition(AnimationMatches{19, true, 0, 1});
  condition.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  source_.Set(Snapshot(owner_, AnimationMatches{19, false, 0, 0}));
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions, AppliedDomain::kAnimation);
  Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        // 현재값은 이미 idle이어도 뒤따르는 callback은 실제 running 표본이다.
        source_.Set(Snapshot(owner_, AnimationMatches{19, false, 1, 1}, 3));
        tasks_.GetMainThreadTaskRunner()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] {
              source_.Emit(
                  {Snapshot(owner_, AnimationMatches{19, true, 0.5, 0.5}, 2)});
              source_.Set(
                  Snapshot(owner_, AnimationMatches{19, false, 1, 1}, 3));
            }));
      }));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  ASSERT_TRUE(result.diagnostics.conditions[0].observed);
  EXPECT_EQ(2u,
            result.diagnostics.conditions[0].observed->serials.apply_serial);
  EXPECT_FALSE(
      std::get<AnimationMatches>(result.diagnostics.conditions[0].last->value)
          .running);
}

TEST_F(AppliedWaitTest, ObservedGroupRequiresAllValuesInOneSample) {
  auto animation = Condition(AnimationMatches{9, true, 0.2, 0.8});
  auto page = Condition(Page(200));
  for (auto* condition : {&animation, &page}) {
    condition->mode = AppliedEvaluationMode::kObservedSinceDispatch;
    condition->freshness = AppliedFreshness::kNewObservation;
    condition->observation_group = 7;
  }
  const AppliedConditions conditions{animation, page};
  source_.Set(Snapshot(owner_, AnimationMatches{9, false, 0, 0}));
  const auto ticket = Begin(conditions);
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        source_.Emit({Snapshot(owner_, AnimationMatches{9, true, 0.5, 0.5}, 2),
                      Snapshot(owner_, Page(200), 3)});
      }));
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  source_.Emit({Snapshot(owner_, AnimationMatches{9, true, 0.5, 0.5}, 4),
                Snapshot(owner_, Page(200), 4)});
  const auto& result = future.Get();
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
  EXPECT_EQ(4u, result.diagnostics.conditions[0].observed->sample_id);
  EXPECT_EQ(4u, result.diagnostics.conditions[1].observed->sample_id);
}

TEST_F(AppliedWaitTest, DifferentObservedGroupsCanLatchSeparateSteps) {
  auto running = Condition(AnimationMatches{9, true, 0.2, 0.8});
  running.freshness = AppliedFreshness::kNewObservation;
  running.mode = AppliedEvaluationMode::kObservedSinceDispatch;
  auto idle = running;
  idle.expected = AnimationMatches{9, false, 1, 1};
  idle.observation_group = 1;
  const AppliedConditions conditions{running, idle};
  source_.Set(Snapshot(owner_, AnimationMatches{9, false, 0, 0}));
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             Apply(AnimationMatches{9, true, 0.5, 0.5}, 2);
             Apply(AnimationMatches{9, false, 1, 1}, 3);
           }));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
  EXPECT_EQ(2u, result.diagnostics.conditions[0].observed->sample_id);
  EXPECT_EQ(3u, result.diagnostics.conditions[1].observed->sample_id);
}

TEST_F(AppliedWaitTest, NoEvaluationGrowthWithoutEventsAndOneTaskPerWakeBatch) {
  const AppliedConditions conditions{Condition(Page(200))};
  const auto ticket = Begin(conditions);
  tasks_.RunUntilIdle();
  const auto initial = session_.GetDiagnostics(ticket)->evaluation_count;
  EXPECT_EQ(1u, initial);
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             for (uint64_t serial = 2; serial < 30; ++serial) {
               Apply(Page(90), serial);
             }
           }));
  tasks_.RunUntilIdle();
  EXPECT_EQ(initial + 1, session_.GetDiagnostics(ticket)->evaluation_count);
  const auto reads = source_.read_count;
  const auto wakes = session_.GetDiagnostics(ticket)->wake_count;
  tasks_.FastForwardBy(base::Seconds(5));
  tasks_.RunUntilIdle();
  EXPECT_EQ(initial + 1, session_.GetDiagnostics(ticket)->evaluation_count);
  EXPECT_EQ(wakes, session_.GetDiagnostics(ticket)->wake_count);
  EXPECT_EQ(reads, source_.read_count);
}

TEST_F(AppliedWaitTest, EveryWakeRereadsAllCurrentConditions) {
  const AppliedConditions conditions{Condition(), Condition(Page(), other_)};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::DoNothing());
  source_.Set(Snapshot(other_, Page(), 2));
  Apply();
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, CurrentAfterDispatchCanUseUnchangedCurrentState) {
  auto condition = Condition();
  condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::DoNothing());
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, AsyncAckFencesSynchronousAppliedSignals) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  DispatchDone ack;
  session_.DispatchAsync(ticket,
                         base::BindLambdaForTesting([&](DispatchDone done) {
                           Apply();
                           ack = std::move(done);
                         }));
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  EXPECT_FALSE(session_.GetDiagnostics(ticket)->dispatch_acknowledged);
  std::move(ack).Run(AppliedDispatchStatus::kAccepted);
  EXPECT_EQ(AppliedResultKind::kApplied, future.Get().kind);
}

TEST_F(AppliedWaitTest, RejectedDispatchAndMissingAckHaveDifferentResults) {
  const AppliedConditions conditions{Condition()};
  const auto rejected = Begin(conditions);
  session_.DispatchAsync(
      rejected, base::BindLambdaForTesting([&](DispatchDone done) {
        Apply();
        std::move(done).Run(AppliedDispatchStatus::kRejected);
      }));
  EXPECT_EQ(AppliedResultKind::kDispatchFailed,
            session_.WaitForApplied(rejected, conditions).kind);
  const auto missing = Begin(conditions);
  session_.DispatchAsync(
      missing,
      base::BindLambdaForTesting([&](DispatchDone done) { Apply(Page(), 3); }));
  const auto result = session_.WaitForApplied(missing, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  EXPECT_FALSE(result.diagnostics.dispatch_acknowledged);
}

TEST_F(AppliedWaitTest, DeadlineBudgetStartsAtBeginAndIsNotExtendedByDispatch) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  tasks_.FastForwardBy(base::Seconds(9));
  Dispatch(ticket, base::DoNothing());
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  EXPECT_EQ(base::Seconds(10),
            result.diagnostics.captured_at - result.diagnostics.began_at);
}

TEST_F(AppliedWaitTest, SubscribePrecedesBaselineRead) {
  source_.on_subscribe = base::BindLambdaForTesting([&] { Apply(); });
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  EXPECT_EQ(2u, ticket.baselines[0].serials.apply_serial);
  Dispatch(ticket, base::DoNothing());
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest,
       UnsupportedNativeCapabilityFailsAtBeginWithoutSubscription) {
  source_.unsupported.insert(AppliedConditionKind::kNativeWindowMatches);
  const AppliedConditions conditions{Condition(NativeWindowMatches{})};
  auto options = Options();
  options.backend = WaitOptions::Backend::kHeadless;
  const auto ticket =
      session_.BeginRequest(Intent(conditions), conditions, options);
  EXPECT_FALSE(source_.has_observers());
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kUnsupportedCondition, result.kind);
  EXPECT_EQ(0u, result.diagnostics.evaluation_count);
  EXPECT_EQ(
      std::vector<AppliedConditionBlocker>{
          AppliedConditionBlocker::kUnsupported},
      result.diagnostics.conditions[0].blockers);
  EXPECT_NE(std::string::npos,
            result.ToString().find("blockers=[unsupported,"));
  EXPECT_NE(std::string::npos, result.ToString().find("last: unsupported"));
}

TEST_F(AppliedWaitTest, RequestEpochMustMatchWhenProductProvidesCorrelation) {
  const AppliedConditions conditions{Condition()};
  auto intent = Intent(conditions);
  intent.request_epoch = 77;
  const auto ticket = session_.BeginRequest(intent, conditions, Options());
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             auto snapshot = Snapshot(owner_, Page(), 2);
             snapshot.request_epoch = 76;
             source_.Emit({snapshot});
           }));
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  auto snapshot = Snapshot(owner_, Page(), 3);
  snapshot.request_epoch = 77;
  source_.Emit({snapshot});
  EXPECT_EQ(AppliedResultKind::kApplied, future.Get().kind);
}

TEST_F(AppliedWaitTest, SurfaceEpochCombinesWithIndependentlySerialedPageHost) {
  const SurfaceEpochCommitted epoch{
      77, SurfaceEpochCommitted::Phase::kStable, 77,
      SurfaceEpochCommitted::Destination::kFullscreen};
  const AppliedConditions conditions{Condition(epoch),
                                     Condition(Page(), other_)};
  auto intent = Intent(conditions, AppliedDomain::kSurface);
  intent.request_epoch = 77;
  const auto ticket = session_.BeginRequest(intent, conditions, Options());
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             auto surface = Snapshot(owner_, epoch, 2);
             surface.request_epoch = 77;
             source_.Emit({surface, Snapshot(other_, Page(), 2)});
           }));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
}

TEST_F(AppliedWaitTest, InvalidForeignAndMutatedTicketsCannotDispatchOrWait) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  AppliedWaitSession foreign(source_);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            foreign.WaitForApplied(ticket, conditions).kind);
  std::vector<AppliedTicket> invalids{AppliedTicket{}, ticket, ticket,
                                      ticket,          ticket, ticket};
  invalids[1].request_serial += 1;
  invalids[2].domain = AppliedDomain::kDrag;
  invalids[3].owner_tokens[0] = other_;
  invalids[4].baselines[0].serials.apply_serial += 1;
  invalids[5].intent.operation = AppliedIntent::Operation::kDrag;
  int actions = 0;
  for (const auto& invalid : invalids) {
    EXPECT_EQ(AppliedResultKind::kInvalidTicket,
              session_.WaitForApplied(invalid, conditions).kind);
    const auto dispatch = session_.Dispatch(
        invalid, base::BindLambdaForTesting([&] { ++actions; }));
    ASSERT_TRUE(dispatch);
    EXPECT_EQ(AppliedResultKind::kInvalidTicket, dispatch->kind);
    EXPECT_EQ(AppliedResultKind::kInvalidTicket,
              session_.Cancel(invalid)->kind);
    EXPECT_FALSE(session_.GetDiagnostics(invalid));
  }
  EXPECT_EQ(0, actions);
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest,
       ChangedConditionsAreInvalidWithoutConsumingCorrectWait) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  for (int field = 0; field < 4; ++field) {
    auto changed = conditions;
    if (field == 0) {
      changed[0].expected = Page(200);
    } else if (field == 1) {
      changed[0].freshness = AppliedFreshness::kNewObservation;
    } else if (field == 2) {
      changed[0].mode = AppliedEvaluationMode::kObservedSinceDispatch;
    } else {
      changed[0].owner = other_;
    }
    EXPECT_EQ(AppliedResultKind::kInvalidTicket,
              session_.WaitForApplied(ticket, changed).kind);
  }
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, RedispatchIsInvalidAndActionRunsOnlyOnce) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  int actions = 0;
  Dispatch(ticket, base::BindLambdaForTesting([&] { ++actions; }));
  const auto invalid =
      session_.Dispatch(ticket, base::BindLambdaForTesting([&] { ++actions; }));
  ASSERT_TRUE(invalid);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket, invalid->kind);
  EXPECT_EQ(1, actions);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, DuplicateConsumersAreInvalidAndFirstCallbackSurvives) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  base::test::TestFuture<AppliedWaitResult> first;
  base::test::TestFuture<AppliedWaitResult> second;
  auto first_handle =
      session_.ObserveApplied(ticket, conditions, first.GetCallback());
  auto second_handle =
      session_.ObserveApplied(ticket, conditions, second.GetCallback());
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(ticket, conditions).kind);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket, second.Get().kind);
  EXPECT_EQ(AppliedResultKind::kApplied, first.Get().kind);
  base::test::TestFuture<AppliedWaitResult> third;
  auto third_handle =
      session_.ObserveApplied(ticket, conditions, third.GetCallback());
  EXPECT_EQ(AppliedResultKind::kInvalidTicket, third.Get().kind);
}

TEST_F(AppliedWaitTest,
       WaitBeforeDispatchIsInvalidButObserveCanArmBeforeDispatch) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(ticket, conditions).kind);
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation =
      session_.ObserveApplied(ticket, conditions, future.GetCallback());
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  EXPECT_EQ(AppliedResultKind::kApplied, future.Get().kind);
}

TEST_F(AppliedWaitTest, InvalidBeginInputsAndNullDispatchAreRejected) {
  const AppliedConditions conditions{Condition()};
  for (int invalid_input = 0; invalid_input < 6; ++invalid_input) {
    auto input = conditions;
    auto intent = Intent(input);
    auto options = Options();
    switch (invalid_input) {
      case 0:
        input.clear();
        intent.owner_tokens.clear();
        break;
      case 1:
        options.deadline = {};
        break;
      case 2:
        intent.owner_tokens = {other_};
        break;
      case 3:
        intent.owner_tokens.push_back(owner_);
        break;
      case 4:
        intent.targets.push_back({owner_, Page(200)});
        break;
      case 5:
        input[0].owner = {};
        break;
    }
    const auto ticket = session_.BeginRequest(intent, input, options);
    EXPECT_EQ(AppliedResultKind::kInvalidTicket,
              session_.WaitForApplied(ticket, input).kind);
  }
  const auto ticket = Begin(conditions);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.Dispatch(ticket, {})->kind);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, AppliedTerminalIsImmutableAndConsumedWaitCannotRepeat) {
  const AppliedConditions conditions{Condition()};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(); }));
  tasks_.RunUntilIdle();
  const auto final = session_.GetDiagnostics(ticket);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.Dispatch(ticket, base::DoNothing())->kind);
  session_.Cancel(ticket);
  tasks_.FastForwardBy(base::Seconds(20));
  EXPECT_EQ(final->evaluation_count,
            session_.GetDiagnostics(ticket)->evaluation_count);
  EXPECT_EQ(AppliedResultKind::kApplied,
            session_.WaitForApplied(ticket, conditions).kind);
  EXPECT_EQ(AppliedResultKind::kInvalidTicket,
            session_.WaitForApplied(ticket, conditions).kind);
}

TEST_F(AppliedWaitTest, InvalidObservationCallbackCanBeReleasedBeforeDelivery) {
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation = session_.ObserveApplied({}, {}, future.GetCallback());
  observation.Reset();
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
}

TEST_F(AppliedWaitTest, SessionDestructionDropsInvalidObservationDelivery) {
  auto session = std::make_unique<AppliedWaitSession>(source_);
  base::test::TestFuture<AppliedWaitResult> future;
  auto observation = session->ObserveApplied({}, {}, future.GetCallback());
  session.reset();
  tasks_.RunUntilIdle();
  EXPECT_FALSE(future.IsReady());
  observation.Reset();
}

TEST_F(AppliedWaitTest, DiagnosticsUseHostPlatformAndRedactNonTestLabels) {
  const AppliedConditions conditions{Condition()};
  auto options = Options();
  options.test_name = "https://private.invalid";
  options.parameter = "private page body";
  const auto ticket =
      session_.BeginRequest(Intent(conditions), conditions, options);
  session_.Cancel(ticket);
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ("<redacted>", result.diagnostics.options.test_name);
  EXPECT_EQ("<redacted>", result.diagnostics.options.parameter);
  const auto text = result.ToString();
  EXPECT_EQ(std::string::npos, text.find("private"));
  EXPECT_NE(std::string::npos, text.find("tier=A backend=fake"));
#if BUILDFLAG(IS_MAC)
  EXPECT_NE(std::string::npos, text.find("OS=macOS"));
#elif BUILDFLAG(IS_WIN)
  EXPECT_NE(std::string::npos, text.find("OS=Windows"));
#else
  EXPECT_NE(std::string::npos, text.find("OS=Linux"));
#endif
}

TEST_F(AppliedWaitTest, TimeoutDiagnosticsRecomputeSatisfactionFromFinalRead) {
  for (bool initially_matches : {false, true}) {
    source_.Set(Snapshot(owner_, Page(initially_matches ? 200 : 100)));
    auto condition = Condition(Page(200));
    condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
    const AppliedConditions conditions{condition};
    const auto ticket = Begin(conditions);
    session_.DispatchAsync(ticket,
                           base::BindLambdaForTesting([](DispatchDone) {}));
    tasks_.RunUntilIdle();
    EXPECT_EQ(initially_matches,
              session_.GetDiagnostics(ticket)->conditions[0].satisfied);
    source_.Set(Snapshot(owner_, Page(initially_matches ? 100 : 200), 2));
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    EXPECT_EQ(!initially_matches, result.diagnostics.conditions[0].satisfied);
    EXPECT_NE(std::string::npos,
              result.ToString().find("ACK-pending,deadline-exceeded,"));
    EXPECT_EQ(Page(initially_matches ? 100 : 200),
              std::get<PageHostsApplied>(
                  result.diagnostics.conditions[0].last->value));
  }
}

TEST_F(AppliedWaitTest, FrameGeometrySurvivesUnobservedHoverAndFocusChanges) {
  ViewStateMatches view{
      .drawn = true, .visible = true, .bounds = gfx::Rect(0, 0, 300, 200)};
  auto before = Snapshot(owner_, view);
  const auto digest = AppliedFrameGeometryDigest({before});
  view.hovered = true;
  view.focus_change_reason = 3;
  auto after = Snapshot(owner_, view, 2);
  EXPECT_EQ(digest, AppliedFrameGeometryDigest({after}));
  AppliedCondition hover{.owner = owner_,
                         .expected = ViewStateMatches{.hovered = false},
                         .match = AppliedMatch::kViewHovered};
  EXPECT_FALSE(AppliedValueMatches(hover, after.value));
  std::get<ViewStateMatches>(after.value).bounds.set_height(201);
  EXPECT_NE(digest, AppliedFrameGeometryDigest({after}));
}

TEST_F(AppliedWaitTest, FrameGeometryKeepsIdentityCoordinatesAndClip) {
  auto before = Snapshot(owner_, Page());
  const auto digest = AppliedFrameGeometryDigest({before});
  auto after = before;
  after.owner = other_;
  EXPECT_NE(digest, AppliedFrameGeometryDigest({after}));
  after = before;
  after.coordinates.device_scale_factor = 2;
  EXPECT_NE(digest, AppliedFrameGeometryDigest({after}));
  after = before;
  std::get<PageHostsApplied>(after.value).clip.set_width(50);
  EXPECT_NE(digest, AppliedFrameGeometryDigest({after}));
  after = before;
  ++after.serials.apply_serial;
  ++after.sample_id;
  after.adapter_diagnostics = "feedback_requests=2";
  EXPECT_EQ(digest, AppliedFrameGeometryDigest({after}));
}

TEST_F(AppliedWaitTest, UnmetDiagnosticsExplainValueCoordinatesAndEpoch) {
  using Blocker = AppliedConditionBlocker;
  const auto blockers = std::to_array<Blocker>(
      {Blocker::kValueMismatch, Blocker::kCoordinatesMismatch,
       Blocker::kRequestEpochMismatch});
  const auto labels = std::to_array<const char*>(
      {"value-mismatch", "coordinates-mismatch", "request-epoch-mismatch"});
  for (size_t i = 0; i < std::size(blockers); ++i) {
    SCOPED_TRACE(labels[i]);
    source_.Set(Snapshot(owner_, Page(), 1));
    auto condition = Condition();
    condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
    auto intent = Intent({condition});
    intent.request_epoch = 42;
    const auto ticket = session_.BeginRequest(intent, {condition}, Options());
    Dispatch(ticket, base::DoNothing());
    auto snapshot = Snapshot(owner_, Page(), 2);
    snapshot.request_epoch = 42;
    snapshot.adapter_diagnostics = "adapter-source-probe";
    switch (i) {
      case 0:
        snapshot.value = Page(200);
        break;
      case 1:
        snapshot.coordinates.units = AppliedCoordinates::Units::kPixel;
        break;
      case 2:
        snapshot.request_epoch = 41;
        break;
    }
    source_.Set(snapshot);
    const auto result = session_.WaitForApplied(ticket, {condition});
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    EXPECT_EQ(std::vector<Blocker>{blockers[i]},
              result.diagnostics.conditions[0].blockers);
    EXPECT_NE(std::string::npos, result.ToString().find(labels[i]));
    EXPECT_NE(std::string::npos, result.ToString().find("high_water: apply="));
    EXPECT_NE(std::string::npos, result.ToString().find("event_high_water:"));
    EXPECT_NE(std::string::npos,
              result.ToString().find("adapter={adapter-source-probe}"));
  }
}

TEST_F(AppliedWaitTest, IdenticalValuesExplainFreshnessAndSerialRejection) {
  using Blocker = AppliedConditionBlocker;
  const auto freshness = std::to_array<AppliedFreshness>(
      {AppliedFreshness::kNewApply, AppliedFreshness::kNewObservation,
       AppliedFreshness::kCurrentAfterDispatch});
  const auto blockers = std::to_array<Blocker>({Blocker::kApplyNotNew,
                                                Blocker::kObservationNotNew,
                                                Blocker::kSerialRegression});
  const auto labels = std::to_array<const char*>(
      {"freshness:apply-not-new", "freshness:observation-not-new",
       "serial-regression"});
  for (size_t i = 0; i < std::size(freshness); ++i) {
    SCOPED_TRACE(labels[i]);
    source_.Set(Snapshot(owner_, Page(), 1));
    auto condition = Condition();
    condition.freshness = freshness[i];
    const auto ticket = Begin({condition});
    Dispatch(ticket, base::DoNothing());
    auto snapshot = Snapshot(owner_, Page(), i == 2 ? 0 : 1);
    if (i == 0)
      snapshot.serials.observation_sequence = 2;
    source_.Set(snapshot);
    const auto result = session_.WaitForApplied(ticket, {condition});
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    const auto& diagnostic = result.diagnostics.conditions[0];
    ASSERT_TRUE(diagnostic.last);
    EXPECT_EQ(diagnostic.condition.expected, diagnostic.last->value);
    EXPECT_EQ(std::vector<Blocker>{blockers[i]}, diagnostic.blockers);
    EXPECT_NE(std::string::npos, result.ToString().find(labels[i]));
  }
}

TEST_F(AppliedWaitTest, ObservedDiagnosticsExplainMissingSamplesAndGroup) {
  using Blocker = AppliedConditionBlocker;
  for (bool grouped : {false, true}) {
    SCOPED_TRACE(grouped);
    source_.Set(Snapshot(owner_, Page(), 1));
    source_.Set(Snapshot(other_, Page(), 1));
    auto first = Condition(Page(200));
    first.mode = AppliedEvaluationMode::kObservedSinceDispatch;
    AppliedConditions conditions{first};
    if (grouped) {
      auto second = first;
      second.owner = other_;
      conditions.push_back(second);
    }
    const auto ticket = Begin(conditions);
    Dispatch(ticket, base::DoNothing());
    source_.Set(Snapshot(other_, Page(200), 2));
    auto event = Snapshot(owner_, Page(200), 2);
    if (!grouped)
      event.sample_id = 0;
    source_.Emit({event});
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    for (const auto& diagnostic : result.diagnostics.conditions) {
      EXPECT_FALSE(diagnostic.satisfied);
      EXPECT_EQ(diagnostic.condition.expected, diagnostic.last->value);
      EXPECT_TRUE(std::ranges::contains(diagnostic.blockers,
                                        Blocker::kNoObservedSample));
      EXPECT_TRUE(std::ranges::contains(
          diagnostic.blockers, grouped ? Blocker::kObservationGroupNotLatched
                                       : Blocker::kNoSampleIdentity));
    }
    EXPECT_NE(std::string::npos,
              result.ToString().find("mode=ObservedSinceDispatch"));
    EXPECT_NE(std::string::npos,
              result.ToString().find("mode:no-observed-sample"));
  }
}

TEST_F(AppliedWaitTest, UnmetDiagnosticsExplainMissingAndMismatchedOwners) {
  using Blocker = AppliedConditionBlocker;
  const auto blockers =
      std::to_array<Blocker>({Blocker::kNoCurrentSnapshot,
                              Blocker::kOwnerMismatch, Blocker::kKindMismatch});
  const auto labels = std::to_array<const char*>(
      {"no-current-snapshot", "owner-mismatch", "kind-mismatch"});
  for (size_t i = 0; i < std::size(blockers); ++i) {
    SCOPED_TRACE(labels[i]);
    source_.read_override.reset();
    source_.Set(Snapshot(owner_, Page(), 1));
    auto condition = Condition();
    condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
    const auto ticket = Begin({condition});
    Dispatch(ticket, base::DoNothing());
    switch (i) {
      case 0:
        source_.Erase({owner_, AppliedConditionKind::kPageHostsApplied});
        break;
      case 1:
        source_.read_override = Snapshot(other_, Page(), 2);
        break;
      case 2:
        source_.read_override = Snapshot(owner_, FocusMatches{}, 2);
        break;
    }
    const auto result = session_.WaitForApplied(ticket, {condition});
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    EXPECT_EQ(std::vector<Blocker>{blockers[i]},
              result.diagnostics.conditions[0].blockers);
    EXPECT_NE(std::string::npos, result.ToString().find(labels[i]));
  }
  source_.read_override.reset();
}

TEST_F(AppliedWaitTest, WaitFromTestTaskPumpsFinalApplyAndDelivery) {
  base::RunLoop test_body;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindLambdaForTesting([&] {
        const AppliedConditions conditions{Condition(Page(200))};
        const auto ticket = Begin(conditions);
        Dispatch(ticket, base::DoNothing());
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindLambdaForTesting([&] { Apply(Page(200)); }));
        const auto result = session_.WaitForApplied(ticket, conditions);
        EXPECT_EQ(AppliedResultKind::kApplied, result.kind)
            << result.ToString();
        EXPECT_LT(result.diagnostics.captured_at,
                  result.diagnostics.options.deadline);
        test_body.Quit();
      }));
  test_body.Run();
}

TEST_F(AppliedWaitTest, DeadlineReadCannotTurnLateMatchingStateIntoApplied) {
  const AppliedConditions conditions{Condition(Page(200))};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::DoNothing());
  tasks_.RunUntilIdle();
  EXPECT_FALSE(session_.GetDiagnostics(ticket)->conditions[0].satisfied);
  // 신호가 없는 새 값은 deadline 재조회에서 처음 보이므로 성공이 아니다.
  source_.Set(Snapshot(owner_, Page(200), 2));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
  EXPECT_TRUE(result.diagnostics.conditions[0].satisfied);
  EXPECT_TRUE(result.diagnostics.conditions[0].blockers.empty());
  EXPECT_NE(std::string::npos,
            result.ToString().find("completion_blockers=[deadline-exceeded,"));
  EXPECT_EQ(result.diagnostics.options.deadline,
            result.diagnostics.evaluated_at);
}

TEST_F(AppliedWaitTest, EvaluationQueueDelayIsReported) {
  const AppliedConditions conditions{Condition(Page(200))};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::DoNothing());
  tasks_.AdvanceClock(base::Seconds(3));
  const auto pending = session_.GetDiagnostics(ticket);
  EXPECT_FALSE(pending->evaluation_queued_at.is_null());
  EXPECT_NE(std::string::npos, pending->ToString().find("queued_ms=3000"));
  tasks_.RunUntilIdle();
  Apply(Page(200));
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
  EXPECT_EQ(base::Seconds(3), result.diagnostics.max_evaluation_delay);
  EXPECT_NE(std::string::npos,
            result.ToString().find("max_evaluation_delay_ms=3000"));
}

TEST_F(AppliedWaitTest, DragStepRequiresInputModelAndAppliedPreviewTogether) {
  const DragPhaseMatches phase{.session = 8,
                               .step = 2,
                               .source_handle = 43,
                               .phase = DragPhaseMatches::Phase::kAttached,
                               .attached_browser = owner_,
                               .input_acknowledged = true};
  const TabModelMatches model{.tabs = {{.handle = 42}, {.handle = 43}}};
  PreviewApplied preview{.source_handle = 43,
                         .visible = true,
                         .screen_bounds = gfx::Rect(20, 30, 200, 32)};
  source_.Set(Snapshot(owner_, DragPhaseMatches{}));
  source_.Set(Snapshot(owner_, TabModelMatches{}));
  source_.Set(Snapshot(other_, preview));
  auto drag = Condition(phase);
  drag.match = AppliedMatch::kDragPhase;
  auto order = Condition(model);
  order.match = AppliedMatch::kTabOrder;
  auto visual = Condition(preview, other_);
  visual.match = AppliedMatch::kPreviewSettled;
  drag.freshness = order.freshness = AppliedFreshness::kCurrentAfterDispatch;
  visual.freshness = AppliedFreshness::kNewApply;
  const AppliedConditions conditions{drag, order, visual};
  const auto ticket = Begin(conditions, AppliedDomain::kDrag);
  base::test::TestFuture<AppliedWaitResult> completed;
  auto observation =
      session_.ObserveApplied(ticket, conditions, completed.GetCallback());
  DispatchDone acknowledge;
  ASSERT_FALSE(session_.DispatchAsync(
      ticket, base::BindLambdaForTesting(
                  [&](DispatchDone done) { acknowledge = std::move(done); })));
  const auto evaluate = [&] {
    base::test::TestFuture<void> turn;
    tasks_.GetMainThreadTaskRunner()->PostTask(FROM_HERE, turn.GetCallback());
    return turn.Wait();
  };
  source_.Emit({Snapshot(owner_, phase, 2), Snapshot(owner_, model, 2),
                Snapshot(other_, preview, 1)});
  ASSERT_TRUE(evaluate());
  EXPECT_FALSE(completed.IsReady());
  preview.geometry_applied = false;
  source_.Emit({Snapshot(other_, preview, 3)});
  std::move(acknowledge).Run(AppliedDispatchStatus::kAccepted);
  ASSERT_TRUE(evaluate());
  EXPECT_FALSE(completed.IsReady());
  preview.geometry_applied = true;
  preview.source_handle = 42;
  source_.Emit({Snapshot(other_, preview, 4)});
  ASSERT_TRUE(evaluate());
  EXPECT_FALSE(completed.IsReady());
  preview.source_handle = 43;
  auto stale = phase;
  stale.step = 1;
  source_.Emit({Snapshot(owner_, stale, 3), Snapshot(other_, preview, 5)});
  ASSERT_TRUE(evaluate());
  EXPECT_FALSE(completed.IsReady());
  source_.Emit({Snapshot(owner_, phase, 4)});
  ASSERT_TRUE(completed.Wait());
  EXPECT_EQ(AppliedResultKind::kApplied, completed.Get().kind);
}

TEST_F(AppliedWaitTest, PressAckDoesNotClaimControllerCreation) {
  auto condition =
      Condition(DragPhaseMatches{.session = 12,
                                 .step = 1,
                                 .source_handle = 42,
                                 .last_input_screen = gfx::Point(30, 40)});
  condition.match = AppliedMatch::kDragInputAcknowledged;
  DragPhaseMatches value{.session = 12,
                         .step = 1,
                         .source_handle = 42,
                         .phase = DragPhaseMatches::Phase::kInactive,
                         .input_acknowledged = true,
                         .last_input_screen = gfx::Point(30, 40)};
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  value.phase = DragPhaseMatches::Phase::kCreated;
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  value.input_acknowledged = false;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.input_acknowledged = true;
  ++value.step;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  --value.step;
  ++value.source_handle;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  --value.source_handle;
  value.last_input_screen = gfx::Point(31, 40);
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.last_input_screen = gfx::Point(30, 40);
  condition.match = AppliedMatch::kDragPhase;
  std::get<DragPhaseMatches>(condition.expected).phase =
      DragPhaseMatches::Phase::kAttached;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.phase = DragPhaseMatches::Phase::kAttached;
  EXPECT_TRUE(AppliedValueMatches(condition, value));
}

TEST_F(AppliedWaitTest, DragOrderPreservesHandlesAndGroupBoundaries) {
  auto condition =
      Condition(TabModelMatches{.tabs = {{.handle = 42, .group = owner_},
                                         {.handle = 43, .group = owner_},
                                         {.handle = 44}}});
  condition.match = AppliedMatch::kTabOrder;
  TabModelMatches actual{.tabs = {{.handle = 42, .group = other_},
                                  {.handle = 43, .group = other_},
                                  {.handle = 44}}};
  EXPECT_TRUE(AppliedValueMatches(condition, actual));
  actual.tabs[1].group = {};
  EXPECT_FALSE(AppliedValueMatches(condition, actual));
  actual.tabs[1].group = other_;
  std::swap(actual.tabs[0], actual.tabs[1]);
  EXPECT_FALSE(AppliedValueMatches(condition, actual));
  condition.expected = TabModelMatches{.tabs = {{.handle = 43}}};
  condition.match = AppliedMatch::kPinnedTabOrder;
  actual.tabs[0].pinned = true;
  EXPECT_TRUE(AppliedValueMatches(condition, actual));
  actual.tabs[1].pinned = true;
  EXPECT_FALSE(AppliedValueMatches(condition, actual));
  condition.expected =
      PreviewApplied{.source_handle = 43, .tab_handle = 42, .visible = true};
  condition.match = AppliedMatch::kPreviewAligned;
  PreviewApplied preview{.source_handle = 43,
                         .tab_handle = 42,
                         .visible = true,
                         .screen_bounds = gfx::Rect(20, 30, 200, 32),
                         .source_screen_x = 20,
                         .source_screen_width = 200};
  EXPECT_TRUE(AppliedValueMatches(condition, preview));
  ++preview.source_screen_x;
  EXPECT_FALSE(AppliedValueMatches(condition, preview));
  --preview.source_screen_x;
  ++preview.source_screen_width;
  EXPECT_FALSE(AppliedValueMatches(condition, preview));
}

std::vector<AppliedConditionValue> AllValues(AppliedOwnerToken token) {
  return {
      Page(200),
      PaneLayoutMatches{8,
                        AppliedAxis::kVertical,
                        gfx::Size(800, 600),
                        0.4,
                        gfx::Insets(2),
                        gfx::Rect(0, 240, 800, 4),
                        {41, 42}},
      ViewStateMatches{true,
                       true,
                       gfx::Rect(1, 2, 3, 4),
                       token,
                       {token},
                       gfx::Vector2d(0, 8)},
      AnimationMatches{9, true, 0.5, 0.5},
      SurfaceEpochCommitted{10, SurfaceEpochCommitted::Phase::kStable, 11,
                            SurfaceEpochCommitted::Destination::kFullscreen},
      WidgetStateMatches{true, gfx::Rect(1, 2, 3, 4), true,
                         WidgetStateMatches::ShowState::kFullscreen},
      FocusMatches{FocusMatches::Kind::kRendererDocument, token, token, true},
      NativeWindowMatches{true, true, true, true, true},
      TabModelMatches{{{42, true, token, 8}},
                      {{8, AppliedAxis::kVertical, 0.4, {41, 42}}},
                      42},
      BrowserSetMatches{{{token, {41, 42}}}, {token}, {}},
      DragPhaseMatches{12, 3, 41, 42, DragPhaseMatches::Phase::kAttached, token,
                       true, gfx::Point(100, 20)},
      PreviewApplied{41, 42, true, true, gfx::Rect(100, 20, 30, 40), 100, 30},
      RendererViewportMatches{token, token, token, token, gfx::Size(1000, 800),
                              1.25},
      WebUiStateMatches{token, {41, 42}, 1, gfx::RectF(1, 2, 3, 4), 0.5, true},
      PresentationMatches{4, 5, 0xff123456, 0xffabcdef, 6, true},
      PresentedAfterApply{token, 123, 99, true},
  };
}

AppliedConditionValue DifferentValue(AppliedConditionValue value) {
  std::visit(
      [](auto& state) {
        using T = std::decay_t<decltype(state)>;
        if constexpr (std::is_same_v<T, PageHostsApplied>) {
          state.clip = gfx::Rect();
        } else if constexpr (std::is_same_v<T, PaneLayoutMatches>) {
          state.parent_extent = gfx::Size();
        } else if constexpr (std::is_same_v<T, ViewStateMatches>) {
          state.children.clear();
        } else if constexpr (std::is_same_v<T, AnimationMatches>) {
          state.running = false;
        } else if constexpr (std::is_same_v<T, SurfaceEpochCommitted>) {
          ++state.committed_generation;
        } else if constexpr (std::is_same_v<T, WidgetStateMatches>) {
          state.capture = false;
        } else if constexpr (std::is_same_v<T, FocusMatches>) {
          state.kind = FocusMatches::Kind::kNativeKeyWindow;
        } else if constexpr (std::is_same_v<T, NativeWindowMatches>) {
          state.on_active_space = false;
        } else if constexpr (std::is_same_v<T, TabModelMatches>) {
          state.tabs[0].pinned = false;
        } else if constexpr (std::is_same_v<T, BrowserSetMatches>) {
          state.browsers[0].tab_handles.clear();
        } else if constexpr (std::is_same_v<T, DragPhaseMatches>) {
          state.phase = DragPhaseMatches::Phase::kCreated;
        } else if constexpr (std::is_same_v<T, PreviewApplied>) {
          ++state.tab_handle;
        } else if constexpr (std::is_same_v<T, RendererViewportMatches>) {
          state.document = AppliedOwnerToken::Create();
        } else if constexpr (std::is_same_v<T, WebUiStateMatches>) {
          ++state.selected_index;
        } else if constexpr (std::is_same_v<T, PresentationMatches>) {
          ++state.icon_id;
        } else if constexpr (std::is_same_v<T, PresentedAfterApply>) {
          state.compositor = AppliedOwnerToken::Create();
        }
      },
      value);
  return value;
}

TEST_F(AppliedWaitTest, PartialVisibilityStillRequiresFreshApplyAndOwner) {
  source_.Set(Snapshot(owner_, ViewStateMatches{.visible = false}));
  auto condition = Condition(ViewStateMatches{.visible = true});
  condition.match = AppliedMatch::kViewVisible;
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions);
  Dispatch(ticket, base::DoNothing());
  source_.Emit({Snapshot(other_, ViewStateMatches{.visible = true}, 2)});
  // fake 신호의 예약 평가를 전달하는 mock TaskEnvironment 자극이다.
  tasks_.RunUntilIdle();
  EXPECT_FALSE(session_.GetDiagnostics(ticket)->conditions[0].satisfied);
  source_.Emit({Snapshot(owner_, ViewStateMatches{.visible = true}, 1)});
  // 같은 serial의 평가가 끝나도 freshness가 충족되지 않아야 한다.
  tasks_.RunUntilIdle();
  EXPECT_FALSE(session_.GetDiagnostics(ticket)->conditions[0].satisfied);
  source_.Emit({Snapshot(
      owner_,
      ViewStateMatches{.visible = true, .bounds = gfx::Rect(3, 4, 80, 60)},
      2)});
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kApplied, result.kind) << result.ToString();
  EXPECT_EQ(
      gfx::Rect(3, 4, 80, 60).ToString(),
      std::get<ViewStateMatches>(result.diagnostics.conditions[0].last->value)
          .bounds.ToString());
}

TEST_F(AppliedWaitTest, IncompatibleProjectionFailsBeforeSubscribing) {
  auto condition = Condition(ViewStateMatches{});
  condition.match = AppliedMatch::kNativeFullscreen;
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions);
  const auto result = session_.WaitForApplied(ticket, conditions);
  EXPECT_EQ(AppliedResultKind::kUnsupportedCondition, result.kind);
  EXPECT_FALSE(source_.has_observers());
  EXPECT_EQ(0u, result.diagnostics.evaluation_count);
}

TEST_F(AppliedWaitTest, DestinationNeedsANewCommittedEpochAndBoundFrame) {
  SurfaceEpochCommitted expected{
      .epoch = 4,
      .committed_generation = 4,
      .destination = SurfaceEpochCommitted::Destination::kWindowed};
  AppliedCondition condition{.owner = owner_,
                             .expected = expected,
                             .match = AppliedMatch::kSurfaceDestination};
  EXPECT_FALSE(AppliedValueMatches(condition, expected));
  auto actual = expected;
  actual.epoch = 7;
  EXPECT_FALSE(AppliedValueMatches(condition, actual));
  actual.committed_generation = 7;
  actual.phase = SurfaceEpochCommitted::Phase::kTransitioning;
  EXPECT_FALSE(AppliedValueMatches(condition, actual));
  actual.phase = SurfaceEpochCommitted::Phase::kStable;
  EXPECT_TRUE(AppliedValueMatches(condition, actual));
  actual.destination = SurfaceEpochCommitted::Destination::kImmersive;
  EXPECT_FALSE(AppliedValueMatches(condition, actual));

  PresentedAfterApply frame{.compositor = owner_, .applied_sample_id = 8};
  condition.expected = frame;
  condition.match = AppliedMatch::kPresentedFrame;
  EXPECT_TRUE(AppliedValueMatches(condition, frame));
  frame.compositor = other_;
  EXPECT_FALSE(AppliedValueMatches(condition, frame));
  frame.compositor = owner_;
  frame.successful = false;
  EXPECT_FALSE(AppliedValueMatches(condition, frame));
}

TEST_F(AppliedWaitTest, TabActiveMatchesNativeHandleAcrossIndexChanges) {
  const TabModelMatches expected{.active_tab = 34};
  const AppliedCondition condition{
      .owner = owner_, .expected = expected, .match = AppliedMatch::kTabActive};
  TabModelMatches value{.tabs = {{12}, {34}}, .active_tab = 34};
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  std::ranges::reverse(value.tabs);
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  value.active_tab = 12;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.active_tab = 0;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  EXPECT_FALSE(AppliedValueMatches({.owner = owner_,
                                    .expected = TabModelMatches{},
                                    .match = AppliedMatch::kTabActive},
                                   value));
}

TEST_F(AppliedWaitTest,
       NewTabActiveRequiresUniqueInsertionAndPreservesExistingTabs) {
  const TabModelMatches expected{.tabs = {{12}, {34}}, .active_tab = 12};
  const AppliedCondition condition{.owner = owner_,
                                   .expected = expected,
                                   .match = AppliedMatch::kNewTabActive};
  for (int index : {0, 1, 2}) {
    auto value = expected;
    value.tabs.insert(value.tabs.begin() + index, AppliedTabState{56});
    value.active_tab = 56;
    EXPECT_TRUE(AppliedValueMatches(condition, value));
    value.active_tab = 34;
    EXPECT_FALSE(AppliedValueMatches(condition, value));
  }
  auto value = expected;
  value.active_tab = 56;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.tabs = {{12}, {34}, {56}, {78}};
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.tabs = {{34}, {12}, {56}};
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.tabs = {{12, true}, {34}, {56}};
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.tabs = {{12}, {78}, {56}};
  EXPECT_FALSE(AppliedValueMatches(condition, value));
}

TEST_F(AppliedWaitTest, WebUiRowsFitCurrentNativeBounds) {
  const AppliedCondition condition{
      .owner = owner_,
      .expected = WebUiStateMatches{.document = other_, .popup_rows = {1, 2}},
      .match = AppliedMatch::kWebUiRowsFitNativeBounds};
  WebUiStateMatches value{
      .document = other_,
      .popup_rows = {1, 2},
      .row_bounds = {gfx::RectF(0, 0, 200, 40), gfx::RectF(0, 40, 200, 40)},
      .native_view_bounds_in_screen = gfx::Rect(10, 20, 200, 80),
      .native_widget_bounds_in_screen = gfx::Rect(8, 18, 204, 84)};
  ASSERT_TRUE(AppliedValueMatches(condition, value));
  value.popup_rows.push_back(3);
  value.row_bounds.push_back(gfx::RectF(0, 80, 200, 40.5));
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_view_bounds_in_screen.set_height(120);
  value.native_widget_bounds_in_screen.set_height(124);
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_view_bounds_in_screen.set_height(121);
  value.native_widget_bounds_in_screen.set_height(122);
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_widget_bounds_in_screen.set_height(125);
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  std::ranges::reverse(value.row_bounds);
  value.native_view_bounds_in_screen.set_height(80);
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_view_bounds_in_screen.set_height(121);
  value.native_widget_bounds_in_screen = gfx::Rect();
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_widget_bounds_in_screen = gfx::Rect(8, 18, 204, 125);
  value.native_view_bounds_in_screen.set_width(1);
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.native_view_bounds_in_screen.set_width(200);
  value.document = owner_;
  EXPECT_FALSE(AppliedValueMatches(condition, value));
  value.document = other_;
  value.row_bounds.pop_back();
  EXPECT_FALSE(AppliedValueMatches(condition, value));
}

TEST_F(AppliedWaitTest, PopupGrowthCannotReuseEarlierRowBounds) {
  WebUiStateMatches value{
      .document = other_,
      .popup_rows = {1, 2},
      .row_bounds = {gfx::RectF(0, 0, 200, 40), gfx::RectF(0, 40, 200, 40)},
      .native_view_bounds_in_screen = gfx::Rect(10, 20, 200, 1),
      .native_widget_bounds_in_screen = gfx::Rect(8, 18, 204, 125)};
  source_.Set(Snapshot(owner_, value));
  const AppliedConditions conditions{
      {.owner = owner_,
       .expected = WebUiStateMatches{.document = other_, .popup_rows = {1, 2}},
       .freshness = AppliedFreshness::kCurrentAfterDispatch,
       .match = AppliedMatch::kWebUiRowsFitNativeBounds}};
  const auto ticket = Begin(conditions);
  base::test::TestFuture<AppliedWaitResult> result;
  auto observation =
      session_.ObserveApplied(ticket, conditions, result.GetCallback());
  Dispatch(ticket, base::BindLambdaForTesting([&] {
             value.popup_rows.push_back(3);
             value.row_bounds.push_back(gfx::RectF(0, 80, 200, 40));
             value.native_view_bounds_in_screen.set_height(80);
             Apply(value);
           }));
  // fake DOM/native 표본을 전달해 과거의 두 행 높이로 완료되지 않음을 확인한다.
  tasks_.RunUntilIdle();
  EXPECT_FALSE(result.IsReady());
  value.native_view_bounds_in_screen.set_height(120);
  Apply(value, 3);
  EXPECT_EQ(AppliedResultKind::kApplied, result.Get().kind);
}

TEST_F(AppliedWaitTest,
       PaneMatchSeparatesTargetParentsFromCardsAndExtentChanges) {
  PaneLayoutMatches value{.split_id = 1,
                          .parent_extent = gfx::Size(800, 600),
                          .ratio = .5,
                          .tab_handles = {12, 34},
                          .start_parent_extent = gfx::Size(397, 557),
                          .end_parent_extent = gfx::Size(397, 527),
                          .start_container_extent = gfx::Size(399, 600),
                          .end_container_extent = gfx::Size(399, 600),
                          .geometry_matches_ratio = true};
  AppliedCondition condition{owner_, value};
  condition.match = AppliedMatch::kPaneSplit;
  EXPECT_TRUE(AppliedValueMatches(condition, value));
  auto changed = value;
  changed.parent_extent = gfx::Size(1000, 600);
  changed.start_container_extent = changed.end_container_extent =
      gfx::Size(499, 600);
  changed.start_parent_extent = gfx::Size(497, 557);
  changed.end_parent_extent = gfx::Size(497, 527);
  EXPECT_FALSE(AppliedValueMatches(condition, changed));
  condition.match = AppliedMatch::kPaneSplitCurrentExtent;
  EXPECT_TRUE(AppliedValueMatches(condition, changed));
  changed.start_parent_extent.Enlarge(-1, 0);
  EXPECT_FALSE(AppliedValueMatches(condition, changed));
  changed.start_parent_extent.Enlarge(1, 0);
  changed.end_parent_extent.Enlarge(0, -1);
  EXPECT_FALSE(AppliedValueMatches(condition, changed));
  changed.end_parent_extent.Enlarge(0, 1);
  changed.tab_handles = {34, 12};
  EXPECT_FALSE(AppliedValueMatches(condition, changed));
  changed = value;
  changed.geometry_matches_ratio = false;
  EXPECT_FALSE(AppliedValueMatches(condition, changed));
}

TEST_F(AppliedWaitTest, ClosedSchemasMatchAndFormatEveryConditionKind) {
  const auto values = AllValues(other_);
  ASSERT_EQ(std::variant_size_v<AppliedConditionValue>, values.size());
  for (const auto& value : values) {
    SCOPED_TRACE(FormatAppliedValue(value));
    source_.Set(Snapshot(owner_, DifferentValue(value), 1));
    const AppliedConditions conditions{Condition(value)};
    const auto ticket = Begin(conditions);
    Dispatch(ticket, base::BindLambdaForTesting([&] { Apply(value); }));
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kApplied, result.kind);
    EXPECT_FALSE(FormatAppliedValue(value).empty());
    EXPECT_NE(std::string::npos,
              result.ToString().find(FormatAppliedValue(value)));
  }
}

TEST_F(AppliedWaitTest,
       ClosedSchemasRejectMismatchedValuesForEveryConditionKind) {
  for (const auto& value : AllValues(other_)) {
    SCOPED_TRACE(FormatAppliedValue(value));
    source_.Set(Snapshot(owner_, DifferentValue(value), 1));
    auto condition = Condition(value);
    condition.freshness = AppliedFreshness::kCurrentAfterDispatch;
    const AppliedConditions conditions{condition};
    const auto ticket = Begin(conditions);
    Dispatch(ticket, base::DoNothing());
    const auto result = session_.WaitForApplied(ticket, conditions);
    EXPECT_EQ(AppliedResultKind::kTimedOut, result.kind);
    EXPECT_EQ(
        std::vector<AppliedConditionBlocker>{
            AppliedConditionBlocker::kValueMismatch},
        result.diagnostics.conditions[0].blockers);
    EXPECT_NE(std::string::npos, result.ToString().find("value-mismatch"));
  }
}

TEST_F(AppliedWaitTest,
       PresentedSampleMustStillMatchCurrentCompositorAndGeometry) {
  const PresentedAfterApply expected{other_, 123, 99, true};
  source_.Set(Snapshot(owner_, PresentedAfterApply{other_, 122, 98, true}, 1));
  auto condition = Condition(expected);
  condition.freshness = AppliedFreshness::kNewObservation;
  const AppliedConditions conditions{condition};
  const auto ticket = Begin(conditions);
  Dispatch(
      ticket, base::BindLambdaForTesting([&] {
        Apply(expected, 2);
        Apply(PresentedAfterApply{AppliedOwnerToken::Create(), 124, 100, true},
              3);
      }));
  EXPECT_EQ(AppliedResultKind::kTimedOut,
            session_.WaitForApplied(ticket, conditions).kind);
}

}  // namespace
}  // namespace yee::test
