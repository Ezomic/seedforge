#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Maximum length of a stable id.
constexpr std::size_t kMaxIdLength = 64;

// Empty when `id` is a stable string id, else the reason it is not. A stable id is
// dot-separated segments of [a-z][a-z0-9_]*, so it never starts with a digit and survives a
// rename of neighbours. Rejected as positional: a segment that is only digits, or one that
// ends in _<digits> (item_47), because such a name only means something relative to its place
// in a list.
std::string idProblem(std::string_view id);

struct IdViolation {
    std::string file;   // data-root-relative, forward slashes
    std::string path;   // JSON location of the id, e.g. "[2]/id" or "/elements[1]/id"
    std::string id;     // the offending value, empty when it is not a string
    std::string reason;
};

// "<file>: <path>: <reason>" plus the value when there is one.
std::string describe(const IdViolation& violation);

struct IdAuditReport {
    std::vector<IdViolation> violations; // sorted by file, path
    std::size_t filesScanned = 0;
    std::size_t idsChecked = 0;

    bool ok() const { return violations.empty(); }
};

// Scans every *.json under `dataRoot`. Checks that:
//  - every "id" key, at any depth, holds a string that passes idProblem;
//  - every element of a top-level array and every axiom file has an "id";
//  - ids are unique within their scope: the same directory and the same JSON location with
//    list indices erased, across all files (the DataSet table rule, applied to nested lists too);
//  - the file parses.
// Files are visited in sorted order so the report is identical on every platform.
IdAuditReport auditIds(const std::filesystem::path& dataRoot);

}
