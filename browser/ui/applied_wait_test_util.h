// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_UTIL_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_UTIL_H_

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>

#include "base/command_line.h"
#include "base/test/bind.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/ui/views/yee/applied_wait_views_observers.h"
#include "chrome/browser/ui/views/yee/page_viewport_hosts.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace yee::test {

// fixture가 소유하는 observer/session과 요청 생성의 공통 코드다.
// 조건은 action 전에 확정하고 action은 한 번만 실행한다.
template <class Source>
class AppliedTestContext {
 public:
  // fixture member는 browser process보다 먼저 생성된다. 관측은 처음 사용할 때
  // 만들고, fixture가 TearDownOnMainThread()에서 Stop()해 browser 수명 안에서
  // 해제한다.
  Source& source() {
    if (!state_)
      state_.emplace();
    return state_->source;
  }
  AppliedWaitSession& session() {
    source();
    return state_->session;
  }
  void Stop() { state_.reset(); }

  AppliedViewsObservers& views() {
    if constexpr (std::is_same_v<Source, AppliedViewsObservers>)
      return source();
    else
      return source().views();
  }

  AppliedCondition Condition(
      AppliedOwnerToken owner,
      AppliedConditionValue expected,
      AppliedMatch match = AppliedMatch::kExact,
      AppliedFreshness freshness = AppliedFreshness::kCurrentAfterDispatch) {
    const auto value =
        source().Read({owner, GetAppliedConditionKind(expected)});
    return {.owner = owner,
            .expected = std::move(expected),
            .freshness = freshness,
            .coordinates = value ? value->coordinates : AppliedCoordinates{},
            .match = match};
  }

  AppliedCondition View(views::View& view,
                        ViewStateMatches expected,
                        AppliedMatch match) {
    return Condition(views().ObserveView(view), std::move(expected), match);
  }
  AppliedCondition Widget(views::Widget& widget,
                          WidgetStateMatches expected,
                          AppliedMatch match) {
    return Condition(views().ObserveWidget(widget), std::move(expected), match);
  }
  AppliedCondition Focus(views::View& view, bool focused = true) {
    const auto owner = views().ObserveView(view);
    return Condition(owner, FocusMatches{.target = owner, .focused = focused});
  }

  AppliedCondition Frame(views::Widget& widget, bool redraw = true) {
    AppliedConditions geometry{View(*widget.GetRootView(),
                                    {.bounds = widget.GetRootView()->bounds()},
                                    AppliedMatch::kViewBounds)};
    uint64_t pane = 0;
    const auto collect = [&](auto&& self, views::View& view) -> void {
      if (auto* state = PageViewportAppliedState::From(view);
          state && state->GetSnapshot().apply_serial && view.IsDrawn() &&
          !view.bounds().IsEmpty()) {
        const auto owner = views().ObservePageHosts(view, ++pane, 0);
        auto condition =
            Condition(owner, PageHostsApplied{}, AppliedMatch::kPageGeometry);
        condition.coordinates =
            GetAppliedViewCoordinates(view, AppliedCoordinates::Space::kParent);
        geometry.push_back(std::move(condition));
      }
      for (views::View* child : view.children())
        self(self, *child);
    };
    collect(collect, *widget.GetRootView());
    return views().ObservePresentedAfterApply(*widget.GetRootView(),
                                              std::move(geometry), redraw);
  }
  testing::AssertionResult WaitForPresentedFrame(views::Widget& widget,
                                                 bool redraw = true) {
    return Run(
        {Frame(widget, redraw)}, [] {}, AppliedDomain::kFrame, base::Seconds(5),
        true);
  }

  AppliedTicket Begin(const AppliedConditions& conditions,
                      AppliedDomain domain = AppliedDomain::kLayout,
                      base::TimeDelta budget = TestTimeouts::action_timeout(),
                      bool no_op = false) {
    AppliedIntent intent{.domain = domain};
    if (no_op)
      intent.operation = AppliedIntent::Operation::kNoOp;
    for (const auto& condition : conditions) {
      if (!std::ranges::contains(intent.owner_tokens, condition.owner))
        intent.owner_tokens.push_back(condition.owner);
    }
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    const auto& command = *base::CommandLine::ForCurrentProcess();
    const bool headless = command.HasSwitch("headless");
    WaitOptions options{
        .deadline = base::TimeTicks::Now() + budget,
        .test_name =
            info ? std::string(info->test_suite_name()) + "." + info->name()
                 : "fixture",
        .tier = headless ? WaitOptions::Tier::kB : WaitOptions::Tier::kC,
        .backend = headless ? WaitOptions::Backend::kHeadless
                            : WaitOptions::Backend::kNative};
    if (no_op)
      options.already_applied =
          WaitOptions::AlreadyApplied::kAllowAlreadyApplied;
    return session().BeginRequest(std::move(intent), conditions, options);
  }

  testing::AssertionResult Wait(AppliedTicket ticket,
                                const AppliedConditions& conditions) {
    const auto result = session().WaitForApplied(ticket, conditions);
    if (result.kind == AppliedResultKind::kApplied)
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << result.ToString();
  }

  template <class Action>
  testing::AssertionResult Run(
      AppliedConditions conditions,
      Action action,
      AppliedDomain domain = AppliedDomain::kLayout,
      base::TimeDelta budget = TestTimeouts::action_timeout(),
      bool no_op = false) {
    const auto ticket = Begin(conditions, domain, budget, no_op);
    if (auto error = session().DispatchAsync(
            ticket, base::BindLambdaForTesting([action = std::move(action)](
                                                   DispatchDone done) mutable {
              action();
              // Send*Sync가 반환한 시점은 native 입력 완료 ACK 뒤다.
              std::move(done).Run(testing::Test::HasFatalFailure()
                                      ? AppliedDispatchStatus::kRejected
                                      : AppliedDispatchStatus::kAccepted);
            })))
      return testing::AssertionFailure() << error->ToString();
    return Wait(ticket, conditions);
  }

 private:
  struct State {
    Source source;
    AppliedWaitSession session{source};
  };
  std::optional<State> state_;
};

}  // namespace yee::test

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_APPLIED_WAIT_TEST_UTIL_H_
