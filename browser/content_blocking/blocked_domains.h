// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#ifndef CHROME_BROWSER_YEE_CONTENT_BLOCKING_BLOCKED_DOMAINS_H_
#define CHROME_BROWSER_YEE_CONTENT_BLOCKING_BLOCKED_DOMAINS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yee::content_blocking {

inline constexpr size_t kMaxBlockedDomains = 5000;
// Allow a CSV export of all 5,000 maximum-length domain rules to be restored.
inline constexpr size_t kMaxDomainImportBytes = 2 * 1024 * 1024;

struct BlockedDomain {
  std::string domain;
  bool include_subdomains = true;
  bool operator==(const BlockedDomain&) const = default;
};

// Accept a bare DNS hostname, including IDNs. URLs, IP addresses, wildcard
// syntax and filter expressions cannot silently become broader domain rules.
std::optional<std::string> CanonicalBlockedDomain(std::string_view input);

enum class DomainImportFormat { kCsv, kText };
enum class DomainImportRowError {
  kNone,
  kInvalidDomain,
  kInvalidColumns,
  kInvalidScope,
  kDuplicate,
};
struct DomainImportRow {
  size_t line;
  std::string input;
  std::optional<BlockedDomain> rule;
  DomainImportRowError error = DomainImportRowError::kNone;
};
struct DomainImport {
  std::vector<DomainImportRow> rows;
  // A file-level error prevents importing any rows.
  std::string error;
};

// CSV requires domain[,include_subdomains]; TXT has one domain per line.
// Both accept UTF-8 with a BOM and LF/CRLF. Parsing has no profile or UI state
// and runs on a worker before previewing or modifying profile preferences.
DomainImport ParseDomainImport(std::string_view text,
                               DomainImportFormat format);

}  // namespace yee::content_blocking
#endif
