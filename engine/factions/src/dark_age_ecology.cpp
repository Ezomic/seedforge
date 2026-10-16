#include "ichor/factions/dark_age_ecology.h"

#include <algorithm>

namespace ichor {

namespace {

constexpr std::size_t kMaxAxiomId = 64;

bool hasPlainId(std::string_view id, std::string_view prefix)
{
    if (id.size() <= prefix.size() || id.size() > kMaxAxiomId || id.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return std::all_of(id.begin() + static_cast<std::ptrdiff_t>(prefix.size()), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

}

std::optional<EcologyPlan> planEcology(std::string_view powersAxiomId, std::string_view lawAxiomId)
{
    if (!hasPlainId(powersAxiomId, "powers.") || !hasPlainId(lawAxiomId, "law.")) {
        return std::nullopt;
    }
    EcologyPlan plan;
    plan.postState = powersAxiomId == "powers.dark_age";
    auto set = [&plan](FactionRole r, int lo, int hi, int expansion = kNormalExpansionPermille) { plan.roles[static_cast<std::size_t>(r)] = RoleEcology{lo, hi, expansion}; };
    const int vacuum = plan.postState ? kDarkAgeExpansionPermille : kNormalExpansionPermille;
    set(FactionRole::Incumbent, plan.postState ? 0 : 1, plan.postState ? 0 : 4);
    set(FactionRole::Challenger, 1, 2);
    set(FactionRole::Parasite, 1, 7, vacuum);
    set(FactionRole::Custodian, 1, 7);
    set(FactionRole::Refusenik, 1, 7, vacuum);
    set(FactionRole::Herald, 1, 7);
    return plan;
}

std::vector<std::string> checkAgainstEcology(const EcologyPlan& plan, const std::vector<Faction>& factions)
{
    std::vector<std::string> problems;
    std::array<int, kFactionRoleCount> counts{};
    for (const auto& faction : factions) {
        ++counts[static_cast<std::size_t>(faction.role)];
    }
    for (std::size_t i = 0; i < kFactionRoleCount; ++i) {
        const auto role = static_cast<FactionRole>(i);
        const auto& want = plan.roles[i];
        if (counts[i] < want.minCount || counts[i] > want.maxCount) {
            const std::string what = plan.postState && role == FactionRole::Incumbent ? "a post-state world has no incumbent" : "role count out of range";
            problems.push_back(std::string(factionRoleName(role)) + ": " + what + " (" + std::to_string(counts[i]) + ", want " + std::to_string(want.minCount) + " to " + std::to_string(want.maxCount) + ")");
        }
    }
    return problems;
}

std::vector<int> territoryWeightsPermille(const EcologyPlan& plan, const std::vector<Faction>& factions)
{
    std::vector<int> weights;
    weights.reserve(factions.size());
    for (const auto& faction : factions) {
        weights.push_back(plan.roles[static_cast<std::size_t>(faction.role)].expansionPermille);
    }
    return weights;
}

}
