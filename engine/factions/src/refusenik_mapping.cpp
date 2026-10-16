#include "ichor/factions/refusenik_mapping.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace ichor {

std::string_view refusenikPlacementName(RefusenikPlacement placement)
{
    return placement == RefusenikPlacement::MarginalLand ? "marginal_land" : "routes";
}

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxText = 400;
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

// [a-z][a-z0-9_]*
bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
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

bool parseRefusenikMapping(std::string_view label, std::string_view text, std::vector<RefusenikEntry>& out, std::vector<std::string>& problems)
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
        return fail("expected an array of refusenik entries, got " + std::string(typeName(json)));
    }
    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " entries");
    }

    std::vector<RefusenikEntry> entries;
    std::map<std::string, std::size_t, std::less<>> idIndex;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        RefusenikEntry entry;
        entry.faction.role = FactionRole::Refusenik;
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "name" && key != "placement" && key != "claims_territory" && key != "offers_services_anywhere" && key != "summary") {
                add(at + "." + key.substr(0, 40), "unknown key");
            }
        }

        const auto field = [&](const char* key, bool wantBool) -> const Json* {
            const std::string path = at + "." + key;
            const auto it = item.find(key);
            if (it == item.end()) {
                add(path, "missing");
                return nullptr;
            }
            if (wantBool ? !it->is_boolean() : !it->is_string()) {
                add(path, std::string("expected a ") + (wantBool ? "boolean" : "string") + ", got " + std::string(typeName(*it)));
                return nullptr;
            }
            return &*it;
        };
        const auto prose = [&](const char* key, std::size_t limit, std::string& target) {
            const Json* value = field(key, false);
            if (value == nullptr) {
                return;
            }
            const std::string& str = value->get_ref<const std::string&>();
            const std::string path = at + "." + key;
            if (isBlank(str)) {
                add(path, "must not be empty");
            } else if (str.size() > limit) {
                add(path, "longer than " + std::to_string(limit) + " bytes");
            } else if (std::any_of(str.begin(), str.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; })) {
                add(path, "must not contain control characters");
            } else if (str.find(kEmDash) != std::string::npos) {
                add(path, "must not contain an em-dash");
            } else {
                target = str;
            }
        };

        if (const Json* id = field("id", false)) {
            const std::string& str = id->get_ref<const std::string&>();
            if (str.size() > kMaxId) {
                add(at + ".id", "longer than " + std::to_string(kMaxId) + " bytes");
            } else if (!isWord(str)) {
                add(at + ".id", "expected lowercase ascii letters, digits and underscores starting with a letter, got " + shown(str));
            } else if (const auto seen = idIndex.emplace(str, i); !seen.second) {
                add(at + ".id", "duplicate of " + shown(str) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                entry.faction.id = str;
            }
        }
        prose("name", kMaxName, entry.faction.name);
        prose("summary", kMaxText, entry.summary);
        if (const Json* placement = field("placement", false)) {
            const std::string& str = placement->get_ref<const std::string&>();
            if (str == "marginal_land") {
                entry.placement = RefusenikPlacement::MarginalLand;
            } else if (str == "routes") {
                entry.placement = RefusenikPlacement::Routes;
            } else {
                add(at + ".placement", "expected \"marginal_land\" or \"routes\", got " + shown(str));
            }
        }
        if (const Json* flag = field("claims_territory", true)) {
            entry.claimsTerritory = flag->get<bool>();
        }
        if (const Json* flag = field("offers_services_anywhere", true)) {
            entry.offersServicesAnywhere = flag->get<bool>();
        }
        entries.push_back(std::move(entry));
    }

    if (problems.size() != before) {
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const RefusenikEntry& a, const RefusenikEntry& b) { return a.faction.id < b.faction.id; });
    out = std::move(entries);
    return true;
}

const RefusenikEntry* refusenikFor(const std::vector<RefusenikEntry>& mapping, std::string_view id)
{
    for (const RefusenikEntry& entry : mapping) {
        if (entry.faction.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const RefusenikEntry*> servicesAnywhereRefuseniks(const std::vector<RefusenikEntry>& mapping)
{
    std::vector<const RefusenikEntry*> available;
    for (const RefusenikEntry& entry : mapping) {
        if (entry.offersServicesAnywhere) {
            available.push_back(&entry);
        }
    }
    return available;
}

}
