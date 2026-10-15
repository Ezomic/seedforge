#include "ichor/progression/capabilities.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <tuple>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxDescription = 400;
constexpr std::size_t kMaxEntries = 256;
constexpr std::size_t kMaxPrerequisites = 16;
constexpr std::uint32_t kStateVersion = 1;
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

    // Reads a required text field into `out`; returns false (after reporting) when it is unusable.
    bool text(const Json& item, const std::string& at, const char* key, std::size_t limit, std::string& out)
    {
        const std::string path = at + "." + key;
        const auto it = item.find(key);
        if (it == item.end()) {
            add(path, "missing");
            return false;
        }
        if (!isString(*it, path)) {
            return false;
        }
        const std::string& value = it->get_ref<const std::string&>();
        if (isBlank(value)) {
            add(path, "must not be empty");
        } else if (value.size() > limit) {
            add(path, "longer than " + std::to_string(limit) + " bytes");
        } else if (value.find(kEmDash) != std::string::npos) {
            add(path, "must not contain an em-dash");
        } else {
            out = value;
            return true;
        }
        return false;
    }

private:
    std::string_view label_;
    std::vector<std::string>& problems_;
};

std::string joinPath(const std::vector<std::string>& path)
{
    std::string out;
    for (const std::string& id : path) {
        out += out.empty() ? id : " -> " + id;
    }
    return out;
}

}

bool parseCapabilities(std::string_view label, std::string_view text, std::vector<Capability>& out, std::vector<std::string>& problems)
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
        return fail("expected an array of capabilities, got " + std::string(typeName(json)));
    }
    if (json.size() > kMaxEntries) {
        return fail("more than " + std::to_string(kMaxEntries) + " capabilities");
    }

    std::vector<Capability> table;
    std::map<std::string, std::size_t, std::less<>> firstIndex;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            check.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        Capability capability;
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "name" && key != "description" && key != "requires") {
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
                capability.id = id;
            }
        }
        check.text(item, at, "name", kMaxName, capability.name);
        check.text(item, at, "description", kMaxDescription, capability.description);

        if (const auto it = item.find("requires"); it != item.end()) {
            if (!it->is_array()) {
                check.add(at + ".requires", "expected an array, got " + std::string(typeName(*it)));
            } else if (it->size() > kMaxPrerequisites) {
                check.add(at + ".requires", "more than " + std::to_string(kMaxPrerequisites) + " prerequisites");
            } else {
                for (std::size_t p = 0; p < it->size(); ++p) {
                    const std::string path = at + ".requires[" + std::to_string(p) + "]";
                    if (!check.isString((*it)[p], path)) {
                        continue;
                    }
                    const std::string& required = (*it)[p].get_ref<const std::string&>();
                    if (std::find(capability.prerequisites.begin(), capability.prerequisites.end(), required) != capability.prerequisites.end()) {
                        check.add(path, "duplicate of " + shown(required));
                    } else {
                        capability.prerequisites.push_back(required);
                    }
                }
            }
        }
        table.push_back(std::move(capability));
    }

    for (std::size_t i = 0; i < table.size(); ++i) {
        for (std::size_t p = 0; p < table[i].prerequisites.size(); ++p) {
            const std::string& required = table[i].prerequisites[p];
            if (!firstIndex.contains(required)) {
                check.add("[" + std::to_string(i) + "].requires[" + std::to_string(p) + "]", "unknown capability " + shown(required));
            }
        }
    }

    if (const auto cycle = findPrerequisiteCycle(table); !cycle.empty()) {
        check.add("", "prerequisite cycle: " + joinPath(cycle));
    }

    if (problems.size() != before) {
        return false;
    }
    out = std::move(table);
    return true;
}

std::vector<std::string> findPrerequisiteCycle(const std::vector<Capability>& table)
{
    std::map<std::string, const Capability*> byId;
    for (const Capability& capability : table) {
        if (!capability.id.empty()) {
            byId.emplace(capability.id, &capability);
        }
    }

    enum class Mark { Open, Done };
    std::map<std::string, Mark> marks;
    std::vector<std::string> stack;
    std::vector<std::string> cycle;

    const std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        const auto found = byId.find(id);
        if (found == byId.end()) {
            return false;
        }
        if (const auto mark = marks.find(id); mark != marks.end()) {
            if (mark->second == Mark::Done) {
                return false;
            }
            const auto start = std::find(stack.begin(), stack.end(), id);
            cycle.assign(start, stack.end());
            cycle.push_back(id);
            return true;
        }
        marks[id] = Mark::Open;
        stack.push_back(id);
        for (const std::string& required : found->second->prerequisites) {
            if (visit(required)) {
                return true;
            }
        }
        stack.pop_back();
        marks[id] = Mark::Done;
        return false;
    };

    for (const auto& [id, capability] : byId) {
        if (visit(id)) {
            return cycle;
        }
    }
    return {};
}

std::string capabilitiesCanonicalJson(const std::vector<Capability>& table)
{
    std::vector<const Capability*> sorted;
    for (const Capability& capability : table) {
        sorted.push_back(&capability);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Capability* a, const Capability* b) {
        return std::tie(a->id, a->name, a->description, a->prerequisites) < std::tie(b->id, b->name, b->description, b->prerequisites);
    });

    Json json = Json::array();
    for (const Capability* capability : sorted) {
        Json item = Json::object();
        item["id"] = capability->id;
        item["name"] = capability->name;
        item["description"] = capability->description;
        std::vector<std::string> required = capability->prerequisites;
        std::sort(required.begin(), required.end());
        item["requires"] = required;
        json.push_back(std::move(item));
    }
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::uint64_t capabilitiesDigest(const std::vector<Capability>& table)
{
    return hash64(capabilitiesCanonicalJson(table));
}

CapabilityUnlocks::CapabilityUnlocks(std::vector<Capability> table)
    : table_(std::move(table))
{
    for (std::size_t i = 0; i < table_.size(); ++i) {
        index_.emplace(table_[i].id, i);
    }
}

bool CapabilityUnlocks::can(std::string_view id) const
{
    return unlocked_.find(id) != unlocked_.end();
}

UnlockResult CapabilityUnlocks::unlock(std::string_view id)
{
    const auto found = index_.find(id);
    if (found == index_.end()) {
        return UnlockResult::UnknownCapability;
    }
    if (can(id)) {
        return UnlockResult::AlreadyUnlocked;
    }
    const auto& prerequisites = table_[found->second].prerequisites;
    if (!std::all_of(prerequisites.begin(), prerequisites.end(), [&](const std::string& required) { return can(required); })) {
        return UnlockResult::PrerequisiteUnmet;
    }
    unlocked_.emplace(id);
    return UnlockResult::Unlocked;
}

std::vector<std::string> CapabilityUnlocks::unlocked() const
{
    return {unlocked_.begin(), unlocked_.end()};
}

std::uint64_t CapabilityUnlocks::digest() const
{
    std::uint64_t h = hash64("capability_unlocks");
    for (const std::string& id : unlocked_) {
        h = hash64(id, h);
        h = hash64("\n", h);
    }
    return h;
}

std::vector<std::uint8_t> CapabilityUnlocks::serialise() const
{
    BinaryWriter writer;
    writer.write(kStateVersion);
    writer.write(unlocked());
    return writer.take();
}

bool CapabilityUnlocks::deserialise(std::span<const std::uint8_t> bytes)
{
    BinaryReader reader(bytes);
    std::uint32_t version = 0;
    std::vector<std::string> ids;
    if (!reader.read(version) || version != kStateVersion || !reader.read(ids) || !reader.atEnd()) {
        return false;
    }

    std::set<std::string, std::less<>> loaded;
    for (const std::string& id : ids) {
        if (!index_.contains(id) || !loaded.insert(id).second) {
            return false;
        }
    }
    for (const std::string& id : loaded) {
        const auto& prerequisites = table_[index_.find(id)->second].prerequisites;
        if (!std::all_of(prerequisites.begin(), prerequisites.end(), [&](const std::string& required) { return loaded.contains(required); })) {
            return false;
        }
    }
    unlocked_ = std::move(loaded);
    return true;
}

}
