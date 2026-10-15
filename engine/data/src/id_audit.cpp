#include "ichor/data/id_audit.h"

#include "ichor/core/file_io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::uintmax_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 64;

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string segmentProblem(std::string_view segment)
{
    if (segment.empty()) {
        return "empty segment";
    }
    if (!isLower(segment.front())) {
        return isDigit(segment.front()) ? "segment starts with a digit (numeric or positional id)" : "segment must start with a lowercase letter";
    }
    for (const char c : segment) {
        if (!isLower(c) && !isDigit(c) && c != '_') {
            return "only lowercase letters, digits and underscores are allowed";
        }
    }
    std::size_t end = segment.size();
    while (end > 0 && isDigit(segment[end - 1])) {
        --end;
    }
    if (end < segment.size() && segment[end - 1] == '_') {
        return "trailing _<number> reads as a list position";
    }
    return {};
}

struct Scanner {
    std::string file;
    std::string dir;
    IdAuditReport& report;
    std::map<std::string, std::string>& seen; // directory, scope and id -> first location

    void add(const std::string& path, const std::string& id, const std::string& reason) const
    {
        report.violations.push_back({file, path, id, reason});
    }

    void checkId(const Json& value, const std::string& path, const std::string& scope) const
    {
        if (!value.is_string()) {
            add(path, {}, "id is not a string");
            return;
        }
        const std::string id = value.get<std::string>();
        ++report.idsChecked;
        if (const std::string problem = idProblem(id); !problem.empty()) {
            add(path, id, problem);
            return;
        }
        const auto [it, fresh] = seen.emplace(dir + "|" + scope + "|" + id, file + ": " + path);
        if (!fresh) {
            add(path, id, "duplicate id, first defined at " + it->second);
        }
    }

    void walk(const Json& node, const std::string& path, const std::string& scope, int depth) const
    {
        if (depth > kMaxDepth) {
            add(path, {}, "nesting too deep to audit");
            return;
        }
        if (node.is_object()) {
            for (const auto& [key, child] : node.items()) {
                if (key == "id") {
                    checkId(child, path + "/id", scope + "/id");
                } else {
                    walk(child, path + "/" + key, scope + "/" + key, depth + 1);
                }
            }
        } else if (node.is_array()) {
            for (std::size_t i = 0; i < node.size(); ++i) {
                walk(node[i], path + "[" + std::to_string(i) + "]", scope + "[]", depth + 1);
            }
        }
    }

    void scanRoot(const Json& root) const
    {
        if (root.is_array()) {
            for (std::size_t i = 0; i < root.size(); ++i) {
                if (!root[i].is_object() || !root[i].contains("id")) {
                    add("[" + std::to_string(i) + "]", {}, "record has no id");
                }
            }
        } else if (root.is_object() && dir == "axioms" && !root.contains("id")) {
            add("", {}, "axiom has no id");
        }
        walk(root, "", "", 0);
    }
};

}

std::string idProblem(std::string_view id)
{
    if (id.empty()) {
        return "id is empty";
    }
    if (id.size() > kMaxIdLength) {
        return "id is longer than " + std::to_string(kMaxIdLength) + " characters";
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = id.find('.', start);
        const std::string_view segment = id.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (std::string problem = segmentProblem(segment); !problem.empty()) {
            return problem;
        }
        if (dot == std::string_view::npos) {
            return {};
        }
        start = dot + 1;
    }
}

std::string describe(const IdViolation& violation)
{
    std::string text = violation.file + ": " + (violation.path.empty() ? "(root)" : violation.path) + ": " + violation.reason;
    if (!violation.id.empty()) {
        text += " \"" + violation.id + "\"";
    }
    return text;
}

IdAuditReport auditIds(const std::filesystem::path& dataRoot)
{
    IdAuditReport report;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dataRoot, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".json") {
            files.emplace_back(std::filesystem::relative(it->path(), dataRoot, ec).generic_string(), it->path());
        }
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, std::string> seen;
    for (const auto& [relative, path] : files) {
        ++report.filesScanned;
        const std::size_t slash = relative.find('/');
        const std::string dir = slash == std::string::npos ? std::string() : relative.substr(0, slash);
        const Scanner scanner{relative, dir, report, seen};

        std::vector<std::uint8_t> bytes;
        if (!readFileBytes(path, bytes, kMaxFileBytes)) {
            scanner.add("", {}, "file could not be read or is larger than the audit limit");
            continue;
        }
        const Json root = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
        if (root.is_discarded()) {
            scanner.add("", {}, "file is not valid JSON");
            continue;
        }
        scanner.scanRoot(root);
    }
    std::sort(report.violations.begin(), report.violations.end(), [](const IdViolation& a, const IdViolation& b) {
        return std::tie(a.file, a.path, a.reason) < std::tie(b.file, b.path, b.reason);
    });
    return report;
}

}
