#include "ichor/ui/law_hud_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <set>

namespace ichor {

namespace {

constexpr std::array<std::string_view, 10> kIds{"compass", "clock", "water", "health", "vitality_cost", "favour_count", "metal_supply", "rite_loadout", "consecrated_ground", "debt_meter"};

struct Problems {
    std::vector<std::string> list;
    std::size_t dropped = 0;

    void add(std::string text)
    {
        if (list.size() < kMaxLawHudProblems) {
            list.push_back(std::move(text));
        } else {
            ++dropped;
        }
    }

    std::vector<std::string> finish()
    {
        if (dropped > 0) {
            list.push_back("and " + std::to_string(dropped) + " more");
        }
        return std::move(list);
    }
};

bool readList(const nlohmann::json& value, const std::string& path, Problems& problems, std::vector<std::string>& out)
{
    if (!value.is_array()) {
        problems.add(path + ": must be a list");
        return false;
    }
    bool valid = true;
    std::set<std::string> seen;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const std::string where = path + "[" + std::to_string(index) + "]";
        if (!value[index].is_string()) {
            problems.add(where + ": must be a string");
            valid = false;
            continue;
        }
        const std::string id = value[index].get<std::string>();
        if (!isLawHudElementId(id)) {
            problems.add(where + ": unknown element '" + id + "'");
            valid = false;
        } else if (!seen.insert(id).second) {
            problems.add(where + ": duplicate '" + id + "'");
            valid = false;
        } else {
            out.push_back(id);
        }
    }
    return valid;
}

}

bool isLawHudElementId(std::string_view id)
{
    return std::find(kIds.begin(), kIds.end(), id) != kIds.end();
}

LawHudConfigResult parseLawHudConfig(std::string_view json)
{
    LawHudConfigResult result;
    const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        result.problems = {"not valid JSON"};
        return result;
    }
    if (!root.is_object()) {
        result.problems = {"$: must be an object with base and laws"};
        return result;
    }

    Problems problems;
    LawHudConfig config;
    const auto base = root.find("base");
    if (base == root.end()) {
        problems.add("base: is required");
    } else {
        readList(*base, "base", problems, config.base);
    }
    const auto laws = root.find("laws");
    if (laws == root.end()) {
        problems.add("laws: is required");
    } else if (!laws->is_object()) {
        problems.add("laws: must be an object");
    } else {
        for (const auto& [id, list] : laws->items()) {
            std::vector<std::string> elements;
            if (readList(list, "laws." + id, problems, elements)) {
                config.laws.emplace(id, std::move(elements));
            }
        }
    }

    result.problems = problems.finish();
    if (result.problems.empty()) {
        result.config = std::move(config);
    }
    return result;
}

std::vector<std::string> lawHudElements(const LawHudConfig& config, std::string_view lawId)
{
    const auto found = config.laws.find(lawId);
    return found == config.laws.end() ? config.base : found->second;
}

}
