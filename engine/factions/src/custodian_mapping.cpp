#include "ichor/factions/custodian_mapping.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxEntries = 64;
constexpr std::size_t kMaxCataclysms = 64;
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

// "cataclysm." then a word.
bool isCataclysmId(std::string_view text)
{
    constexpr std::string_view kPrefix = "cataclysm.";
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

std::optional<CustodianReward> rewardFromName(std::string_view name)
{
    if (name == "secret") {
        return CustodianReward::Secret;
    }
    if (name == "map_knowledge") {
        return CustodianReward::MapKnowledge;
    }
    return std::nullopt;
}

}

std::string_view custodianRewardName(CustodianReward reward)
{
    return reward == CustodianReward::Secret ? "secret" : "map_knowledge";
}

bool parseCustodianMapping(std::string_view label, std::string_view text, std::vector<CustodianEntry>& out, std::vector<std::string>& problems)
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
        return fail("expected an array of custodian entries, got " + std::string(typeName(json)));
    }
    if (json.empty()) {
        return fail("no entries: every Cataclysm needs a Custodian");
    }
    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " entries");
    }

    std::vector<CustodianEntry> entries;
    std::map<std::string, std::size_t, std::less<>> idAt;
    std::map<std::string, std::string, std::less<>> cataclysmAt;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            check.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "name" && key != "reward" && key != "cataclysms") {
                check.add(at + "." + key.substr(0, 40), "unknown key");
            }
        }

        CustodianEntry entry;
        entry.faction.role = FactionRole::Custodian;
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
        if (const std::string* reward = check.text(item, at, "reward")) {
            if (const auto parsed = rewardFromName(*reward)) {
                entry.reward = *parsed;
            } else {
                check.add(at + ".reward", "expected \"secret\" or \"map_knowledge\", got " + shown(*reward));
            }
        }

        const auto list = item.find("cataclysms");
        if (list == item.end()) {
            check.add(at + ".cataclysms", "missing");
        } else if (!list->is_array()) {
            check.add(at + ".cataclysms", "expected an array, got " + std::string(typeName(*list)));
        } else if (list->empty()) {
            check.add(at + ".cataclysms", "must not be empty");
        } else if (list->size() > kMaxCataclysms) {
            check.add(at + ".cataclysms", "more than " + std::to_string(kMaxCataclysms) + " entries");
        } else {
            for (std::size_t j = 0; j < list->size(); ++j) {
                const std::string path = at + ".cataclysms[" + std::to_string(j) + "]";
                const Json& value = (*list)[j];
                if (!value.is_string()) {
                    check.add(path, "expected a string, got " + std::string(typeName(value)));
                    continue;
                }
                const std::string& axiom = value.get_ref<const std::string&>();
                if (axiom.size() > kMaxId || !isCataclysmId(axiom)) {
                    check.add(path, "expected a Cataclysm axiom id like \"cataclysm.flood\", got " + shown(axiom));
                } else if (const auto seen = cataclysmAt.emplace(axiom, at); !seen.second) {
                    check.add(path, "duplicate of " + shown(axiom) + " claimed at " + seen.first->second);
                } else {
                    entry.cataclysms.push_back(axiom);
                }
            }
        }
        entries.push_back(std::move(entry));
    }

    if (problems.size() != before) {
        return false;
    }
    std::sort(entries.begin(), entries.end(), [](const CustodianEntry& a, const CustodianEntry& b) { return a.faction.id < b.faction.id; });
    for (CustodianEntry& entry : entries) {
        std::sort(entry.cataclysms.begin(), entry.cataclysms.end());
    }
    out = std::move(entries);
    return true;
}

const CustodianEntry* custodianFor(const std::vector<CustodianEntry>& mapping, std::string_view cataclysmAxiomId)
{
    for (const CustodianEntry& entry : mapping) {
        if (std::binary_search(entry.cataclysms.begin(), entry.cataclysms.end(), cataclysmAxiomId, [](std::string_view a, std::string_view b) { return a < b; })) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<std::string> custodianCoverageProblems(const std::vector<CustodianEntry>& mapping, const std::vector<std::string>& cataclysmAxiomIds)
{
    std::vector<std::string> problems;
    for (const std::string& id : cataclysmAxiomIds) {
        if (custodianFor(mapping, id) == nullptr) {
            problems.push_back("no Custodian for " + shown(id));
        }
    }
    return problems;
}

}
