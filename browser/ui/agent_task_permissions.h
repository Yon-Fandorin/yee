#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_TASK_PERMISSIONS_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_TASK_PERMISSIONS_H_

#include <string>
#include <string_view>
#include "base/values.h"

namespace yee {
// Browser-action rules granted by the native attach dialog, never by page data.
// The owner binds this object to one WebContents and revokes it on takeover.
class AgentTaskPermissions {
 public:
  enum class Rule { kAsk, kAllow, kDeny };
  static bool Valid(const base::DictValue& rules) {
    for (const auto [key, value] : rules) {
      if ((key != "fill" && key != "click" && key != "navigate") ||
          !value.is_string() || (value.GetString() != "ask" &&
          value.GetString() != "allow" && value.GetString() != "deny"))
        return false;
    }
    return true;
  }
  bool Grant(std::string origin, const base::DictValue& rules) {
    Revoke();
    if (origin.empty() || origin == "null" || !Valid(rules))
      return false;
    origin_ = std::move(origin);
    rules_ = rules.Clone();
    return true;
  }
  bool Covers(std::string_view origin) const {
    return !origin_.empty() && origin_ == origin;
  }
  void Revoke() { origin_.clear(); rules_.clear(); }
  Rule Get(std::string_view origin, std::string_view command) const {
    if (!Covers(origin)) return Rule::kAsk;
    const auto* value = rules_.FindString(command);
    if (value && *value == "deny") return Rule::kDeny;
    if (value && *value == "allow") return Rule::kAllow;
    return Rule::kAsk;
  }
  const base::DictValue& rules() const { return rules_; }
 private:
  std::string origin_;
  base::DictValue rules_;
};
}  // namespace yee
#endif
