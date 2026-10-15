#include "ichor/factions/parasite_mapping.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxEntries = 64;
constexpr std::string_view kEmDash = "\xE2\x80\x94";
constexpr std::string_view kLawPrefix = "law.";

std::string_view typeName(const Json& json)
{
    if (json.is_null()) {
        return "null";
    }
    if (json.is_boolean()) {
        return "boolean";
    }
    if (json.is_number()) {
        return "number";
    }
    if (json.is_string()) {
        return "string";
    }
    return json.is_array() ? "array" : "object";
}

std::string shown(std::string_view text)
{
    constexpr std::size_t kLimit = 40;
    std::string out = "\"";
    out.append(text.substr(0, kLimit));
    if (text.size() > kLimit) {
        out += "...";
    }
    return out + "\"";
}

bool isBlank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

bool isLawAxiomId(std::string_view text)
{
    return text.size() <= kMaxId && text.substr(0, kLawPrefix.size()) == kLawPrefix && isWord(text.substr(kLawPrefix.size()));
}

// Nesting is counted before parsing, so a hostile file never reaches a recursive walk.
bool nestsTooDeep(std::string_view text)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxDepth) {
                return true;
            }
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

}

bool parseParasiteMapping(std::string_view label, std::string_view text, std::vector<ParasiteEntry>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    const auto add = [&](const std::string& path, const std::string& message) {
        std::string line(label);
        line += ": ";
        if (!path.empty()) {
            line += path + ": ";
        }
        problems.push_back(line + message);
    };
    const auto fail = [&](const std::string& message) {
        add("", message);
        return false;
    };
    if (text.size() > kMaxFileBytes) {
        return fail("file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
    }
    if (isBlank(text)) {
        return fail("empty file");
    }
    if (nestsTooDeep(text)) {
        return fail("nested deeper than " + std::to_string(kMaxDepth) + " levels");
    }
    // The parser keeps the last of two equal keys; a duplicate is an authoring mistake, so it is refused.
    std::vector<std::set<std::string>> keysByDepth;
    std::string duplicate;
    const Json json = Json::parse(
        text.begin(), text.end(),
        [&](int depth, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                keysByDepth.resize(std::max(keysByDepth.size(), static_cast<std::size_t>(depth) + 2));
                keysByDepth[static_cast<std::size_t>(depth) + 1].clear();
            } else if (event == Json::parse_event_t::key && !keysByDepth[static_cast<std::size_t>(depth)].insert(parsed.get<std::string>()).second && duplicate.empty()) {
                duplicate = parsed.get<std::string>().substr(0, 40);
            }
            return true;
        },
        false);
    if (json.is_discarded()) {
        return fail("not valid JSON");
    }
    if (!duplicate.empty()) {
        return fail("duplicate key \"" + duplicate + "\"");
    }
    if (!json.is_array()) {
        return fail("expected an array of parasite entries, got " + std::string(typeName(json)));
    }
    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " entries");
    }

    std::vector<ParasiteEntry> entries;
    std::map<std::string, std::size_t, std::less<>> axiomIndex;
    std::map<std::string, std::size_t, std::less<>> idIndex;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        ParasiteEntry entry;
        for (const auto& [key, value] : item.items()) {
            if (key != "axiom" && key != "id" && key != "name") {
                add(at + "." + key.substr(0, 40), "unknown key");
            }
        }

        const auto stringField = [&](const char* key) -> const std::string* {
            const std::string path = at + "." + key;
            const auto it = item.find(key);
            if (it == item.end()) {
                add(path, "missing");
                return nullptr;
            }
            if (!it->is_string()) {
                add(path, "expected a string, got " + std::string(typeName(*it)));
                return nullptr;
            }
            return &it->get_ref<const std::string&>();
        };

        if (const std::string* axiom = stringField("axiom")) {
            if (!isLawAxiomId(*axiom)) {
                add(at + ".axiom", "expected a Law axiom id like \"law.<word>\", got " + shown(*axiom));
            } else if (const auto seen = axiomIndex.emplace(*axiom, i); !seen.second) {
                add(at + ".axiom", "duplicate of " + shown(*axiom) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                entry.axiom = *axiom;
            }
        }

        if (const std::string* id = stringField("id")) {
            if (id->size() > kMaxId) {
                add(at + ".id", "longer than " + std::to_string(kMaxId) + " bytes");
            } else if (!isWord(*id)) {
                add(at + ".id", "expected lowercase ascii letters, digits and underscores starting with a letter, got " + shown(*id));
            } else if (const auto seen = idIndex.emplace(*id, i); !seen.second) {
                add(at + ".id", "duplicate of " + shown(*id) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                entry.id = *id;
            }
        }

        if (const std::string* name = stringField("name")) {
            if (isBlank(*name)) {
                add(at + ".name", "must not be empty");
            } else if (name->size() > kMaxName) {
                add(at + ".name", "longer than " + std::to_string(kMaxName) + " bytes");
            } else if (name->find(kEmDash) != std::string::npos) {
                add(at + ".name", "must not contain an em-dash");
            } else {
                entry.name = *name;
            }
        }
        entries.push_back(std::move(entry));
    }

    if (problems.size() != before) {
        return false;
    }
    out = std::move(entries);
    return true;
}

std::optional<Faction> parasiteFor(const std::vector<ParasiteEntry>& mapping, std::string_view lawAxiomId)
{
    for (const ParasiteEntry& entry : mapping) {
        if (entry.axiom == lawAxiomId) {
            return Faction{entry.id, entry.name, FactionRole::Parasite};
        }
    }
    return std::nullopt;
}

std::vector<std::string> parasiteCoverageProblems(const std::vector<ParasiteEntry>& mapping, const std::vector<std::string>& lawAxiomIds)
{
    std::vector<std::string> problems;
    for (const std::string& axiom : lawAxiomIds) {
        if (!parasiteFor(mapping, axiom)) {
            problems.push_back("parasite mapping: no Parasite face for Law axiom " + shown(axiom));
        }
    }
    return problems;
}

}
