// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_input_request.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
namespace {

AgentInputRequestIdentity Identity() {
  return {.task_id = "task-1", .document_ref = "doc-1", .request_id = "req-1"};
}

TEST(AgentInputRequestTest, StartsOnlyOnceAndResolvesOnce) {
  AgentInputRequest request(Identity(), AgentInputRequestKind::kInput, 100);

  EXPECT_EQ(AgentInputRequestStatus::kCreated, request.status());
  EXPECT_FALSE(request.Resolve(Identity(), 1));
  EXPECT_TRUE(request.Start());
  EXPECT_FALSE(request.Start());
  EXPECT_EQ(AgentInputRequestStatus::kPending, request.status());
  EXPECT_TRUE(request.Resolve(Identity(), 99));
  EXPECT_EQ(AgentInputRequestStatus::kResolved, request.status());
  EXPECT_FALSE(request.Resolve(Identity(), 99));
}

TEST(AgentInputRequestTest, EmptyIdentityCannotStart) {
  AgentInputRequest request({}, AgentInputRequestKind::kInput, 100);
  EXPECT_FALSE(request.Start());
  EXPECT_FALSE(request.Resolve({}, 1));
}

TEST(AgentInputRequestTest, RejectsStaleIdentity) {
  AgentInputRequest request(Identity(), AgentInputRequestKind::kApproval, 100);
  ASSERT_TRUE(request.Start());

  AgentInputRequestIdentity stale = Identity();
  stale.request_id = "req-old";
  EXPECT_FALSE(request.Resolve(stale, 1));
  EXPECT_EQ(AgentInputRequestStatus::kPending, request.status());

  stale = Identity();
  stale.document_ref = "doc-new";
  EXPECT_FALSE(request.Resolve(stale, 1));
  EXPECT_EQ(AgentInputRequestStatus::kPending, request.status());
}

TEST(AgentInputRequestTest, ExpiresAtBoundaryAndRejectsLateReply) {
  AgentInputRequest request(Identity(), AgentInputRequestKind::kInput, 100);
  ASSERT_TRUE(request.Start());

  EXPECT_FALSE(request.Resolve(Identity(), 100));
  EXPECT_EQ(AgentInputRequestStatus::kExpired, request.status());
  EXPECT_FALSE(request.ExpireIfNeeded(100));
}

TEST(AgentInputRequestTest, ExplicitExpiryCanBeAppliedBeforeReply) {
  AgentInputRequest request(Identity(), AgentInputRequestKind::kInput, 100);
  ASSERT_TRUE(request.Start());

  EXPECT_TRUE(request.ExpireIfNeeded(101));
  EXPECT_FALSE(request.Resolve(Identity(), 1));
}

TEST(AgentInputRequestTest, CancelAndDocumentChangeInvalidatePendingRequest) {
  AgentInputRequest request(Identity(), AgentInputRequestKind::kInput, 100);
  ASSERT_TRUE(request.Start());
  EXPECT_TRUE(request.InvalidateForDocumentChange("doc-2"));
  EXPECT_EQ(AgentInputRequestStatus::kCancelled, request.status());
  EXPECT_FALSE(request.Resolve(Identity(), 1));
  EXPECT_FALSE(request.Cancel());

  AgentInputRequest cancelled(Identity(), AgentInputRequestKind::kInput, 100);
  ASSERT_TRUE(cancelled.Start());
  EXPECT_TRUE(cancelled.Cancel());
  EXPECT_FALSE(cancelled.InvalidateForDocumentChange("doc-2"));
}

}  // namespace
}  // namespace yee
