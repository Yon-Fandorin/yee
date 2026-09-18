// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_INPUT_REQUEST_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_INPUT_REQUEST_H_

#include <cstdint>
#include <string>

namespace yee {

enum class AgentInputRequestKind {
  kInput,
  kApproval,
};

enum class AgentInputRequestStatus {
  kCreated,
  kPending,
  kResolved,
  kCancelled,
  kExpired,
};

struct AgentInputRequestIdentity {
  std::string task_id;
  std::string document_ref;
  std::string request_id;

  bool operator==(const AgentInputRequestIdentity&) const = default;
};

// A single, browser-owned request awaiting a response. Resolve must only be
// called from a trusted user interaction channel; an agent is never given an
// approval API that could approve its own request. Times are monotonic
// milliseconds from the same clock. This object tracks response eligibility;
// the caller owns answer/approve/deny payloads and actual action enforcement.
class AgentInputRequest {
 public:
  AgentInputRequest(AgentInputRequestIdentity identity,
                    AgentInputRequestKind kind,
                    int64_t expires_at);
  ~AgentInputRequest();

  AgentInputRequest(const AgentInputRequest&) = delete;
  AgentInputRequest& operator=(const AgentInputRequest&) = delete;

  bool Start();
  bool Resolve(const AgentInputRequestIdentity& identity, int64_t now);
  bool Cancel();
  bool ExpireIfNeeded(int64_t now);
  bool InvalidateForDocumentChange(const std::string& document_ref);

  const AgentInputRequestIdentity& identity() const { return identity_; }
  AgentInputRequestKind kind() const { return kind_; }
  AgentInputRequestStatus status() const { return status_; }
  int64_t expires_at() const { return expires_at_; }

 private:
  AgentInputRequestIdentity identity_;
  AgentInputRequestKind kind_;
  int64_t expires_at_;
  AgentInputRequestStatus status_ = AgentInputRequestStatus::kCreated;
  bool started_ = false;
};

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_AGENT_INPUT_REQUEST_H_
