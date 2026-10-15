#include "ichor/world/encounter_budget.h"

#include "ichor/core/rng.h"

#include <array>
#include <set>

namespace ichor {

std::string_view enemyRoleName(EnemyRole role)
{
    switch (role) {
    case EnemyRole::Skirmisher: return "skirmisher";
    case EnemyRole::Anchor: return "anchor";
    case EnemyRole::Artillery: return "artillery";
    case EnemyRole::Controller: return "controller";
    case EnemyRole::Swarm: return "swarm";
    case EnemyRole::Ambusher: return "ambusher";
    }
    return "unknown";
}

std::vector<std::string> validateRoleBudgets(const std::vector<RoleBudget>& table)
{
    std::vector<std::string> problems;
    std::array<bool, kEnemyRoleCount> seen{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        const RoleBudget& entry = table[i];
        const auto index = static_cast<std::size_t>(entry.role);
        const std::string where = "[" + std::to_string(i) + "] ";
        if (index >= kEnemyRoleCount) {
            problems.push_back(where + "unknown role");
            continue;
        }
        const std::string name(enemyRoleName(entry.role));
        if (seen[index]) {
            problems.push_back(where + "duplicate role " + name);
        }
        seen[index] = true;
        if (entry.cost == 0) {
            problems.push_back(where + name + " cost must be positive");
        }
        if (entry.weight == 0 || entry.weight > kMaxRoleWeight) {
            problems.push_back(where + name + " weight must be in 1.." + std::to_string(kMaxRoleWeight));
        }
        if (entry.maxCount == 0) {
            problems.push_back(where + name + " maxCount must be positive");
        }
    }
    for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
        if (!seen[r]) {
            problems.push_back("missing role " + std::string(enemyRoleName(static_cast<EnemyRole>(r))));
        }
    }
    return problems;
}

namespace {

const Skin* bestSkin(const std::vector<Skin>& skins, EnemyRole role, const EncounterRequest& request)
{
    const Skin* best = nullptr;
    int bestScore = -1;
    for (const Skin& skin : skins) {
        if (skin.role != role) {
            continue;
        }
        if (!skin.faction.empty() && skin.faction != request.faction) {
            continue;
        }
        if (!skin.biome.empty() && skin.biome != request.biome) {
            continue;
        }
        const int score = (skin.faction.empty() ? 0 : 2) + (skin.biome.empty() ? 0 : 1);
        if (score > bestScore || (score == bestScore && skin.id < best->id)) {
            best = &skin;
            bestScore = score;
        }
    }
    return best;
}

}

Encounter composeEncounter(const EncounterRequest& request, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems)
{
    Encounter encounter;
    const std::vector<std::string> tableProblems = validateRoleBudgets(table);
    problems.insert(problems.end(), tableProblems.begin(), tableProblems.end());
    std::set<std::string> skinIds;
    for (const Skin& skin : skins) {
        if (skin.id.empty()) {
            problems.push_back("skin with empty id");
        } else if (!skinIds.insert(skin.id).second) {
            problems.push_back("duplicate skin id " + skin.id);
        }
    }
    if (!tableProblems.empty() || skinIds.size() != skins.size()) {
        return encounter;
    }

    std::array<const RoleBudget*, kEnemyRoleCount> byRole{};
    std::array<const Skin*, kEnemyRoleCount> skinFor{};
    for (const RoleBudget& entry : table) {
        const auto index = static_cast<std::size_t>(entry.role);
        byRole[index] = &entry;
        skinFor[index] = bestSkin(skins, entry.role, request);
    }

    Rng rng = Rng(request.seed).derive("encounter");
    std::array<std::uint32_t, kEnemyRoleCount> counts{};
    std::uint32_t remaining = request.budget;
    while (encounter.members.size() < kMaxEncounterMembers) {
        std::uint64_t total = 0;
        for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
            const RoleBudget& entry = *byRole[r];
            if (skinFor[r] != nullptr && entry.cost <= remaining && counts[r] < entry.maxCount) {
                total += entry.weight;
            }
        }
        if (total == 0) {
            break;
        }
        std::uint64_t roll = rng.nextU64() % total;
        for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
            const RoleBudget& entry = *byRole[r];
            if (skinFor[r] == nullptr || entry.cost > remaining || counts[r] >= entry.maxCount) {
                continue;
            }
            if (roll < entry.weight) {
                encounter.members.push_back({entry.role, skinFor[r]->id, entry.cost});
                encounter.spent += entry.cost;
                remaining -= entry.cost;
                ++counts[r];
                break;
            }
            roll -= entry.weight;
        }
    }
    return encounter;
}

}
