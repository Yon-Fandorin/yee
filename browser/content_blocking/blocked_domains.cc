// Copyright 2026 The Yee Authors. BSD-style license in LICENSE.
#include "chrome/browser/yee_content_blocking/blocked_domains.h"

#include <set>
#include <utility>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "url/gurl.h"

namespace yee::content_blocking {
namespace {

struct CsvRow {
  size_t line;
  std::vector<std::string> fields;
};

std::optional<std::vector<CsvRow>> ReadCsv(std::string_view text) {
  std::vector<CsvRow> rows;
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  bool closed_quote = false;
  size_t line = 1;
  size_t row_line = 1;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char c = i == text.size() ? '\0' : text[i];
    if (quoted) {
      if (i == text.size()) {
        return std::nullopt;
      }
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
          closed_quote = true;
        }
      } else {
        field += c;
        if (c == '\n') {
          ++line;
        }
      }
      continue;
    }
    if (c == ',' || c == '\n' || c == '\r' || i == text.size()) {
      fields.emplace_back(base::TrimWhitespaceASCII(field, base::TRIM_ALL));
      field.clear();
      closed_quote = false;
      if (c == ',') {
        continue;
      }
      if (fields.size() > 1 || !fields[0].empty()) {
        rows.push_back({row_line, std::move(fields)});
        if (rows.size() > kMaxBlockedDomains + 1) {
          return rows;
        }
      }
      fields.clear();
      if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      row_line = ++line;
    } else if (c == '"' && field.empty() && !closed_quote) {
      quoted = true;
    } else if (closed_quote) {
      if (c != ' ' && c != '\t') {
        return std::nullopt;
      }
    } else {
      if (c == '"') {
        return std::nullopt;
      }
      field += c;
    }
  }
  return rows;
}

void AddRow(DomainImport& result,
            size_t line,
            std::string input,
            std::optional<bool> scope,
            std::set<std::string>& seen,
            bool valid_columns = true) {
  // Bound preview labels independently of the total file size.
  DomainImportRow row{line,
                      std::string(base::TruncateUTF8ToByteSize(input, 512)),
                      std::nullopt};
  const auto domain = CanonicalBlockedDomain(input);
  if (!valid_columns) {
    row.error = DomainImportRowError::kInvalidColumns;
  } else if (!domain) {
    row.error = DomainImportRowError::kInvalidDomain;
  } else if (!scope.has_value()) {
    row.error = DomainImportRowError::kInvalidScope;
  } else {
    row.rule = BlockedDomain{*domain, *scope};
    if (!seen.insert(*domain).second) {
      row.error = DomainImportRowError::kDuplicate;
    }
  }
  result.rows.push_back(std::move(row));
}

std::optional<bool> ReadScope(std::string_view value) {
  if (value.empty() || base::EqualsCaseInsensitiveASCII(value, "true") ||
      value == "1") {
    return true;
  }
  if (base::EqualsCaseInsensitiveASCII(value, "false") || value == "0") {
    return false;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> CanonicalBlockedDomain(std::string_view input) {
  input = base::TrimWhitespaceASCII(input, base::TRIM_ALL);
  if (input.empty() || input.size() > 1024 ||
      input.find_first_of("/:@?#%*\\\"' \t\r\n") != std::string_view::npos ||
      input.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }
  const GURL url("https://" + std::string(input) + "/");
  if (!url.is_valid() || url.host().empty() || url.HostIsIPAddress()) {
    return std::nullopt;
  }
  std::string host(url.host());
  if (host.ends_with('.')) {
    host.pop_back();
  }
  if (host.empty() || host.size() > 253) {
    return std::nullopt;
  }
  for (const auto label : base::SplitStringPiece(
           host, ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (label.empty() || label.size() > 63 || label.front() == '-' ||
        label.back() == '-') {
      return std::nullopt;
    }
    for (const char c : label) {
      if (!base::IsAsciiAlphaNumeric(c) && c != '-') {
        return std::nullopt;
      }
    }
  }
  return host;
}

DomainImport ParseDomainImport(std::string_view text,
                               DomainImportFormat format) {
  DomainImport result;
  if (text.size() > kMaxDomainImportBytes) {
    return {{}, "file-too-large"};
  }
  if (!base::IsStringUTF8(text) || text.find('\0') != std::string_view::npos) {
    return {{}, "invalid-file"};
  }
  if (text.starts_with("\xef\xbb\xbf")) {
    text.remove_prefix(3);
  }
  std::set<std::string> seen;
  if (format == DomainImportFormat::kText) {
    size_t line = 0;
    for (auto input : base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE,
                                             base::SPLIT_WANT_ALL)) {
      ++line;
      if (input.empty() || input.starts_with('#')) {
        continue;
      }
      if (result.rows.size() == kMaxBlockedDomains) {
        return {{}, "too-many-domains"};
      }
      AddRow(result, line, std::string(input), true, seen);
    }
  } else {
    const auto csv = ReadCsv(text);
    if (!csv || csv->empty()) {
      return {{}, "invalid-csv"};
    }
    if (csv->size() > kMaxBlockedDomains + 1) {
      return {{}, "too-many-domains"};
    }
    const auto& header = csv->front().fields;
    size_t domain_column = header.size();
    size_t scope_column = header.size();
    for (size_t i = 0; i < header.size(); ++i) {
      const std::string column = base::ToLowerASCII(header[i]);
      if (column == "domain" && domain_column == header.size()) {
        domain_column = i;
      } else if (column == "include_subdomains" &&
                 scope_column == header.size()) {
        scope_column = i;
      } else {
        return {{}, "invalid-csv-header"};
      }
    }
    if (domain_column == header.size()) {
      return {{}, "invalid-csv-header"};
    }
    for (size_t i = 1; i < csv->size(); ++i) {
      const auto& row = (*csv)[i];
      const std::string input = domain_column < row.fields.size()
                                    ? row.fields[domain_column]
                                    : std::string();
      const auto scope = scope_column < row.fields.size()
                             ? ReadScope(row.fields[scope_column])
                             : std::optional<bool>(true);
      AddRow(result, row.line, input, scope, seen,
             row.fields.size() == header.size());
    }
  }
  if (result.rows.empty()) {
    result.error = "empty-file";
  }
  return result;
}

}  // namespace yee::content_blocking
