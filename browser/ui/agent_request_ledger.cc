#include "chrome/browser/ui/views/yee/agent_request_ledger.h"

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"

namespace yee {
namespace {
bool PrivateFile(const base::FilePath& path) {
#if BUILDFLAG(IS_POSIX)
  return base::SetPosixFilePermissions(path, 0600);
#else
  // The native mailbox is currently POSIX-only.
  return true;
#endif
}
base::FilePath Path(const base::FilePath& directory, std::string_view id,
                    std::string_view suffix) {
  return directory.AppendASCII("native-request-" + base::HexEncode(id) +
                               std::string(suffix));
}
bool ValidResponse(const std::string& response, std::string_view id) {
  auto value = base::JSONReader::Read(response, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict())
    return false;
  const auto& dict = value->GetDict();
  const auto* recorded_id = dict.FindString("id");
  return recorded_id && *recorded_id == id && dict.FindBool("ok").has_value() &&
         dict.FindBool("execution_settled").has_value();
}
bool ValidStopReason(std::string_view reason) {
  return reason == "user_cancelled" || reason == "user_takeover" ||
         reason == "client_cancelled" || reason == "agent_cancelled";
}
std::string StopReason(const base::FilePath& path, bool client_control) {
  std::string data;
  if (base::IsLink(path) ||
      !base::ReadFileToStringWithMaxSize(path, &data, 4096))
    return "stop_state_unavailable";
  auto value = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict())
    return "stop_state_unavailable";
  const auto& dict = value->GetDict();
  const auto* id = dict.FindString("id");
  const auto* reason = dict.FindString("reason");
  if (dict.size() != 2 || !id || id->empty() || id->size() > 64 || !reason ||
      !ValidStopReason(*reason) ||
      (client_control && *reason != "client_cancelled"))
    return "stop_state_unavailable";
  return *reason;
}
}  // namespace

bool StoreAgentSessionStop(const base::FilePath& directory,
                          std::string_view id, std::string_view reason) {
  if (id.empty() || id.size() > 64 || !ValidStopReason(reason))
    return false;
  const auto path = directory.AppendASCII("native-session-stop.json");
  if (base::PathExists(path) || base::IsLink(path))
    return StopReason(path, false) != "stop_state_unavailable";
  base::File file(path, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (!file.IsValid() || !PrivateFile(path))
    return false;
  base::DictValue value;
  value.Set("id", std::string(id));
  value.Set("reason", std::string(reason));
  std::string json;
  if (!base::JSONWriter::Write(value, &json) ||
      !file.WriteAtCurrentPosAndCheck(base::as_byte_span(json)) ||
      !file.Flush())
    return false;
  return true;
}

std::string ReadAgentSessionStop(const base::FilePath& directory) {
  const auto path = directory.AppendASCII("native-session-stop.json");
  if (base::PathExists(path) || base::IsLink(path))
    return StopReason(path, false);
  const auto control = directory.AppendASCII("client-stop.json");
  if (!base::PathExists(control) && !base::IsLink(control))
    return {};
  const auto reason = StopReason(control, true);
  if (reason == "stop_state_unavailable")
    return reason;
  std::string data;
  if (!base::ReadFileToStringWithMaxSize(control, &data, 4096))
    return "stop_state_unavailable";
  auto value = base::JSONReader::Read(data, base::JSON_PARSE_RFC);
  if (!value || !value->is_dict())
    return "stop_state_unavailable";
  const auto* id = value->GetDict().FindString("id");
  if (!id || !StoreAgentSessionStop(directory, *id, reason))
    return "stop_state_unavailable";
  return reason;
}

AgentRequestClaim ClaimAgentRequest(const base::FilePath& directory,
                                   std::string_view id) {
  if (id.empty() || id.size() > 64)
    return {};
  const auto claim = Path(directory, id, ".claim");
  // Exclusive creation is the admission point. Never truncate an old claim.
  base::File file(claim, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (file.IsValid()) {
    if (!PrivateFile(claim) || !file.Flush())
      return {};
    return {true, {}};
  }
  if (file.error_details() != base::File::FILE_ERROR_EXISTS ||
      base::IsLink(claim))
    return {};
  const auto result = Path(directory, id, ".result");
  std::string response;
  if (base::IsLink(result) ||
      !base::ReadFileToStringWithMaxSize(result, &response, 1024 * 1024) ||
      !ValidResponse(response, id))
    return {};
  return {false, std::move(response)};
}

bool StoreAgentResponse(const base::FilePath& directory, std::string_view id,
                        const std::string& response) {
  if (id.empty() || id.size() > 64 || !ValidResponse(response, id))
    return false;
  const auto claim = Path(directory, id, ".claim");
  if (base::IsLink(claim) || !base::PathExists(claim))
    return false;
  const auto temporary = Path(directory, id, ".tmp");
  const auto result = Path(directory, id, ".result");
  if (base::PathExists(result) || base::IsLink(result) ||
      base::IsLink(temporary))
    return false;
  base::File file(temporary, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  if (!file.IsValid() || !PrivateFile(temporary))
    return false;
  file.Close();
  if (!base::WriteFile(temporary, response))
    return false;
  file.Initialize(temporary, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  if (!file.IsValid() || !file.Flush())
    return false;
  file.Close();
  return base::ReplaceFile(temporary, result, nullptr);
}
}  // namespace yee
