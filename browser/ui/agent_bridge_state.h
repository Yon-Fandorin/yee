#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_STATE_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BRIDGE_STATE_H_
#include "base/callback_list.h"
#include "chrome/browser/ui/views/yee/agent_tab_activity.h"
namespace content {
class WebContents;
}
namespace yee {
AgentTabActivity GetBridgeActivity(content::WebContents* contents);
void SetBridgeActivity(content::WebContents* contents,
                       AgentTabActivity activity);
base::CallbackListSubscription SubscribeBridgeActivity(
    base::RepeatingClosure callback);
void NotifyBridgeActivityChanged();
}  // namespace yee
#endif
