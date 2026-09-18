#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_REQUEST_LEDGER_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_REQUEST_LEDGER_H_

#include <string>
#include <string_view>
#include "base/files/file_path.h"

namespace yee {
// Runs on the mailbox I/O sequence, inside the validated private directory.
// Claims survive browser process restarts. Missing/corrupt results never grant
// permission to execute again. This is not a power-loss durability guarantee.
struct AgentRequestClaim {
  bool fresh = false;
  std::string response;
};
AgentRequestClaim ClaimAgentRequest(const base::FilePath& directory,
                                   std::string_view id);
bool StoreAgentResponse(const base::FilePath& directory,
                        std::string_view id,
                        const std::string& response);
// A stop belongs to one opt-in mailbox, not to the browser's tab model.
// Neither reconnect, detach, nor a new model request may clear it.
std::string ReadAgentSessionStop(const base::FilePath& directory);
bool StoreAgentSessionStop(const base::FilePath& directory,
                          std::string_view id,
                          std::string_view reason);
}  // namespace yee
#endif
