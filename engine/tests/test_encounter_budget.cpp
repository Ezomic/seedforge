#include "ichor/world/encounter_budget.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace ichor;

namespace {

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
        const std::string name(enemyRoleName(role));
        out.push_back({name + ".base", role, "", ""});
        out.push_back({name + ".marsh", role, "", "marsh"});
        out.push_back({name + ".league", role, "league", ""});
    }
    return out;
}

Encounter compose(std::uint64_t seed, std::uint32_t budget, const std::string& faction, const std::string& biome, std::vector<std::string>& problems)
{
    return composeEncounter({seed, budget, faction, biome}, table(), skins(), problems);
}

}

TEST_CASE("encounter spends within the budget and nearly all of it", "[encounter]")
{
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        std::vector<std::string> problems;
        const Encounter e = compose(seed, 30, "league", "marsh", problems);
        REQUIRE(problems.empty());
        std::uint32_t sum = 0;
        for (const auto& m : e.members) {
            sum += m.cost;
        }
        REQUIRE(sum == e.spent);
        REQUIRE(e.spent <= 30);
        // Swarm costs 1 with a cap of 12, so the leftover is only ever small.
        REQUIRE(e.spent >= 30 - 1);
    }
}

TEST_CASE("encounter is deterministic and varies with the seed", "[encounter]")
{
    std::vector<std::string> p;
    const Encounter a = compose(7, 40, "league", "marsh", p);
    const Encounter b = compose(7, 40, "league", "marsh", p);
    REQUIRE(a.members.size() == b.members.size());
    for (std::size_t i = 0; i < a.members.size(); ++i) {
        REQUIRE(a.members[i].role == b.members[i].role);
        REQUIRE(a.members[i].skin == b.members[i].skin);
    }
    bool differs = false;
    for (std::uint64_t seed = 8; seed < 40 && !differs; ++seed) {
        const Encounter c = compose(seed, 40, "league", "marsh", p);
        differs = c.members.size() != a.members.size() || (!c.members.empty() && c.members[0].role != a.members[0].role);
    }
    REQUIRE(differs);
}

TEST_CASE("role caps hold and the weights matter", "[encounter]")
{
    std::map<EnemyRole, std::uint32_t> totals;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        std::vector<std::string> p;
        const Encounter e = compose(seed, 60, "", "", p);
        std::map<EnemyRole, std::uint32_t> counts;
        for (const auto& m : e.members) {
            ++counts[m.role];
            ++totals[m.role];
        }
        REQUIRE(counts[EnemyRole::Controller] <= 1);
        REQUIRE(counts[EnemyRole::Anchor] <= 2);
        REQUIRE(counts[EnemyRole::Swarm] <= 12);
    }
    REQUIRE(totals[EnemyRole::Controller] > 0);
    REQUIRE(totals[EnemyRole::Skirmisher] > totals[EnemyRole::Controller]);
}

TEST_CASE("skins prefer faction over biome over the default", "[encounter]")
{
    std::vector<std::string> p;
    auto allSkins = [](const Encounter& e, const std::string& suffix) {
        REQUIRE_FALSE(e.members.empty());
        for (const auto& m : e.members) {
            REQUIRE(m.skin == std::string(enemyRoleName(m.role)) + suffix);
        }
    };
    allSkins(compose(1, 25, "league", "marsh", p), ".league");
    allSkins(compose(1, 25, "other", "marsh", p), ".marsh");
    allSkins(compose(1, 25, "other", "desert", p), ".base");
}

TEST_CASE("a role without a skin is never drawn", "[encounter]")
{
    std::vector<Skin> some;
    for (const Skin& s : skins()) {
        if (s.role != EnemyRole::Anchor) {
            some.push_back(s);
        }
    }
    std::uint32_t drawn = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        std::vector<std::string> p;
        const Encounter e = composeEncounter({seed, 50, "", ""}, table(), some, p);
        drawn += static_cast<std::uint32_t>(e.members.size());
        for (const auto& m : e.members) {
            REQUIRE(m.role != EnemyRole::Anchor);
        }
    }
    REQUIRE(drawn > 0);
}

TEST_CASE("budget edge cases", "[encounter]")
{
    std::vector<std::string> p;
    REQUIRE(compose(1, 0, "", "", p).members.empty());
    const Encounter huge = compose(1, 0xFFFFFFFFu, "", "", p);
    REQUIRE(huge.members.size() <= kMaxEncounterMembers);
    REQUIRE_FALSE(huge.members.empty());
    REQUIRE(p.empty());
    std::vector<RoleBudget> pricey = table();
    for (auto& r : pricey) {
        r.cost = 100;
    }
    REQUIRE(composeEncounter({1, 99, "", ""}, pricey, skins(), p).members.empty());
}

TEST_CASE("hostile tables are rejected without composing", "[encounter]")
{
    std::vector<std::string> p;
    std::vector<RoleBudget> zeroCost = table();
    zeroCost[0].cost = 0;
    REQUIRE(composeEncounter({1, 30, "", ""}, zeroCost, skins(), p).members.empty());
    REQUIRE_FALSE(p.empty());

    std::vector<RoleBudget> dup = table();
    dup.push_back(dup[0]);
    REQUIRE_FALSE(validateRoleBudgets(dup).empty());

    std::vector<RoleBudget> missing = table();
    missing.pop_back();
    REQUIRE_FALSE(validateRoleBudgets(missing).empty());

    std::vector<RoleBudget> heavy = table();
    heavy[0].weight = kMaxRoleWeight + 1;
    REQUIRE_FALSE(validateRoleBudgets(heavy).empty());

    std::vector<RoleBudget> badRole = table();
    badRole[0].role = static_cast<EnemyRole>(99);
    REQUIRE_FALSE(validateRoleBudgets(badRole).empty());

    p.clear();
    std::vector<Skin> dupSkins = skins();
    dupSkins.push_back(dupSkins[0]);
    REQUIRE(composeEncounter({1, 30, "", ""}, table(), dupSkins, p).members.empty());
    REQUIRE_FALSE(p.empty());

    p.clear();
    REQUIRE(composeEncounter({1, 30, "", ""}, table(), {}, p).members.empty());
    REQUIRE(composeEncounter({1, 30, "", ""}, {}, skins(), p).members.empty());
}
