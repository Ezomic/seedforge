#include "ichor/world/forged_region_tiers.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ichor {

namespace {

ForgedRegionTiers failure(std::string message)
{
    ForgedRegionTiers out;
    out.error = std::move(message);
    return out;
}

bool isHeavy(EnemyRole role)
{
    return role == EnemyRole::Anchor || role == EnemyRole::Artillery || role == EnemyRole::Controller;
}

}

std::uint8_t ForgedRegionTiers::tierAt(double worldX, double worldZ) const
{
    if (!ok() || tiers.empty()) return 0;
    if (!std::isfinite(worldX) || !std::isfinite(worldZ)) return static_cast<std::uint8_t>(tierCount - 1);
    const double last = static_cast<double>(gridSize - 1);
    const double scale = last / static_cast<double>(sizeMetres);
    const auto cell = [&](double metres) { return static_cast<std::size_t>(std::floor(std::clamp(metres * scale, 0.0, last) + 0.5)); };
    return tiers[cell(worldZ) * static_cast<std::size_t>(gridSize) + cell(worldX)];
}

ForgedRegionTiers forgedRegionTiers(const worldforge::ForgedWorld& world, const WorldDesc& desc, const ForgedTierParams& params)
{
    if (!desc.valid()) return failure("the world description is not valid");
    if (!world.hydrology().has_value()) return failure(world.ok() ? "the forged world stopped before hydrology" : world.error());
    const worldforge::HydrologyMap& map = *world.hydrology();
    if (map.width < 2 || map.width != map.height) return failure(std::format("the forged grid is {}x{}, not square", map.width, map.height));
    const int n = map.width;

    const int startX = params.startCellX < 0 ? n / 2 : params.startCellX;
    const int startY = params.startCellY < 0 ? n / 2 : params.startCellY;
    if (startX >= n || startY >= n) return failure(std::format("the start cell ({}, {}) is outside the {}x{} grid", startX, startY, n, n));

    const auto& politics = world.politics();
    const std::size_t cells = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    const bool hasPolitics = politics.has_value() && politics->width == n && politics->height == n && politics->rival.size() == cells;

    const double metresPerCell = static_cast<double>(desc.sizeMetres) / static_cast<double>(n - 1);
    std::vector<worldforge::RegionTierInput> inputs(cells);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const std::int64_t dx = x - startX;
            const std::int64_t dy = y - startY;
            const std::size_t c = static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x);
            inputs[c].distance = std::sqrt(static_cast<double>(dx * dx + dy * dy)) * metresPerCell;
            inputs[c].danger = hasPolitics && politics->rival[c] != worldforge::kPoliticsNoOwner ? 1.0 : 0.0;
        }
    }

    auto assigned = worldforge::assignRegionTiers(world.seed(), params.rings, inputs);
    if (!assigned) return failure("the region tier parameters are not valid");

    ForgedRegionTiers out;
    out.tierCount = params.rings.tierCount;
    out.gridSize = n;
    out.sizeMetres = desc.sizeMetres;
    out.tiers = std::move(*assigned);
    return out;
}

std::vector<std::string> validateTierEncounterParams(const TierEncounterParams& params)
{
    std::vector<std::string> problems;
    if (params.baseBudget == 0) problems.push_back("tier encounter: baseBudget must be at least 1");
    if (params.maxBudget < params.baseBudget) problems.push_back("tier encounter: maxBudget is below baseBudget");
    return problems;
}

std::uint32_t encounterBudgetForTier(std::uint32_t tier, const TierEncounterParams& params)
{
    const std::uint64_t raw = static_cast<std::uint64_t>(params.baseBudget) + static_cast<std::uint64_t>(tier) * params.budgetPerTier;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(raw, params.maxBudget));
}

std::vector<RoleBudget> roleTableForTier(const std::vector<RoleBudget>& table, std::uint32_t tier, const TierEncounterParams& params)
{
    std::vector<RoleBudget> out = table;
    const std::uint64_t extra = static_cast<std::uint64_t>(tier) * params.heavyWeightPermillePerTier;
    for (RoleBudget& r : out) {
        if (!isHeavy(r.role)) continue;
        const std::uint64_t weight = static_cast<std::uint64_t>(r.weight) * (1000 + extra) / 1000;
        r.weight = static_cast<std::uint32_t>(std::min<std::uint64_t>(weight, kMaxRoleWeight));
    }
    return out;
}

Encounter composeEncounterAt(const ForgedRegionTiers& tiers, double worldX, double worldZ, std::uint64_t seed, const std::string& faction, const std::string& biome,
    const TierEncounterParams& params, const std::vector<RoleBudget>& table, const std::vector<Skin>& skins, std::vector<std::string>& problems)
{
    if (!tiers.ok()) {
        problems.push_back("tier encounter: " + tiers.error);
        return {};
    }
    const std::vector<std::string> bad = validateTierEncounterParams(params);
    if (!bad.empty()) {
        problems.insert(problems.end(), bad.begin(), bad.end());
        return {};
    }
    const std::uint32_t tier = tiers.tierAt(worldX, worldZ);
    EncounterRequest request;
    request.seed = seed;
    request.budget = encounterBudgetForTier(tier, params);
    request.faction = faction;
    request.biome = biome;
    return composeEncounter(request, roleTableForTier(table, tier, params), skins, problems);
}

}
