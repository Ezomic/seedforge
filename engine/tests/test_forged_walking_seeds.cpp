#include "forged_walk_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <string>

using namespace ichor;
using namespace ichor::walksupport;

namespace {

constexpr int kGrid = 129;
constexpr std::array<std::uint64_t, 8> kSeeds = {1, 2, 3, 4, 5, 6, 7, 8};

}

TEST_CASE("from the spawn toward the nearest settlement on 8 seeds the walker arrives or names what stopped it", "[walking][bake]")
{
    int arrived = 0;
    int blocked = 0;
    int withSettlement = 0;
    std::string log;
    for (const std::uint64_t seed : kSeeds) {
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
    REQUIRE(withSettlement == static_cast<int>(kSeeds.size()));
    REQUIRE(arrived + blocked == withSettlement);
    // A walker that stops at every obstacle would still report honestly; the rules must also let it get places.
    // The rest end in a pocket of a few hundred thousand square metres that cliffs close in; the log above names each.
    REQUIRE(arrived >= 5);
}
