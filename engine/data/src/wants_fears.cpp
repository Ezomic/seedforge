#include "ichor/data/wants_fears.h"

#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxRecords = 5000;
constexpr std::size_t kMaxWord = 64;

constexpr std::array<std::pair<std::string_view, CompareOp>, 5> kOps = {{
    {"<", CompareOp::Lt}, {"<=", CompareOp::Le}, {"==", CompareOp::Eq}, {">=", CompareOp::Ge}, {">", CompareOp::Gt},
}};

constexpr std::array<std::string_view, 7> kKeys = { "id", "kind", "subject", "intensity", "tag", "op", "value" };

// [a-z][a-z0-9_.]*
bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && text.size() <= kMaxWord && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_' || c == '.'; });
}

std::string shown(std::string_view text)
{
    return "\"" + std::string(text.substr(0, 40)) + (text.size() > 40 ? "...\"" : "\"");
}

class Reader {
public:
    Reader(std::string_view label, std::vector<std::string>& problems)
        : label_(label)
        , problems_(problems)
    {
    }

    void add(const std::string& path, const std::string& message) { problems_.push_back(std::string(label_) + ": " + path + ": " + message); }

    bool desire(const Json& item, const std::string& path, Desire& out)
    {
        if (!item.is_object()) {
            add(path, "expected an object");
            return false;
        }
        bool ok = true;
        for (const auto& [key, value] : item.items()) {
            if (std::find(kKeys.begin(), kKeys.end(), key) == kKeys.end()) {
                add(path + "." + key.substr(0, 40), "unknown key");
                ok = false;
            }
        }
        ok = word(item, path, "id", out.id) && ok;
        ok = word(item, path, "subject", out.subject) && ok;
        ok = word(item, path, "tag", out.condition.tag) && ok;
        ok = kind(item, path, out.kind) && ok;
        ok = intensity(item, path, out.intensity) && ok;
        ok = op(item, path, out.condition.op) && ok;
        ok = value(item, path, out.condition.value) && ok;
        return ok;
    }

private:
    const Json* field(const Json& item, const std::string& path, const char* key)
    {
        const auto it = item.find(key);
        if (it == item.end()) {
            add(path + "." + key, "missing");
            return nullptr;
        }
        return &*it;
    }

    bool word(const Json& item, const std::string& path, const char* key, std::string& out)
    {
        const Json* json = field(item, path, key);
        if (json == nullptr) {
            return false;
        }
        const std::string where = path + "." + key;
        if (!json->is_string()) {
            add(where, "expected a string");
            return false;
        }
        const std::string& text = json->get_ref<const std::string&>();
        if (!isWord(text)) {
            add(where, "expected a lowercase ascii id of 1 to " + std::to_string(kMaxWord) + " characters (letters, digits, underscores, dots), got " + shown(text));
            return false;
        }
        out = text;
        return true;
    }

    bool kind(const Json& item, const std::string& path, DesireKind& out)
    {
        const Json* json = field(item, path, "kind");
        if (json == nullptr) {
            return false;
        }
        for (const DesireKind candidate : { DesireKind::Want, DesireKind::Fear }) {
            if (json->is_string() && json->get_ref<const std::string&>() == desireKindName(candidate)) {
                out = candidate;
                return true;
            }
        }
        add(path + ".kind", "expected \"want\" or \"fear\"");
        return false;
    }

    bool intensity(const Json& item, const std::string& path, int& out)
    {
        const Json* json = field(item, path, "intensity");
        if (json == nullptr) {
            return false;
        }
        if (!json->is_number_integer() || *json < kMinIntensity || *json > kMaxIntensity) {
            add(path + ".intensity", "expected an integer from " + std::to_string(kMinIntensity) + " to " + std::to_string(kMaxIntensity));
            return false;
        }
        out = json->get<int>();
        return true;
    }

    bool op(const Json& item, const std::string& path, CompareOp& out)
    {
        const Json* json = field(item, path, "op");
        if (json == nullptr) {
            return false;
        }
        if (json->is_string()) {
            for (const auto& [name, value] : kOps) {
                if (json->get_ref<const std::string&>() == name) {
                    out = value;
                    return true;
                }
            }
        }
        add(path + ".op", "expected one of <, <=, ==, >=, >");
        return false;
    }

    bool value(const Json& item, const std::string& path, std::int64_t& out)
    {
        const Json* json = field(item, path, "value");
        if (json == nullptr) {
            return false;
        }
        constexpr auto kMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (!json->is_number_integer() || (json->is_number_unsigned() && json->get<std::uint64_t>() > kMax)) {
            add(path + ".value", "expected a 64-bit integer");
            return false;
        }
        out = json->get<std::int64_t>();
        return true;
    }

    std::string_view label_;
    std::vector<std::string>& problems_;
};

}

std::string_view desireKindName(DesireKind kind)
{
    return kind == DesireKind::Want ? "want" : "fear";
}

std::string_view compareOpName(CompareOp op)
{
    for (const auto& [name, value] : kOps) {
        if (value == op) {
            return name;
        }
    }
    return "==";
}

bool parseDesires(std::string_view label, std::string_view text, std::vector<Desire>& out, std::vector<std::string>& problems)
{
    Reader reader(label, problems);
    const std::size_t before = problems.size();
    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        reader.add("$", "not valid JSON");
        return false;
    }
    if (!json.is_array()) {
        reader.add("$", "expected an array of records");
        return false;
    }
    if (json.size() > kMaxRecords) {
        reader.add("$", "more than " + std::to_string(kMaxRecords) + " records");
        return false;
    }

    std::vector<Desire> parsed;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string path = "[" + std::to_string(i) + "]";
        Desire desire;
        const bool valid = reader.desire(json[i], path, desire);
        if (!desire.id.empty() && !seen.insert(desire.id).second) {
            reader.add(path + ".id", "duplicate " + shown(desire.id));
        } else if (valid) {
            parsed.push_back(std::move(desire));
        }
    }
    if (problems.size() != before) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool conditionHolds(const Condition& condition, const WorldSnapshot& snapshot)
{
    const auto it = snapshot.find(condition.tag);
    if (it == snapshot.end()) {
        return false;
    }
    switch (condition.op) {
    case CompareOp::Lt:
        return it->second < condition.value;
    case CompareOp::Le:
        return it->second <= condition.value;
    case CompareOp::Eq:
        return it->second == condition.value;
    case CompareOp::Ge:
        return it->second >= condition.value;
    case CompareOp::Gt:
        return it->second > condition.value;
    }
    return false;
}

std::vector<Desire> activeDesires(const std::vector<Desire>& desires, const WorldSnapshot& snapshot)
{
    std::vector<Desire> active;
    std::copy_if(desires.begin(), desires.end(), std::back_inserter(active), [&](const Desire& d) { return conditionHolds(d.condition, snapshot); });
    std::sort(active.begin(), active.end(), [](const Desire& a, const Desire& b) {
        return a.intensity != b.intensity ? a.intensity > b.intensity : a.id < b.id;
    });
    return active;
}

std::string desireCanonicalJson(const Desire& desire)
{
    Json json = Json::object();
    json["id"] = desire.id;
    json["kind"] = std::string(desireKindName(desire.kind));
    json["subject"] = desire.subject;
    json["intensity"] = desire.intensity;
    json["tag"] = desire.condition.tag;
    json["op"] = std::string(compareOpName(desire.condition.op));
    json["value"] = desire.condition.value;
    return json.dump();
}

std::uint64_t desireDigest(const Desire& desire)
{
    return hash64(desireCanonicalJson(desire));
}

std::uint64_t desiresDigest(const std::vector<Desire>& desires)
{
    std::map<std::string, std::uint64_t> byId;
    for (const Desire& d : desires) {
        byId[d.id] = desireDigest(d);
    }
    std::uint64_t h = 0;
    for (const auto& [id, digest] : byId) {
        h = hashCombine(hashCombine(h, hash64(id)), digest);
    }
    return h;
}

}
