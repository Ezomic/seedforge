#include "ichor/factions/standing.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 1024 * 1024;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxName = 120;
constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxEntries = 64;
constexpr std::uint32_t kFormatVersion = 1;
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

class Report {
public:
    Report(std::string_view label, std::vector<std::string>& problems)
        : label_(label)
        , problems_(problems)
        , before_(problems.size())
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

    bool clean() const { return problems_.size() == before_; }

private:
    std::string_view label_;
    std::vector<std::string>& problems_;
    std::size_t before_;
};

// Parses `text` to an array of at most kMaxEntries, reporting a document-level problem otherwise.
bool parseArray(std::string_view text, Report& report, Json& out)
{
    if (text.size() > kMaxFileBytes) {
        report.add("", "file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
        return false;
    }
    if (isBlank(text)) {
        report.add("", "empty file");
        return false;
    }
    if (nestsTooDeep(text)) {
        report.add("", "nested deeper than " + std::to_string(kMaxDepth) + " levels");
        return false;
    }
    std::vector<std::set<std::string>> keysByDepth;
    std::string duplicate;
    Json json = Json::parse(
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
        report.add("", "not valid JSON");
        return false;
    }
    if (!duplicate.empty()) {
        report.add("", "duplicate key \"" + duplicate + "\"");
        return false;
    }
    if (!json.is_array()) {
        report.add("", "expected an array, got " + std::string(typeName(json)));
        return false;
    }
    if (json.size() > kMaxEntries) {
        report.add("", "more than " + std::to_string(kMaxEntries) + " entries");
        return false;
    }
    out = std::move(json);
    return true;
}

// Reads item[key] as a string into `out`, reporting a problem and returning false otherwise.
bool readString(const Json& item, const std::string& at, const char* key, Report& report, std::string& out)
{
    const std::string path = at + "." + key;
    const auto it = item.find(key);
    if (it == item.end()) {
        report.add(path, "missing");
        return false;
    }
    if (!it->is_string()) {
        report.add(path, "expected a string, got " + std::string(typeName(*it)));
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readId(const Json& item, const std::string& at, Report& report, std::string& out)
{
    std::string id;
    if (!readString(item, at, "id", report, id)) {
        return false;
    }
    if (id.size() > kMaxId) {
        report.add(at + ".id", "longer than " + std::to_string(kMaxId) + " bytes");
        return false;
    }
    if (!isWord(id)) {
        report.add(at + ".id", "expected lowercase ascii letters, digits and underscores starting with a letter, got " + shown(id));
        return false;
    }
    out = std::move(id);
    return true;
}

bool readName(const Json& item, const std::string& at, Report& report, std::string& out)
{
    std::string name;
    if (!readString(item, at, "name", report, name)) {
        return false;
    }
    if (isBlank(name)) {
        report.add(at + ".name", "must not be empty");
        return false;
    }
    if (name.size() > kMaxName) {
        report.add(at + ".name", "longer than " + std::to_string(kMaxName) + " bytes");
        return false;
    }
    if (name.find(kEmDash) != std::string::npos) {
        report.add(at + ".name", "must not contain an em-dash");
        return false;
    }
    out = std::move(name);
    return true;
}

void rejectUnknownKeys(const Json& item, const std::string& at, std::initializer_list<std::string_view> known, Report& report)
{
    for (const auto& [key, value] : item.items()) {
        if (std::find(known.begin(), known.end(), key) == known.end()) {
            report.add(at + "." + key.substr(0, 40), "unknown key");
        }
    }
}

std::size_t tierIndexOf(const std::vector<StandingTier>& tiers, std::int32_t value)
{
    std::size_t index = 0;
    for (std::size_t i = 0; i < tiers.size(); ++i) {
        if (tiers[i].threshold <= value) {
            index = i;
        }
    }
    return index;
}

}

bool parseStandingTiers(std::string_view label, std::string_view text, std::vector<StandingTier>& out, std::vector<std::string>& problems)
{
    Report report(label, problems);
    Json json;
    if (!parseArray(text, report, json)) {
        return false;
    }
    if (json.empty()) {
        report.add("", "expected at least one tier");
        return false;
    }

    std::vector<StandingTier> tiers;
    std::set<std::string> ids;
    bool havePrevious = false;
    std::int64_t previous = 0;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            report.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        rejectUnknownKeys(item, at, {"id", "name", "threshold"}, report);
        StandingTier tier;
        if (readId(item, at, report, tier.id) && !ids.insert(tier.id).second) {
            report.add(at + ".id", "duplicate of " + shown(tier.id));
        }
        readName(item, at, report, tier.name);

        const auto it = item.find("threshold");
        if (it == item.end()) {
            report.add(at + ".threshold", "missing");
        } else if (!it->is_number_integer()) {
            report.add(at + ".threshold", "expected an integer, got " + std::string(typeName(*it)));
        } else if (it->is_number_unsigned() && it->get<std::uint64_t>() > static_cast<std::uint64_t>(kStandingMax)) {
            report.add(at + ".threshold", "outside " + std::to_string(kStandingMin) + " to " + std::to_string(kStandingMax));
        } else {
            const std::int64_t value = it->is_number_unsigned() ? static_cast<std::int64_t>(it->get<std::uint64_t>()) : it->get<std::int64_t>();
            if (value < kStandingMin || value > kStandingMax) {
                report.add(at + ".threshold", "outside " + std::to_string(kStandingMin) + " to " + std::to_string(kStandingMax));
            } else {
                tier.threshold = static_cast<std::int32_t>(value);
            }
            if (havePrevious && value <= previous) {
                report.add(at + ".threshold", "thresholds must be strictly ascending");
            } else if (!havePrevious && value != kStandingMin) {
                report.add(at + ".threshold", "the first tier must start at " + std::to_string(kStandingMin));
            }
            havePrevious = true;
            previous = value;
        }
        tiers.push_back(std::move(tier));
    }

    if (!report.clean()) {
        return false;
    }
    out = std::move(tiers);
    return true;
}

bool parseStandingGates(std::string_view label, std::string_view text, const std::vector<StandingTier>& tiers, const std::vector<Faction>& factions,
    std::vector<StandingGate>& out, std::vector<std::string>& problems)
{
    Report report(label, problems);
    Json json;
    if (!parseArray(text, report, json)) {
        return false;
    }

    std::vector<StandingGate> gates;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            report.add(at, "expected an object, got " + std::string(typeName(item)));
            continue;
        }
        rejectUnknownKeys(item, at, {"id", "faction", "min_tier"}, report);
        StandingGate gate;
        if (readId(item, at, report, gate.id) && !ids.insert(gate.id).second) {
            report.add(at + ".id", "duplicate of " + shown(gate.id));
        }
        if (readString(item, at, "faction", report, gate.faction)
            && std::none_of(factions.begin(), factions.end(), [&](const Faction& f) { return f.id == gate.faction; })) {
            report.add(at + ".faction", "unknown faction " + shown(gate.faction));
        }
        if (readString(item, at, "min_tier", report, gate.minTier)
            && std::none_of(tiers.begin(), tiers.end(), [&](const StandingTier& t) { return t.id == gate.minTier; })) {
            report.add(at + ".min_tier", "unknown tier " + shown(gate.minTier));
        }
        gates.push_back(std::move(gate));
    }

    if (!report.clean()) {
        return false;
    }
    out = std::move(gates);
    return true;
}

FactionStanding::FactionStanding(const std::vector<Faction>& factions, std::vector<StandingTier> tiers, std::vector<StandingGate> gates)
    : tiers_(std::move(tiers))
{
    for (const Faction& faction : factions) {
        values_.emplace(faction.id, 0);
    }
    for (StandingGate& gate : gates) {
        std::string id = gate.id;
        gates_.emplace(std::move(id), std::move(gate));
    }
}

std::optional<std::int32_t> FactionStanding::standing(std::string_view faction) const
{
    const auto it = values_.find(faction);
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

const StandingTier* FactionStanding::tier(std::string_view faction) const
{
    const auto it = values_.find(faction);
    if (it == values_.end() || tiers_.empty()) {
        return nullptr;
    }
    return &tiers_[tierIndexOf(tiers_, it->second)];
}

std::optional<StandingChange> FactionStanding::change(std::string_view faction, std::int32_t delta)
{
    const auto it = values_.find(faction);
    if (it == values_.end()) {
        return std::nullopt;
    }
    const std::int64_t sum = static_cast<std::int64_t>(it->second) + delta;
    StandingChange result{it->first, it->second, static_cast<std::int32_t>(std::clamp<std::int64_t>(sum, kStandingMin, kStandingMax))};
    it->second = result.newValue;
    return result;
}

bool FactionStanding::allowed(std::string_view gate) const
{
    const auto found = gates_.find(gate);
    const auto value = found == gates_.end() ? values_.end() : values_.find(found->second.faction);
    if (value == values_.end()) {
        return false;
    }
    const auto minimum = std::find_if(tiers_.begin(), tiers_.end(), [&](const StandingTier& t) { return t.id == found->second.minTier; });
    if (minimum == tiers_.end()) {
        return false;
    }
    return tierIndexOf(tiers_, value->second) >= static_cast<std::size_t>(minimum - tiers_.begin());
}

std::vector<std::uint8_t> FactionStanding::serialise() const
{
    BinaryWriter writer;
    writer.write(kFormatVersion);
    writer.write(static_cast<std::uint32_t>(values_.size()));
    for (const auto& [id, value] : values_) {
        writer.write(id);
        writer.write(value);
    }
    return writer.take();
}

std::uint64_t FactionStanding::digest() const
{
    const auto bytes = serialise();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool FactionStanding::deserialise(std::span<const std::uint8_t> bytes)
{
    BinaryReader reader(bytes);
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!reader.read(version) || version != kFormatVersion || !reader.read(count) || count != values_.size()) {
        return false;
    }
    std::map<std::string, std::int32_t, std::less<>> loaded;
    for (const auto& [expected, unused] : values_) {
        std::string id;
        std::int32_t value = 0;
        if (!reader.read(id) || id != expected || !reader.read(value) || value < kStandingMin || value > kStandingMax) {
            return false;
        }
        loaded.emplace(std::move(id), value);
    }
    if (!reader.atEnd()) {
        return false;
    }
    values_ = std::move(loaded);
    return true;
}

}
