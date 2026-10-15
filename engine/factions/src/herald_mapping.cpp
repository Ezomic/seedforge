#include "ichor/factions/herald_mapping.h"

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

bool isLower(char c)
{
    return c >= 'a' && c <= 'z';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    return !text.empty() && isLower(text[0]) && std::all_of(text.begin(), text.end(), [](char c) { return isLower(c) || isDigit(c) || c == '_'; });
}

// "pressure." then a word.
bool isPressureId(std::string_view text)
{
    constexpr std::string_view kPrefix = "pressure.";
    return text.substr(0, kPrefix.size()) == kPrefix && isWord(text.substr(kPrefix.size()));
}

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

class Checker {
public:
    Checker(std::string_view label, std::vector<std::string>& problems)
        : label_(label)
        , problems_(problems)
    {
    }

    void add(const std::string& path, const std::string& message)
    {
        std::string line(label_);
        line += ": ";
        if (!path.empty()) {
            line += path + ": ";
        }
        problems_.push_back(line + message);
    }

    // The string at `key` of `item`, or nullptr after reporting why it is unusable.
    const std::string* text(const Json& item, const std::string& at, const char* key)
    {
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
    }

private:
    std::string_view label_;
    std::vector<std::string>& problems_;
};

}

bool parseHeraldMapping(std::string_view label, std::string_view text, std::vector<HeraldEntry>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    Checker check(label, problems);
    const auto fail = [&](const std::string& message) {
        check.add("", message);
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
        return fail("expected an array of herald entries, got " + std::string(typeName(json)));
    }
    if (json.empty()) {
        return fail("no entries: every Pressure axiom needs a Herald");
    }
    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " entries");
    }

    std::vector<HeraldEntry> entries;
    std::map<std::string, std::size_t, std::less<>> idAt;
    std::map<std::string, std::size_t, std::less<>> pressureAt;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            check.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "pressure" && key != "name") {
                check.add(at + "." + key.substr(0, 40), "unknown key");
            }
        }

        HeraldEntry entry;
        entry.faction.role = FactionRole::Herald;
        if (const std::string* id = check.text(item, at, "id")) {
            if (id->size() > kMaxId) {
                check.add(at + ".id", "longer than " + std::to_string(kMaxId) + " bytes");
            } else if (!isWord(*id)) {
                check.add(at + ".id", "expected lowercase ascii letters, digits and underscores starting with a letter, got " + shown(*id));
            } else if (const auto seen = idAt.emplace(*id, i); !seen.second) {
                check.add(at + ".id", "duplicate of " + shown(*id) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                entry.faction.id = *id;
            }
        }
        if (const std::string* pressure = check.text(item, at, "pressure")) {
            if (pressure->size() > kMaxId) {
                check.add(at + ".pressure", "longer than " + std::to_string(kMaxId) + " bytes");
            } else if (!isPressureId(*pressure)) {
                check.add(at + ".pressure", "expected a Pressure axiom id like \"pressure.miasma\", got " + shown(*pressure));
            } else if (const auto seen = pressureAt.emplace(*pressure, i); !seen.second) {
                check.add(at + ".pressure", "duplicate of " + shown(*pressure) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                entry.pressure = *pressure;
            }
        }
        if (const std::string* name = check.text(item, at, "name")) {
            if (isBlank(*name)) {
                check.add(at + ".name", "must not be empty");
            } else if (name->size() > kMaxName) {
                check.add(at + ".name", "longer than " + std::to_string(kMaxName) + " bytes");
            } else if (name->find(kEmDash) != std::string::npos) {
                check.add(at + ".name", "must not contain an em-dash");
            } else {
                entry.faction.name = *name;
            }
        }
        entries.push_back(std::move(entry));
    }

    if (problems.size() != before) {
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const HeraldEntry& a, const HeraldEntry& b) { return a.pressure < b.pressure; });
    out = std::move(entries);
    return true;
}

const Faction* heraldFor(const std::vector<HeraldEntry>& mapping, std::string_view pressureAxiomId)
{
    const auto it = std::lower_bound(mapping.begin(), mapping.end(), pressureAxiomId, [](const HeraldEntry& entry, std::string_view id) { return entry.pressure < id; });
    return it != mapping.end() && it->pressure == pressureAxiomId ? &it->faction : nullptr;
}

}
