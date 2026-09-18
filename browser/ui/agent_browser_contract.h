// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BROWSER_CONTRACT_H_
#define CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BROWSER_CONTRACT_H_

#include <cstddef>
#include <string>
#include <vector>

class GURL;

namespace yee {

// Current browser location, separate from page-provided text and link targets.
// A clipped or credential-redacted URL is not an exact navigation/return target.
struct AgentPageLocation {
  std::string url;
  bool truncated = false;
  bool credentials_redacted = false;
};

AgentPageLocation GetAgentPageLocation(const GURL& url);

// This is the intentionally small browser-facing contract for an agent.  It
// is not a DOM dump and it must never carry credentials.  A Chromium adapter
// will map the accessibility tree to these roles; keeping the contract here
// lets the agent runtime remain independent of TabStripModel and WebContents.
enum class AgentSemanticRole {
  kButton,
  kLink,
  kTextField,
  kCheckBox,
  kComboBox,
  kHeading,
  kDialog,
  kAlert,
  kText,
};

struct AgentSemanticNode {
  // Opaque, stable only for the current document revision. It is an action
  // target, not a CSS selector or backend implementation detail.
  std::string ref;
  AgentSemanticRole role = AgentSemanticRole::kText;
  std::string name;
  std::string value;
  bool enabled = true;
  bool checked = false;
  bool focused = false;
  bool visible = true;
  // Passwords, payment values, and any host-classified secret use this flag.
  // Their value is omitted even if a buggy producer supplies one.
  bool secret = false;

  // Observed link destination, never an authorization to navigate. Keep it
  // separate from labels and field values; secret nodes must not expose it.
  std::string href;

  bool operator==(const AgentSemanticNode&) const = default;
};

struct AgentSemanticSnapshot {
  std::string document_ref;
  size_t revision = 0;
  std::string title;
  std::string origin;
  std::vector<AgentSemanticNode> nodes;
};

// A small set of effects with explicit user-approval boundaries. Credentials
// are filled by browser-owned infrastructure and are never an agent argument.
enum class AgentBrowserEffect {
  kRead,
  kNavigate,
  kFillNonSensitiveField,
  kDownload,
  kUpload,
  kSendMessage,
  kPublish,
  kPurchase,
  kUseCredential,
};

bool AgentBrowserEffectRequiresApproval(AgentBrowserEffect effect);

// Ignores revision and redacted secret values; compares all observable nodes
// before output truncation, including identity and order. Explicit content-only
// waiting may ignore node refs; document identity and all content remain checked.
bool AgentSnapshotsHaveSameObservableSemantics(
    const AgentSemanticSnapshot& lhs,
    const AgentSemanticSnapshot& rhs,
    bool compare_references = true);

// Produces a compact, line-oriented snapshot. `previous` makes this a delta:
// unchanged nodes disappear, removals are explicit, and references remain
// stable within a document. `max_characters` is a UTF-8 byte limit, not a token
// count. Truncated responses end in !truncated (or ! for tiny budgets) and MUST
// NOT be acknowledged as a complete baseline for future deltas. The caller
// must request a full/narrower observation. This serializer does not validate
// action references or enforce effect approval.
std::string SerializeAgentSemanticSnapshot(
    const AgentSemanticSnapshot& snapshot,
    const AgentSemanticSnapshot* previous,
    size_t max_characters = 8000);

}  // namespace yee

#endif  // CHROME_BROWSER_UI_VIEWS_YEE_AGENT_BROWSER_CONTRACT_H_
