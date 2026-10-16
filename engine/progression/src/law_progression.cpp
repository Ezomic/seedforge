#include "ichor/progression/law_progression.h"

#include <array>

namespace ichor {

namespace {

constexpr std::array<std::string_view, 3> kLogistics = {"carry_capacity", "refining_efficiency", "supply_lines"};
constexpr std::array<std::string_view, 1> kVitality = {"max_health"};
constexpr std::array<std::string_view, 1> kInitiation = {"rites_held"};
constexpr std::array<std::string_view, 2> kCartography = {"sanctuary_attunement", "sanctuaries_known"};
constexpr std::array<std::string_view, 1> kConservation = {"cost_efficiency"};
constexpr std::array<std::string_view, 1> kDebt = {"debt_ceiling"};

constexpr std::array<LawProgressionDef, 6> kDefs = {{
    {"law.orichalcum", LawProgressionKind::Logistics, "Power is infrastructure: carry capacity, refining and supply lines.", kLogistics,
     ProgressionMeasure::GrowthPermille, true, false},
    {"law.hecatomb", LawProgressionKind::Vitality, "The health pool is the offering pool: one stat for survival and casting.", kVitality,
     ProgressionMeasure::GrowthPermille, true, true},
    {"law.mysteries", LawProgressionKind::Initiation, "Admissions are the character sheet: how many rites you hold.", kInitiation,
     ProgressionMeasure::Count, true, false},
    {"law.temenos", LawProgressionKind::Cartography, "Attunement to sanctuary types plus knowing where they are.", kCartography,
     ProgressionMeasure::Count, true, false},
    {"law.last_favour", LawProgressionKind::Conservation, "No stronger, only cheaper: efficiency is progression.", kConservation,
     ProgressionMeasure::CostPermille, false, false},
    {"law.oath", LawProgressionKind::DebtCeiling, "More power available, more attention on the reckoning.", kDebt,
     ProgressionMeasure::Count, true, false},
}};

}

const LawProgressionDef* lawProgression(std::string_view axiomId)
{
    for (const LawProgressionDef& def : kDefs)
        if (def.axiomId == axiomId) return &def;
    return nullptr;
}

std::span<const LawProgressionDef> allLawProgressions()
{
    return kDefs;
}

std::vector<const LawProgressionDef*> lawProgressionsFor(const std::vector<std::string>& axiomIds)
{
    std::vector<const LawProgressionDef*> out;
    for (const std::string& id : axiomIds) {
        const LawProgressionDef* def = lawProgression(id);
        if (def == nullptr) continue;
        bool seen = false;
        for (const LawProgressionDef* have : out) seen = seen || have == def;
        if (!seen) out.push_back(def);
        if (out.size() == kDefs.size()) break;
    }
    return out;
}

std::string_view lawProgressionKindName(LawProgressionKind kind)
{
    switch (kind) {
    case LawProgressionKind::Logistics: return "logistics";
    case LawProgressionKind::Vitality: return "vitality";
    case LawProgressionKind::Initiation: return "initiation";
    case LawProgressionKind::Cartography: return "cartography";
    case LawProgressionKind::Conservation: return "conservation";
    case LawProgressionKind::DebtCeiling: return "debt_ceiling";
    }
    return "unknown";
}

}
