#include "chrome/browser/ui/views/yee/agent_bridge.h"
#include "chrome/browser/ui/views/yee/agent_task_permissions.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include "build/build_config.h"
#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/file_path_watcher.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/sequence_bound.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "chrome/browser/ui/views/yee/agent_bridge_prompt.h"
#include "chrome/browser/ui/views/yee/brand.h"
#include "chrome/browser/ui/views/yee/agent_bridge_script.h"
#include "chrome/browser/ui/views/yee/agent_bridge_state.h"
#include "chrome/browser/ui/views/yee/agent_browser_contract.h"
#include "chrome/browser/ui/views/yee/agent_request_timing.h"
#include "chrome/browser/ui/views/yee/agent_request_ledger.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/class_property.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/widget/root_view.h"
#include "url/gurl.h"
#include "url/origin.h"

DEFINE_UI_CLASS_PROPERTY_TYPE(yee::AgentBridge*)

namespace yee {
std::u16string AgentActivityAccessibleDescription(AgentTabActivity activity) {
  if (activity == AgentTabActivity::kNeedsInput) {
    return branding::ProductName() +
           u" Agent needs your input. Respond in the browser dialog.";
  }
  if (activity == AgentTabActivity::kWorking) {
    return branding::ProductName() + u" Agent is working on this tab.";
  }
  return {};
}

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(AgentBridge, kAgentBridgeKey)
#if BUILDFLAG(IS_POSIX)
namespace {
bool g_claimed = false;

bool PrivateDirectory(const base::FilePath& path) {
  int permissions = 0;
  struct stat info;
  return path.IsAbsolute() && !base::IsLink(path) &&
         lstat(path.value().c_str(), &info) == 0 && info.st_uid == geteuid() &&
         base::DirectoryExists(path) &&
         base::GetPosixFilePermissions(path, &permissions) &&
         (permissions & 0077) == 0;
}

struct ReadRequestResult {
  std::string data;
  AgentRequestClaim claim;
  std::string stop_reason;
};

ReadRequestResult ReadRequest(base::FilePath directory) {
  auto path = directory;
  if (!PrivateDirectory(path))
    return {};
  path = path.AppendASCII("request.json");
  if (base::IsLink(path))
    return {{}, {}, ReadAgentSessionStop(directory)};
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(path, &data, 65536))
    return {{}, {}, ReadAgentSessionStop(directory)};
  auto value = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  AgentRequestClaim claim;
  if (value && value->is_dict()) {
    const auto* id = value->GetDict().FindString("id");
    if (id)
      claim = ClaimAgentRequest(directory, *id);
  }
  base::DeleteFile(path);
  return {std::move(data), std::move(claim), ReadAgentSessionStop(directory)};
}

void WriteResponse(base::FilePath directory, std::string data) {
  if (!PrivateDirectory(directory))
    return;
  const auto temporary = directory.AppendASCII("native-response.tmp");
  if (base::IsLink(temporary))
    return;
  if (base::WriteFile(temporary, data)) {
    base::SetPosixFilePermissions(temporary, 0600);
    base::ReplaceFile(temporary, directory.AppendASCII("response.json"),
                      nullptr);
  }
}

bool WriteDurableResponse(base::FilePath directory, std::string id,
                          std::string json, bool claimed) {
  const bool stored = claimed && StoreAgentResponse(directory, id, json);
  if (!stored) {
    auto value = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (value && value->is_dict()) {
      auto& response = value->GetDict();
      response.Set("receipt_persisted", false);
      if (!response.contains("error"))
        response.Set("error", "request_receipt_unavailable");
      response.Set("ok", false);
      base::JSONWriter::Write(response, &json);
    }
  }
  WriteResponse(directory, std::move(json));
  return stored;
}

std::string ReadWaitCancellation(base::FilePath directory, std::string id) {
  if (!PrivateDirectory(directory))
    return {};
  const auto path = directory.AppendASCII("cancel-wait-" + base::HexEncode(id) + ".json");
  if (base::IsLink(path))
    return {};
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(path, &data, 4096))
    return {};
  base::DeleteFile(path);
  return data;
}

bool Eligible(content::WebContents* contents) {
  if (!contents || !contents->GetPrimaryMainFrame() ||
      !contents->GetPrimaryMainFrame()->IsRenderFrameLive())
    return false;
  const GURL& url = contents->GetLastCommittedURL();
  return url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile();
}

std::string String(const base::DictValue& value, const char* key) {
  const auto* text = value.FindString(key);
  return text ? *text : std::string();
}

AgentSemanticRole Role(std::string_view role) {
  if (role == "button")
    return AgentSemanticRole::kButton;
  if (role == "link")
    return AgentSemanticRole::kLink;
  if (role == "field")
    return AgentSemanticRole::kTextField;
  if (role == "checkbox")
    return AgentSemanticRole::kCheckBox;
  if (role == "combobox")
    return AgentSemanticRole::kComboBox;
  if (role == "heading")
    return AgentSemanticRole::kHeading;
  if (role == "dialog")
    return AgentSemanticRole::kDialog;
  if (role == "alert")
    return AgentSemanticRole::kAlert;
  return AgentSemanticRole::kText;
}

// Consent registry only: Chromium owns tab lifetime and selection.
class GrantedAgentTab : public content::WebContentsObserver {
 public:
  explicit GrantedAgentTab(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        id(base::Uuid::GenerateRandomV4().AsLowercaseString()) {}
  void Grant() {
    authorized = true;
    const auto full_title = base::UTF16ToUTF8(web_contents()->GetTitle());
    const auto full_url = web_contents()->GetLastCommittedURL().spec();
    metadata_truncated = full_title.size() > 512 || full_url.size() > 2048;
    base::TruncateUTF8ToByteSize(full_title, 512, &title);
    base::TruncateUTF8ToByteSize(full_url, 2048, &url);
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->HasCommitted() && handle->IsInPrimaryMainFrame()) {
      if (!permissions.Covers(url::Origin::Create(handle->GetURL()).Serialize())) {
        authorized = false;
        permissions.Revoke();
      }
    }
  }
  void WebContentsDestroyed() override {
    authorized = false;
    Observe(nullptr);
  }
  AgentTaskPermissions permissions;
  const std::string id;
  bool authorized = false;
  bool metadata_truncated = false;
  std::string title, url;
};

class AgentBridgeImpl : public AgentBridge,
                        public content::WebContentsObserver,
                        public views::WidgetObserver {
 public:
  AgentBridgeImpl(views::View* owner,
                  base::RepeatingCallback<content::WebContents*()> active,
                  base::RepeatingCallback<bool(content::WebContents*)> select,
                  base::FilePath directory)
      : owner_(owner),
        active_(std::move(active)),
        select_(std::move(select)),
        directory_(directory),
        io_(base::ThreadPool::CreateSequencedTaskRunner(
            {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
        request_watcher_(io_) {
    poll_.Start(FROM_HERE, base::Milliseconds(150), this,
                &AgentBridgeImpl::Poll);
    // Watch the atomic mailbox path, including its creation/replacement. File
    // I/O and watcher destruction stay on the I/O sequence. Keep the timer as
    // a bounded recovery path if registration or later notifications fail.
    request_watcher_.AsyncCall(&base::FilePathWatcher::Watch)
        .WithArgs(directory_.AppendASCII("request.json"),
                  base::FilePathWatcher::Type::kNonRecursive,
                  base::BindPostTaskToCurrentDefault(base::BindRepeating(
                      &AgentBridgeImpl::RequestChanged, weak_.GetWeakPtr())))
        .Then(base::BindOnce(&AgentBridgeImpl::WatchStarted, weak_.GetWeakPtr()));
  }
  ~AgentBridgeImpl() override {
    busy_ = false;
    weak_.InvalidateWeakPtrs();
    ClosePrompt();
    ClosePointer();
    SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
    g_claimed = false;
  }
  base::RepeatingClosure PendingWaitCancellation() override {
    if (!busy_ || !waiting_for_change_ || quarantined_ ||
        active_.Run() != web_contents())
      return {};
    return base::BindRepeating(
        [](base::WeakPtr<AgentBridgeImpl> self, std::string id, std::string document) {
          if (self && self->busy_ && self->waiting_for_change_ &&
              self->id_ == id && self->document_ == document &&
              self->active_.Run() == self->web_contents())
            self->Error("wait_cancelled");
        }, weak_.GetWeakPtr(), id_, document_);
  }

 private:
  GrantedAgentTab* GrantFor(content::WebContents* contents) {
    for (const auto& grant : grants_) {
      if (grant->web_contents() == contents)
        return grant.get();
    }
    return nullptr;
  }
  void WatchStarted(bool watching) {
    if (watching)
      Poll();  // Also handle a request published before registration finished.
  }
  void RequestChanged(const base::FilePath&, bool error) {
    if (!error)
      Poll();
  }
  void Poll() {
    // Separate control mailbox: never overwrite or claim a second action while
    // the original request is active. Capture identity before asynchronous I/O
    // so a late control cannot affect a subsequent request.
    if (busy_ && waiting_for_change_ && !reading_cancel_) {
      reading_cancel_ = true;
      io_->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&ReadWaitCancellation, directory_, id_),
          base::BindOnce(&AgentBridgeImpl::CancelWait, weak_.GetWeakPtr(), id_));
    }
    if (busy_ && stopped_reason_.empty() && !reading_stop_) {
      reading_stop_ = true;
      io_->PostTaskAndReplyWithResult(
          FROM_HERE, base::BindOnce(&ReadAgentSessionStop, directory_),
          base::BindOnce(&AgentBridgeImpl::StopReceived, weak_.GetWeakPtr()));
    }
    if (reading_ || busy_ || writing_response_ || quarantined_)
      return;
    reading_ = true;
    io_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&ReadRequest, directory_),
        base::BindOnce(&AgentBridgeImpl::Received, weak_.GetWeakPtr()));
  }

  void StopSession(std::string reason) {
    if (!stopped_reason_.empty())
      return;
    stopped_reason_ = std::move(reason);
    ClosePointer();
    attached_ = false;
    baseline_.reset();
    for (auto& grant : grants_) {
      grant->authorized = false;
      grant->permissions.Revoke();
    }
    if (stopped_reason_ != "stop_state_unavailable" && !id_.empty())
      io_->PostTask(FROM_HERE, base::BindOnce(
          [](base::FilePath directory, std::string id, std::string reason) {
            StoreAgentSessionStop(directory, id, reason);
          }, directory_, id_, stopped_reason_));
  }

  void StopReceived(std::string reason) {
    reading_stop_ = false;
    if (reason.empty())
      return;
    StopSession(std::move(reason));
    if (busy_)
      Error(stopped_reason_);
  }

  void CancelWait(std::string expected_id, std::string data) {
    reading_cancel_ = false;
    if (!busy_ || !waiting_for_change_ || quarantined_ || id_ != expected_id)
      return;
    auto value = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
    if (!value || !value->is_dict())
      return;
    const auto& control = value->GetDict();
    if (control.size() != 2 || String(control, "id") != id_ ||
        String(control, "document") != document_)
      return;
    // Error retains the original identity and waits for an in-flight renderer
    // callback before reporting settlement; this never detaches other grants.
    Error("wait_cancelled");
  }

  void Received(ReadRequestResult result) {
    reading_ = false;
    if (!result.stop_reason.empty())
      StopSession(std::move(result.stop_reason));
    auto& data = result.data;
    if (data.empty())
      return;
    auto value = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
    if (!value || !value->is_dict())
      return;
    request_ = std::move(*value).TakeDict();
    id_ = String(request_, "id");
    if (id_.empty() || id_.size() > 64)
      return;
    request_claimed_ = result.claim.fresh;
    if (!result.claim.fresh) {
      if (!result.claim.response.empty()) {
        auto cached = base::JSONReader::Read(result.claim.response,
                                            base::JSON_PARSE_RFC);
        if (!cached->GetDict().FindBool("execution_settled").value())
          quarantined_ = true;
        io_->PostTask(FROM_HERE, base::BindOnce(
            &WriteResponse, directory_, std::move(result.claim.response)));
        return;
      }
      // A prior execution may have happened before its result was archived.
      // Neither a duplicate nor an I/O failure may authorize redispatch.
      busy_ = true;
      quarantined_ = true;
      timing_.Start(base::TimeTicks::Now());
      Error("request_outcome_unknown");
      return;
    }
    busy_ = true;
    timing_.Start(base::TimeTicks::Now());
    deferred_error_.reset();
    const auto now = base::TimeTicks::Now();
    const double wall_now =
        base::Time::Now().InMillisecondsFSinceUnixEpochIgnoringNull();
    const auto client_expiry = request_.FindDouble("expires_unix_ms");
    if (!client_expiry || !std::isfinite(*client_expiry)) {
      Error("invalid_request_deadline");
      return;
    }
    const double remaining_ms = std::min(
        *client_expiry - wall_now,
        static_cast<double>(std::clamp(
            request_.FindInt("timeout_ms").value_or(120000), 1000, 120000)));
    expires_unix_ms_ = wall_now + remaining_ms;
    if (remaining_ms <= 0) {
      Error("request_expired");
      return;
    }
    expires_ticks_ = now + base::Milliseconds(remaining_ms);
    deadline_.Start(
        FROM_HERE, base::Milliseconds(remaining_ms),
        base::BindOnce(&AgentBridgeImpl::Error, weak_.GetWeakPtr(),
                       "request_expired"));
    command_ = String(request_, "command");
    if (!stopped_reason_.empty() && command_ != "status" &&
        command_ != "detach" && command_ != "cancel") {
      base::DictValue response;
      response.Set("error", "session_stopped");
      response.Set("reason", stopped_reason_);
      Finish(std::move(response));
      return;
    }
    if (command_ == "tabs") {
      base::ListValue tabs;
      for (const auto& grant : grants_) {
        if (!grant->web_contents())
          continue;
        base::DictValue tab;
        tab.Set("tab", grant->id);
        tab.Set("title", grant->title);
        tab.Set("url", grant->url);
        tab.Set("metadata_truncated", grant->metadata_truncated);
        tab.Set("permission", grant->authorized ? "granted" : "expired");
        tab.Set("active", active_.Run() == grant->web_contents());
        tabs.Append(std::move(tab));
      }
      base::DictValue response;
      response.Set("tabs", std::move(tabs));
      response.Set("scope", "previously approved tabs only; cached consent metadata");
      Finish(std::move(response));
      return;
    }
    if (command_ == "select-tab") {
      const auto tab_id = String(request_, "tab");
      GrantedAgentTab* target = nullptr;
      for (const auto& grant : grants_) {
        if (grant->id == tab_id)
          target = grant.get();
      }
      if (!target || !target->web_contents()) {
        Error("unknown_tab_capability");
        return;
      }
      if (!target->authorized) {
        Error("tab_permission_expired");
        return;
      }
      if (!Eligible(target->web_contents())) {
        target->authorized = false;
        Error("ineligible_tab");
        return;
      }
      if (!select_ || !select_.Run(target->web_contents()) ||
          active_.Run() != target->web_contents()) {
        Error("tab_selection_failed");
        return;
      }
      SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
      Observe(target->web_contents());
      attached_ = true;
      ResetDocument();
      SetBridgeActivity(web_contents(), AgentTabActivity::kWorking);
      command_ = "observe";
      Execute();
      return;
    }
    if (command_ == "status") {
      base::DictValue response;
      response.Set("session_stopped", !stopped_reason_.empty());
      if (!stopped_reason_.empty())
        response.Set("reason", stopped_reason_);
      response.Set("attached", attached_ && web_contents() != nullptr);
      if (auto* grant = GrantFor(web_contents()))
        response.Set("task_permissions", grant->permissions.rules().Clone());
      response.Set("document", document_);
      if (auto* widget = owner_->GetWidget()) {
        response.Set("window_visible", widget->IsVisible());
        response.Set("window_active", widget->IsActive());
      }
      Finish(std::move(response));
      return;
    }
    if (command_ == "cancel" || command_ == "detach") {
      if (command_ == "cancel")
        StopSession("agent_cancelled");
      SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
      Observe(nullptr);
      attached_ = false;
      baseline_.reset();
      grants_.clear();
      Finish(base::DictValue());
      return;
    }
    if (command_ == "attach") {
      if (request_.contains("permissions") &&
          (!request_.FindDict("permissions") ||
           !AgentTaskPermissions::Valid(*request_.FindDict("permissions")))) {
        Error("invalid_permissions");
        return;
      }
      if (request_.contains("permissions") && active_.Run() &&
          !active_.Run()->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
        Error("task_permissions_require_http_origin");
        return;
      }
      if (!Eligible(active_.Run())) {
        Error("ineligible_tab");
        return;
      }
      SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
      std::erase_if(grants_, [](const auto& grant) {
        return grant->web_contents() == nullptr;
      });
      auto* existing = GrantFor(active_.Run());
      if (!existing && grants_.size() >= 32) {
        Error("tab_grant_limit");
        return;
      }
      if (existing) {
        existing->authorized = false;
        existing->permissions.Revoke();
      }
      Observe(active_.Run());
      attached_ = false;
      ResetDocument();
      std::string scope = "\n\nVisible page text will be returned to the local CLI. ";
      if (const auto* rules = request_.FindDict("permissions")) {
        scope += "Task rules for this tab and this site:\n";
        for (const char* action : {"fill", "click", "navigate"}) {
          const auto* rule = rules->FindString(action);
          scope += std::string(action) + ": " + (rule ? *rule : "ask") + "\n";
        }
        scope += "Allow runs without asking again. Ask confirms each action. Deny blocks it. "
                 "Clicks may submit data. Same-site navigation retains these rules. "
                 "Another site, page takeover, detach or browser exit ends the task grant. "
                 "Other tabs require separate approval.";
      } else {
        scope += "This document can remain selected/read until navigation, detach or browser exit. "
                 "Other tabs require separate approval. Edits and clicks ask each time or per ordered batch.";
      }
      Ask("Allow local agent access to this tab?\n\n" +
          url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize() + scope, true);
      return;
    }
    if (!Eligible(web_contents()) || !attached_) {
      Error("not_attached");
      return;
    }
    SetBridgeActivity(web_contents(), AgentTabActivity::kWorking);
    if (command_ == "wait-change") {
      if (request_.contains("wait_mode") &&
          String(request_, "wait_mode") != "content") {
        Error("invalid_wait_mode");
        return;
      }
      if (active_.Run() != web_contents()) {
        Error("activate_attached_tab");
        return;
      }
      const auto wait_ms = request_.FindInt("wait_ms");
      if (!wait_ms || *wait_ms < 1 || *wait_ms > 30000) {
        Error("invalid_wait");
        return;
      }
      if (String(request_, "document") != document_ || !baseline_ ||
          String(request_, "baseline_document") != document_ ||
          request_.FindInt("baseline_revision").value_or(-1) !=
              static_cast<int>(baseline_->revision)) {
        Error("stale_document");
        return;
      }
      waiting_for_change_ = true;
      wait_limit_reached_ = wait_dirty_ = false;
      wait_probes_ = 0;
      wait_accessibility_ = content::BrowserAccessibilityState::GetInstance()
          ->CreateScopedModeForWebContents(web_contents(), ui::kAXModeWebContentsOnly);
      wait_limit_.Start(FROM_HERE, base::Milliseconds(*wait_ms),
                       base::BindOnce(&AgentBridgeImpl::WaitLimitReached,
                                      weak_.GetWeakPtr()));
      NotifyBridgeActivityChanged();
      // First observe closes the gap between the acknowledged response and
      // installation of the event subscription. No hidden baseline is saved.
      Execute();
      return;
    }
    if (command_ == "observe") {
      Execute();
      return;
    }
    if (command_ == "read") {
      auto ref = String(request_, "ref");
      if (ref.starts_with("@"))
        ref.erase(0, 1);
      request_.Set("ref", ref);
      if (ref.find(document_ + "_") != 0) {
        Error("stale_document");
        return;
      }
      Execute();
      return;
    }
    if (active_.Run() != web_contents()) {
      Error("activate_attached_tab");
      return;
    }
    if (command_ == "scroll") {
      const auto pages = request_.FindInt("pages");
      const auto direction = String(request_, "direction");
      if (String(request_, "document") != document_) {
        Error("stale_document");
        return;
      }
      if (!pages || *pages < 1 || *pages > 3 ||
          (direction != "up" && direction != "down")) {
        Error("invalid_scroll");
        return;
      }
      Execute();
      return;
    }
    if (command_ == "ask") {
      auto question = String(request_, "question");
      if (question.empty() || question.size() > 2000) {
        Error("invalid_question");
        return;
      }
      Ask("Agent question (untrusted text; do not enter passwords):\n\n" +
              question,
          false);
      return;
    }
    if (command_ == "navigate") {
      GURL url(String(request_, "url"));
      if (!url.is_valid() ||
          !(url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile()) ||
          url.has_username() || url.has_password()) {
        Error("invalid_url");
        return;
      }
      auto rule = ActionRule("navigate");
      if (rule == AgentTaskPermissions::Rule::kDeny) {
        Error("permission_denied");
      } else if (rule == AgentTaskPermissions::Rule::kAllow &&
                 url::Origin::Create(url) == url::Origin::Create(web_contents()->GetLastCommittedURL())) {
        Answered(id_, document_, true, {});
      } else {
        Ask("Navigate this tab to:\n\n" + url.spec(), true);
      }
      return;
    }
    if (command_ == "batch") {
      const auto* actions = request_.FindList("actions");
      if (!baseline_ || String(request_, "baseline_document") != document_ ||
          request_.FindInt("baseline_revision").value_or(-1) !=
              static_cast<int>(baseline_->revision)) {
        Error("stale_document");
        return;
      }
      if (!actions || actions->empty() || actions->size() > 8) {
        Error("invalid_batch");
        return;
      }
      auto quoted = [](const std::string& text) {
        std::string result;
        base::JSONWriter::Write(base::Value(text), &result);
        return result;
      };
      std::string question = "Allow these ordered actions once?\n\n" +
          url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize();
      std::vector<std::string> seen;
      for (size_t i = 0; i < actions->size(); ++i) {
        const auto* action = (*actions)[i].GetIfDict();
        if (!action) { Error("invalid_batch"); return; }
        const auto command = String(*action, "command");
        const auto ref = String(*action, "ref");
        const bool fill = command == "fill";
        const bool check = command == "check";
        if ((!fill && !check && (command != "click" || i + 1 != actions->size())) ||
            action->size() != (fill || check ? 3u : 2u) ||
            !action->FindString("ref") ||
            (fill && !action->FindString("value")) ||
            (check && !action->FindBool("checked").has_value()) ||
            std::ranges::find(seen, ref) != seen.end()) {
          Error("invalid_batch"); return;
        }
        seen.push_back(ref);
        const auto found = std::ranges::find(baseline_->nodes, ref, &AgentSemanticNode::ref);
        if (found == baseline_->nodes.end() || !ref.starts_with(document_ + "_") ||
            found->secret || !found->enabled || found->name.empty() ||
            found->name.size() > 160 ||
            found->role != (fill ? AgentSemanticRole::kTextField :
                            check ? AgentSemanticRole::kCheckBox : AgentSemanticRole::kButton) ||
            String(*action, "value").size() > 4000) {
          Error("invalid_batch_target"); return;
        }
        question += "\n" + std::to_string(i + 1) + ". " + command + " " + quoted(found->name);
        if (fill) question += " = " + quoted(String(*action, "value"));
        if (check) question += action->FindBool("checked").value() ? " = true" : " = false";
      }
      question += "\n\nOnly this list is approved. Stops on error; completed actions cannot be undone. "
                  "Page actions may send data or navigate.";
      if (question.size() > 48 * 1024) { Error("batch_prompt_too_large"); return; }
      Preflight(std::move(question));
      return;
    }
    if (command_ == "fill" || command_ == "click") {
      auto ref = String(request_, "ref");
      if (ref.starts_with("@"))
        ref.erase(0, 1);
      request_.Set("ref", ref);
      if (!baseline_ || ref.find(document_ + "_") != 0) {
        Error("stale_document");
        return;
      }
      auto found =
          std::ranges::find(baseline_->nodes, ref, &AgentSemanticNode::ref);
      if (found == baseline_->nodes.end()) {
        Error("unknown_reference");
        return;
      }
      if (found->secret) {
        Error("secret_field_forbidden");
        return;
      }
      if (String(request_, "value").size() > 4000) {
        Error("value_too_large");
        return;
      }
      Preflight("Allow one " + command_ + " on this page?\n\n" +
              url::Origin::Create(web_contents()->GetLastCommittedURL())
                  .Serialize() +
              "\nTarget: " + found->name.substr(0, 250) +
              (command_ == "fill" ? "\nValue: " + String(request_, "value")
                                  : "") +
              "\n\nPage actions may send data or navigate.");
      return;
    }
    Error("unknown_command");
  }

  AgentTaskPermissions::Rule ActionRule(std::string_view action) {
    // Explicit checkbox state changes have the same authority as a click.
    if (action == "check") action = "click";
    auto* grant = GrantFor(web_contents());
    return grant ? grant->permissions.Get(
        url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize(), action)
        : AgentTaskPermissions::Rule::kAsk;
  }

  AgentTaskPermissions::Rule RequestRule() {
    if (command_ != "batch") return ActionRule(command_);
    auto rule = AgentTaskPermissions::Rule::kAllow;
    for (const auto& action : *request_.FindList("actions")) {
      const auto current = ActionRule(String(action.GetDict(), "command"));
      if (current == AgentTaskPermissions::Rule::kDeny) return current;
      if (current == AgentTaskPermissions::Rule::kAsk) rule = current;
    }
    return rule;
  }

  void Preflight(std::string question) {
    if (RequestRule() == AgentTaskPermissions::Rule::kDeny) {
      Error("permission_denied");
      return;
    }
    preflight_question_ = std::move(question);
    Execute();
  }

  void Ask(std::string question, bool approval) {
    timing_.BeginUserWait(base::TimeTicks::Now());
    SetBridgeActivity(web_contents(), AgentTabActivity::kNeedsInput);
#if !BUILDFLAG(IS_MAC)
    if (owner_->GetWidget() && !owner_->GetWidget()->IsActive())
      owner_->GetWidget()->FlashFrame(true);
#endif
    prompt_ = ShowAgentBridgePrompt(
        owner_, base::UTF8ToUTF16(question), approval,
        base::BindOnce(&AgentBridgeImpl::Answered, weak_.GetWeakPtr(), id_,
                       document_),
        command_ == "attach" && request_.contains("permissions") ? u"Allow task" : u"Allow once");
    if (prompt_)
      prompt_->AddObserver(this);
    else
      Error("prompt_unavailable");
  }

  void Answered(std::string id,
                std::string document,
                bool accepted,
                std::string answer) {
    if (!busy_ || id != id_ || document != document_)
      return;
    timing_.EndUserWait(base::TimeTicks::Now());
    // The dialog is closing itself. Remove observation without re-closing it.
    if (prompt_) {
      prompt_->RemoveObserver(this);
      prompt_ = nullptr;
    }
    if (base::TimeTicks::Now() >= expires_ticks_) {
      Error("request_expired");
      return;
    }
    if (!accepted) {
      if (command_ == "attach") {
        attached_ = false;
      }
      Error("user_cancelled");
      return;
    }
    if (!Eligible(web_contents()) || active_.Run() != web_contents()) {
      Error("tab_changed");
      return;
    }
    SetBridgeActivity(web_contents(), AgentTabActivity::kWorking);
    if (command_ == "ask") {
      base::DictValue response;
      response.Set("answer", std::move(answer));
      Finish(std::move(response));
    } else if (command_ == "attach") {
      attached_ = true;
      auto* grant = GrantFor(web_contents());
      if (!grant) {
        grants_.push_back(std::make_unique<GrantedAgentTab>(web_contents()));
        grant = grants_.back().get();
      }
      grant->Grant();
      if (const auto* rules = request_.FindDict("permissions")) {
        grant->permissions.Grant(
            url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize(), *rules);
      }
      command_ = "observe";
      Execute();
    } else if (command_ == "navigate") {
      navigating_ = true;
      content::NavigationController::LoadURLParams params(
          GURL(String(request_, "url")));
      params.transition_type = ui::PAGE_TRANSITION_TYPED;
      web_contents()->GetController().LoadURLWithParams(params);
    } else {
      Execute();
    }
  }

  void Execute() {
    if (!busy_ || deferred_error_)
      return;
    if (renderer_pending_) {
      if (waiting_for_change_)
        wait_dirty_ = true;
      return;
    }
    wait_wakeup_.Stop();
    if (base::TimeTicks::Now() >= expires_ticks_) {
      Error("request_expired");
      return;
    }
    if (!Eligible(web_contents())) {
      Error("ineligible_tab");
      return;
    }
    if (waiting_for_change_ && active_.Run() != web_contents()) {
      Error("tab_changed");
      return;
    }
    base::DictValue arguments;
    arguments.Set("command", command_);
    arguments.Set("preflight", preflight_question_.has_value());
    arguments.Set("expires_unix_ms", expires_unix_ms_);
    arguments.Set("document", document_);
    arguments.Set("ref", String(request_, "ref"));
    arguments.Set("value", String(request_, "value"));
    if (command_ == "scroll") {
      arguments.Set("direction", String(request_, "direction"));
      arguments.Set("pages", *request_.FindInt("pages"));
    }
    if (command_ == "batch")
      arguments.Set("actions", request_.FindList("actions")->Clone());
    std::string json;
    base::JSONWriter::Write(arguments, &json);
    const auto source =
        "(()=>{const request=" + json + ";" + kAgentBridgeScript + "})()";
    renderer_pending_ = true;
    if (waiting_for_change_) {
      wait_dirty_ = false;
      ++wait_probes_;
    }
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(source),
        base::BindOnce(&AgentBridgeImpl::Observed, weak_.GetWeakPtr(), id_,
                       document_),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void Observed(std::string id, std::string document, base::Value result) {
    if (!busy_ || id != id_ || !renderer_pending_)
      return;
    renderer_pending_ = false;
    // A missing result can be a disconnected renderer pipe. It is not proof
    // that previously queued JavaScript was cancelled; keep this bridge fenced.
    if (!result.is_dict())
      quarantined_ = true;
    if (deferred_error_) {
      base::DictValue response;
      response.Set("error", *deferred_error_);
      response.Set("partial_effect_possible", true);
      if (const auto* result_dict = result.GetIfDict()) {
        if (auto completed = result_dict->FindInt("completed"))
          response.Set("completed", *completed);
        if (const auto* error = result_dict->FindString("error"))
          response.Set("renderer_error", *error);
      }
      Finish(std::move(response));
      return;
    }
    if (document != document_) {
      Error("stale_document");
      return;
    }
    const auto* dict = result.GetIfDict();
    if (!dict) {
      Error("renderer_unavailable");
      return;
    }
    if (const auto* error = dict->FindString("error")) {
      base::DictValue response;
      response.Set("error", *error);
      if (auto completed = dict->FindInt("completed"))
        response.Set("completed", *completed);
      if (dict->FindBool("partial_effect_possible").value_or(false))
        response.Set("partial_effect_possible", true);
      Finish(std::move(response));
      return;
    }
    if (preflight_question_) {
      if (!dict->FindBool("preflight_valid").value_or(false)) {
        Error("invalid_preflight");
        return;
      }
      if (base::TimeTicks::Now() >= expires_ticks_) {
        Error("request_expired");
        return;
      }
      if (!Eligible(web_contents()) || active_.Run() != web_contents()) {
        Error("tab_changed");
        return;
      }
      auto question = std::move(*preflight_question_);
      preflight_question_.reset();
      const auto rule = RequestRule();
      if (rule == AgentTaskPermissions::Rule::kDeny)
        Error("permission_denied");
      else if (rule == AgentTaskPermissions::Rule::kAllow)
        Answered(id_, document_, true, {});
      else
        Ask(std::move(question), true);
      return;
    }
    if (const auto* text = dict->FindString("text")) {
      base::DictValue response;
      response.Set("text", *text);
      response.Set("field_truncated",
                   dict->FindBool("field_truncated").value_or(true));
      Finish(std::move(response));
      return;
    }
    const auto* nodes = dict->FindList("nodes");
    if (!nodes) {
      Error("invalid_observation");
      return;
    }
    // A renderer reply can arrive after the user switches tabs. Its action
    // result still settles, but feedback must not appear over another page.
    if (const auto* point = dict->FindDict("point");
        point && active_.Run() == web_contents() &&
        web_contents()->GetVisibility() == content::Visibility::VISIBLE) {
      const auto bounds = web_contents()->GetContainerBounds();
      const int x = static_cast<int>(point->FindDouble("x").value_or(0));
      const int y = static_cast<int>(point->FindDouble("y").value_or(0));
      ClosePointer();
      pointer_ = views::ShowAgentBridgePointer(
          owner_,
          gfx::Point(
              bounds.x() + std::clamp(x, 0, std::max(0, bounds.width() - 132)),
              bounds.y() +
                  std::clamp(y, 0, std::max(0, bounds.height() - 36))));
      if (pointer_) {
        pointer_->AddObserver(this);
        pointer_timer_.Start(FROM_HERE, base::Milliseconds(1500), this,
                             &AgentBridgeImpl::ClosePointer);
      }
    }
    AgentSemanticSnapshot snapshot;
    snapshot.document_ref = document_;
    snapshot.revision = ++revision_;
    snapshot.title = base::UTF16ToUTF8(web_contents()->GetTitle());
    snapshot.origin =
        url::Origin::Create(web_contents()->GetLastCommittedURL()).Serialize();
    for (const auto& value : *nodes) {
      if (!value.is_dict())
        continue;
      const auto& node = value.GetDict();
      AgentSemanticNode semantic;
      semantic.ref = String(node, "ref");
      semantic.role = Role(String(node, "role"));
      semantic.name = String(node, "name");
      semantic.secret = node.FindBool("secret").value_or(true);
      semantic.value = semantic.secret ? "" : String(node, "value");
      semantic.href = semantic.secret ? "" : String(node, "href");
      semantic.enabled = node.FindBool("enabled").value_or(false);
      semantic.checked = node.FindBool("checked").value_or(false);
      semantic.focused = node.FindBool("focused").value_or(false);
      snapshot.nodes.push_back(std::move(semantic));
    }
    const bool full = request_.FindBool("full").value_or(false);
    const bool acknowledged =
        baseline_ &&
        String(request_, "baseline_document") == baseline_->document_ref &&
        request_.FindInt("baseline_revision").value_or(-1) ==
            static_cast<int>(baseline_->revision);
    auto serialized = SerializeAgentSemanticSnapshot(
        snapshot, !waiting_for_change_ && !full && acknowledged ? &*baseline_ : nullptr, 16000);
    bool truncated = dict->FindBool("truncated").value_or(true) ||
                     serialized.find("!truncated") != std::string::npos;
    bool wait_changed = false;
    if (waiting_for_change_) {
      const auto* viewport = dict->FindDict("viewport");
      wait_changed = !baseline_ ||
          !AgentSnapshotsHaveSameObservableSemantics(
              snapshot, *baseline_, String(request_, "wait_mode") != "content") ||
          !viewport || *viewport != baseline_viewport_;
      if (!truncated && !wait_changed && !wait_limit_reached_) {
        if (wait_dirty_)
          WakeWait();
        return;
      }
    }
    if (truncated && serialized.find("!truncated") == std::string::npos)
      serialized += "!truncated adapter_scope_limit\n";
    base::DictValue response;
    response.Set("snapshot", serialized);
    if (const auto* scroll = dict->FindDict("scroll"))
      response.Set("scroll", scroll->Clone());
    if (auto completed = dict->FindInt("completed"))
      response.Set("completed", *completed);
    response.Set("observation_bytes", static_cast<int>(serialized.size()));
    response.Set("truncated", truncated);
    response.Set("scope", "main_document_visible_dom");
    response.Set("document", document_);
    response.Set("revision", static_cast<int>(revision_));
    // Read the live committed URL on every observation, including node deltas.
    // Grant inventory records may predate a same-origin navigation.
    const auto location =
        GetAgentPageLocation(web_contents()->GetLastCommittedURL());
    response.Set("url", location.url);
    if (location.truncated)
      response.Set("url_truncated", true);
    if (location.credentials_redacted)
      response.Set("url_credentials_redacted", true);
    if (waiting_for_change_) {
      base::DictValue wait;
      wait.Set("reason", truncated ? "scope_limit" : wait_changed ? "changed" : "limit");
      wait.Set("mechanism", "accessibility_events");
      wait.Set("comparison", String(request_, "wait_mode") == "content" ? "content" : "identity");
      wait.Set("probes", wait_probes_);
      response.Set("wait", std::move(wait));
    }
    // Viewport is current observation metadata, not part of the node delta.
    // Report it after actions too: unchanged nodes do not imply unchanged
    // page dimensions (e.g. the user may have resized or opened a side panel).
    if (const auto* viewport = dict->FindDict("viewport")) {
      response.Set("viewport", viewport->Clone());
      baseline_viewport_ = viewport->Clone();
    }
    if (truncated)
      baseline_.reset();
    else
      baseline_ = std::move(snapshot);
    Finish(std::move(response));
  }

  void Error(std::string error) {
    if (error == "user_cancelled" || error == "user_takeover" ||
        error == "client_cancelled")
      StopSession(error);
    if (!busy_)
      return;
    // Invalidating a callback does not cancel JavaScript already sent to a
    // renderer or an in-flight navigation. Keep the mailbox occupied until its
    // completion signal, preserving the first interruption as the outcome.
    if (renderer_pending_ || navigating_) {
      if (!deferred_error_)
        deferred_error_ = std::move(error);
      deadline_.Stop();
      ClosePrompt();
      return;
    }
    base::DictValue response;
    response.Set("error", std::move(error));
    if (deferred_error_)
      response.Set("partial_effect_possible", true);
    Finish(std::move(response));
  }
  void Finish(base::DictValue response) {
    const auto finished = base::TimeTicks::Now();
    timing_.EndUserWait(finished);
    if (request_.FindBool("record_timing").value_or(false)) {
      base::DictValue timing;
      timing.Set("native_elapsed_ms", timing_.Elapsed(finished).InMillisecondsF());
      timing.Set("user_wait_ms", timing_.UserWait().InMillisecondsF());
      response.Set("timing", std::move(timing));
    }
    busy_ = false;
    preflight_question_.reset();
    waiting_for_change_ = false;
    wait_limit_.Stop();
    wait_wakeup_.Stop();
    wait_accessibility_.reset();
    navigating_ = false;
    deadline_.Stop();
    ClosePrompt();
#if !BUILDFLAG(IS_MAC)
    if (owner_->GetWidget())
      owner_->GetWidget()->FlashFrame(false);
#endif
    SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
    response.Set("execution_settled", !quarantined_);
    response.Set("receipt_persisted", request_claimed_);
    if (web_contents()) {
      if (auto* grant = GrantFor(web_contents()))
        response.Set("tab", grant->id);
    }
    response.Set("id", id_);
    response.Set("ok", !response.contains("error"));
    response.Set("status", attached_ ? "idle" : "detached");
    std::string json;
    base::JSONWriter::Write(response, &json);
    writing_response_ = true;
    io_->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&WriteDurableResponse, directory_, id_,
                                 std::move(json), request_claimed_),
        base::BindOnce(&AgentBridgeImpl::ResponseWritten, weak_.GetWeakPtr()));
  }
  void ResponseWritten(bool stored) {
    writing_response_ = false;
    if (!stored)
      quarantined_ = true;
    Poll();
  }
  void ClosePrompt() {
    if (!prompt_)
      return;
    auto* widget = prompt_.get();
    prompt_ = nullptr;
    widget->RemoveObserver(this);
    widget->Close();
  }
  void OnWidgetDestroyed(views::Widget* widget) override {
    if (widget == prompt_)
      prompt_ = nullptr;
    if (widget == pointer_)
      pointer_ = nullptr;
  }
  void ClosePointer() {
    pointer_timer_.Stop();
    if (!pointer_)
      return;
    auto* widget = pointer_.get();
    pointer_ = nullptr;
    widget->RemoveObserver(this);
    widget->Close();
  }
  void ResetDocument() {
    ClosePointer();
    document_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    revision_ = 0;
    baseline_.reset();
    baseline_viewport_.clear();
  }
  void WakeWait() {
    if (!busy_ || !waiting_for_change_ || deferred_error_)
      return;
    if (renderer_pending_) {
      wait_dirty_ = true;
      return;
    }
    if (!wait_wakeup_.IsRunning())
      wait_wakeup_.Start(FROM_HERE, base::Milliseconds(50), this,
                         &AgentBridgeImpl::Execute);
  }
  void WaitLimitReached() {
    wait_limit_reached_ = true;
    WakeWait();
  }
  void AccessibilityEventReceived(const ui::AXUpdatesAndEvents&) override {
    WakeWait();
  }
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID&, ui::AXLocationAndScrollUpdates&) override {
    WakeWait();
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE) {
      ClosePointer();
      if (waiting_for_change_)
        Error("tab_changed");
    }
  }
  void PrimaryMainFrameWasResized(bool) override {
    WakeWait();
  }
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (!handle->HasCommitted() || !handle->IsInPrimaryMainFrame())
      return;
    auto* grant = GrantFor(web_contents());
    const bool task_navigation = grant && grant->permissions.Covers(
        url::Origin::Create(handle->GetURL()).Serialize());
    if (!task_navigation && grant) grant->permissions.Revoke();
    if (!navigating_ && !task_navigation)
      attached_ = false;
    ResetDocument();
    if (busy_ && !navigating_) {
      // A read-only probe of the old document can lose its callback when that
      // frame enters the back/forward cache. Its capability is now invalid, so
      // do not leave the mailbox waiting for that frame to run again. Late
      // callbacks are ignored by Observed's request-id/pending checks. Already
      // dispatched mutations still retain their original settlement fence.
      if (waiting_for_change_ || command_ == "observe" || command_ == "read" ||
          preflight_question_.has_value()) {
        renderer_pending_ = false;
      }
      Error(deferred_error_.value_or("stale_document"));
    }
  }
  void DidStopLoading() override {
    if (busy_ && navigating_) {
      navigating_ = false;
      if (deferred_error_) {
        Error(*deferred_error_);
        return;
      }
      command_ = "observe";
      Execute();
    }
  }
  void DidGetUserInteraction(const blink::WebInputEvent& event) override {
    ClosePointer();
    StopSession("user_takeover");
    if (auto* grant = GrantFor(web_contents())) grant->permissions.Revoke();
    baseline_.reset();
    if (busy_)
      Error("user_takeover");
  }
  void WebContentsDestroyed() override {
    ClosePointer();
    navigating_ = false;
    attached_ = false;
    if (busy_)
      Error("tab_closed");
    SetBridgeActivity(web_contents(), AgentTabActivity::kNone);
    Observe(nullptr);
    attached_ = false;
    baseline_.reset();
  }

  raw_ptr<views::View> owner_;
  base::RepeatingCallback<content::WebContents*()> active_;
  base::RepeatingCallback<bool(content::WebContents*)> select_;
  std::vector<std::unique_ptr<GrantedAgentTab>> grants_;
  base::FilePath directory_;
  scoped_refptr<base::SequencedTaskRunner> io_;
  base::SequenceBound<base::FilePathWatcher> request_watcher_;
  base::RepeatingTimer poll_;
  base::OneShotTimer deadline_;
  base::OneShotTimer pointer_timer_;
  base::OneShotTimer wait_limit_;
  base::OneShotTimer wait_wakeup_;
  std::unique_ptr<content::ScopedAccessibilityMode> wait_accessibility_;
  bool waiting_for_change_ = false;
  bool wait_limit_reached_ = false;
  bool wait_dirty_ = false;
  int wait_probes_ = 0;
  raw_ptr<views::Widget> prompt_ = nullptr;
  raw_ptr<views::Widget> pointer_ = nullptr;
  bool reading_ = false;
  bool reading_cancel_ = false;
  bool reading_stop_ = false;
  bool writing_response_ = false;
  bool busy_ = false;
  bool renderer_pending_ = false;
  bool quarantined_ = false;
  bool request_claimed_ = false;
  std::optional<std::string> deferred_error_;
  std::optional<std::string> preflight_question_;
  base::TimeTicks expires_ticks_;
  double expires_unix_ms_ = 0;
  AgentRequestTiming timing_;
  bool attached_ = false;
  bool navigating_ = false;
  std::string id_, command_, document_;
  std::string stopped_reason_;
  size_t revision_ = 0;
  base::DictValue request_;
  std::optional<AgentSemanticSnapshot> baseline_;
  base::DictValue baseline_viewport_;
  base::WeakPtrFactory<AgentBridgeImpl> weak_{this};
};
}  // namespace
#endif

std::unique_ptr<AgentBridge> CreateAgentBridge(
    views::View* owner,
    base::RepeatingCallback<content::WebContents*()> active_contents,
    base::RepeatingCallback<bool(content::WebContents*)> select_contents) {
#if BUILDFLAG(IS_POSIX)
  auto path = base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
      "yee-agent-bridge");
  if (path.empty() || g_claimed)
    return nullptr;
  // Directory validation and all mailbox IO happen off the UI thread.
  if (!path.IsAbsolute())
    return nullptr;
  g_claimed = true;
  return std::make_unique<AgentBridgeImpl>(owner, std::move(active_contents),
                                         std::move(select_contents), path);
#else
  return nullptr;
#endif
}
base::RepeatingClosure AgentBridge::PendingWaitCancellation() {
  return {};
}
void InitializeAgentBridge(
    views::View* owner,
    base::RepeatingCallback<content::WebContents*()> active_contents,
    base::RepeatingCallback<bool(content::WebContents*)> select_contents) {
  auto bridge = CreateAgentBridge(owner, std::move(active_contents),
                                  std::move(select_contents));
  if (bridge)
    owner->SetProperty(kAgentBridgeKey, std::move(bridge));
}
void DestroyAgentBridge(views::View* owner) {
  owner->ClearProperty(kAgentBridgeKey);
}
namespace {
AgentBridge* FindWindowBridge(views::View* view) {
  if (auto* bridge = view->GetProperty(kAgentBridgeKey))
    return bridge;
  for (const auto& child : view->children()) {
    if (auto* bridge = FindWindowBridge(child.get()))
      return bridge;
  }
  return nullptr;
}
}  // namespace
base::RepeatingClosure GetPendingAgentWaitCancellation(views::View* view) {
  if (!view || !view->GetWidget())
    return {};
  auto* bridge = FindWindowBridge(view->GetWidget()->GetRootView());
  return bridge ? bridge->PendingWaitCancellation() : base::RepeatingClosure();
}
}  // namespace yee
