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
#include "base/hash/hash.h"
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

const char* MatchName(AppliedMatch match) {
  switch (match) {
    case AppliedMatch::kExact:
      return "kExact";
    case AppliedMatch::kPageIdentity:
      return "kPageIdentity";
    case AppliedMatch::kPageGeometry:
      return "kPageGeometry";
    case AppliedMatch::kPaneSplit:
      return "kPaneSplit";
    case AppliedMatch::kPaneSplitCurrentExtent:
      return "kPaneSplitCurrentExtent";
    case AppliedMatch::kViewVisible:
      return "kViewVisible";
    case AppliedMatch::kViewDrawn:
      return "kViewDrawn";
    case AppliedMatch::kViewBounds:
      return "kViewBounds";
    case AppliedMatch::kViewSize:
      return "kViewSize";
    case AppliedMatch::kViewHeight:
      return "kViewHeight";
    case AppliedMatch::kViewParent:
      return "kViewParent";
    case AppliedMatch::kViewParentDifferent:
      return "kViewParentDifferent";
    case AppliedMatch::kViewChildren:
      return "kViewChildren";
    case AppliedMatch::kViewNonEmpty:
      return "kViewNonEmpty";
    case AppliedMatch::kViewCenterVisible:
      return "kViewCenterVisible";
    case AppliedMatch::kViewPointerFocus:
      return "kViewPointerFocus";
    case AppliedMatch::kViewDestroyed:
      return "kViewDestroyed";
    case AppliedMatch::kViewOpacity:
      return "kViewOpacity";
    case AppliedMatch::kViewOpacityPositive:
      return "kViewOpacityPositive";
    case AppliedMatch::kViewHovered:
      return "kViewHovered";
    case AppliedMatch::kViewChildCount:
      return "kViewChildCount";
    case AppliedMatch::kSidebarHover:
      return "kSidebarHover";
    case AppliedMatch::kTabHoverIdle:
      return "kTabHoverIdle";
    case AppliedMatch::kViewScrollIdle:
      return "kViewScrollIdle";
    case AppliedMatch::kOmniboxPopup:
      return "kOmniboxPopup";
    case AppliedMatch::kOmniboxEditing:
      return "kOmniboxEditing";
    case AppliedMatch::kWidgetVisible:
      return "kWidgetVisible";
    case AppliedMatch::kWidgetOpacity:
      return "kWidgetOpacity";
    case AppliedMatch::kWidgetBounds:
      return "kWidgetBounds";
    case AppliedMatch::kWidgetClosed:
      return "kWidgetClosed";
    case AppliedMatch::kWidgetCapture:
      return "kWidgetCapture";
    case AppliedMatch::kWidgetActive:
      return "kWidgetActive";
    case AppliedMatch::kWidgetReady:
      return "kWidgetReady";
    case AppliedMatch::kWidgetShowState:
      return "kWidgetShowState";
    case AppliedMatch::kWidgetSizeNear:
      return "kWidgetSizeNear";
    case AppliedMatch::kFooterMenu:
      return "kFooterMenu";
    case AppliedMatch::kProjectRefreshed:
      return "kProjectRefreshed";
    case AppliedMatch::kProjectTitle:
      return "kProjectTitle";
    case AppliedMatch::kProjectLive:
      return "kProjectLive";
    case AppliedMatch::kTabCount:
      return "kTabCount";
    case AppliedMatch::kTabActive:
      return "kTabActive";
    case AppliedMatch::kNewTabActive:
      return "kNewTabActive";
    case AppliedMatch::kPresentationRevision:
      return "kPresentationRevision";
    case AppliedMatch::kPresentationRevisionChanged:
      return "kPresentationRevisionChanged";
    case AppliedMatch::kPresentationIconChanged:
      return "kPresentationIconChanged";
    case AppliedMatch::kPresentationIconReady:
      return "kPresentationIconReady";
    case AppliedMatch::kSamplingSettled:
      return "kSamplingSettled";
    case AppliedMatch::kNativeActive:
      return "kNativeActive";
    case AppliedMatch::kNativeFullscreen:
      return "kNativeFullscreen";
    case AppliedMatch::kPresentationColor:
      return "kPresentationColor";
    case AppliedMatch::kPresentationIcon:
      return "kPresentationIcon";
    case AppliedMatch::kPresentationAlpha:
      return "kPresentationAlpha";
    case AppliedMatch::kPresentationDark:
      return "kPresentationDark";
    case AppliedMatch::kWebUiRowsAtLeast:
      return "kWebUiRowsAtLeast";
    case AppliedMatch::kWebUiRowsFitNativeBounds:
      return "kWebUiRowsFitNativeBounds";
    case AppliedMatch::kWebUiSelection:
      return "kWebUiSelection";
    case AppliedMatch::kWebUiOpacity:
      return "kWebUiOpacity";
    case AppliedMatch::kWebUiHover:
      return "kWebUiHover";
    case AppliedMatch::kWebUiPadding:
      return "kWebUiPadding";
    case AppliedMatch::kWebUiResultsPadding:
      return "kWebUiResultsPadding";
    case AppliedMatch::kWebUiMedia:
      return "kWebUiMedia";
    case AppliedMatch::kWebUiHoverColor:
      return "kWebUiHoverColor";
    case AppliedMatch::kWebUiCustomizationReady:
      return "kWebUiCustomizationReady";
    case AppliedMatch::kWebUiClickCount:
      return "kWebUiClickCount";
    case AppliedMatch::kSurfaceDestination:
      return "kSurfaceDestination";
    case AppliedMatch::kViewScrollOffset:
      return "kViewScrollOffset";
    case AppliedMatch::kViewScrolledDown:
      return "kViewScrolledDown";
    case AppliedMatch::kViewGroupDrag:
      return "kViewGroupDrag";
    case AppliedMatch::kTabOrder:
      return "kTabOrder";
    case AppliedMatch::kPinnedTabOrder:
      return "kPinnedTabOrder";
    case AppliedMatch::kTabMembership:
      return "kTabMembership";
    case AppliedMatch::kDragInputAcknowledged:
      return "kDragInputAcknowledged";
    case AppliedMatch::kDragPhase:
      return "kDragPhase";
    case AppliedMatch::kPreviewSettled:
      return "kPreviewSettled";
    case AppliedMatch::kPreviewAligned:
      return "kPreviewAligned";
    case AppliedMatch::kCapsuleTheme:
      return "kCapsuleTheme";
    case AppliedMatch::kPresentedFrame:
      return "kPresentedFrame";
  }
  return "invalid";
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
      diagnostic.supported = IsAppliedMatchCompatible(diagnostic.condition) &&
                             source_->Supports(diagnostic.condition);
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
    // 절대 deadline의 일회 실패 알림이며 상태 재검사를 예약하지 않는다.
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
    } else if (!AppliedValueMatches(condition, snapshot.value)) {
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

bool IsAppliedMatchCompatible(const AppliedCondition& condition) {
  const auto kind = GetAppliedConditionKind(condition.expected);
  switch (condition.match) {
    case AppliedMatch::kExact:
      return true;
    case AppliedMatch::kPageIdentity:
    case AppliedMatch::kPageGeometry:
      return kind == AppliedConditionKind::kPageHostsApplied;
    case AppliedMatch::kPaneSplit:
    case AppliedMatch::kPaneSplitCurrentExtent:
      return kind == AppliedConditionKind::kPaneLayoutMatches;
    case AppliedMatch::kViewVisible:
    case AppliedMatch::kViewDrawn:
    case AppliedMatch::kViewBounds:
    case AppliedMatch::kViewSize:
    case AppliedMatch::kViewHeight:
    case AppliedMatch::kViewParent:
    case AppliedMatch::kViewParentDifferent:
    case AppliedMatch::kViewChildren:
    case AppliedMatch::kViewNonEmpty:
    case AppliedMatch::kViewCenterVisible:
    case AppliedMatch::kViewPointerFocus:
    case AppliedMatch::kViewDestroyed:
    case AppliedMatch::kViewOpacity:
    case AppliedMatch::kViewOpacityPositive:
    case AppliedMatch::kViewHovered:
    case AppliedMatch::kViewChildCount:
    case AppliedMatch::kSidebarHover:
    case AppliedMatch::kTabHoverIdle:
    case AppliedMatch::kViewScrollIdle:
    case AppliedMatch::kViewScrollOffset:
    case AppliedMatch::kViewScrolledDown:
    case AppliedMatch::kViewGroupDrag:
    case AppliedMatch::kOmniboxPopup:
    case AppliedMatch::kOmniboxEditing:
    case AppliedMatch::kFooterMenu:
    case AppliedMatch::kProjectRefreshed:
      return kind == AppliedConditionKind::kViewStateMatches;
    case AppliedMatch::kWidgetVisible:
    case AppliedMatch::kWidgetOpacity:
    case AppliedMatch::kWidgetBounds:
    case AppliedMatch::kWidgetClosed:
    case AppliedMatch::kWidgetCapture:
    case AppliedMatch::kWidgetActive:
    case AppliedMatch::kWidgetReady:
    case AppliedMatch::kWidgetShowState:
    case AppliedMatch::kWidgetSizeNear:
      return kind == AppliedConditionKind::kWidgetStateMatches;
    case AppliedMatch::kProjectTitle:
    case AppliedMatch::kProjectLive:
    case AppliedMatch::kTabCount:
    case AppliedMatch::kTabOrder:
    case AppliedMatch::kPinnedTabOrder:
    case AppliedMatch::kTabMembership:
    case AppliedMatch::kTabActive:
    case AppliedMatch::kNewTabActive:
      return kind == AppliedConditionKind::kTabModelMatches;
    case AppliedMatch::kPresentationRevision:
    case AppliedMatch::kPresentationRevisionChanged:
    case AppliedMatch::kPresentationIconChanged:
    case AppliedMatch::kPresentationIconReady:
    case AppliedMatch::kSamplingSettled:
    case AppliedMatch::kPresentationColor:
    case AppliedMatch::kPresentationIcon:
    case AppliedMatch::kPresentationAlpha:
    case AppliedMatch::kPresentationDark:
    case AppliedMatch::kCapsuleTheme:
      return kind == AppliedConditionKind::kPresentationMatches;
    case AppliedMatch::kNativeActive:
    case AppliedMatch::kNativeFullscreen:
      return kind == AppliedConditionKind::kNativeWindowMatches;
    case AppliedMatch::kWebUiRowsAtLeast:
    case AppliedMatch::kWebUiRowsFitNativeBounds:
    case AppliedMatch::kWebUiSelection:
    case AppliedMatch::kWebUiOpacity:
    case AppliedMatch::kWebUiHover:
    case AppliedMatch::kWebUiPadding:
    case AppliedMatch::kWebUiResultsPadding:
    case AppliedMatch::kWebUiMedia:
    case AppliedMatch::kWebUiHoverColor:
    case AppliedMatch::kWebUiCustomizationReady:
    case AppliedMatch::kWebUiClickCount:
      return kind == AppliedConditionKind::kWebUiStateMatches;
    case AppliedMatch::kSurfaceDestination:
      return kind == AppliedConditionKind::kSurfaceEpochCommitted;
    case AppliedMatch::kDragInputAcknowledged:
    case AppliedMatch::kDragPhase:
      return kind == AppliedConditionKind::kDragPhaseMatches;
    case AppliedMatch::kPreviewSettled:
    case AppliedMatch::kPreviewAligned:
      return kind == AppliedConditionKind::kPreviewApplied;
    case AppliedMatch::kPresentedFrame:
      return kind == AppliedConditionKind::kPresentedAfterApply;
  }
  return false;
}

bool AppliedValueMatches(const AppliedCondition& condition,
                         const AppliedConditionValue& actual) {
  if (condition.match == AppliedMatch::kExact)
    return ValueMatches(condition.expected, actual);
  if (condition.expected.index() != actual.index())
    return false;
  return std::visit(
      [&](const auto& expected) {
        using T = std::decay_t<decltype(expected)>;
        const auto& value = std::get<T>(actual);
        if constexpr (std::is_same_v<T, PageHostsApplied>) {
          if (condition.match == AppliedMatch::kPageGeometry)
            return value.geometry_applied && !value.bounds.IsEmpty();
          return condition.match == AppliedMatch::kPageIdentity &&
                 expected.pane_id == value.pane_id &&
                 expected.tab_handle == value.tab_handle &&
                 value.geometry_applied && !value.bounds.IsEmpty();
        } else if constexpr (std::is_same_v<T, PaneLayoutMatches>) {
          // Header·InfoBar·footer가 차지하는 요청 geometry를 각 카드에 적용해
          // 실제 renderer 부모의 목표 extent를 비교한다. 카드 ratio만으로
          // PageTargetHost의 적용 완료를 대신하지 않는다.
          const auto parent_target = [](gfx::Size container,
                                        gfx::Size expected_container,
                                        gfx::Size expected_parent) {
            return gfx::Size(container.width() - expected_container.width() +
                                 expected_parent.width(),
                             container.height() - expected_container.height() +
                                 expected_parent.height());
          };
          return (condition.match == AppliedMatch::kPaneSplit ||
                  condition.match == AppliedMatch::kPaneSplitCurrentExtent) &&
                 expected.split_id == value.split_id &&
                 expected.axis == value.axis &&
                 (condition.match == AppliedMatch::kPaneSplitCurrentExtent ||
                  expected.parent_extent == value.parent_extent) &&
                 value.ratio >= expected.ratio_min.value_or(expected.ratio) &&
                 value.ratio <= expected.ratio_max.value_or(expected.ratio) &&
                 expected.tab_handles == value.tab_handles &&
                 !value.start_parent_extent.IsEmpty() &&
                 !value.end_parent_extent.IsEmpty() &&
                 value.start_parent_extent ==
                     parent_target(value.start_container_extent,
                                   expected.start_container_extent,
                                   expected.start_parent_extent) &&
                 value.end_parent_extent ==
                     parent_target(value.end_container_extent,
                                   expected.end_container_extent,
                                   expected.end_parent_extent) &&
                 value.geometry_matches_ratio;
        } else if constexpr (std::is_same_v<T, ViewStateMatches>) {
          switch (condition.match) {
            case AppliedMatch::kViewVisible:
              return expected.visible == value.visible;
            case AppliedMatch::kViewDrawn:
              return expected.drawn == value.drawn;
            case AppliedMatch::kViewBounds:
              return expected.bounds == value.bounds;
            case AppliedMatch::kViewSize:
              return expected.bounds.size() == value.bounds.size();
            case AppliedMatch::kViewHeight:
              return expected.bounds.height() == value.bounds.height();
            case AppliedMatch::kViewParent:
              return expected.parent == value.parent;
            case AppliedMatch::kViewParentDifferent:
              return expected.parent != value.parent;
            case AppliedMatch::kViewChildren:
              return expected.children == value.children;
            case AppliedMatch::kViewNonEmpty:
              return value.bounds.width() >= expected.bounds.width() &&
                     value.bounds.height() >= expected.bounds.height();
            case AppliedMatch::kViewCenterVisible:
              return !value.bounds.IsEmpty() &&
                     value.visible_bounds.Contains(
                         gfx::Rect(value.bounds.size()).CenterPoint());
            case AppliedMatch::kViewPointerFocus:
              return value.has_background &&
                     expected.focus_change_reason == value.focus_change_reason;
            case AppliedMatch::kViewDestroyed:
              return expected.destroyed == value.destroyed;
            case AppliedMatch::kViewOpacity:
              return expected.opacity == value.opacity;
            case AppliedMatch::kViewOpacityPositive:
              return value.opacity > 0;
            case AppliedMatch::kViewHovered:
              return expected.hovered == value.hovered;
            case AppliedMatch::kViewChildCount:
              return expected.children.size() == value.children.size();
            case AppliedMatch::kProjectRefreshed:
              return !value.refresh_pending;
            case AppliedMatch::kFooterMenu:
              return expected.visible == value.visible &&
                     (!expected.visible || value.drawn);
            case AppliedMatch::kOmniboxPopup:
              return expected.popup_open == value.popup_open &&
                     (!expected.popup_open || value.visible);
            case AppliedMatch::kOmniboxEditing:
              return expected.editing == value.editing;
            case AppliedMatch::kViewScrollOffset:
              return expected.scroll_offset == value.scroll_offset;
            case AppliedMatch::kViewScrolledDown:
              return value.scroll_offset.y() > expected.scroll_offset.y();
            case AppliedMatch::kViewGroupDrag:
              return expected.handling_drag == value.handling_drag &&
                     expected.collapsed == value.collapsed &&
                     expected.vertical_drag_only == value.vertical_drag_only;
            case AppliedMatch::kViewScrollIdle:
              return !value.scroll_animation_running;
            case AppliedMatch::kSidebarHover:
              return expected.expanded_on_hover == value.expanded_on_hover;
            case AppliedMatch::kTabHoverIdle:
              return !value.hovered && !value.hover_animation_running &&
                     value.opacity == 0;
            default:
              return false;
          }
        } else if constexpr (std::is_same_v<T, WidgetStateMatches>) {
          switch (condition.match) {
            case AppliedMatch::kWidgetOpacity:
              return expected.opacity == value.opacity;
            case AppliedMatch::kWidgetVisible:
              return expected.visible == value.visible;
            case AppliedMatch::kWidgetBounds:
              return expected.bounds == value.bounds;
            case AppliedMatch::kWidgetClosed:
              return expected.closed == value.closed;
            case AppliedMatch::kWidgetCapture:
              return expected.capture == value.capture;
            case AppliedMatch::kWidgetActive:
              return expected.active == value.active;
            case AppliedMatch::kWidgetReady:
              return value.visible_on_screen && value.active &&
                     value.paint_as_active;
            case AppliedMatch::kWidgetShowState:
              return expected.show_state == value.show_state;
            case AppliedMatch::kWidgetSizeNear:
              return std::abs(expected.bounds.width() - value.bounds.width()) <=
                         1 &&
                     std::abs(expected.bounds.height() -
                              value.bounds.height()) <= 1;
            default:
              return false;
          }
        } else if constexpr (std::is_same_v<T, NativeWindowMatches>) {
          if (condition.match == AppliedMatch::kNativeActive)
            return expected.active == value.active;
          return condition.match == AppliedMatch::kNativeFullscreen &&
                 expected.fullscreen == value.fullscreen &&
                 value.fullscreen_transition_complete;
        } else if constexpr (std::is_same_v<T, PresentationMatches>) {
          if (condition.match == AppliedMatch::kSamplingSettled)
            return value.sampling_settled;
          if (condition.match == AppliedMatch::kPresentationRevision)
            return expected.presentation_id == value.presentation_id;
          if (condition.match == AppliedMatch::kPresentationRevisionChanged)
            return expected.presentation_id != value.presentation_id;
          if (condition.match == AppliedMatch::kPresentationIconReady)
            return value.icon_ready;
          if (condition.match == AppliedMatch::kPresentationIconChanged)
            return value.icon_ready && expected.icon_id != value.icon_id;
          if (condition.match == AppliedMatch::kPresentationIcon)
            return expected.icon_id == value.icon_id;
          if (condition.match == AppliedMatch::kPresentationAlpha) {
            const auto alpha = value.background_color >> 24;
            return expected.background_color ? alpha > 0 && alpha < 255
                                             : alpha == 0;
          }
          if (condition.match == AppliedMatch::kPresentationDark ||
              condition.match == AppliedMatch::kCapsuleTheme)
            return expected.use_native_theme == value.use_native_theme;
          return condition.match == AppliedMatch::kPresentationColor &&
                 expected.source_id == value.source_id &&
                 expected.background_color == value.background_color;
        } else if constexpr (std::is_same_v<T, TabModelMatches>) {
          if (condition.match == AppliedMatch::kTabMembership) {
            return std::ranges::all_of(expected.tabs, [&](const auto& tab) {
              const auto found = std::ranges::find(value.tabs, tab.handle,
                                                   &AppliedTabState::handle);
              return found != value.tabs.end() && found->group == tab.group &&
                     found->pinned == tab.pinned;
            });
          }
          if (condition.match == AppliedMatch::kPinnedTabOrder) {
            std::vector<uint64_t> handles;
            for (const auto& tab : value.tabs) {
              if (tab.pinned)
                handles.push_back(tab.handle);
            }
            std::vector<uint64_t> wanted;
            for (const auto& tab : expected.tabs)
              wanted.push_back(tab.handle);
            return handles == wanted;
          }
          if (condition.match == AppliedMatch::kTabOrder) {
            if (expected.tabs.size() != value.tabs.size())
              return false;
            // 중첩 handle 목록의 그룹 경계를 보존하되 새 그룹의 token은
            // 기존 URL 목록과 같은 의미로 비교한다.
            for (size_t i = 0; i < expected.tabs.size(); ++i) {
              const auto& a = expected.tabs[i];
              const auto& b = value.tabs[i];
              if (a.handle != b.handle ||
                  a.group.is_empty() != b.group.is_empty())
                return false;
              for (size_t j = 0; j < i; ++j) {
                if ((a.group == expected.tabs[j].group) !=
                    (b.group == value.tabs[j].group))
                  return false;
              }
            }
            return true;
          }
          if (condition.match == AppliedMatch::kTabCount)
            return expected.tabs.size() == value.tabs.size();
          if (condition.match == AppliedMatch::kTabActive)
            return expected.active_tab != 0 &&
                   expected.active_tab == value.active_tab;
          if (condition.match == AppliedMatch::kNewTabActive) {
            if (value.tabs.size() != expected.tabs.size() + 1 ||
                value.active_tab == 0)
              return false;
            auto remaining = value.tabs;
            const auto inserted = std::ranges::find(remaining, value.active_tab,
                                                    &AppliedTabState::handle);
            if (inserted == remaining.end() ||
                std::ranges::find(expected.tabs, value.active_tab,
                                  &AppliedTabState::handle) !=
                    expected.tabs.end())
              return false;
            remaining.erase(inserted);
            return remaining == expected.tabs;
          }
          if (expected.project_page != value.project_page)
            return false;
          if (condition.match == AppliedMatch::kProjectTitle)
            return expected.project_title == value.project_title;
          if (condition.match == AppliedMatch::kProjectLive)
            return bool(expected.project_live_tab) ==
                   bool(value.project_live_tab);
          return false;
        } else if constexpr (std::is_same_v<T, WebUiStateMatches>) {
          if (expected.document != value.document)
            return false;
          switch (condition.match) {
            case AppliedMatch::kWebUiRowsAtLeast:
              return value.popup_rows.size() >= expected.popup_rows.size();
            case AppliedMatch::kWebUiRowsFitNativeBounds: {
              const auto& viewport = value.native_view_bounds_in_screen;
              const auto& widget = value.native_widget_bounds_in_screen;
              return value.popup_rows.size() >= expected.popup_rows.size() &&
                     !value.row_bounds.empty() &&
                     value.row_bounds.size() == value.popup_rows.size() &&
                     viewport.width() > 1 && widget.Contains(viewport) &&
                     std::ranges::all_of(
                         value.row_bounds, [&](const auto& row) {
                           return !row.IsEmpty() && row.bottom() > 0 &&
                                  row.bottom() <= viewport.height();
                         });
            }
            case AppliedMatch::kWebUiSelection:
              return expected.selected_index == value.selected_index;
            case AppliedMatch::kWebUiOpacity:
              return expected.opacity == value.opacity;
            case AppliedMatch::kWebUiHover:
              return expected.hovered_index == value.hovered_index;
            case AppliedMatch::kWebUiPadding:
              return expected.padding_top == value.padding_top &&
                     value.popup_rows.size() >= expected.popup_rows.size();
            case AppliedMatch::kWebUiResultsPadding:
              return expected.results_padding == value.results_padding;
            case AppliedMatch::kWebUiMedia:
              return expected.forced_colors == value.forced_colors &&
                     expected.prefers_contrast_more ==
                         value.prefers_contrast_more &&
                     expected.focus_indicator_visible ==
                         value.focus_indicator_visible;
            case AppliedMatch::kWebUiHoverColor:
              return expected.hover_background == value.hover_background &&
                     !value.popup_rows.empty();
            case AppliedMatch::kWebUiCustomizationReady:
              return expected.customization_ready == value.customization_ready;
            case AppliedMatch::kWebUiClickCount:
              return expected.click_count == value.click_count;
            default:
              return false;
          }
        } else if constexpr (std::is_same_v<T, PresentedAfterApply>) {
          return condition.match == AppliedMatch::kPresentedFrame &&
                 expected.compositor == value.compositor &&
                 !value.compositor.is_empty() &&
                 expected.applied_sample_id == value.applied_sample_id &&
                 value.successful;
        } else if constexpr (std::is_same_v<T, DragPhaseMatches>) {
          return (condition.match == AppliedMatch::kDragPhase ||
                  condition.match == AppliedMatch::kDragInputAcknowledged) &&
                 expected.session == value.session &&
                 expected.step == value.step &&
                 expected.source_handle == value.source_handle &&
                 (condition.match == AppliedMatch::kDragInputAcknowledged ||
                  expected.phase == value.phase) &&
                 value.input_acknowledged &&
                 (expected.attached_browser.is_empty() ||
                  expected.attached_browser == value.attached_browser) &&
                 (!expected.last_input_screen ||
                  expected.last_input_screen == value.last_input_screen);
        } else if constexpr (std::is_same_v<T, PreviewApplied>) {
          const bool settled =
              expected.source_handle == value.source_handle &&
              expected.visible == value.visible && value.geometry_applied &&
              (!value.visible ||
               (!value.animation_running && !value.screen_bounds.IsEmpty()));
          if (condition.match == AppliedMatch::kPreviewSettled)
            return settled;
          return condition.match == AppliedMatch::kPreviewAligned && settled &&
                 expected.tab_handle == value.tab_handle &&
                 value.screen_bounds.x() == value.source_screen_x &&
                 value.screen_bounds.width() == value.source_screen_width;
        } else if constexpr (std::is_same_v<T, SurfaceEpochCommitted>) {
          return condition.match == AppliedMatch::kSurfaceDestination &&
                 value.phase == SurfaceEpochCommitted::Phase::kStable &&
                 value.destination == expected.destination &&
                 value.epoch > expected.epoch &&
                 value.committed_generation == value.epoch &&
                 value.committed_generation > expected.committed_generation;
        }
        return false;
      },
      condition.expected);
}

AppliedConditionKind GetAppliedConditionKind(
    const AppliedConditionValue& value) {
  return static_cast<AppliedConditionKind>(value.index());
}

uint64_t AppliedFrameGeometryDigest(
    const std::vector<AppliedSnapshot>& snapshots) {
  std::string geometry;
  for (const auto& snapshot : snapshots) {
    if (std::holds_alternative<AnimationMatches>(snapshot.value))
      continue;
    auto value = snapshot.value;
    if (auto* view = std::get_if<ViewStateMatches>(&value)) {
      view->hovered = false;
      view->expanded_on_hover = false;
      view->hover_animation_running = false;
      view->scroll_animation_running = false;
      view->refresh_pending = false;
      view->popup_open = false;
      view->editing = false;
      view->focus_change_reason = 0;
      view->handling_drag = false;
      view->collapsed = false;
      view->vertical_drag_only = false;
    }
    geometry += snapshot.owner.ToString() + FormatAppliedValue(value) +
                FormatCoordinates(snapshot.coordinates);
  }
  return base::PersistentHash(geometry);
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
              .Add("target", state.target)
              .Add("overlay", state.overlay)
              .Add("applied_bounds", state.applied_bounds)
              .Add("applied_clip", state.applied_clip)
              .Add("applied_target", state.applied_target)
              .Add("applied_overlay", state.applied_overlay)
              .Add("geometry_applied", state.geometry_applied);
        } else if constexpr (std::is_same_v<T, PaneLayoutMatches>) {
          fields.Add("split", state.split_id)
              .Add("axis", state.axis)
              .Add("parent_extent", state.parent_extent)
              .Add("ratio", state.ratio)
              .Add("insets", state.insets)
              .Add("divider", state.divider)
              .Add("tabs", state.tab_handles)
              .Add("start_parent", state.start_parent_extent)
              .Add("end_parent", state.end_parent_extent)
              .Add("start_container", state.start_container_extent)
              .Add("end_container", state.end_container_extent)
              .Add("geometry_matches_ratio", state.geometry_matches_ratio)
              .Add("ratio_min", state.ratio_min.value_or(state.ratio))
              .Add("ratio_max", state.ratio_max.value_or(state.ratio));
        } else if constexpr (std::is_same_v<T, ViewStateMatches>) {
          fields.Add("drawn", state.drawn)
              .Add("visible", state.visible)
              .Add("bounds", state.bounds)
              .Add("parent", state.parent)
              .Add("children", state.children)
              .Add("scroll", state.scroll_offset)
              .Add("destroyed", state.destroyed)
              .Add("opacity", state.opacity)
              .Add("hovered", state.hovered)
              .Add("expanded_on_hover", state.expanded_on_hover)
              .Add("hover_animation_running", state.hover_animation_running)
              .Add("scroll_animation_running", state.scroll_animation_running)
              .Add("refresh_pending", state.refresh_pending)
              .Add("popup_open", state.popup_open)
              .Add("editing", state.editing)
              .Add("screen_bounds", state.screen_bounds)
              .Add("visible_bounds", state.visible_bounds)
              .Add("painted_bounds", state.painted_bounds)
              .Add("contents_bounds_in_screen", state.contents_bounds_in_screen)
              .Add("has_background", state.has_background)
              .Add("focus_change_reason", state.focus_change_reason)
              .Add("handling_drag", state.handling_drag)
              .Add("collapsed", state.collapsed)
              .Add("vertical_drag_only", state.vertical_drag_only);
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
              .Add("closed", state.closed)
              .Add("active", state.active)
              .Add("opacity", state.opacity)
              .Add("visible_on_screen", state.visible_on_screen)
              .Add("paint_as_active", state.paint_as_active);
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
          fields.Add("active", state.active_tab)
              .Add("project_page", state.project_page)
              .Add("project_title", state.project_title)
              .Add("project_live_tab", state.project_live_tab);
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
              .Add("source_width", state.source_screen_width)
              .Add("geometry_applied", state.geometry_applied);
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
              .Add("focused", state.focused)
              .Add("hovered_index", state.hovered_index)
              .Add("padding_top", state.padding_top)
              .Add("results_padding", state.results_padding)
              .Add("forced_colors", state.forced_colors)
              .Add("contrast_more", state.prefers_contrast_more)
              .Add("focus_indicator", state.focus_indicator_visible)
              .Add("hover_background", state.hover_background)
              .Add("customization_ready", state.customization_ready)
              .Add("click_count", state.click_count)
              .Add("native_view_bounds_in_screen",
                   state.native_view_bounds_in_screen)
              .Add("native_widget_bounds_in_screen",
                   state.native_widget_bounds_in_screen);
          for (const auto& rect : state.row_bounds)
            fields.Add("row_bounds", rect);
        } else if constexpr (std::is_same_v<T, PresentationMatches>) {
          fields.Add("source_id", state.source_id)
              .Add("presentation", state.presentation_id)
              .Add("background", state.background_color)
              .Add("foreground", state.foreground_color)
              .Add("icon", state.icon_id)
              .Add("native_theme", state.use_native_theme)
              .Add("popup_revision", state.popup_revision)
              .Add("icon_ready", state.icon_ready)
              .Add("sampling_settled", state.sampling_settled);
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
        << " match=" << MatchName(condition.match)
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
        if (!s->adapter_diagnostics.empty())
          out << " adapter={" << s->adapter_diagnostics << '}';
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
