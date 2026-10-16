#include "ichor/world/coop_scaling.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
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
        out.push_back({std::string(enemyRoleName(role)) + ".base", role, "", ""});
    }
    return out;
}

ScaledEncounter scale(std::uint64_t seed, std::uint32_t budget, std::uint32_t players, const CoopCurve& curve, std::vector<std::string>& problems)
{
    return scaleEncounterForCoop({seed, budget, "", ""}, players, curve, table(), skins(), problems);
}

bool sameMember(const EncounterMember& a, const EncounterMember& b)
{
    return a.role == b.role && a.skin == b.skin && a.cost == b.cost;
}

}

TEST_CASE("one player gets exactly the unscaled encounter", "[coop]")
{
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        std::vector<std::string> problems;
        const Encounter base = composeEncounter({seed, 30, "", ""}, table(), skins(), problems);
        const ScaledEncounter one = scale(seed, 30, 1, CoopCurve{}, problems);
        REQUIRE(problems.empty());
        REQUIRE(one.encounter.members.size() == base.members.size());
        for (std::size_t i = 0; i < base.members.size(); ++i) {
            REQUIRE(sameMember(one.encounter.members[i], base.members[i]));
        }
        REQUIRE(one.extraBudget == 0);
    }
}

TEST_CASE("more players add enemies and never change an enemy", "[coop]")
{
    const std::vector<RoleBudget> roles = table();
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        std::vector<std::string> problems;
        const ScaledEncounter one = scale(seed, 30, 1, CoopCurve{}, problems);
        ScaledEncounter previous = one;
        for (std::uint32_t n = 2; n <= 8; ++n) {
            const ScaledEncounter now = scale(seed, 30, n, CoopCurve{}, problems);
            REQUIRE(problems.empty());
            REQUIRE(now.encounter.members.size() >= previous.encounter.members.size());
            for (std::size_t i = 0; i < previous.encounter.members.size(); ++i) {
                REQUIRE(sameMember(now.encounter.members[i], previous.encounter.members[i]));
            }
            previous = now;
        }
        REQUIRE(previous.encounter.members.size() > one.encounter.members.size());
        // Every enemy costs exactly what its role costs in the table: scaling the
        // party never made a single enemy bigger, so a health or damage multiplier
        // has nowhere to hide.
        for (const EncounterMember& m : previous.encounter.members) {
            REQUIRE(m.cost == roles[static_cast<std::size_t>(m.role)].cost);
        }
    }
}

TEST_CASE("mean enemy count at four players is well above one player", "[coop]")
{
    double one = 0;
    double four = 0;
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        std::vector<std::string> problems;
        one += static_cast<double>(scale(seed, 40, 1, CoopCurve{}, problems).encounter.members.size());
        four += static_cast<double>(scale(seed, 40, 4, CoopCurve{}, problems).encounter.members.size());
    }
    REQUIRE(four > one * 1.5);
}

TEST_CASE("extra percent is monotonic, bounded and flattens", "[coop]")
{
    const CoopCurve curve;
    REQUIRE(coopExtraPercent(1, curve) == 0);
    std::uint32_t previous = 0;
    std::uint32_t previousStep = 0xFFFFFFFFu;
    for (std::uint32_t n = 2; n <= curve.maxPlayers; ++n) {
        const std::uint32_t now = coopExtraPercent(n, curve);
        REQUIRE(now >= previous);
        REQUIRE(now <= curve.maxExtraPercent);
        REQUIRE(now - previous <= previousStep);
        previousStep = now - previous;
        previous = now;
    }
    CoopCurve tight = curve;
    tight.maxExtraPercent = 70;
    REQUIRE(coopExtraPercent(8, tight) == 70);
}

TEST_CASE("player counts of zero, huge and non-finite are clamped", "[coop]")
{
    const CoopCurve curve;
    REQUIRE(effectivePlayers(0u, curve) == 1);
    REQUIRE(effectivePlayers(0xFFFFFFFFu, curve) == curve.maxPlayers);
    REQUIRE(effectivePlayers(std::numeric_limits<double>::quiet_NaN(), curve) == 1);
    REQUIRE(effectivePlayers(-std::numeric_limits<double>::infinity(), curve) == 1);
    REQUIRE(effectivePlayers(std::numeric_limits<double>::infinity(), curve) == curve.maxPlayers);
    REQUIRE(effectivePlayers(-3.0, curve) == 1);
    REQUIRE(effectivePlayers(0.5, curve) == 1);
    REQUIRE(effectivePlayers(3.9, curve) == 3);
    REQUIRE(effectivePlayers(1e300, curve) == curve.maxPlayers);

    std::vector<std::string> problems;
    const ScaledEncounter capped = scale(7, 30, 0xFFFFFFFFu, curve, problems);
    const ScaledEncounter atMax = scale(7, 30, curve.maxPlayers, curve, problems);
    REQUIRE(capped.encounter.members.size() == atMax.encounter.members.size());
    REQUIRE(capped.players == curve.maxPlayers);
    REQUIRE(scale(7, 30, 0, curve, problems).encounter.members.size() == scale(7, 30, 1, curve, problems).encounter.members.size());
}

TEST_CASE("growth is capped by the extra percent and the member limit", "[coop]")
{
    CoopCurve curve;
    curve.firstStepPercent = 1000;
    curve.decayPercent = 100;
    curve.maxExtraPercent = 1000;
    curve.maxPlayers = kMaxCoopPlayers;
    std::vector<std::string> problems;
    const ScaledEncounter big = scale(3, 4000000000u, kMaxCoopPlayers, curve, problems);
    REQUIRE(big.encounter.members.size() <= kMaxEncounterMembers);

    CoopCurve modest;
    modest.maxExtraPercent = 100;
    const ScaledEncounter s = scale(5, 60, 8, modest, problems);
    REQUIRE(s.encounter.spent <= 60 + 60);
}

TEST_CASE("scaling is deterministic", "[coop]")
{
    std::vector<std::string> problems;
    const ScaledEncounter a = scale(11, 33, 5, CoopCurve{}, problems);
    const ScaledEncounter b = scale(11, 33, 5, CoopCurve{}, problems);
    REQUIRE(a.encounter.members.size() == b.encounter.members.size());
    for (std::size_t i = 0; i < a.encounter.members.size(); ++i) {
        REQUIRE(sameMember(a.encounter.members[i], b.encounter.members[i]));
    }
}

TEST_CASE("invalid curves and empty budgets are reported without scaling", "[coop]")
{
    CoopCurve bad;
    bad.decayPercent = 101;
    bad.maxPlayers = 0;
    REQUIRE(validateCoopCurve(bad).size() == 2);
    REQUIRE(validateCoopCurve(CoopCurve{}).empty());

    std::vector<std::string> problems;
    const ScaledEncounter s = scale(1, 30, 4, bad, problems);
    REQUIRE_FALSE(problems.empty());
    REQUIRE(s.encounter.members.size() == s.baseMembers);

    std::vector<std::string> none;
    REQUIRE(scale(1, 0, 4, CoopCurve{}, none).encounter.members.empty());
}
