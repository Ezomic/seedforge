#include "ichor/factions/incumbent.h"

#include <array>
#include <string>

namespace ichor {

namespace {

struct SingleEntry {
    std::string_view axiom;
    std::string_view id;
    std::string_view name;
};

constexpr std::array<SingleEntry, 4> kSingles = {{
    {"powers.hegemony", "archonate", "The Archonate"},
    {"powers.league", "hellenotamiai", "The Hellenotamiai"},
    {"powers.satrapy", "garrison", "The Garrison"},
    {"powers.amphictyony", "council", "The Council"},
}};

constexpr std::array<std::string_view, kMaxPoleisIncumbents> kPolisOrdinals = {"First", "Second", "Third", "Fourth"};

IncumbentPlan poleisPlan(int count)
{
    IncumbentPlan plan{IncumbentShape::Poleis, {}};
    for (int i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>(i);
        plan.incumbents.push_back(Faction{"polis_" + std::to_string(i + 1), "The " + std::string(kPolisOrdinals[index]) + " Polis", FactionRole::Incumbent});
    }
    return plan;
}

}

std::optional<IncumbentPlan> planIncumbents(std::string_view powersAxiomId, int poleisCount)
{
    for (const auto& entry : kSingles) {
        if (powersAxiomId == entry.axiom) {
            return IncumbentPlan{IncumbentShape::Single, {Faction{std::string(entry.id), std::string(entry.name), FactionRole::Incumbent}}};
        }
    }
    if (powersAxiomId == "powers.poleis") {
        if (poleisCount < kMinPoleisIncumbents || poleisCount > kMaxPoleisIncumbents) {
            return std::nullopt;
        }
        return poleisPlan(poleisCount);
    }
    if (powersAxiomId == "powers.dark_age") {
        return IncumbentPlan{IncumbentShape::None, {}};
    }
    return std::nullopt;
}

}
