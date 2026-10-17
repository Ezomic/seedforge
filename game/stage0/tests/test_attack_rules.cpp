#include "ichor/stage0/attack_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <limits>

using namespace ichor::stage0;

namespace {

// A target that circles the attacker at 180 deg/s (3000 mdeg per tick) so an unclamped tracker
// would turn 3000 per tick. Returns the mean absolute turn per tick over the phase.
double meanTurn(AttackPhase phase, int ticks)
{
    std::int32_t facing = 0;
    std::int64_t target = 90000;
    std::int64_t total = 0;
    for (int i = 0; i < ticks; ++i) {
        target += 3000;
        const std::int32_t next = clampedFacing(phase, facing, normalizeMdeg(target));
        total += std::abs(shortestArcMdeg(facing, next));
        facing = next;
    }
    return static_cast<double>(total) / ticks;
}

} // namespace

TEST_CASE("windup turns at most 30 deg/s on average while the target circles faster", "[attack_rules]")
{
    const double mean = meanTurn(AttackPhase::Windup, 120);
    CHECK(mean == 500.0);
    CHECK(meanTurn(AttackPhase::Neutral, 120) > 2500.0);
}

TEST_CASE("active frames freeze facing completely", "[attack_rules]")
{
    CHECK(meanTurn(AttackPhase::Active, 120) == 0.0);
    CHECK(clampedFacing(AttackPhase::Active, 12345, 200000) == 12345);
}

TEST_CASE("windup takes the short way round the wrap", "[attack_rules]")
{
    CHECK(clampedFacing(AttackPhase::Windup, 359800, 20000) == 300);
    CHECK(clampedFacing(AttackPhase::Windup, 200, 340000) == 359700);
    CHECK(clampedFacing(AttackPhase::Windup, 1000, 1200) == 1200);
}

TEST_CASE("other phases track freely", "[attack_rules]")
{
    for (auto p : {AttackPhase::Neutral, AttackPhase::Tell, AttackPhase::Recovery}) {
        CHECK(clampedFacing(p, 0, 180000) == 180000);
    }
}

TEST_CASE("hostile angles and limits stay in range", "[attack_rules]")
{
    constexpr auto lo = std::numeric_limits<std::int32_t>::min();
    constexpr auto hi = std::numeric_limits<std::int32_t>::max();
    for (std::int32_t a : {lo, -1, 0, 1, hi}) {
        for (std::int32_t b : {lo, -1, 0, 1, hi}) {
            const auto r = clampedFacing(AttackPhase::Windup, a, b);
            CHECK(r >= 0);
            CHECK(r < kFullTurnMdeg);
            const auto arc = shortestArcMdeg(a, b);
            CHECK(arc > -kFullTurnMdeg / 2);
            CHECK(arc <= kFullTurnMdeg / 2);
        }
    }
    RotationLimits bad{-5, -5};
    CHECK(clampedFacing(AttackPhase::Windup, 100, 5000, bad) == 100);
    RotationLimits huge{hi, hi};
    CHECK(clampedFacing(AttackPhase::Windup, 100, 5000, huge) == 5000);
}

TEST_CASE("an off-screen attacker needs audio", "[attack_rules]")
{
    const CommitRules rules;
    AttackGate gate;
    CommitInput in;
    in.attackerOnScreen = false;
    in.distanceMm = 10000;
    CHECK(canCommit(gate, in, rules) == CommitVeto::OffscreenSilent);
    in.audioTelegraphPlays = true;
    CHECK(canCommit(gate, in, rules) == CommitVeto::None);
    in.attackerOnScreen = true;
    in.audioTelegraphPlays = false;
    CHECK(canCommit(gate, in, rules) == CommitVeto::None);
}

TEST_CASE("only one follow-up while the player stays staggered", "[attack_rules]")
{
    const CommitRules rules{.spacingBeatTicks = 0};
    AttackGate gate;
    CommitInput in;
    in.playerStaggered = true;
    in.distanceMm = 10000;

    int landed = 0;
    for (int i = 0; i < 600; ++i) {
        tickGate(gate, in, rules);
        if (canCommit(gate, in, rules) == CommitVeto::None) {
            noteCommit(gate, in);
            ++landed;
        }
    }
    CHECK(landed == 1);

    in.playerStaggered = false;
    tickGate(gate, in, rules);
    CHECK(canCommit(gate, in, rules) == CommitVeto::None);
    in.playerStaggered = true;
    noteCommit(gate, in);
    CHECK(canCommit(gate, in, rules) == CommitVeto::StaggerChain);
}

TEST_CASE("spacing beat separates attacks and the opener is not delayed", "[attack_rules]")
{
    const CommitRules rules;
    AttackGate gate;
    CommitInput in;
    in.distanceMm = 1000;
    CHECK(canCommit(gate, in, rules) == CommitVeto::None);
    noteCommit(gate, in);

    int ticksToNextCommit = 0;
    in.distanceMm = rules.spacingMinMm;
    while (canCommit(gate, in, rules) != CommitVeto::None) {
        tickGate(gate, in, rules);
        ++ticksToNextCommit;
        REQUIRE(ticksToNextCommit < 1000);
    }
    CHECK(ticksToNextCommit == rules.spacingBeatTicks);

    noteCommit(gate, in);
    in.distanceMm = rules.spacingMinMm - 1;
    for (int i = 0; i < 500; ++i) tickGate(gate, in, rules);
    CHECK(canCommit(gate, in, rules) == CommitVeto::NotSpaced);

    in.distanceMm = rules.spacingMinMm;
    for (int i = 0; i < rules.spacingBeatTicks - 1; ++i) tickGate(gate, in, rules);
    CHECK(canCommit(gate, in, rules) == CommitVeto::NotSpaced);
    tickGate(gate, in, rules);
    CHECK(canCommit(gate, in, rules) == CommitVeto::None);
}

TEST_CASE("negative and extreme distances are handled", "[attack_rules]")
{
    const CommitRules rules;
    AttackGate gate;
    CommitInput in;
    for (std::int64_t d : {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()}) {
        in.distanceMm = d;
        tickGate(gate, in, rules);
        canCommit(gate, in, rules);
    }
    CHECK(gate.ticksAtSpacing >= 0);
}
