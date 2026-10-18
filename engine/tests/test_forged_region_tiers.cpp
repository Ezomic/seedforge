#include "ichor/world/forged_region_tiers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 65;

WorldDesc smallWorld(std::uint64_t seed = 7)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return desc;
}

worldforge::ForgedWorld forged(const WorldDesc& desc, worldforge::ForgePhase stop = worldforge::ForgePhase::Hydrology)
{
    return worldforge::forgeWorld(desc.seed, kGrid, {}, stop);
}

ForgedTierParams ringsOf(double width)
{
    ForgedTierParams p;
    p.rings.ringWidth = width;
    return p;
}

std::vector<RoleBudget> table()
{
    return {
        {EnemyRole::Skirmisher, 3, 10, 4},
        {EnemyRole::Anchor, 8, 4, 2},
        {EnemyRole::Artillery, 5, 6, 3},
        {EnemyRole::Controller, 6, 3, 1},
        {EnemyRole::Swarm, 1, 8, 12},
        {EnemyRole::Ambusher, 4, 4, 2},
    };
}

std::vector<Skin> skins()
{
    std::vector<Skin> out;
    for (std::size_t r = 0; r < kEnemyRoleCount; ++r) {
        const auto role = static_cast<EnemyRole>(r);
        out.push_back({std::string(enemyRoleName(role)) + ".base", role, "", ""});
    }
    return out;
}

bool isHeavy(EnemyRole role)
{
    return role == EnemyRole::Anchor || role == EnemyRole::Artillery || role == EnemyRole::Controller;
}

}

TEST_CASE("the start is the lowest tier and the far corner the highest", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const ForgedRegionTiers tiers = forgedRegionTiers(forged(desc), desc, ringsOf(250.0));
    REQUIRE(tiers.ok());
    REQUIRE(tiers.tiers.size() == static_cast<std::size_t>(kGrid) * kGrid);
    const double mid = desc.sizeMetres / 2.0;
    CHECK(tiers.tierAt(mid, mid) == 0);
    CHECK(tiers.tierAt(0.0, 0.0) == tiers.tierCount - 1);
    CHECK(tiers.tierAt(desc.sizeMetres, desc.sizeMetres) == tiers.tierCount - 1);
}

TEST_CASE("tier never falls as the distance from the start grows", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const ForgedRegionTiers tiers = forgedRegionTiers(forged(desc), desc, ringsOf(250.0));
    REQUIRE(tiers.ok());
    const double mid = desc.sizeMetres / 2.0;
    std::uint8_t previous = 0;
    for (double d = 0.0; d <= mid; d += 8.0) {
        const std::uint8_t tier = tiers.tierAt(mid + d, mid);
        CHECK(tier >= previous);
        previous = tier;
    }
    CHECK(previous > 0);
}

TEST_CASE("a contested region is never a lower tier than the same region without politics", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const worldforge::ForgedWorld withPolitics = forged(desc, worldforge::ForgePhase::Politics);
    REQUIRE(withPolitics.ok());
    const ForgedRegionTiers plain = forgedRegionTiers(forged(desc), desc, ringsOf(250.0));
    const ForgedRegionTiers political = forgedRegionTiers(withPolitics, desc, ringsOf(250.0));
    REQUIRE(plain.ok());
    REQUIRE(political.ok());

    int contested = 0;
    for (std::uint8_t r : withPolitics.politics()->rival) contested += r != worldforge::kPoliticsNoOwner ? 1 : 0;
    int raised = 0;
    for (std::size_t c = 0; c < plain.tiers.size(); ++c) {
        CHECK(political.tiers[c] >= plain.tiers[c]);
        raised += political.tiers[c] > plain.tiers[c] ? 1 : 0;
    }
    if (contested > 0) CHECK(raised > 0);
}

TEST_CASE("the same seed and params give the same tiers", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    CHECK(forgedRegionTiers(forged(desc), desc).tiers == forgedRegionTiers(forged(desc), desc).tiers);
}

TEST_CASE("two regions of different tier get different budgets", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const ForgedRegionTiers tiers = forgedRegionTiers(forged(desc), desc, ringsOf(250.0));
    REQUIRE(tiers.ok());
    const TierEncounterParams params;
    const double mid = desc.sizeMetres / 2.0;
    const std::uint32_t near = encounterBudgetForTier(tiers.tierAt(mid, mid), params);
    const std::uint32_t far = encounterBudgetForTier(tiers.tierAt(0.0, 0.0), params);
    CHECK(near == params.baseBudget);
    CHECK(far > near);

    std::vector<std::string> problems;
    const Encounter a = composeEncounterAt(tiers, mid, mid, 11, "", "", params, table(), skins(), problems);
    const Encounter b = composeEncounterAt(tiers, 0.0, 0.0, 11, "", "", params, table(), skins(), problems);
    REQUIRE(problems.empty());
    CHECK(a.spent <= near);
    CHECK(b.spent <= far);
    CHECK(b.spent > a.spent);
}

TEST_CASE("a higher tier draws heavy roles more often", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const ForgedRegionTiers tiers = forgedRegionTiers(forged(desc), desc, ringsOf(250.0));
    REQUIRE(tiers.ok());
    const TierEncounterParams params;
    const double mid = desc.sizeMetres / 2.0;

    const auto heavyShare = [&](double x, double z) {
        double heavy = 0.0;
        double all = 0.0;
        for (std::uint64_t seed = 0; seed < 300; ++seed) {
            std::vector<std::string> problems;
            const Encounter e = composeEncounterAt(tiers, x, z, seed, "", "", params, table(), skins(), problems);
            REQUIRE(problems.empty());
            for (const EncounterMember& m : e.members) {
                all += 1.0;
                heavy += isHeavy(m.role) ? 1.0 : 0.0;
            }
        }
        return heavy / all;
    };
    CHECK(heavyShare(0.0, 0.0) > heavyShare(mid, mid));
}

TEST_CASE("the role table only reweights the heavy roles", "[forged_region_tiers]")
{
    const TierEncounterParams params;
    const std::vector<RoleBudget> base = table();
    const std::vector<RoleBudget> raised = roleTableForTier(base, 4, params);
    REQUIRE(raised.size() == base.size());
    for (std::size_t i = 0; i < base.size(); ++i) {
        CHECK(raised[i].cost == base[i].cost);
        CHECK(raised[i].maxCount == base[i].maxCount);
        if (isHeavy(base[i].role)) CHECK(raised[i].weight > base[i].weight);
        else CHECK(raised[i].weight == base[i].weight);
    }
    CHECK(roleTableForTier(base, 0, params)[1].weight == base[1].weight);
    CHECK(validateRoleBudgets(raised).empty());
}

TEST_CASE("hostile inputs are rejected or clamped without crashing", "[forged_region_tiers]")
{
    const WorldDesc desc = smallWorld();
    const ForgedRegionTiers tiers = forgedRegionTiers(forged(desc), desc);
    REQUIRE(tiers.ok());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(tiers.tierAt(nan, 0.0) == tiers.tierCount - 1);
    CHECK(tiers.tierAt(0.0, inf) == tiers.tierCount - 1);
    CHECK(tiers.tierAt(-1e30, 1e30) == tiers.tierAt(0.0, desc.sizeMetres));

    CHECK(!forgedRegionTiers(forged(desc, worldforge::ForgePhase::Erosion), desc).ok());
    WorldDesc bad = desc;
    bad.sizeMetres = 0;
    CHECK(!forgedRegionTiers(forged(desc), bad).ok());
    ForgedTierParams badRings;
    badRings.rings.ringWidth = nan;
    CHECK(!forgedRegionTiers(forged(desc), desc, badRings).ok());
    badRings = {};
    badRings.rings.tierCount = 0;
    CHECK(!forgedRegionTiers(forged(desc), desc, badRings).ok());
    ForgedTierParams outside;
    outside.startCellX = kGrid;
    CHECK(!forgedRegionTiers(forged(desc), desc, outside).ok());

    const ForgedRegionTiers broken;
    CHECK(broken.tierAt(1.0, 1.0) == 0);
    std::vector<std::string> problems;
    CHECK(composeEncounterAt(ForgedRegionTiers{"nope"}, 0.0, 0.0, 1, "", "", TierEncounterParams{}, table(), skins(), problems).members.empty());
    CHECK(!problems.empty());

    TierEncounterParams zero;
    zero.baseBudget = 0;
    CHECK(!validateTierEncounterParams(zero).empty());
    problems.clear();
    CHECK(composeEncounterAt(tiers, 0.0, 0.0, 1, "", "", zero, table(), skins(), problems).members.empty());
    CHECK(!problems.empty());

    TierEncounterParams huge;
    huge.baseBudget = 0xFFFFFFFFu;
    huge.budgetPerTier = 0xFFFFFFFFu;
    huge.maxBudget = 0xFFFFFFFFu;
    huge.heavyWeightPermillePerTier = 0xFFFFFFFFu;
    CHECK(encounterBudgetForTier(255, huge) == 0xFFFFFFFFu);
    for (const RoleBudget& r : roleTableForTier(table(), 255, huge)) CHECK(r.weight <= kMaxRoleWeight);
}
