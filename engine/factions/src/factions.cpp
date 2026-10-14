#include "ichor/factions/factions.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxEntries = 64;
constexpr std::size_t kMinFactions = 5;
constexpr std::size_t kMaxFactions = 7;
constexpr std::size_t kMaxChallengers = 2;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

constexpr std::array<std::string_view, kFactionRoleCount> kRoleNames = {"incumbent", "challenger", "parasite", "custodian", "refusenik", "herald"};

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

    bool isString(const Json& json, const std::string& path)
    {
        if (!json.is_string()) {
            add(path, "expected a string, got " + std::string(typeName(json)));
            return false;
        }
        return true;
    }

private:
    std::string_view label_;
    std::vector<std::string>& problems_;
};

}

std::string_view factionRoleName(FactionRole role)
{
    return kRoleNames[static_cast<std::size_t>(role)];
}

std::optional<FactionRole> factionRoleFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kRoleNames.size(); ++i) {
        if (kRoleNames[i] == name) {
            return static_cast<FactionRole>(i);
        }
    }
    return std::nullopt;
}

bool parseFactions(std::string_view label, std::string_view text, std::vector<Faction>& out, std::vector<std::string>& problems)
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
        return fail("expected an array of factions, got " + std::string(typeName(json)));
    }

    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " factions");
    }

    std::vector<Faction> factions;
    std::map<std::string, std::size_t, std::less<>> firstIndex;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            check.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        Faction faction;
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "name" && key != "role") {
                check.add(at + "." + key.substr(0, 40), "unknown key");
            }
        }

        if (const auto it = item.find("id"); it == item.end()) {
            check.add(at + ".id", "missing");
        } else if (check.isString(*it, at + ".id")) {
            const std::string& id = it->get_ref<const std::string&>();
            if (id.size() > kMaxId) {
                check.add(at + ".id", "longer than " + std::to_string(kMaxId) + " bytes");
            } else if (!isWord(id)) {
                check.add(at + ".id", "expected lowercase ascii letters, digits and underscores starting with a letter, got " + shown(id));
            } else if (const auto seen = firstIndex.emplace(id, i); !seen.second) {
                check.add(at + ".id", "duplicate of " + shown(id) + " at [" + std::to_string(seen.first->second) + "]");
            } else {
                faction.id = id;
            }
        }

        if (const auto it = item.find("name"); it == item.end()) {
            check.add(at + ".name", "missing");
        } else if (check.isString(*it, at + ".name")) {
            const std::string& name = it->get_ref<const std::string&>();
            if (isBlank(name)) {
                check.add(at + ".name", "must not be empty");
            } else if (name.size() > kMaxName) {
                check.add(at + ".name", "longer than " + std::to_string(kMaxName) + " bytes");
            } else if (name.find(kEmDash) != std::string::npos) {
                check.add(at + ".name", "must not contain an em-dash");
            } else {
                faction.name = name;
            }
        }

        if (const auto it = item.find("role"); it == item.end()) {
            check.add(at + ".role", "missing");
        } else if (check.isString(*it, at + ".role")) {
            const std::string& name = it->get_ref<const std::string&>();
            if (const auto role = factionRoleFromName(name)) {
                faction.role = *role;
            } else {
                check.add(at + ".role", "expected one of incumbent, challenger, parasite, custodian, refusenik, herald, got " + shown(name));
            }
        }
        factions.push_back(std::move(faction));
    }

    if (problems.size() != before) {
        return false;
    }
    out = std::move(factions);
    return true;
}

std::string factionsCanonicalJson(const std::vector<Faction>& factions)
{
    std::vector<const Faction*> sorted;
    for (const Faction& faction : factions) {
        sorted.push_back(&faction);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Faction* a, const Faction* b) { return std::tie(a->id, a->name, a->role) < std::tie(b->id, b->name, b->role); });

    Json json = Json::array();
    for (const Faction* faction : sorted) {
        Json item = Json::object();
        item["id"] = faction->id;
        item["name"] = faction->name;
        item["role"] = std::string(factionRoleName(faction->role));
        json.push_back(std::move(item));
    }
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::uint64_t factionsDigest(const std::vector<Faction>& factions)
{
    return hash64(factionsCanonicalJson(factions));
}

std::vector<std::string> validateFactionSet(const std::vector<Faction>& factions)
{
    std::vector<std::string> violations;
    if (factions.size() < kMinFactions || factions.size() > kMaxFactions) {
        violations.push_back("a world has 5 to 7 factions, got " + std::to_string(factions.size()));
    }

    std::set<std::string> seen;
    std::set<std::string> reported;
    std::array<std::size_t, kFactionRoleCount> counts{};
    for (const Faction& faction : factions) {
        if (!seen.insert(faction.id).second && reported.insert(faction.id).second) {
            violations.push_back("duplicate faction id " + shown(faction.id));
        }
        ++counts[static_cast<std::size_t>(faction.role)];
    }

    for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
        const auto role = static_cast<FactionRole>(i);
        if (role == FactionRole::Incumbent) {
            continue;
        }
        if (counts[i] == 0) {
            violations.push_back("no " + std::string(factionRoleName(role)) + ": every world needs at least one");
        } else if (role == FactionRole::Challenger && counts[i] > kMaxChallengers) {
            violations.push_back("too many challengers: at most " + std::to_string(kMaxChallengers) + ", got " + std::to_string(counts[i]));
        }
    }
    return violations;
}

}
