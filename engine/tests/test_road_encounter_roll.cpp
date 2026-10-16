#include "ichor/world/road_encounter_roll.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

RoadEncounterRoll roll(std::uint64_t seed, double lengthM, RoadSafety safety, double hour, std::vector<std::string>& problems)
{
    RoadEncounterQuery q;
    q.seed = seed;
    q.segmentId = 7;
    q.lengthMetres = lengthM;
    q.safety = safety;
    q.hourOfDay = hour;
    return rollRoadEncounter(q, RoadEncounterParams{}, table(), skins(), problems);
}

int countHits(RoadSafety safety, double hour)
{
    int hits = 0;
    for (std::uint64_t seed = 0; seed < 4000; ++seed) {
        std::vector<std::string> problems;
        const RoadEncounterRoll r = roll(seed, 1000.0, safety, hour, problems);
        REQUIRE(problems.empty());
        hits += r.occurs ? 1 : 0;
    }
    return hits;
}

}

TEST_CASE("unpatrolled roads host far more ambushes than patrolled ones", "[road_encounter]")
{
    const int unsafe = countHits(RoadSafety::Unpatrolled, 12.0);
    const int safe = countHits(RoadSafety::Patrolled, 12.0);
    REQUIRE(unsafe > 100);
    REQUIRE(safe * 5 < unsafe);
}

TEST_CASE("night raises the ambush rate on an unpatrolled road", "[road_encounter]")
{
    REQUIRE(countHits(RoadSafety::Unpatrolled, 23.0) > countHits(RoadSafety::Unpatrolled, 12.0) * 3 / 2);
}

TEST_CASE("ambient combat stays rare even on the worst road", "[road_encounter]")
{
    const RoadEncounterParams params;
    REQUIRE(countHits(RoadSafety::Unpatrolled, 23.0) <= 4000 * static_cast<int>(params.maxChancePermille) / 1000 * 11 / 10);
}

TEST_CASE("the same query always gives the same roll", "[road_encounter]")
{
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        std::vector<std::string> pa;
        std::vector<std::string> pb;
        const RoadEncounterRoll a = roll(seed, 2500.0, RoadSafety::Unpatrolled, 22.0, pa);
        const RoadEncounterRoll b = roll(seed, 2500.0, RoadSafety::Unpatrolled, 22.0, pb);
        REQUIRE(a.occurs == b.occurs);
        REQUIRE(a.chancePermille == b.chancePermille);
        REQUIRE(a.budget == b.budget);
        REQUIRE(a.encounter.members.size() == b.encounter.members.size());
        for (std::size_t i = 0; i < a.encounter.members.size(); ++i) {
            REQUIRE(a.encounter.members[i].skin == b.encounter.members[i].skin);
        }
    }
}

TEST_CASE("an occurring encounter spends within its budget and the budget is capped", "[road_encounter]")
{
    const RoadEncounterParams params;
    int seen = 0;
    for (std::uint64_t seed = 0; seed < 2000; ++seed) {
        std::vector<std::string> problems;
        const RoadEncounterRoll r = roll(seed, 100000.0, RoadSafety::Unpatrolled, 2.0, problems);
        REQUIRE(r.budget <= params.maxBudget);
        REQUIRE(r.chancePermille <= params.maxChancePermille);
        if (r.occurs) {
            ++seen;
            REQUIRE(!r.encounter.members.empty());
            REQUIRE(r.encounter.spent <= r.budget);
            REQUIRE(r.encounter.members.size() <= kMaxEncounterMembers);
        } else {
            REQUIRE(r.encounter.members.empty());
        }
    }
    REQUIRE(seen > 0);
}

TEST_CASE("longer segments are more dangerous", "[road_encounter]")
{
    std::vector<std::string> problems;
    const RoadEncounterRoll shortRoad = roll(1, 200.0, RoadSafety::Unpatrolled, 12.0, problems);
    const RoadEncounterRoll longRoad = roll(1, 3000.0, RoadSafety::Unpatrolled, 12.0, problems);
    REQUIRE(longRoad.chancePermille > shortRoad.chancePermille);
    REQUIRE(longRoad.budget >= shortRoad.budget);
}

TEST_CASE("bad inputs yield no encounter and a problem, never a crash", "[road_encounter]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double bad[] = {nan, inf, -inf, -1.0, 1e300};
    for (double v : bad) {
        std::vector<std::string> problems;
        RoadEncounterRoll r = roll(1, v, RoadSafety::Unpatrolled, 12.0, problems);
        REQUIRE(!r.occurs);
        REQUIRE(!problems.empty());
        problems.clear();
        r = roll(1, 1000.0, RoadSafety::Unpatrolled, v, problems);
        REQUIRE(!r.occurs);
        REQUIRE(!problems.empty());
    }
    std::vector<std::string> problems;
    REQUIRE(!roll(1, 1000.0, RoadSafety::Unpatrolled, 24.0, problems).occurs);
    REQUIRE(!problems.empty());
    problems.clear();
    REQUIRE(!roll(1, 1000.0, static_cast<RoadSafety>(9), 12.0, problems).occurs);
    REQUIRE(!problems.empty());
}

TEST_CASE("a zero length road has no ambush", "[road_encounter]")
{
    std::vector<std::string> problems;
    const RoadEncounterRoll r = roll(1, 0.0, RoadSafety::Unpatrolled, 12.0, problems);
    REQUIRE(problems.empty());
    REQUIRE(!r.occurs);
    REQUIRE(r.chancePermille == 0);
}

TEST_CASE("a broken role table is reported and yields no encounter", "[road_encounter]")
{
    RoadEncounterQuery q;
    q.seed = 3;
    q.segmentId = 1;
    q.lengthMetres = 5000.0;
    q.hourOfDay = 23.0;
    std::vector<std::string> problems;
    const RoadEncounterRoll r = rollRoadEncounter(q, RoadEncounterParams{}, {}, skins(), problems);
    REQUIRE(!r.occurs);
    REQUIRE(!problems.empty());
}

TEST_CASE("invalid tuning is rejected", "[road_encounter]")
{
    RoadEncounterParams p;
    p.maxBudget = 0;
    REQUIRE(!validateRoadEncounterParams(p).empty());
    p = RoadEncounterParams{};
    p.maxChancePermille = 1001;
    REQUIRE(!validateRoadEncounterParams(p).empty());
    p = RoadEncounterParams{};
    p.nightStartHour = 30.0;
    REQUIRE(!validateRoadEncounterParams(p).empty());
    REQUIRE(validateRoadEncounterParams(RoadEncounterParams{}).empty());
}
