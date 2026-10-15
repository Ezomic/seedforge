#include "ichor/progression/power_growth.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 64 * 1024;
constexpr int kMaxDepth = 8;
constexpr std::int64_t kMaxValue = 1'000'000'000;
constexpr std::int64_t kWeightTotal = 1000;
constexpr std::array<std::string_view, 4> kSpineIds{"attunement", "carry", "health", "stamina"};

std::string line(std::string_view label, const std::string& path, const std::string& message)
{
    std::string out(label);
    out += ": ";
    if (!path.empty()) {
        out += path + ": ";
    }
    return out + message;
}

bool isBlank(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

// Counted before parsing, so a hostile file never reaches a recursive walk.
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
        } else if ((c == '[' || c == '{') && ++depth > kMaxDepth) {
            return true;
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

std::string shown(std::string_view text)
{
    return "\"" + std::string(text.substr(0, 40)) + (text.size() > 40 ? "...\"" : "\"");
}

// Reads a required integer in [0, kMaxValue]; floats, strings and huge values are reported, never cast.
bool readInt(const Json& item, const std::string& at, const char* key, std::vector<std::string>& problems, std::string_view label, std::int64_t& out)
{
    const std::string path = at + "." + key;
    const auto it = item.find(key);
    if (it == item.end()) {
        problems.push_back(line(label, path, "missing"));
        return false;
    }
    if (!it->is_number_integer()) {
        problems.push_back(line(label, path, "expected a whole number"));
        return false;
    }
    const bool unsignedValue = it->is_number_unsigned();
    const bool inRange = unsignedValue ? it->get<std::uint64_t>() <= static_cast<std::uint64_t>(kMaxValue)
                                       : it->get<std::int64_t>() >= 0 && it->get<std::int64_t>() <= kMaxValue;
    if (!inRange) {
        problems.push_back(line(label, path, "outside 0 to " + std::to_string(kMaxValue)));
        return false;
    }
    out = unsignedValue ? static_cast<std::int64_t>(it->get<std::uint64_t>()) : it->get<std::int64_t>();
    return true;
}

}

std::int64_t statGrowthPermille(const SpineStat& stat)
{
    if (stat.start <= 0 || stat.start > kMaxValue || stat.end < 0 || stat.end > kMaxValue) {
        return 0;
    }
    return stat.end * 1000 / stat.start;
}

std::int64_t totalGrowthPermille(const std::vector<SpineStat>& stats)
{
    std::int64_t weights = 0;
    std::int64_t sum = 0;
    for (const SpineStat& stat : stats) {
        if (stat.weightPermille < 0 || stat.weightPermille > kWeightTotal) {
            return 0;
        }
        weights += stat.weightPermille;
        sum += stat.weightPermille * statGrowthPermille(stat);
    }
    return weights == kWeightTotal ? sum / kWeightTotal : 0;
}

void checkPowerGrowthCap(std::string_view label, const std::vector<SpineStat>& stats, std::vector<std::string>& problems)
{
    std::int64_t weights = 0;
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const SpineStat& stat = stats[i];
        const std::int64_t growth = statGrowthPermille(stat);
        if (growth < kSpineMinGrowthPermille || growth > kSpineMaxGrowthPermille) {
            problems.push_back(line(label, "[" + std::to_string(i) + "]", shown(stat.id) + " grows " + std::to_string(growth) + " permille, expected " + std::to_string(kSpineMinGrowthPermille) + " to " + std::to_string(kSpineMaxGrowthPermille)));
        }
        if (stat.weightPermille < 0 || stat.weightPermille > kWeightTotal) {
            problems.push_back(line(label, "[" + std::to_string(i) + "]", "weight outside 0 to " + std::to_string(kWeightTotal)));
        }
        weights += std::clamp<std::int64_t>(stat.weightPermille, -kMaxValue, kMaxValue);
    }
    if (weights != kWeightTotal) {
        problems.push_back(line(label, "", "weights sum to " + std::to_string(weights) + ", expected " + std::to_string(kWeightTotal)));
    } else if (const std::int64_t total = totalGrowthPermille(stats); total > kPowerCapPermille) {
        problems.push_back(line(label, "", "total power grows " + std::to_string(total) + " permille, over the cap of " + std::to_string(kPowerCapPermille)));
    }
}

bool parsePowerGrowth(std::string_view label, std::string_view text, std::vector<SpineStat>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    const auto fail = [&](const std::string& message) {
        problems.push_back(line(label, "", message));
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
    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        return fail("not valid JSON");
    }
    if (!json.is_array()) {
        return fail("expected an array of stats");
    }
    if (json.size() > kSpineIds.size()) {
        return fail("more than " + std::to_string(kSpineIds.size()) + " stats");
    }

    std::vector<SpineStat> stats;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const std::string at = "[" + std::to_string(i) + "]";
        const Json& item = json[i];
        if (!item.is_object()) {
            problems.push_back(line(label, at, "expected an object"));
            continue;
        }
        for (const auto& [key, value] : item.items()) {
            if (key != "id" && key != "start" && key != "end" && key != "weight_permille") {
                problems.push_back(line(label, at + "." + key.substr(0, 40), "unknown key"));
            }
        }
        SpineStat stat;
        if (const auto it = item.find("id"); it == item.end() || !it->is_string()) {
            problems.push_back(line(label, at + ".id", "expected a string"));
        } else {
            stat.id = it->get<std::string>();
            if (std::find(kSpineIds.begin(), kSpineIds.end(), stat.id) == kSpineIds.end()) {
                problems.push_back(line(label, at + ".id", "not a spine stat: " + shown(stat.id)));
            } else if (!seen.insert(stat.id).second) {
                problems.push_back(line(label, at + ".id", "duplicate of " + shown(stat.id)));
            }
        }
        const bool haveStart = readInt(item, at, "start", problems, label, stat.start);
        const bool haveEnd = readInt(item, at, "end", problems, label, stat.end);
        readInt(item, at, "weight_permille", problems, label, stat.weightPermille);
        if (haveStart && stat.start == 0) {
            problems.push_back(line(label, at + ".start", "must be above zero"));
        }
        if (haveStart && haveEnd && stat.end < stat.start) {
            problems.push_back(line(label, at + ".end", "below start, the spine only grows"));
        }
        stats.push_back(std::move(stat));
    }
    for (const std::string_view id : kSpineIds) {
        if (!seen.contains(std::string(id))) {
            problems.push_back(line(label, "", "missing spine stat " + shown(id)));
        }
    }
    if (problems.size() != before) {
        return false;
    }

    checkPowerGrowthCap(label, stats, problems);
    if (problems.size() != before) {
        return false;
    }
    std::sort(stats.begin(), stats.end(), [](const SpineStat& a, const SpineStat& b) { return a.id < b.id; });
    out = std::move(stats);
    return true;
}

}
