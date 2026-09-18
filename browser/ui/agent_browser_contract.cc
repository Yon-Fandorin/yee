// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/yee/agent_browser_contract.h"

#include <algorithm>
#include <map>
#include <string_view>

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "url/gurl.h"

namespace yee {

AgentPageLocation GetAgentPageLocation(const GURL& url) {
  AgentPageLocation location;
  if (!url.is_valid() ||
      !(url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile())) {
    return location;
  }
  location.credentials_redacted = url.has_username() || url.has_password();
  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.ClearPassword();
  const auto safe_url = url.ReplaceComponents(replacements).spec();
  location.url = base::TruncateUTF8ToByteSize(safe_url, 2048);
  location.truncated = location.url.size() < safe_url.size();
  return location;
}

namespace {

constexpr size_t kMaxFieldCharacters = 160;
constexpr size_t kMaxFieldValueBytes = 4096;
constexpr size_t kMaxTextBytes = 4096;

std::string EscapeReference(std::string_view value) {
  constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string result;
  result.reserve(value.size());
  for (unsigned char character : value) {
    // References are opaque identifiers, but are embedded in a line-oriented
    // protocol. Encode everything outside the URI unreserved set so a long
    // reference cannot be confused with a delimiter or another reference.
    const bool unreserved = (character >= 'a' && character <= 'z') ||
                            (character >= 'A' && character <= 'Z') ||
                            (character >= '0' && character <= '9') ||
                            character == '-' || character == '.' ||
                            character == '_' || character == '~';
    if (unreserved) {
      result.push_back(static_cast<char>(character));
    } else {
      result.push_back('%');
      result.push_back(kHex[character >> 4]);
      result.push_back(kHex[character & 0x0F]);
    }
  }
  return result;
}

std::string EscapeAndTruncate(std::string_view value,
                              size_t limit = kMaxFieldCharacters,
                              bool preserve_whitespace = false) {
  std::string result;
  result.reserve(std::min(value.size(), limit));
  const auto prefix = base::TruncateUTF8ToByteSize(value, limit);
  for (char character : prefix) {
    switch (character) {
      case '\\':
        result.append("\\\\");
        break;
      case '"':
        result.append("\\\"");
        break;
      case '\n':
        result.append(preserve_whitespace ? "\\n" : " ");
        break;
      case '\r':
        result.append(preserve_whitespace ? "\\r" : " ");
        break;
      case '\t':
        result.append(preserve_whitespace ? "\\t" : " ");
        break;
      default:
        if (preserve_whitespace && static_cast<unsigned char>(character) < 32)
          result.append(base::StringPrintf("\\u%04x", static_cast<unsigned char>(character)));
        else
          result.push_back(character);
        break;
    }
  }
  if (prefix.size() < value.size()) {
    result.append("…");
  }
  return result;
}

const char* RoleName(AgentSemanticRole role) {
  switch (role) {
    case AgentSemanticRole::kButton:
      return "button";
    case AgentSemanticRole::kLink:
      return "link";
    case AgentSemanticRole::kTextField:
      return "field";
    case AgentSemanticRole::kCheckBox:
      return "checkbox";
    case AgentSemanticRole::kComboBox:
      return "combobox";
    case AgentSemanticRole::kHeading:
      return "heading";
    case AgentSemanticRole::kDialog:
      return "dialog";
    case AgentSemanticRole::kAlert:
      return "alert";
    case AgentSemanticRole::kText:
      return "text";
  }
}

bool IsRelevant(const AgentSemanticNode& node) {
  if (!node.visible) {
    return false;
  }
  // Static text may be the answer (price, confirmation, article, validation
  // error). The producer may prune decoration, but the serializer cannot infer
  // that all non-interactive content is disposable.
  return node.role != AgentSemanticRole::kText || !node.name.empty();
}

std::string RenderNode(char operation, const AgentSemanticNode& node) {
  // Reading prose should not require a second model round trip merely because
  // the accessible label differs from the visible body. Keep that distinction,
  // but do not repeat the body when it is already the accessible name.
  const bool prose = node.role == AgentSemanticRole::kText && !node.secret;
  const bool secret_prose = node.role == AgentSemanticRole::kText && node.secret;
  const size_t name_limit = prose ? kMaxTextBytes : kMaxFieldCharacters;
  std::string line = base::StringPrintf(
      "%c@%s %s \"%s\"", operation, EscapeReference(node.ref),
      RoleName(node.role),
      secret_prose ? "<redacted>" : EscapeAndTruncate(node.name, name_limit));
  if (prose && !node.value.empty() && node.value != node.name) {
    line.append(base::StringPrintf(" text=\"%s\"",
                                  EscapeAndTruncate(node.value, kMaxTextBytes)));
  }
  if (prose && (node.name.size() > kMaxTextBytes ||
                node.value.size() > kMaxTextBytes)) {
    line.append(" text_truncated");
  }
  if (node.role == AgentSemanticRole::kTextField ||
      node.role == AgentSemanticRole::kComboBox) {
    line.append(node.secret
                    ? " value=<redacted>"
                    : base::StringPrintf(" value=\"%s\"",
                                         EscapeAndTruncate(node.value, kMaxFieldValueBytes, true)));
    if (!node.secret && node.value.size() > kMaxFieldValueBytes)
      line.append(" value_truncated");
  }
  if (node.role == AgentSemanticRole::kLink && !node.href.empty()) {
    line.append(node.secret
                    ? " href=<redacted>"
                    : base::StringPrintf(" href=\"%s\"",
                                         EscapeAndTruncate(node.href, kMaxFieldValueBytes, true)));
    if (!node.secret && node.href.size() > kMaxFieldValueBytes)
      line.append(" href_truncated");
  }
  if (!node.enabled) {
    line.append(" disabled");
  }
  if (node.checked) {
    line.append(" checked");
  }
  if (node.focused) {
    line.append(" focused");
  }
  return line;
}

bool NodesHaveSameObservableSemantics(const AgentSemanticNode& lhs,
                                      const AgentSemanticNode& rhs,
                                      bool compare_references = true) {
  if ((compare_references && lhs.ref != rhs.ref) || lhs.role != rhs.role || lhs.name != rhs.name ||
      lhs.enabled != rhs.enabled || lhs.checked != rhs.checked ||
      lhs.focused != rhs.focused || lhs.visible != rhs.visible ||
      lhs.secret != rhs.secret) {
    return false;
  }
  // Secret values are intentionally redacted, so changing only one cannot be
  // observable. Every non-secret value remains part of the semantic delta,
  // even when its display rendering is truncated.
  return lhs.secret || (lhs.value == rhs.value &&
                        (lhs.role != AgentSemanticRole::kLink || lhs.href == rhs.href));
}

bool AppendWithinBudget(std::string& output,
                        std::string_view line,
                        size_t max_characters) {
  if (output.size() + line.size() + 1 > max_characters) {
    return false;
  }
  output.append(line);
  output.push_back('\n');
  return true;
}

}  // namespace

bool AgentBrowserEffectRequiresApproval(AgentBrowserEffect effect) {
  switch (effect) {
    case AgentBrowserEffect::kRead:
    case AgentBrowserEffect::kNavigate:
    case AgentBrowserEffect::kFillNonSensitiveField:
      return false;
    case AgentBrowserEffect::kDownload:
    case AgentBrowserEffect::kUpload:
    case AgentBrowserEffect::kSendMessage:
    case AgentBrowserEffect::kPublish:
    case AgentBrowserEffect::kPurchase:
    case AgentBrowserEffect::kUseCredential:
      return true;
  }
}

bool AgentSnapshotsHaveSameObservableSemantics(
    const AgentSemanticSnapshot& lhs,
    const AgentSemanticSnapshot& rhs,
    bool compare_references) {
  if (lhs.document_ref != rhs.document_ref || lhs.title != rhs.title ||
      lhs.origin != rhs.origin)
    return false;
  auto left = lhs.nodes.begin();
  auto right = rhs.nodes.begin();
  for (;;) {
    while (left != lhs.nodes.end() && !IsRelevant(*left))
      ++left;
    while (right != rhs.nodes.end() && !IsRelevant(*right))
      ++right;
    if (left == lhs.nodes.end() || right == rhs.nodes.end())
      return left == lhs.nodes.end() && right == rhs.nodes.end();
    if (!NodesHaveSameObservableSemantics(*left++, *right++, compare_references))
      return false;
  }
}

std::string SerializeAgentSemanticSnapshot(
    const AgentSemanticSnapshot& snapshot,
    const AgentSemanticSnapshot* previous,
    size_t max_characters) {
  if (max_characters == 0) {
    return {};
  }

  // A navigation or frame replacement invalidates every action reference.
  // Never label that complete replacement as a delta merely because a caller
  // happened to retain an observation from another document.
  const bool is_delta = previous &&
                        previous->document_ref == snapshot.document_ref &&
                        previous->revision <= snapshot.revision;
  constexpr std::string_view kTruncated =
      "!truncated request_full_or_narrower_scope\n";
  const size_t content_budget = max_characters > kTruncated.size()
                                    ? max_characters - kTruncated.size()
                                    : 0;
  bool truncated = false;
  std::string output = base::StringPrintf(
      "page @%s rev=%zu title=\"%s\" origin=\"%s\"%s\n",
      EscapeReference(snapshot.document_ref), snapshot.revision,
      EscapeAndTruncate(snapshot.title), EscapeAndTruncate(snapshot.origin),
      is_delta ? " delta" : "");
  if (is_delta) {
    output += base::StringPrintf("base_rev=%zu\n", previous->revision);
  }
  if (output.size() > content_budget) {
    return max_characters >= kTruncated.size()
               ? std::string(kTruncated)
               : (max_characters >= 2 ? "!\n" : "!");
  }

  std::map<std::string, AgentSemanticNode> prior_nodes;
  if (is_delta) {
    for (const AgentSemanticNode& node : previous->nodes) {
      if (IsRelevant(node)) {
        prior_nodes.emplace(node.ref, node);
      }
    }
  }

  for (const AgentSemanticNode& node : snapshot.nodes) {
    if (!IsRelevant(node)) {
      continue;
    }
    const auto prior = prior_nodes.find(node.ref);
    if (!previous || prior == prior_nodes.end()) {
      truncated |=
          !AppendWithinBudget(output, RenderNode('+', node), content_budget);
    } else if (!NodesHaveSameObservableSemantics(prior->second, node)) {
      truncated |=
          !AppendWithinBudget(output, RenderNode('~', node), content_budget);
    }
    if (prior != prior_nodes.end()) {
      prior_nodes.erase(prior);
    }
  }
  for (const auto& [ref, node] : prior_nodes) {
    truncated |= !AppendWithinBudget(output, "-@" + EscapeReference(ref),
                                     content_budget);
  }
  if (truncated) {
    output.append(kTruncated);
  }
  return output;
}

}  // namespace yee
