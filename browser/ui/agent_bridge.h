#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_H_
#include <memory>
#include <string>
#include "base/functional/callback.h"
#include "chrome/browser/ui/views/yee/agent_tab_activity.h"
namespace content {
class WebContents;
}
namespace views {
class View;
}
namespace yee {
std::u16string AgentActivityAccessibleDescription(AgentTabActivity activity);
class AgentBridge {
 public:
  virtual ~AgentBridge() = default;
  virtual base::RepeatingClosure PendingWaitCancellation();
};
// Opt-in developer prototype. First eligible window owns the mailbox.
std::unique_ptr<AgentBridge> CreateAgentBridge(
    views::View* owner,
    base::RepeatingCallback<content::WebContents*()> active_contents,
    base::RepeatingCallback<bool(content::WebContents*)> select_contents = {});
void InitializeAgentBridge(
    views::View* owner,
    base::RepeatingCallback<content::WebContents*()> active_contents,
    base::RepeatingCallback<bool(content::WebContents*)> select_contents = {});
void DestroyAgentBridge(views::View* owner);
// Returns an identity-bound cancellation for a pending wait in this window.
base::RepeatingClosure GetPendingAgentWaitCancellation(views::View* view);
}  // namespace yee
#endif
