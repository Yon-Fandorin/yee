// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/applied_wait_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "build/build_config.h"

namespace yee::test {
namespace {

constexpr std::array<const char*, 16> kConditionNames = {
    "PageHostsApplied",
    "PaneLayoutMatches",
    "ViewStateMatches",
    "AnimationMatches",
    "SurfaceEpochCommitted",
    "WidgetStateMatches",
    "FocusMatches",
    "NativeWindowMatches",
    "TabModelMatches",
    "BrowserSetMatches",
    "DragPhaseMatches",
    "PreviewApplied",
    "RendererViewportMatches",
    "WebUiStateMatches",
    "PresentationMatches",
    "PresentedAfterApply",
};
static_assert(std::size(kConditionNames) ==
              std::variant_size_v<AppliedConditionValue>);

template <typename T>
void Write(std::ostream& out, const T& value) {
  if constexpr (requires { value.ToString(); }) {
    out << value.ToString();
  } else if constexpr (std::is_enum_v<T>) {
    out << static_cast<int>(value);
  } else {
    out << value;
  }
}

template <typename T>
void Write(std::ostream& out, const std::vector<T>& values) {
  out << '[';
  for (const auto& value : values) {
    Write(out, value);
    out << ',';
  }
  out << ']';
}

class Fields {
 public:
  Fields() { out_.precision(std::numeric_limits<double>::max_digits10); }
  template <typename T>
  Fields& Add(const char* name, const T& value) {
    out_ << name << '=';
    Write(out_, value);
    out_ << ' ';
    return *this;
  }
  std::string str() const { return out_.str(); }

 private:
  std::ostringstream out_;
};

std::string FormatCoordinates(const AppliedCoordinates& coordinates) {
  constexpr std::array<const char*, 3> kSpaces = {"local", "parent", "screen"};
  return Fields()
      .Add("space", kSpaces[static_cast<int>(coordinates.space)])
      .Add("units", coordinates.units == AppliedCoordinates::Units::kDip
                        ? "DIP"
                        : "pixel")
      .Add("DSF", coordinates.device_scale_factor)
      .Add("RTL", coordinates.rtl)
      .str();
}

std::string FormatSerials(const AppliedSerials& serials) {
  return Fields()
      .Add("apply", serials.apply_serial)
      .Add("generation", serials.native_generation)
      .Add("observation", serials.observation_sequence)
      .str();
}

std::string DiagnosticLabel(const std::string& label) {
  // GTest 이름·parameter 식별자만 허용해 URL이나 페이지 문자열 유입을 막는다.
  const bool safe = std::ranges::all_of(label, [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' ||
           c == '-';
  });
  return safe ? label : "<redacted>";
}

AppliedSignalKey KeyFor(const AppliedCondition& condition) {
  return {condition.owner, GetAppliedConditionKind(condition.expected)};
}

bool HasKey(const AppliedSnapshot& snapshot, const AppliedSignalKey& key) {
  return snapshot.owner == key.owner &&
         GetAppliedConditionKind(snapshot.value) == key.kind;
}

bool ExpectsDestruction(const AppliedCondition& condition) {
  if (const auto* view = std::get_if<ViewStateMatches>(&condition.expected)) {
    return view->destroyed;
  }
  if (const auto* widget =
          std::get_if<WidgetStateMatches>(&condition.expected)) {
    return widget->closed;
  }
  return false;
}

bool ValueMatches(const AppliedConditionValue& expected,
                  const AppliedConditionValue& actual) {
  if (const auto* animation = std::get_if<AnimationMatches>(&expected)) {
    const auto* state = std::get_if<AnimationMatches>(&actual);
    return state && state->group_id == animation->group_id &&
           state->running == animation->running &&
           state->progress_min == state->progress_max &&
           state->progress_min >= animation->progress_min &&
           state->progress_max <= animation->progress_max;
  }
  return expected == actual;
}

bool SerialsAtLeast(const AppliedSerials& current,
                    const AppliedSerials& previous) {
  return current.apply_serial >= previous.apply_serial &&
         current.native_generation >= previous.native_generation &&
         current.observation_sequence >= previous.observation_sequence;
}

AppliedWaitResult InvalidResult(const AppliedTicket& ticket,
                                const AppliedWaitDiagnostics* diagnostics) {
  AppliedWaitResult result;
  if (diagnostics) {
    result.diagnostics = *diagnostics;
  } else {
    result.diagnostics.ticket = ticket;
  }
  result.diagnostics.captured_at = base::TimeTicks::Now();
  return result;
}

class Request {
 public:
  Request(AppliedSignalSource& source,
          AppliedTicket ticket,
          AppliedConditions conditions,
          WaitOptions options)
      : source_(source), conditions_(std::move(conditions)) {
    diagnostics_.ticket = std::move(ticket);
    diagnostics_.options = std::move(options);
    diagnostics_.options.test_name =
        DiagnosticLabel(diagnostics_.options.test_name);
    diagnostics_.options.parameter =
        DiagnosticLabel(diagnostics_.options.parameter);
    diagnostics_.began_at = base::TimeTicks::Now();
    for (const auto& condition : conditions_) {
      AppliedConditionDiagnostics diagnostic;
      diagnostic.condition = condition;
      diagnostics_.conditions.push_back(std::move(diagnostic));
    }
    high_water_.resize(conditions_.size());
    event_high_water_.resize(conditions_.size());
    Record(AppliedEventKind::kBegin);
  }

  ~Request() { Shutdown(); }

  const AppliedTicket& ticket() const { return diagnostics_.ticket; }
  const AppliedConditions& conditions() const { return conditions_; }
  bool terminal() const { return result_.has_value(); }
  bool dispatched() const { return diagnostics_.dispatched; }

  void Start() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (!Validate()) {
      Finish(AppliedResultKind::kInvalidTicket);
      return;
    }
    bool supported = true;
    std::vector<AppliedSignalKey> keys;
    for (auto& diagnostic : diagnostics_.conditions) {
      diagnostic.supported = source_->Supports(diagnostic.condition);
      if (!diagnostic.supported) {
        diagnostic.blockers = {AppliedConditionBlocker::kUnsupported};
      }
      supported &= diagnostic.supported;
      keys.push_back(KeyFor(diagnostic.condition));
    }
    if (!supported) {
      Finish(AppliedResultKind::kUnsupportedCondition);
      return;
    }

    // 구독 전에 baseline을 읽으면 그 사이의 동기 알림을 잃을 수 있다.
    subscription_ = source_->Subscribe(
        keys, base::BindRepeating(&Request::OnSignal, work_weak_.GetWeakPtr()));
    for (size_t i = 0; i < conditions_.size(); ++i) {
      const auto key = KeyFor(conditions_[i]);
      const auto snapshot = source_->Read(key);
      if (snapshot && HasKey(*snapshot, key)) {
        diagnostics_.conditions[i].baseline = snapshot->serials;
        diagnostics_.conditions[i].last = snapshot;
        high_water_[i] = snapshot->serials;
        event_high_water_[i] = snapshot->serials;
        diagnostics_.conditions[i].high_water = snapshot->serials;
        diagnostics_.conditions[i].event_high_water = snapshot->serials;
      }
      diagnostics_.ticket.baselines.push_back(
          {key, diagnostics_.conditions[i].baseline});
    }
    for (const auto& owner : ticket().owner_tokens) {
      if (!source_->IsOwnerAlive(owner)) {
        for (auto& diagnostic : diagnostics_.conditions) {
          if (diagnostic.condition.owner == owner) {
            diagnostic.owner_destroyed = true;
            diagnostic.blockers = {
                ExpectsDestruction(diagnostic.condition)
                    ? AppliedConditionBlocker::kDestroyedBeforeDispatch
                    : AppliedConditionBlocker::kOwnerDestroyed};
          }
        }
        Finish(AppliedResultKind::kOwnerDestroyed);
        return;
      }
    }
    if (base::TimeTicks::Now() >= diagnostics_.options.deadline) {
      Finish(AppliedResultKind::kTimedOut);
      return;
    }
    deadline_.Start(
        FROM_HERE, diagnostics_.options.deadline - base::TimeTicks::Now(),
        base::BindOnce(&Request::OnDeadline, work_weak_.GetWeakPtr()));
    Wake();
  }

  std::optional<AppliedWaitResult> StartDispatch(bool has_action) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (terminal() || dispatched() || !has_action) {
      const auto invalid = InvalidResult(ticket(), &diagnostics_);
      if (!terminal()) {
        Finish(AppliedResultKind::kInvalidTicket);
      }
      return invalid;
    }
    if (base::TimeTicks::Now() >= diagnostics_.options.deadline) {
      Finish(AppliedResultKind::kTimedOut);
      return *result_;
    }
    // Begin 이후, 입력 전의 변화도 새 적용으로 오인하지 않도록 fence를
    // 갱신한다.
    for (size_t i = 0; i < conditions_.size(); ++i) {
      const auto snapshot = source_->Read(KeyFor(conditions_[i]));
      if (snapshot) {
        Accept(i, *snapshot, true);
      }
      diagnostics_.conditions[i].dispatch_fence = high_water_[i];
      event_high_water_[i] = high_water_[i];
      diagnostics_.conditions[i].event_high_water = high_water_[i];
    }
    pending_samples_.clear();
    diagnostics_.dispatched = true;
    Record(AppliedEventKind::kDispatch);
    Wake();
    return std::nullopt;
  }

  DispatchDone AckCallback() {
    return base::BindOnce(&Request::Acknowledge, work_weak_.GetWeakPtr());
  }

  void Acknowledge(AppliedDispatchStatus status) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (terminal()) {
      return;
    }
    diagnostics_.dispatch_acknowledged = true;
    Record(AppliedEventKind::kDispatchAck);
    if (status == AppliedDispatchStatus::kRejected) {
      Finish(AppliedResultKind::kDispatchFailed);
      return;
    }
    Wake();
  }

  bool Observe(base::OnceCallback<void(AppliedWaitResult)>&& done) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (claimed_) {
      return false;
    }
    claimed_ = true;
    done_ = std::move(done);
    ScheduleDelivery();
    return true;
  }

  base::OnceClosure ReleaseCallback() {
    return base::BindOnce(&Request::Release, delivery_weak_.GetWeakPtr());
  }

  void Finish(AppliedResultKind kind) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (terminal()) {
      return;
    }
    Record(AppliedEventKind::kTerminal);
    diagnostics_.deadline_expired = kind == AppliedResultKind::kTimedOut;
    StopWork();
    result_ = AppliedWaitResult{kind, Diagnostics()};
    ScheduleDelivery();
  }

  AppliedWaitDiagnostics Diagnostics() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (result_) {
      return result_->diagnostics;
    }
    auto copy = diagnostics_;
    copy.captured_at = base::TimeTicks::Now();
    return copy;
  }

  void Shutdown() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    done_.Reset();
    delivery_weak_.InvalidateWeakPtrs();
    StopWork();
  }

 private:
  bool Validate() const {
    if (conditions_.empty() || diagnostics_.options.deadline.is_null() ||
        (diagnostics_.options.already_applied ==
             WaitOptions::AlreadyApplied::kAllowAlreadyApplied &&
         ticket().intent.operation != AppliedIntent::Operation::kNoOp)) {
      return false;
    }
    std::set<AppliedOwnerToken> condition_owners;
    std::vector<AppliedIntentTarget> targets;
    for (const auto& condition : conditions_) {
      if (condition.owner.is_empty() ||
          !std::isfinite(condition.coordinates.device_scale_factor) ||
          condition.coordinates.device_scale_factor <= 0) {
        return false;
      }
      if (const auto* animation =
              std::get_if<AnimationMatches>(&condition.expected)) {
        if (!std::isfinite(animation->progress_min) ||
            !std::isfinite(animation->progress_max) ||
            animation->progress_min < 0 || animation->progress_max > 1 ||
            animation->progress_min > animation->progress_max) {
          return false;
        }
      }
      condition_owners.insert(condition.owner);
      targets.push_back({condition.owner, condition.expected});
    }
    const auto& owners = ticket().owner_tokens;
    return std::set<AppliedOwnerToken>(owners.begin(), owners.end()) ==
               condition_owners &&
           owners.size() == condition_owners.size() &&
           targets == ticket().intent.targets;
  }

  void Record(AppliedEventKind kind,
              AppliedOwnerToken owner = {},
              std::optional<AppliedConditionKind> condition_kind = std::nullopt,
              AppliedSerials serials = {},
              uint64_t sample_id = 0) {
    if (diagnostics_.events.size() == 32) {
      diagnostics_.events.pop_front();
    }
    diagnostics_.events.push_back({base::TimeTicks::Now(), kind, owner,
                                   condition_kind, serials, sample_id});
  }

  bool Accept(size_t index, const AppliedSnapshot& snapshot, bool from_read) {
    auto& diagnostic = diagnostics_.conditions[index];
    diagnostic.last = snapshot;
    if (!HasKey(snapshot, KeyFor(conditions_[index]))) {
      return false;
    }
    // 비동기로 전달한 적용 당시 표본은 현재값 재조회보다 늦게 도착할 수 있다.
    // 이벤트끼리의 역행만 거부하고 Current의 watermark와 구분한다.
    auto& watermark = from_read ? high_water_[index] : event_high_water_[index];
    if (!SerialsAtLeast(snapshot.serials, watermark) ||
        (!from_read && snapshot.serials.observation_sequence ==
                           watermark.observation_sequence)) {
      Record(AppliedEventKind::kStaleSignal, snapshot.owner,
             GetAppliedConditionKind(snapshot.value), snapshot.serials,
             snapshot.sample_id);
      return false;
    }
    watermark = snapshot.serials;
    auto& current = high_water_[index];
    current.apply_serial =
        std::max(current.apply_serial, snapshot.serials.apply_serial);
    current.native_generation =
        std::max(current.native_generation, snapshot.serials.native_generation);
    current.observation_sequence = std::max(
        current.observation_sequence, snapshot.serials.observation_sequence);
    diagnostic.high_water = high_water_[index];
    diagnostic.event_high_water = event_high_water_[index];
    return true;
  }

  void OnSignal(const AppliedSignalSample& sample) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (terminal()) {
      return;
    }
    bool relevant = false;
    for (const auto& owner : sample.destroyed_owners) {
      if (std::ranges::contains(ticket().owner_tokens, owner)) {
        relevant = true;
        destroyed_.insert(owner);
        if (dispatched()) {
          destroyed_after_dispatch_.insert(owner);
        }
        Record(AppliedEventKind::kOwnerDestroyed, owner);
      }
    }
    std::vector<std::optional<AppliedSnapshot>> observed(conditions_.size());
    for (const auto& snapshot : sample.snapshots) {
      bool recorded = false;
      for (size_t i = 0; i < conditions_.size(); ++i) {
        if (!HasKey(snapshot, KeyFor(conditions_[i]))) {
          continue;
        }
        relevant = true;
        if (!recorded) {
          Record(AppliedEventKind::kSignal, snapshot.owner,
                 GetAppliedConditionKind(snapshot.value), snapshot.serials,
                 snapshot.sample_id);
          recorded = true;
        }
        if (Accept(i, snapshot, false) && dispatched() &&
            conditions_[i].mode ==
                AppliedEvaluationMode::kObservedSinceDispatch) {
          observed[i] = snapshot;
        }
      }
    }
    if (std::ranges::any_of(
            observed, [](const auto& value) { return value.has_value(); })) {
      pending_samples_.push_back(std::move(observed));
    }
    if (relevant) {
      Wake();
    }
  }

  void Wake() {
    ++diagnostics_.wake_count;
    if (evaluation_queued_) {
      return;
    }
    evaluation_queued_ = true;
    diagnostics_.evaluation_queued_at = base::TimeTicks::Now();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Request::Evaluate, work_weak_.GetWeakPtr()));
  }

  std::vector<AppliedConditionBlocker> MatchBlockers(
      size_t index,
      const AppliedSnapshot& snapshot) const {
    using Blocker = AppliedConditionBlocker;
    std::vector<Blocker> blockers;
    const auto& condition = conditions_[index];
    const auto& fence = diagnostics_.conditions[index].dispatch_fence;
    if (condition.owner != snapshot.owner) {
      blockers.push_back(Blocker::kOwnerMismatch);
    }
    if (GetAppliedConditionKind(condition.expected) !=
        GetAppliedConditionKind(snapshot.value)) {
      blockers.push_back(Blocker::kKindMismatch);
    } else if (!ValueMatches(condition.expected, snapshot.value)) {
      blockers.push_back(Blocker::kValueMismatch);
    }
    if (condition.coordinates != snapshot.coordinates) {
      blockers.push_back(Blocker::kCoordinatesMismatch);
    }
    if (ticket().intent.request_epoch && snapshot.request_epoch &&
        ticket().intent.request_epoch != snapshot.request_epoch) {
      blockers.push_back(Blocker::kRequestEpochMismatch);
    }
    if (condition.mode == AppliedEvaluationMode::kObservedSinceDispatch) {
      if (snapshot.sample_id == 0) {
        blockers.push_back(Blocker::kNoSampleIdentity);
      }
      if (snapshot.serials.observation_sequence <= fence.observation_sequence) {
        blockers.push_back(Blocker::kObservationNotNew);
      }
    }
    if (diagnostics_.options.already_applied ==
        WaitOptions::AlreadyApplied::kAllowAlreadyApplied) {
      return blockers;
    }
    switch (condition.freshness) {
      case AppliedFreshness::kNewApply:
        if (snapshot.serials.apply_serial <= fence.apply_serial) {
          blockers.push_back(Blocker::kApplyNotNew);
        }
        [[fallthrough]];
      case AppliedFreshness::kNewObservation:
        if (snapshot.serials.observation_sequence <=
                fence.observation_sequence &&
            !std::ranges::contains(blockers, Blocker::kObservationNotNew)) {
          blockers.push_back(Blocker::kObservationNotNew);
        }
        break;
      case AppliedFreshness::kCurrentAfterDispatch:
        break;
    }
    return blockers;
  }

  void LatchObservedSamples() {
    std::map<uint64_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < conditions_.size(); ++i) {
      if (conditions_[i].mode ==
          AppliedEvaluationMode::kObservedSinceDispatch) {
        groups[conditions_[i].observation_group].push_back(i);
      }
    }
    for (const auto& sample : pending_samples_) {
      for (const auto& [group, indices] : groups) {
        if (diagnostics_.conditions[indices.front()].observed) {
          continue;
        }
        const auto& first = sample[indices.front()];
        if (!first || !std::ranges::all_of(indices, [&](size_t i) {
              return sample[i] && sample[i]->sample_id == first->sample_id &&
                     MatchBlockers(i, *sample[i]).empty();
            })) {
          continue;
        }
        for (size_t i : indices) {
          diagnostics_.conditions[i].observed = sample[i];
        }
      }
    }
    pending_samples_.clear();
  }

  void Evaluate() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    evaluation_queued_ = false;
    diagnostics_.evaluated_at = base::TimeTicks::Now();
    if (!diagnostics_.evaluation_queued_at.is_null()) {
      diagnostics_.max_evaluation_delay = std::max(
          diagnostics_.max_evaluation_delay,
          diagnostics_.evaluated_at - diagnostics_.evaluation_queued_at);
      diagnostics_.evaluation_queued_at = {};
    }
    ++diagnostics_.evaluation_count;
    Record(AppliedEventKind::kEvaluate);
    for (size_t i = 0; i < conditions_.size(); ++i) {
      const auto& condition = conditions_[i];
      if (!source_->IsOwnerAlive(condition.owner)) {
        if (!destroyed_.contains(condition.owner) && dispatched()) {
          destroyed_after_dispatch_.insert(condition.owner);
        }
        destroyed_.insert(condition.owner);
      }
      auto& diagnostic = diagnostics_.conditions[i];
      diagnostic.owner_destroyed = destroyed_.contains(condition.owner);
      if (!diagnostic.owner_destroyed) {
        const auto snapshot = source_->Read(KeyFor(condition));
        if (snapshot) {
          const bool accepted = Accept(i, *snapshot, true);
          diagnostic.blockers = MatchBlockers(i, *snapshot);
          if (!accepted && HasKey(*snapshot, KeyFor(condition))) {
            diagnostic.blockers.push_back(
                AppliedConditionBlocker::kSerialRegression);
          }
        } else {
          diagnostic.blockers = {AppliedConditionBlocker::kNoCurrentSnapshot};
        }
      }
    }
    LatchObservedSamples();
    bool all_satisfied = true;
    for (size_t i = 0; i < conditions_.size(); ++i) {
      auto& diagnostic = diagnostics_.conditions[i];
      if (diagnostic.owner_destroyed) {
        diagnostic.satisfied =
            ExpectsDestruction(conditions_[i]) &&
            destroyed_after_dispatch_.contains(conditions_[i].owner);
        diagnostic.blockers.clear();
        if (!diagnostic.satisfied) {
          diagnostic.blockers.push_back(
              ExpectsDestruction(conditions_[i])
                  ? AppliedConditionBlocker::kDestroyedBeforeDispatch
                  : AppliedConditionBlocker::kOwnerDestroyed);
        }
      } else if (conditions_[i].mode ==
                 AppliedEvaluationMode::kObservedSinceDispatch) {
        diagnostic.satisfied = diagnostic.observed.has_value();
        if (diagnostic.satisfied) {
          diagnostic.blockers.clear();
        } else {
          if (diagnostic.blockers.empty()) {
            diagnostic.blockers.push_back(
                AppliedConditionBlocker::kObservationGroupNotLatched);
          }
          diagnostic.blockers.push_back(
              AppliedConditionBlocker::kNoObservedSample);
        }
      } else {
        diagnostic.satisfied = diagnostic.blockers.empty();
      }
      all_satisfied &= diagnostic.satisfied;
    }
    if (base::TimeTicks::Now() >= diagnostics_.options.deadline) {
      Finish(AppliedResultKind::kTimedOut);
      return;
    }
    for (size_t i = 0; i < conditions_.size(); ++i) {
      if (diagnostics_.conditions[i].owner_destroyed &&
          (!ExpectsDestruction(conditions_[i]) ||
           !destroyed_after_dispatch_.contains(conditions_[i].owner))) {
        Finish(AppliedResultKind::kOwnerDestroyed);
        return;
      }
    }
    if (dispatched() && diagnostics_.dispatch_acknowledged && all_satisfied) {
      Finish(AppliedResultKind::kApplied);
    }
  }

  void OnDeadline() {
    Record(AppliedEventKind::kDeadline);
    Evaluate();
  }

  void StopWork() {
    subscription_ = {};
    deadline_.Stop();
    work_weak_.InvalidateWeakPtrs();
    evaluation_queued_ = false;
    pending_samples_.clear();
  }

  void ScheduleDelivery() {
    if (!result_ || !done_ || delivery_queued_) {
      return;
    }
    delivery_queued_ = true;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&Request::Deliver, delivery_weak_.GetWeakPtr()));
  }

  void Deliver() {
    delivery_queued_ = false;
    // callback이 세션과 Request를 파괴할 수 있으므로 이후 멤버를 읽지 않는다.
    auto done = std::move(done_);
    auto result = *result_;
    if (done) {
      std::move(done).Run(std::move(result));
    }
  }

  void Release() {
    Record(AppliedEventKind::kObservationReleased);
    done_.Reset();
    delivery_weak_.InvalidateWeakPtrs();
    Finish(AppliedResultKind::kCancelled);
  }

  const base::raw_ref<AppliedSignalSource> source_;
  const AppliedConditions conditions_;
  AppliedWaitDiagnostics diagnostics_;
  std::vector<AppliedSerials> high_water_;
  std::vector<AppliedSerials> event_high_water_;
  std::vector<std::vector<std::optional<AppliedSnapshot>>> pending_samples_;
  std::set<AppliedOwnerToken> destroyed_;
  std::set<AppliedOwnerToken> destroyed_after_dispatch_;
  base::CallbackListSubscription subscription_;
  base::OneShotTimer deadline_;
  bool evaluation_queued_ = false;
  bool delivery_queued_ = false;
  bool claimed_ = false;
  std::optional<AppliedWaitResult> result_;
  base::OnceCallback<void(AppliedWaitResult)> done_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<Request> work_weak_{this};
  base::WeakPtrFactory<Request> delivery_weak_{this};
};

}  // namespace

AppliedConditionKind GetAppliedConditionKind(
    const AppliedConditionValue& value) {
  return static_cast<AppliedConditionKind>(value.index());
}

std::string FormatAppliedValue(const AppliedConditionValue& value) {
  return std::visit(
      [](const auto& state) {
        using T = std::decay_t<decltype(state)>;
        Fields fields;
        if constexpr (std::is_same_v<T, PageHostsApplied>) {
          fields.Add("pane", state.pane_id)
              .Add("tab", state.tab_handle)
              .Add("bounds", state.bounds)
              .Add("clip", state.clip)
              .Add("radii", state.radii)
              .Add("target", state.target);
        } else if constexpr (std::is_same_v<T, PaneLayoutMatches>) {
          fields.Add("split", state.split_id)
              .Add("axis", state.axis)
              .Add("parent_extent", state.parent_extent)
              .Add("ratio", state.ratio)
              .Add("insets", state.insets)
              .Add("divider", state.divider)
              .Add("tabs", state.tab_handles);
        } else if constexpr (std::is_same_v<T, ViewStateMatches>) {
          fields.Add("drawn", state.drawn)
              .Add("visible", state.visible)
              .Add("bounds", state.bounds)
              .Add("parent", state.parent)
              .Add("children", state.children)
              .Add("scroll", state.scroll_offset)
              .Add("destroyed", state.destroyed);
        } else if constexpr (std::is_same_v<T, AnimationMatches>) {
          fields.Add("group", state.group_id)
              .Add("running", state.running)
              .Add("progress_min", state.progress_min)
              .Add("progress_max", state.progress_max);
        } else if constexpr (std::is_same_v<T, SurfaceEpochCommitted>) {
          fields.Add("epoch", state.epoch)
              .Add("phase", state.phase)
              .Add("committed_generation", state.committed_generation)
              .Add("destination", state.destination);
        } else if constexpr (std::is_same_v<T, WidgetStateMatches>) {
          fields.Add("visible", state.visible)
              .Add("bounds", state.bounds)
              .Add("capture", state.capture)
              .Add("show_state", state.show_state)
              .Add("closed", state.closed);
        } else if constexpr (std::is_same_v<T, FocusMatches>) {
          constexpr std::array<const char*, 3> kKinds = {
              "logical_view", "renderer_document", "native_key_window"};
          fields.Add("focus_kind", kKinds[static_cast<int>(state.kind)])
              .Add("target", state.target)
              .Add("document", state.document)
              .Add("focused", state.focused);
        } else if constexpr (std::is_same_v<T, NativeWindowMatches>) {
          fields.Add("active", state.active)
              .Add("onActiveSpace", state.on_active_space)
              .Add("unoccluded", state.unoccluded)
              .Add("fullscreen", state.fullscreen)
              .Add("fullscreen_complete", state.fullscreen_transition_complete);
        } else if constexpr (std::is_same_v<T, TabModelMatches>) {
          fields.Add("active", state.active_tab);
          for (const auto& tab : state.tabs) {
            fields.Add("tab", tab.handle)
                .Add("pinned", tab.pinned)
                .Add("group", tab.group)
                .Add("split", tab.split_id);
          }
          for (const auto& split : state.splits) {
            fields.Add("split_id", split.split_id)
                .Add("axis", split.axis)
                .Add("ratio", split.ratio)
                .Add("tabs", split.tab_handles);
          }
        } else if constexpr (std::is_same_v<T, BrowserSetMatches>) {
          for (const auto& browser : state.browsers) {
            fields.Add("browser", browser.browser)
                .Add("tabs", browser.tab_handles);
          }
          fields.Add("created", state.created).Add("closed", state.closed);
        } else if constexpr (std::is_same_v<T, DragPhaseMatches>) {
          fields.Add("drag_session", state.session)
              .Add("step", state.step)
              .Add("source", state.source_handle)
              .Add("target", state.target_handle)
              .Add("phase", state.phase)
              .Add("attached_browser", state.attached_browser)
              .Add("input_ack", state.input_acknowledged);
          if (state.last_input_screen) {
            fields.Add("input_screen", *state.last_input_screen);
          }
        } else if constexpr (std::is_same_v<T, PreviewApplied>) {
          fields.Add("source", state.source_handle)
              .Add("tab", state.tab_handle)
              .Add("visible", state.visible)
              .Add("running", state.animation_running)
              .Add("screen_bounds", state.screen_bounds)
              .Add("source_x", state.source_screen_x)
              .Add("source_width", state.source_screen_width);
        } else if constexpr (std::is_same_v<T, RendererViewportMatches>) {
          fields.Add("web_contents", state.web_contents)
              .Add("RFH", state.render_frame)
              .Add("document", state.document)
              .Add("surface", state.surface)
              .Add("viewport_pixels", state.viewport_pixels)
              .Add("scale", state.scale);
        } else if constexpr (std::is_same_v<T, WebUiStateMatches>) {
          fields.Add("document", state.document)
              .Add("rows", state.popup_rows)
              .Add("selected", state.selected_index)
              .Add("rect", state.rect)
              .Add("opacity", state.opacity)
              .Add("focused", state.focused);
        } else if constexpr (std::is_same_v<T, PresentationMatches>) {
          fields.Add("source_id", state.source_id)
              .Add("presentation", state.presentation_id)
              .Add("background", state.background_color)
              .Add("foreground", state.foreground_color)
              .Add("icon", state.icon_id)
              .Add("native_theme", state.use_native_theme);
        } else if constexpr (std::is_same_v<T, PresentedAfterApply>) {
          fields.Add("compositor", state.compositor)
              .Add("geometry_digest", state.geometry_digest)
              .Add("applied_sample", state.applied_sample_id)
              .Add("successful", state.successful);
        }
        return fields.str();
      },
      value);
}

std::string AppliedWaitDiagnostics::ToString() const {
  constexpr std::array<const char*, 3> kTiers = {"A", "B", "C"};
  constexpr std::array<const char*, 3> kBackends = {"fake", "headless",
                                                    "native"};
  constexpr std::array<const char*, 3> kPlatforms = {"macOS", "Linux",
                                                     "Windows"};
  constexpr std::array<const char*, 3> kFreshness = {
      "NewApply", "NewObservation", "CurrentAfterDispatch"};
  constexpr std::array<const char*, 2> kModes = {"Current",
                                                 "ObservedSinceDispatch"};
  constexpr std::array<const char*, 16> kBlockers = {
      "not-evaluated",
      "unsupported",
      "owner-destroyed",
      "owner-destroyed-before-dispatch",
      "no-current-snapshot",
      "owner-mismatch",
      "kind-mismatch",
      "value-mismatch",
      "coordinates-mismatch",
      "request-epoch-mismatch",
      "serial-regression",
      "freshness:apply-not-new",
      "freshness:observation-not-new",
      "mode:no-sample-identity",
      "mode:no-observed-sample",
      "mode:observation-group-not-latched"};
  std::ostringstream out;
  out << "test=" << options.test_name << " parameter=" << options.parameter
      << " tier=" << kTiers[static_cast<int>(options.tier)]
      << " backend=" << kBackends[static_cast<int>(options.backend)]
      << " OS=" << kPlatforms[static_cast<int>(options.platform)]
      << " session=" << ticket.session_id.ToString()
      << " request=" << ticket.request_serial
      << " domain=" << static_cast<int>(ticket.domain)
      << " intent=" << static_cast<int>(ticket.intent.operation)
      << " elapsed_ms=" << (captured_at - began_at).InMillisecondsF()
      << " budget_ms=" << (options.deadline - began_at).InMillisecondsF()
      << " dispatched=" << dispatched << " ACK=" << dispatch_acknowledged
      << " evaluations=" << evaluation_count << " wakes=" << wake_count
      << " evaluated_ms="
      << (evaluated_at.is_null() ? -1
                                 : (evaluated_at - began_at).InMillisecondsF())
      << " queued_ms="
      << (evaluation_queued_at.is_null()
              ? -1
              : (captured_at - evaluation_queued_at).InMillisecondsF())
      << " max_evaluation_delay_ms=" << max_evaluation_delay.InMillisecondsF()
      << '\n';
  out << "completion_blockers=[";
  if (!dispatched)
    out << "not-dispatched,";
  if (!dispatch_acknowledged)
    out << "ACK-pending,";
  if (deadline_expired)
    out << "deadline-exceeded,";
  if (std::ranges::any_of(conditions, [](const auto& diagnostic) {
        return !diagnostic.satisfied;
      }))
    out << "conditions-unmet,";
  out << "]\n";
  if (ticket.intent.request_epoch) {
    out << "request_epoch=" << *ticket.intent.request_epoch << '\n';
  }
  for (const auto& diagnostic : conditions) {
    const auto& condition = diagnostic.condition;
    out << kConditionNames[static_cast<int>(
               GetAppliedConditionKind(condition.expected))]
        << " owner=" << condition.owner.ToString()
        << " freshness=" << kFreshness[static_cast<int>(condition.freshness)]
        << " mode=" << kModes[static_cast<int>(condition.mode)]
        << " group=" << condition.observation_group
        << " supported=" << diagnostic.supported
        << " satisfied=" << diagnostic.satisfied
        << " destroyed=" << diagnostic.owner_destroyed << " blockers=[";
    for (auto blocker : diagnostic.blockers)
      out << kBlockers[static_cast<int>(blocker)] << ',';
    out << "]\n expected: " << FormatAppliedValue(condition.expected)
        << FormatCoordinates(condition.coordinates) << '\n'
        << " baseline: " << FormatSerials(diagnostic.baseline) << '\n'
        << " dispatch_fence: " << FormatSerials(diagnostic.dispatch_fence)
        << '\n'
        << " high_water: " << FormatSerials(diagnostic.high_water) << '\n'
        << " event_high_water: " << FormatSerials(diagnostic.event_high_water)
        << '\n';
    const auto write_snapshot = [&](const char* label,
                                    const std::optional<AppliedSnapshot>& s) {
      out << label << ": ";
      if (s) {
        out << "owner=" << s->owner.ToString() << " kind="
            << kConditionNames[static_cast<int>(
                   GetAppliedConditionKind(s->value))]
            << ' ' << FormatAppliedValue(s->value) << FormatSerials(s->serials)
            << "sample=" << s->sample_id << ' '
            << FormatCoordinates(s->coordinates);
        if (s->request_epoch) {
          out << "epoch=" << *s->request_epoch;
        }
      } else {
        out << (diagnostic.supported ? "unobserved" : "unsupported");
      }
      out << '\n';
    };
    write_snapshot("last", diagnostic.last);
    write_snapshot("observed", diagnostic.observed);
  }
  constexpr std::array<const char*, 10> kEvents = {
      "begin",     "dispatch", "ACK",      "signal",   "stale",
      "destroyed", "evaluate", "deadline", "terminal", "released"};
  for (const auto& event : events) {
    out << "event +" << (event.timestamp - began_at).InMillisecondsF() << "ms "
        << kEvents[static_cast<int>(event.kind)]
        << " owner=" << event.owner.ToString() << ' '
        << FormatSerials(event.serials) << "sample=" << event.sample_id;
    if (event.condition_kind) {
      out << " kind="
          << kConditionNames[static_cast<int>(*event.condition_kind)];
    }
    out << '\n';
  }
  return out.str();
}

std::string AppliedWaitResult::ToString() const {
  constexpr std::array<const char*, 8> kResults = {
      "Applied",   "TimedOut",       "Superseded",           "OwnerDestroyed",
      "Cancelled", "DispatchFailed", "UnsupportedCondition", "InvalidTicket"};
  return std::string(kResults[static_cast<int>(kind)]) + "\n" +
         diagnostics.ToString();
}

WaitOptions::Platform WaitOptions::CurrentPlatform() {
#if BUILDFLAG(IS_MAC)
  return Platform::kMac;
#elif BUILDFLAG(IS_WIN)
  return Platform::kWindows;
#else
  return Platform::kLinux;
#endif
}

class AppliedWaitSession::Impl {
 public:
  explicit Impl(AppliedSignalSource& source) : source_(source) {}
  ~Impl() {
    for (auto& [serial, request] : requests_) {
      request->Shutdown();
    }
  }

  AppliedTicket Begin(AppliedIntent intent,
                      AppliedConditions conditions,
                      WaitOptions options) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (intent.targets.empty()) {
      for (const auto& condition : conditions) {
        intent.targets.push_back({condition.owner, condition.expected});
      }
    }
    AppliedTicket ticket{
        session_id_, ++request_serial_, intent.domain, intent.owner_tokens, {},
        intent};
    auto request = std::make_shared<Request>(
        *source_, ticket, std::move(conditions), std::move(options));
    requests_.emplace(ticket.request_serial, request);
    request->Start();
    if (!request->terminal()) {
      for (const auto& [serial, previous] : requests_) {
        if (previous == request || previous->terminal() ||
            previous->ticket().domain != ticket.domain) {
          continue;
        }
        if (std::ranges::any_of(ticket.owner_tokens, [&](const auto& owner) {
              return std::ranges::contains(previous->ticket().owner_tokens,
                                           owner);
            })) {
          previous->Finish(AppliedResultKind::kSuperseded);
        }
      }
    }
    return request->ticket();
  }

  std::shared_ptr<Request> Find(const AppliedTicket& ticket) const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    const auto it = requests_.find(ticket.request_serial);
    if (it == requests_.end() || it->second->ticket() != ticket) {
      return nullptr;
    }
    return it->second;
  }

  base::OnceClosure PostInvalid(
      base::OnceCallback<void(AppliedWaitResult)> done,
      AppliedWaitResult result) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    auto callback = std::make_shared<ResultCallback>(std::move(done));
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Impl::DeliverInvalid, weak_.GetWeakPtr(),
                                  callback, std::move(result)));
    return base::BindOnce(
        [](std::shared_ptr<ResultCallback> callback) { callback->Reset(); },
        callback);
  }

 private:
  using ResultCallback = base::OnceCallback<void(AppliedWaitResult)>;

  void DeliverInvalid(std::shared_ptr<ResultCallback> callback,
                      AppliedWaitResult result) {
    if (*callback) {
      std::move(*callback).Run(std::move(result));
    }
  }

  const base::raw_ref<AppliedSignalSource> source_;
  const base::UnguessableToken session_id_ = base::UnguessableToken::Create();
  uint64_t request_serial_ = 0;
  std::map<uint64_t, std::shared_ptr<Request>> requests_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<Impl> weak_{this};
};

AppliedObservation::AppliedObservation() = default;
AppliedObservation::AppliedObservation(base::OnceClosure release)
    : release_(std::move(release)) {}
AppliedObservation::AppliedObservation(AppliedObservation&&) = default;
AppliedObservation& AppliedObservation::operator=(AppliedObservation&&) =
    default;
AppliedObservation::~AppliedObservation() = default;
void AppliedObservation::Reset() {
  release_.RunAndReset();
}

AppliedWaitSession::AppliedWaitSession(AppliedSignalSource& source)
    : impl_(std::make_unique<Impl>(source)) {}
AppliedWaitSession::~AppliedWaitSession() = default;

AppliedTicket AppliedWaitSession::BeginRequest(AppliedIntent intent,
                                               AppliedConditions conditions,
                                               WaitOptions options) {
  return impl_->Begin(std::move(intent), std::move(conditions),
                      std::move(options));
}

std::optional<AppliedWaitResult> AppliedWaitSession::Dispatch(
    AppliedTicket ticket,
    base::OnceClosure action) {
  auto request = impl_->Find(ticket);
  if (!request) {
    return InvalidResult(ticket, nullptr);
  }
  if (auto error = request->StartDispatch(!action.is_null())) {
    return error;
  }
  std::move(action).Run();
  request->Acknowledge(AppliedDispatchStatus::kAccepted);
  return std::nullopt;
}

std::optional<AppliedWaitResult> AppliedWaitSession::DispatchAsync(
    AppliedTicket ticket,
    base::OnceCallback<void(DispatchDone)> submit) {
  auto request = impl_->Find(ticket);
  if (!request) {
    return InvalidResult(ticket, nullptr);
  }
  if (auto error = request->StartDispatch(!submit.is_null())) {
    return error;
  }
  auto ack = request->AckCallback();
  std::move(submit).Run(std::move(ack));
  return std::nullopt;
}

AppliedWaitResult AppliedWaitSession::WaitForApplied(
    AppliedTicket ticket,
    const AppliedConditions& conditions) {
  auto request = impl_->Find(ticket);
  if (!request || request->conditions() != conditions ||
      (!request->dispatched() && !request->terminal())) {
    return InvalidResult(ticket, nullptr);
  }
  std::optional<AppliedWaitResult> result;
  // 테스트 본문이 task에서 실행되어도 평가·배달 task를 처리해야 한다.
  base::RunLoop loop(base::RunLoop::Type::kNestableTasksAllowed);
  if (!request->Observe(base::BindOnce(
          [](std::optional<AppliedWaitResult>* result, base::OnceClosure quit,
             AppliedWaitResult outcome) {
            *result = std::move(outcome);
            std::move(quit).Run();
          },
          &result, loop.QuitClosure()))) {
    const auto diagnostics = request->Diagnostics();
    return InvalidResult(ticket, &diagnostics);
  }
  loop.Run();
  return std::move(*result);
}

AppliedObservation AppliedWaitSession::ObserveApplied(
    AppliedTicket ticket,
    const AppliedConditions& conditions,
    base::OnceCallback<void(AppliedWaitResult)> done) {
  CHECK(done);
  auto request = impl_->Find(ticket);
  if (request && request->conditions() == conditions &&
      request->Observe(std::move(done))) {
    return AppliedObservation(request->ReleaseCallback());
  }
  // 잘못된 호출도 비동기이며 반환한 handle로 배달 전 callback을 취소할 수 있다.
  return AppliedObservation(
      impl_->PostInvalid(std::move(done), InvalidResult(ticket, nullptr)));
}

std::optional<AppliedWaitResult> AppliedWaitSession::Cancel(
    AppliedTicket ticket) {
  auto request = impl_->Find(ticket);
  if (!request) {
    return InvalidResult(ticket, nullptr);
  }
  request->Finish(AppliedResultKind::kCancelled);
  return std::nullopt;
}

std::optional<AppliedWaitDiagnostics> AppliedWaitSession::GetDiagnostics(
    AppliedTicket ticket) const {
  const auto request = impl_->Find(ticket);
  return request ? std::make_optional(request->Diagnostics()) : std::nullopt;
}

}  // namespace yee::test
