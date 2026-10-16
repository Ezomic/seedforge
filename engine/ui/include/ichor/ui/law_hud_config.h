#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Every element id a HUD configuration may name: the four base ones of hud_layout.h (compass,
// clock, water, health) and the six a Law brings: vitality_cost (Hecatomb, in place of the
// health bar), favour_count (The Last Favour), metal_supply (Orichalcum), rite_loadout (The
// Mysteries), consecrated_ground (Temenos) and debt_meter (The Oath).
bool isLawHudElementId(std::string_view id);

constexpr std::size_t kMaxLawHudProblems = 16;

// Which elements the HUD shows, top to bottom, with no Law and with each Law.
struct LawHudConfig {
    std::vector<std::string> base;
    std::map<std::string, std::vector<std::string>, std::less<>> laws;
};

struct LawHudConfigResult {
    LawHudConfig config;
    std::vector<std::string> problems;

    bool ok() const { return problems.empty(); }
};

// Parses {"base": [ids], "laws": {"law.x": [ids]}}. A missing or non-list base, a law that is
// not a list, an id that is not a string, an unknown id and a repeated id within one list are all
// reported, at most kMaxLawHudProblems of them. When anything is reported the config is empty.
// Never throws.
LawHudConfigResult parseLawHudConfig(std::string_view json);

// The elements to draw for the active Law. An empty, unknown or non-Law id gets the base list.
std::vector<std::string> lawHudElements(const LawHudConfig& config, std::string_view lawId);

}
