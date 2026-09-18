#include "chrome/browser/ui/views/yee/agent_task_permissions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace yee {
using Rule = AgentTaskPermissions::Rule;
TEST(AgentTaskPermissionsTest, DefaultsAskAndRejectsInvalidRules) {
  AgentTaskPermissions permissions;
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://example.test", "fill"));
  EXPECT_FALSE(permissions.Grant("https://example.test", base::DictValue().Set("fill", "always")));
  EXPECT_FALSE(permissions.Grant("https://example.test", base::DictValue().Set("read", "allow")));
  EXPECT_FALSE(permissions.Grant("null", base::DictValue().Set("fill", "allow")));
}
TEST(AgentTaskPermissionsTest, ExactOriginAndIndependentRules) {
  AgentTaskPermissions permissions;
  ASSERT_TRUE(permissions.Grant("https://example.test", base::DictValue().Set("fill", "allow").Set("click", "deny")));
  EXPECT_EQ(Rule::kAllow, permissions.Get("https://example.test", "fill"));
  EXPECT_EQ(Rule::kDeny, permissions.Get("https://example.test", "click"));
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://example.test", "navigate"));
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://other.test", "fill"));
  EXPECT_EQ(Rule::kAsk, permissions.Get("http://example.test", "fill"));
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://example.test:8443", "fill"));
}
TEST(AgentTaskPermissionsTest, RevocationAndReplacementRemovePreviousAllow) {
  AgentTaskPermissions permissions;
  ASSERT_TRUE(permissions.Grant("https://example.test", base::DictValue().Set("fill", "allow")));
  permissions.Revoke();
  EXPECT_FALSE(permissions.Covers("https://example.test"));
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://example.test", "fill"));
  ASSERT_TRUE(permissions.Grant("https://example.test", base::DictValue().Set("click", "deny")));
  EXPECT_EQ(Rule::kAsk, permissions.Get("https://example.test", "fill"));
}
}  // namespace yee
