// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_input_request.h"

#include <utility>

namespace yee {

AgentInputRequest::AgentInputRequest(AgentInputRequestIdentity identity,
                                     AgentInputRequestKind kind,
                                     int64_t expires_at)
    : identity_(std::move(identity)), kind_(kind), expires_at_(expires_at) {}

AgentInputRequest::~AgentInputRequest() = default;

bool AgentInputRequest::Start() {
  if (started_ || identity_.task_id.empty() || identity_.document_ref.empty() ||
      identity_.request_id.empty()) {
    return false;
  }
  started_ = true;
  status_ = AgentInputRequestStatus::kPending;
  return true;
}

bool AgentInputRequest::Resolve(const AgentInputRequestIdentity& identity,
                                int64_t now) {
  if (!started_ || status_ != AgentInputRequestStatus::kPending ||
      !(identity == identity_)) {
    return false;
  }
  if (now >= expires_at_) {
    status_ = AgentInputRequestStatus::kExpired;
    return false;
  }
  status_ = AgentInputRequestStatus::kResolved;
  return true;
}

bool AgentInputRequest::Cancel() {
  if (!started_ || status_ != AgentInputRequestStatus::kPending) {
    return false;
  }
  status_ = AgentInputRequestStatus::kCancelled;
  return true;
}

bool AgentInputRequest::ExpireIfNeeded(int64_t now) {
  if (!started_ || status_ != AgentInputRequestStatus::kPending ||
      now < expires_at_) {
    return false;
  }
  status_ = AgentInputRequestStatus::kExpired;
  return true;
}

bool AgentInputRequest::InvalidateForDocumentChange(
    const std::string& document_ref) {
  if (!started_ || status_ != AgentInputRequestStatus::kPending ||
      document_ref == identity_.document_ref) {
    return false;
  }
  status_ = AgentInputRequestStatus::kCancelled;
  return true;
}

}  // namespace yee
