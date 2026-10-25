#include "forged_walk_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <span>
#include <string>

using namespace ichor;
using namespace ichor::walksupport;

namespace {

constexpr int kGrid = 129;
constexpr std::array<std::uint64_t, 8> kSeeds = {1, 2, 3, 4, 5, 6, 7, 8}; // seed 2's world has an enclosed spawn; its blocker is reported, not walked round

}

namespace {

void walkSeedsAndCheck(std::span<const std::uint64_t> seeds, int minimumArrived)
{
    int arrived = 0;
    int blocked = 0;
    int withSettlement = 0;
    std::string log;
    for (const std::uint64_t seed : seeds) {
        const SeedWalk walk = walkSeed(seed, kGrid);
        if (!walk.hadSettlement) {
            log += std::format("seed {}: no settlement in range\n", seed);
            continue;
        }
        ++withSettlement;
        const WalkResult& r = walk.result;
        log += std::format("seed {}: trip {:.0f} m, outcome {}, walked {:.0f} m in {:.0f} s, {:.0f} m short | {} | {}\n", seed, walk.trip,
            r.outcome == WalkOutcome::Arrived ? "arrived" : r.outcome == WalkOutcome::Blocked ? "blocked" : "TIMED OUT", r.metresWalked, r.seconds, r.metresLeft,
            describeRoute(walk), r.report);
        INFO(log);
        // Never silent: a walk that is still moving at the time limit is a failure, and a walk that stopped says why.
        REQUIRE(r.outcome != WalkOutcome::TimedOut);
        if (r.outcome == WalkOutcome::Arrived) {
            ++arrived;
            REQUIRE(r.metresLeft <= kSettlementArrive);
        } else {
            ++blocked;
            REQUIRE(r.blocker != WalkBlock::None);
            REQUIRE_FALSE(r.report.empty());
        }
        // Whatever happened, the walker made an honest attempt: it walked, not froze at the spawn.
        REQUIRE(r.metresWalked > 1.0);
    }
    INFO(log);
    REQUIRE(withSettlement == static_cast<int>(seeds.size()));
    REQUIRE(arrived + blocked == withSettlement);
    // A walker that stops at every obstacle would still report honestly; the rules must also let it get places.
    // The rest end in a pocket of a few hundred thousand square metres that cliffs close in; the log above names each.
    REQUIRE(arrived >= minimumArrived);
}

}

// Seed 4 has the shortest trip of the eight (790 m) and arrives; the others stay in the full case.
TEST_CASE("from the spawn toward the nearest settlement on a recorded short trip the walker arrives", "[walking][bake]")
{
    walkSeedsAndCheck(std::span<const std::uint64_t>(kSeeds).subspan(3, 1), 1);
}

TEST_CASE("from the spawn toward the nearest settlement on 8 seeds the walker arrives or names what stopped it", "[walking][bake]")
{
    walkSeedsAndCheck(kSeeds, 5);
}

// Seed 2 at grid 129 spawns in a pocket of 0.4 km2 that cliffs close in. Flooding it over the window and again over the
// world, for every goal and every other settlement tried as home, took 437 s in Release (SEED-657); the first flood is all it needs.
TEST_CASE("the enclosed spawn of seed 2 names its pocket without searching the world for every goal", "[walking][bake]")
{
    const SeedWalk walk = walkSeed(2, kGrid);
    REQUIRE(walk.hadSettlement);
    REQUIRE_FALSE(walk.usedNavmesh);
    REQUIRE_FALSE(walk.usedGroundRoute);
    REQUIRE(walk.routeFailure == WalkRouteFailure::NoGroundRoute);
    REQUIRE(walk.enclosure.starts_with("enclosed:"));
    REQUIRE(walk.result.outcome == WalkOutcome::Blocked);
    REQUIRE(walk.routeNodesSearched > 0);
    REQUIRE(walk.routeNodesSearched <= 60'000);
}
