#include "chrome/browser/ui/views/yee/agent_bridge_state.h"
#include <map>
#include "base/no_destructor.h"
namespace yee {
namespace {
auto& States() {
  static base::NoDestructor<std::map<content::WebContents*, AgentTabActivity>>
      states;
  return *states;
}
auto& Callbacks() {
  static base::NoDestructor<base::RepeatingClosureList> callbacks;
  return *callbacks;
}
}  // namespace
AgentTabActivity GetBridgeActivity(content::WebContents* contents) {
  auto it = States().find(contents);
  return it == States().end() ? AgentTabActivity::kNone : it->second;
}
void SetBridgeActivity(content::WebContents* contents,
                       AgentTabActivity activity) {
  if (!contents || GetBridgeActivity(contents) == activity)
    return;
  if (activity == AgentTabActivity::kNone)
    States().erase(contents);
  else
    States()[contents] = activity;
  Callbacks().Notify();
}
base::CallbackListSubscription SubscribeBridgeActivity(
    base::RepeatingClosure callback) {
  return Callbacks().Add(std::move(callback));
}
void NotifyBridgeActivityChanged() {
  Callbacks().Notify();
}
}  // namespace yee
